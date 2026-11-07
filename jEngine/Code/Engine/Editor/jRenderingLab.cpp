#include "pch.h"
#include "jRenderingLab.h"
#include "jEditorWorkspace.h"
#include "jEditorCapture.h"
#include "jEditorUiRenderer.h"
#include "jRenderInspector.h"
#include "Code/Engine/jEditor.h"
#include "FileLoader/jEditorAssetPath.h"
#include "FileLoader/jImageFileLoader.h"
#include "Renderer/Experiments/jExperimentImage.h"
#include "Renderer/jAdvancedShadowPass.h"
#include "Renderer/jRenderer.h"
#include "Profiler/jRenderTrace.h"
#include "Profiler/jPerformanceProfile.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/Light/jLight.h"
#include "Material/jMaterial.h"
#include "RHI/jRHIUtil.h"
#include "RHI/Vulkan/jImGui_Vulkan.h"
#include "RHI/Vulkan/jRHI_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jImGui_DX12.h"
#include "RHI/DX12/jRHI_DX12.h"
#endif
#include "Shader/jShaderParameterSet.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "jEngine.h"
#include "jOptions.h"
#include <chrono>
#include <deque>
#include <fstream>
#include <future>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace jRenderingLab
{
namespace
{
namespace ex = jRenderingExperiment;
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
enum class Phase { Idle, Queued, Configure, Warmup, Measure, Drain, Capture, Saving, Reporting };

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jLabCaptureUniform)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, InvSize)
END_SHADER_UNIFORM_BUFFER_STRUCT()
BEGIN_SHADER_PARAMETER_SET(jLabCaptureParameters)
    SHADER_TEXTURE2D(LabSource)
    SHADER_UNIFORM_BUFFER(jLabCaptureUniform, LabCapture)
END_SHADER_PARAMETER_SET()

struct CameraPose
{
    Vector Position, Target, Up;
    float Fov = 0, NearClip = 0, FarClip = 0, Distance = 0, Aspect = 0;
    bool Perspective = true, Infinite = false;
};
struct LightSaved
{
    jLight* Light = nullptr;
    bool Casts = false;
    std::optional<jAdvancedShadows::Settings> Override;
    PRE_UPDATE_LAMBDA Update;
};
struct PendingTiming { uint64_t Frame = 0; double Cpu = 0; };
struct TaskResult
{
    std::string Id, Label, Algorithm, State = "running", Reason;
    uint32_t Repetition = 0;
    std::vector<double> SceneGpu, ShadowGpu, Cpu;
    size_t TargetBytes = 0;
    int AtlasWidth = 0, AtlasHeight = 0;
};
struct UiImage
{
    std::shared_ptr<jTexture> Texture;
    void* Id = nullptr;
    int Width = 0, Height = 0;
};
struct State
{
    bool Visible = false, StartRequested = false, CancelRequested = false, RestorePending = false, ShowResults = false;
    ex::Definition Definition = ex::ShadowDefaults(), RunningDefinition;
    Status Info;
    Phase Step = Phase::Idle;
    fs::path OutputRoot;
    char SpecPath[1024]{}, OutputPath[1024]{}, Name[129] = "Shadow comparison";
    std::vector<ex::Task> Plan;
    size_t TaskIndex = 0;
    uint32_t PhaseFrames = 0, RenderedFrames = 0;
    uint64_t Lease = 0, DocumentRevision = 0, CaptureFrame = 0, Nonfinite = 0;
    int Width = 0, Height = 0;
    jCamera* Camera = nullptr;
    CameraPose Pose;
    CameraPose OriginalPose;
    jLight* Target = nullptr;
    Vector LightDirection;
    std::vector<LightSaved> Lights;
    std::vector<std::pair<jObject*, bool>> Objects;
    std::vector<std::function<void()>> Restore;
    std::vector<TaskResult> Results;
    std::deque<PendingTiming> Timings;
    Clock::time_point Started, FrameStarted;
    std::shared_ptr<jRenderTarget> CaptureTarget;
    std::shared_ptr<jBuffer> CaptureReadback;
    uint32_t CapturePitch = 0;
    std::future<void> Writer;
    Json Report, CompletedReport;
    std::vector<UiImage> Images;
    std::vector<std::string> ImageIds, ImageLabels;
    UiImage Difference;
    int CompareA = 0, CompareB = 1, CompareMode = 0;
    float Split = .5f;
    std::string PreviewError, Failure;
};
State& Get() { static State state; return state; }
void PhaseLabel(Phase phase)
{
    auto& s=Get();s.Step=phase;s.PhaseFrames=0;
    const char* names[]={"Idle","Queued","Preparing variant","Warming up","Measuring","Collecting GPU timestamps","Capturing HDR","Saving capture","Writing report"};
    s.Info.Phase=names[int(phase)];
    s.Info.Frame=0;
    s.Info.FrameCount=phase==Phase::Warmup?s.RunningDefinition.WarmupFrames:
        phase==Phase::Measure?s.RunningDefinition.MeasuredFrames:0;
}
template<class T> void Remember(T& field)
{
    const T original=field;Get().Restore.push_back([pointer=&field,original]{*pointer=original;});
}
std::string HashBytes(const void* bytes,size_t size)
{
    uint64_t hash=14695981039346656037ull;const auto* data=static_cast<const uint8_t*>(bytes);
    for(size_t i=0;i<size;++i){hash^=data[i];hash*=1099511628211ull;}
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
void WriteText(const fs::path& path,const std::string& text)
{
    std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);file<<text;
}
std::string ReadText(const fs::path& path,size_t maximum)
{
    if(fs::file_size(path)>maximum)throw std::runtime_error("Input file exceeds its size budget");
    std::ifstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);
    std::string text(size_t(fs::file_size(path)),'\0');file.read(text.data(),std::streamsize(text.size()));return text;
}
Json Statistics(const std::vector<double>& values)
{
    const auto s=ex::Summarize(values);if(!s)return nullptr;
    return {{"count",s->Count},{"meanMs",s->Mean},{"medianMs",s->Median},{"p95Ms",s->P95},
        {"minMs",s->Minimum},{"maxMs",s->Maximum},{"stddevMs",s->StandardDeviation}};
}
Json ResultJson(const TaskResult& r)
{
    return {{"variant",r.Id},{"label",r.Label},{"algorithm",r.Algorithm},{"repetition",r.Repetition},
        {"status",r.State},{"reason",r.Reason},{"sceneGpu",Statistics(r.SceneGpu)},
        {"shadowGpu",Statistics(r.ShadowGpu)},{"updateDrawCpu",Statistics(r.Cpu)},
        {"sceneGpuSamplesMs",r.SceneGpu},{"shadowGpuSamplesMs",r.ShadowGpu},{"cpuSamplesMs",r.Cpu},
        {"estimatedShadowTargetBytes",r.TargetBytes},{"actualAtlas",Json::array({r.AtlasWidth,r.AtlasHeight})}};
}
Vector RotateY(Vector value,float angle)
{
    const float c=std::cos(angle),sn=std::sin(angle);
    return Vector(c*value.x+sn*value.z,value.y,-sn*value.x+c*value.z);
}
void PlaceCamera(double progress)
{
    auto& s=Get();if(!s.Camera)return;
    const float angle=float(s.RunningDefinition.MotionDegrees*3.141592653589793/180*std::sin(progress*6.283185307179586));
    Vector eye=s.Pose.Position;
    if(s.RunningDefinition.Motion==ex::Scenario::CameraArc)eye=s.Pose.Target+RotateY(eye-s.Pose.Target,angle);
    jCamera::SetCamera(s.Camera,eye,s.Pose.Target,eye+(s.Pose.Up-s.Pose.Position),s.Pose.Fov,
        s.Pose.NearClip,s.Pose.FarClip,float(SCR_WIDTH),float(SCR_HEIGHT),s.Pose.Perspective,s.Pose.Distance);
    s.Camera->ProjectionAspectOverride=s.Pose.Aspect;s.Camera->IsInfinityFar=s.Pose.Infinite;
    if(s.Target)s.Target->SetDirection(s.RunningDefinition.Motion==ex::Scenario::LightSweep?RotateY(s.LightDirection,angle):s.LightDirection);
}
void RestoreWorld()
{
    auto& s=Get();if(!s.RestorePending)return;
    if(g_rhi)g_rhi->Flush(); // Session boundary, never a measured frame.
    const auto& currentLights=jLight::GetLights();
    for(auto& light:s.Lights)
        if(std::find(currentLights.begin(),currentLights.end(),light.Light)!=currentLights.end())
        {light.Light->IsShadowCaster=light.Casts;light.Light->SetShadowOverride(light.Override);light.Light->PreUpdateLambda=light.Update;}
    if(s.Target&&std::find(currentLights.begin(),currentLights.end(),s.Target)!=currentLights.end())s.Target->SetDirection(s.LightDirection);
    const auto& currentObjects=jObject::GetStaticObject();
    for(const auto& [object,updates]:s.Objects)
        if(std::find(currentObjects.begin(),currentObjects.end(),object)!=currentObjects.end())object->IsPostUpdate=updates;
    if(g_Engine&&g_Engine->Game.MainCamera==s.Camera&&s.Camera)
    {
        const auto& pose=s.OriginalPose;
        jCamera::SetCamera(s.Camera,pose.Position,pose.Target,pose.Up,pose.Fov,pose.NearClip,pose.FarClip,
            float(SCR_WIDTH),float(SCR_HEIGHT),pose.Perspective,pose.Distance);
        s.Camera->IsInfinityFar=pose.Infinite;s.Camera->ProjectionAspectOverride=pose.Aspect;s.Camera->UpdateCamera();
        s.Camera->PrevViewProjection=s.Camera->ViewProjection;
    }
    for(auto it=s.Restore.rbegin();it!=s.Restore.rend();++it)(*it)();
    s.Restore.clear();s.RestorePending=false;s.Lights.clear();s.Objects.clear();s.Target=nullptr;s.Camera=nullptr;
    if(s.Lease){jRenderTrace::EndCapture(s.Lease);s.Lease=0;}
    s.CaptureTarget.reset();s.CaptureReadback.reset();s.CaptureFrame=0;
}
void Unregister(UiImage& image)
{
    if(image.Id&&g_ImGUI)
    {
        if(IsUseVulkan())static_cast<jImGUI_Vulkan*>(g_ImGUI)->UnregisterTexture(image.Id);
#ifdef _WIN32
        else if(IsUseDX12())static_cast<jImGUI_DX12*>(g_ImGUI)->UnregisterTexture(image.Id);
#endif
    }
    image={};
}
void ClearImages()
{
    auto& s=Get();if(g_rhi&&(!s.Images.empty()||s.Difference.Id))g_rhi->Flush();
    for(auto& image:s.Images)Unregister(image);Unregister(s.Difference);
    s.Images.clear();s.ImageIds.clear();s.ImageLabels.clear();
}
UiImage Upload(const ex::DisplayImage& pixels)
{
    UiImage image;
    jImageData data;data.Width=int(pixels.Width);data.Height=int(pixels.Height);data.CreateMipmapIfPossible=false;
    data.ImageBulkData.ImageData=pixels.Rgba;image.Texture=g_rhi->CreateTextureFromData(&data);
    if(!image.Texture)throw std::runtime_error("Could not upload comparison preview");
    // Result viewing occurs after measurement; complete the legacy DX12 upload.
#ifdef _WIN32
    if(IsUseDX12())g_rhi->Flush();
#endif
    if(IsUseVulkan())image.Id=static_cast<jImGUI_Vulkan*>(g_ImGUI)->RegisterTexture(image.Texture);
#ifdef _WIN32
    else if(IsUseDX12())image.Id=static_cast<jImGUI_DX12*>(g_ImGUI)->RegisterTexture(image.Texture);
#endif
    if(!image.Id)throw std::runtime_error("Could not register comparison preview");
    image.Width=int(pixels.Width);image.Height=int(pixels.Height);return image;
}
void LoadPreviews()
{
    auto& s=Get();ClearImages();s.PreviewError.clear();
    try
    {
        for(const auto& variant:s.RunningDefinition.Variants)
        {
            const auto file=s.Info.Directory/(variant.Id+".pfm");
            if(!variant.Enabled||!fs::exists(file))continue;
            const auto image=ex::LoadPfm(file);
            s.Images.push_back(Upload(ex::Display(image,s.RunningDefinition.DisplayExposure,768)));
            s.ImageIds.push_back(variant.Id);s.ImageLabels.push_back(variant.Label);
        }
        s.CompareA=0;s.CompareB=s.Images.size()>1?1:0;
    }
    catch(const std::exception& e){s.PreviewError=e.what();}
}
void GenerateReport(const fs::path& directory,Json report,const ex::Definition& definition)
{
    using ex::EscapeHtml;
    report["definition"]=Json::parse(ex::Serialize(definition));
    Json summaries=Json::array();
    const bool baselineExists=fs::exists(directory/(definition.Baseline+".pfm"));
    std::optional<ex::FloatImage> baseline;
    if(baselineExists)baseline=ex::LoadPfm(directory/(definition.Baseline+".pfm"));
    std::ostringstream csv;csv.imbue(std::locale::classic());
    csv<<"variant,algorithm,valid_runs,gpu_median_ms,gpu_p95_ms,shadow_median_ms,linear_rgb_rmse,target_bytes\n";
    std::ostringstream html;
    html<<"<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Rendering Lab</title><style>body{margin:0;background:#121923;color:#eaf1f8;font:16px/1.65 system-ui}main{max-width:1250px;margin:auto;padding:28px}"
        "h1{font-size:30px}h2{font-size:21px}a{color:#8ac8ff}table{width:100%;border-collapse:collapse;white-space:nowrap}td,th{padding:10px;border-bottom:1px solid #3b4e64;text-align:left}"
        ".scroll{overflow:auto}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.card{padding:18px;background:#1a2839;border:1px solid #34495e;border-radius:10px}"
        "img{width:100%;background:black}.note{border-left:3px solid #e0b46a;padding:14px;background:#252a2f}.muted{color:#acc0d4}@media(max-width:760px){.grid{grid-template-columns:1fr}main{padding:16px}}</style><main><h1>"
        <<EscapeHtml(definition.Name)<<"</h1><p>Status: "<<EscapeHtml(report.value("status","unknown"))<<" "<<EscapeHtml(report.value("reason",""))<<"</p><p class=\"note\">Baseline differences are not ground-truth accuracy scores. "
        "Display images use fixed exposure and Reinhard + gamma 2.2; numerical differences use linear HDR RGB. "
        "Measured frames exclude warmup, capture, readback and encoding.</p><p><a href=\"results.json\">JSON and raw samples</a> · <a href=\"summary.csv\">CSV</a> · "
        "<a href=\"experiment.jlab\">Experiment definition</a> · <a href=\"scene-snapshot.jscene\">Scene snapshot</a></p>";
    html<<"<div class=\"scroll\"><table><tr><th>Variant</th><th>Valid runs</th><th>Scene GPU median / P95</th><th>Shadow GPU median</th><th>RGB RMSE vs baseline</th><th>Shadow target estimate</th></tr>";
    for(const auto& variant:definition.Variants)if(variant.Enabled)
    {
        std::vector<double> scene,shadow,cpu;size_t bytes=0,runs=0;
        for(const auto& task:report["tasks"])if(task["variant"]==variant.Id&&task["status"]=="completed")
        {
            ++runs;for(double v:task["sceneGpuSamplesMs"])scene.push_back(v);
            for(double v:task["shadowGpuSamplesMs"])shadow.push_back(v);
            for(double v:task["cpuSamplesMs"])cpu.push_back(v);
            bytes=std::max(bytes,task["estimatedShadowTargetBytes"].get<size_t>());
        }
        Json summary={{"id",variant.Id},{"label",variant.Label},{"algorithm",variant.AlgorithmId},{"validRuns",runs},
            {"sceneGpu",Statistics(scene)},{"shadowGpu",Statistics(shadow)},{"updateDrawCpu",Statistics(cpu)},
            {"estimatedShadowTargetBytes",bytes},{"baselineDifference",nullptr}};
        const auto path=directory/(variant.Id+".pfm");
        if(baseline&&fs::exists(path))
        {
            const auto candidate=ex::LoadPfm(path);
            if(candidate.Width==baseline->Width&&candidate.Height==baseline->Height)
            {
                const auto difference=ex::Compare(baseline->Rgba,candidate.Rgba,candidate.Width,candidate.Height,definition.Region);
                summary["baselineDifference"]={{"valid",difference.Valid},{"pixels",difference.Pixels},{"nonfinitePixels",difference.NonFinitePixels}};
                if(difference.Valid)
                {
                    summary["baselineDifference"]["mae"]=difference.Mae;summary["baselineDifference"]["rmse"]=difference.Rmse;
                    summary["baselineDifference"]["maxAbs"]=difference.Maximum;
                }
                ex::SavePng(directory/(variant.Id+"-difference.png"),ex::Heatmap(*baseline,candidate));
            }
        }
        const auto s=ex::Summarize(scene),sh=ex::Summarize(shadow);
        html<<"<tr><td>"<<EscapeHtml(variant.Label)<<"</td><td>"<<runs<<"</td><td>";
        if(s)html<<s->Median<<" / "<<s->P95<<" ms";else html<<"Unavailable";
        html<<"</td><td>";if(sh)html<<sh->Median<<" ms";else html<<"Unavailable";
        html<<"</td><td>";if(summary["baselineDifference"].is_object()&&summary["baselineDifference"].value("valid",false))html<<summary["baselineDifference"]["rmse"];else html<<"Unavailable";
        html<<"</td><td>"<<double(bytes)/1048576<<" MiB</td></tr>";
        csv<<variant.Id<<','<<variant.AlgorithmId<<','<<runs<<',';
        if(s)csv<<s->Median;csv<<',';if(s)csv<<s->P95;csv<<',';if(sh)csv<<sh->Median;csv<<',';
        if(summary["baselineDifference"].is_object()&&summary["baselineDifference"].value("valid",false))csv<<summary["baselineDifference"]["rmse"];
        csv<<','<<bytes<<'\n';summaries.push_back(std::move(summary));
    }
    html<<"</table></div><p class=\"muted\">Scene GPU is an inclusive graphics-queue interval, not a sum of nested pass times. "
        "Target bytes describe the selected shadow adapter's tracked targets, not total resident VRAM. Measurements include the current renderer and hardware.</p><p>"
        <<EscapeHtml(report.value("projection",""))<<"</p><section class=\"grid\">";
    for(const auto& variant:definition.Variants)if(variant.Enabled&&fs::exists(directory/(variant.Id+".png")))
    {
        html<<"<article class=\"card\"><h2>"<<EscapeHtml(variant.Label)<<"</h2><img src=\""<<variant.Id<<".png\" alt=\""<<EscapeHtml(variant.Label)<<" actual rendered output\">"
            "<p><a href=\""<<variant.Id<<".pfm\">Linear float RGB (PFM)</a></p>";
        if(fs::exists(directory/(variant.Id+"-difference.png")))html<<"<p>Difference heatmap ×4: black = equal, red/yellow = larger difference.</p><img src=\""<<variant.Id<<"-difference.png\" alt=\"Difference heatmap\">";
        html<<"</article>";
    }
    html<<"</section><h2>Applied paths and diagnostics</h2><ul>";
    for(const auto& task:report["tasks"])html<<"<li>"<<EscapeHtml(task.value("label",""))<<" · repeat "<<task.value("repetition",0)+1
        <<" · "<<EscapeHtml(task.value("status",""))<<" · "<<EscapeHtml(task.value("reason",""))<<"</li>";
    html<<"</ul><p>Each quality image is captured at the scenario endpoint after the first repetition. "
        "This version does not quantify motion-compensated ghosting or treat the baseline as a physical reference.</p></main></html>";
    report["summary"]=std::move(summaries);
    WriteText(directory/"results.json",report.dump(2)+"\n");WriteText(directory/"summary.csv",csv.str());WriteText(directory/"report.html",html.str());
}
void ReportAndStop(const std::string& state,const std::string& reason)
{
    auto& s=Get();s.Info.Error=reason;
    if(!s.Results.empty()&&s.Results.back().State=="running"){s.Results.back().State=state;s.Results.back().Reason=reason;}
    s.Report["status"]=state;s.Report["reason"]=reason;s.Report["renderedFrames"]=s.RenderedFrames;s.Report["tasks"]=Json::array();
    for(const auto& task:s.Results)s.Report["tasks"].push_back(ResultJson(task));
    RestoreWorld();
    PhaseLabel(Phase::Reporting);
    auto directory=s.Info.Directory;auto report=s.Report;auto definition=s.RunningDefinition;
    s.Writer=std::async(std::launch::async,[directory,report=std::move(report),definition=std::move(definition)]()mutable{GenerateReport(directory,std::move(report),definition);});
}
void NextTask()
{
    auto& s=Get();s.Timings.clear();s.CaptureFrame=0;
    if(++s.TaskIndex>=s.Plan.size())
    {
        const bool failed=std::any_of(s.Results.begin(),s.Results.end(),[](const auto& r){return r.State!="completed";});
        ReportAndStop(failed?"completed-with-unavailable-variants":"completed","");return;
    }
    PhaseLabel(Phase::Configure);
}
jAdvancedShadows::Settings ShadowSettings(const ex::Parameters& p,const std::string& algorithm)
{
    jAdvancedShadows::Settings settings;
    settings.DirectionalProjection=jAdvancedShadows::Projection::Cascaded;settings.CascadeCount=1;
    settings.MapResolution=uint32_t(p.at("resolution"));settings.MaxDistance=float(p.at("maxDistance"));settings.CasterPadding=float(p.at("casterPadding"));
    settings.BlurRadius=uint32_t(p.at("blurRadius"));settings.PositiveExponent=float(p.at("positiveExponent"));
    settings.NegativeExponent=float(p.at("negativeExponent"));settings.MinimumVariance=float(p.at("minimumVariance"));settings.BleedReduction=float(p.at("bleedReduction"));
    if(algorithm=="shadow.pcf")settings.Method=jAdvancedShadows::Representation::Depth;
    else if(algorithm=="shadow.vsm")settings.Method=jAdvancedShadows::Representation::Variance;
    else if(algorithm=="shadow.esm")settings.Method=jAdvancedShadows::Representation::Exponential;
    else if(algorithm=="shadow.evsm")settings.Method=jAdvancedShadows::Representation::ExponentialVariance;
    else throw std::runtime_error("Algorithm adapter not registered");
    std::string error;if(!jAdvancedShadows::ValidateForLight(settings,jAdvancedShadows::LightKind::Directional,error))throw std::runtime_error(error);
    return settings;
}
void BeginSession()
{
    auto& s=Get();
    if(!g_Editor||!g_Engine||!g_Engine->Game.MainCamera)throw std::runtime_error("An initialized editor scene is required");
    if(g_Editor->GetDocument().IsPlaying()||g_Editor->GetDocument().HasTransaction())throw std::runtime_error("Stop Play and finish the current property edit first");
    if(g_Engine->Game.IsUsingPathTracingRenderer())throw std::runtime_error("Shadow Lab requires a raster renderer");
    if(g_Engine->Game.IsSceneImportPending()||g_Editor->PendingImportCount())throw std::runtime_error("Wait for scene imports to finish");
    if(jEditorCapture::GetStatus().State!=jEditorCapture::Phase::Idle)throw std::runtime_error("Finish the current image/video capture first");
    if(uint64_t(SCR_WIDTH)*SCR_HEIGHT>4194304||SCR_WIDTH<16||SCR_HEIGHT<16)throw std::runtime_error("Lab capture supports a rendered area up to 4 megapixels");
    for(const auto& [id,entity]:g_Editor->GetDocument().Entities())
        if(entity.Kind==jEditorAuthoring::EntityKind::Water&&g_Editor->GetDocument().IsEffectivelyVisible(id))
            throw std::runtime_error("The first Shadow Lab requires static geometry; hide animated water before running");
    s.Target=nullptr;
    for(auto* light:jLight::GetLights())if(light&&light->GetLightType()==ELightType::DIRECTIONAL){s.Target=light;break;}
    if(!s.Target)throw std::runtime_error("Add a directional light or open the Shadow Lab sample");
    ClearImages();s.CompletedReport=Json();s.Report=Json::object();s.Results.clear();s.Timings.clear();
    fs::create_directories(s.OutputRoot);
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    for(uint32_t attempt=0;attempt<100;++attempt)
    {
        s.Info.Directory=s.OutputRoot/("run-"+std::to_string(stamp)+"-"+std::to_string(attempt));
        if(fs::create_directory(s.Info.Directory))break;
        if(attempt==99)throw std::runtime_error("Could not reserve a unique run directory");
    }
    const auto scene=g_Editor->GetDocument().ToJson();
    WriteText(s.Info.Directory/"scene-snapshot.jscene",scene);
    s.Report={{"schema",1},{"status","running"},{"sceneName",g_Editor->GetDocument().Name()},
        {"sceneFingerprintFnv1a64",HashBytes(scene.data(),scene.size())},{"sourceProject",jEditorAssetPath::ToUtf8(GetAuthoringProjectFile())},
        {"sceneRevision",g_Editor->GetDocument().Revision()},{"rhi",g_rhi->GetRHIName().ToStr()},
        {"build",std::string(__DATE__)+" "+__TIME__},{"resolution",Json::array({SCR_WIDTH,SCR_HEIGHT})},
        {"reproducibilityScope","Authored scene snapshot and asset revision references; external assets are referenced, not bundled."},
        {"timingScope","Graphics scene interval plus AdvancedShadows interval. Warmup/capture/readback/encoding excluded; no sum of nested scopes."},
        {"cpuScope","Controlled Game.Update/Draw/EndOfFrame; experiment telemetry and OS event polling excluded"},
        {"comparisonKind","baseline difference, not ground truth"},{"randomization","Production shadow patterns are deterministic; no stochastic seed parameter is used"},
        {"projection","One-cascade CSM in a 2x2 atlas for every method. Production VSM/ESM/EVSM additionally fit receiver/caster bounds; this compares complete shadow methods, not isolated filters."}};
#ifdef NDEBUG
    s.Report["configuration"]="Release";
#else
    s.Report["configuration"]="Debug";
#endif
    if(IsUseVulkan())
    {
        s.Report["gpu"]=g_rhi_vk->DeviceProperties.properties.deviceName;
        s.Report["driverVersion"]=g_rhi_vk->DeviceProperties.properties.driverVersion;
    }
    else s.Report["gpu"]="DX12 device; see platform diagnostics";
    s.Camera=g_Engine->Game.MainCamera;
    s.Pose={s.Camera->Pos,s.Camera->Target,s.Camera->Up,s.Camera->FOVRad,s.Camera->Near,s.Camera->Far,
        s.Camera->Distance,s.Camera->ProjectionAspectOverride,s.Camera->IsPerspectiveProjection,s.Camera->IsInfinityFar};
    s.OriginalPose=s.Pose;
    if(s.RunningDefinition.Camera)
    {
        const auto& c=*s.RunningDefinition.Camera;
        s.Pose.Position=Vector(float(c.Position[0]),float(c.Position[1]),float(c.Position[2]));
        s.Pose.Target=Vector(float(c.Target[0]),float(c.Target[1]),float(c.Target[2]));
        s.Pose.Up=s.Pose.Position+Vector::UpVector;s.Pose.Distance=(s.Pose.Target-s.Pose.Position).Length();
        s.Pose.Fov=DegreeToRadian(float(c.FovDegrees));s.Pose.NearClip=float(c.NearClip);s.Pose.FarClip=float(c.FarClip);
        s.Pose.Aspect=float(c.Aspect);s.Pose.Perspective=true;s.Pose.Infinite=false;
    }
    else
    {
        if(!s.Pose.Perspective)throw std::runtime_error("This Shadow Lab version requires a perspective camera");
        ex::CameraSettings c;
        c.Position={s.Pose.Position.x,s.Pose.Position.y,s.Pose.Position.z};c.Target={s.Pose.Target.x,s.Pose.Target.y,s.Pose.Target.z};
        c.FovDegrees=s.Pose.Fov*180/3.141592653589793;c.NearClip=s.Pose.NearClip;c.FarClip=s.Pose.FarClip;c.Aspect=s.Pose.Aspect;
        s.RunningDefinition.Camera=c;
    }
    WriteText(s.Info.Directory/"experiment.jlab",ex::Serialize(s.RunningDefinition));
    s.Report["camera"]={{"position",Json::array({s.Pose.Position.x,s.Pose.Position.y,s.Pose.Position.z})},
        {"target",Json::array({s.Pose.Target.x,s.Pose.Target.y,s.Pose.Target.z})},{"fovRadians",s.Pose.Fov},{"aspectOverride",s.Pose.Aspect}};
    s.Width=SCR_WIDTH;s.Height=SCR_HEIGHT;s.DocumentRevision=g_Editor->GetDocument().Revision();
    s.LightDirection=s.Target->GetDirection();s.Report["directionalLightDirection"]=Json::array({s.LightDirection.x,s.LightDirection.y,s.LightDirection.z});
    s.RestorePending=true;
    Remember(gOptions.AdvancedShadows);Remember(gOptions.SceneFeatures);Remember(gOptions.Subsurface);Remember(gOptions.TextureSubsurface);
    Remember(gOptions.HiZOcclusion);Remember(gOptions.RequestedMSAASamples);Remember(gOptions.UseSubpass);Remember(gOptions.UseMemoryless);
    Remember(gOptions.UseHWRTDirectLighting);Remember(gOptions.UseSurfelGI);Remember(gOptions.UseSSGI);Remember(gOptions.AOType);
    Remember(gOptions.BloomEyeAdaptation);Remember(gOptions.UseAtmosphericShadowing);Remember(gOptions.UseVRS);Remember(gOptions.ShowVRSArea);
    Remember(gOptions.ShowGrid);Remember(gOptions.ShowDebugObject);Remember(gOptions.ShowDebugRT);Remember(gOptions.EarthQuake);
    Remember(gOptions.ShadowFilterMode);Remember(gOptions.ShadowFilterRadiusTexels);Remember(gOptions.UseRaytracing);Remember(gOptions.CameraPos);
    gOptions.SceneFeatures={};gOptions.Subsurface.Enabled=false;gOptions.HiZOcclusion.Enabled=false;
    gOptions.RequestedMSAASamples=1;gOptions.UseSubpass=false;gOptions.UseMemoryless=false;
    gOptions.UseHWRTDirectLighting=false;gOptions.UseSurfelGI=false;gOptions.UseSSGI=false;gOptions.AOType=0;gOptions.UseRaytracing=false;
    gOptions.BloomEyeAdaptation=false;gOptions.UseAtmosphericShadowing=false;gOptions.UseVRS=false;gOptions.ShowVRSArea=false;
    gOptions.ShowGrid=false;gOptions.ShowDebugObject=false;gOptions.ShowDebugRT=false;gOptions.EarthQuake=false;
    const bool inspector=jRenderInspector::IsVisible();const auto mode=jRenderInspector::GetViewMode();
    s.Restore.push_back([inspector,mode]{jRenderInspector::SetViewMode(mode);jRenderInspector::SetVisible(inspector);});
    jRenderInspector::SetVisible(false);jRenderInspector::SetViewMode(jRenderInspector::ViewMode::Final);
    for(auto* light:jLight::GetLights())if(light)
    {
        s.Lights.push_back({light,light->IsShadowCaster,light->GetShadowOverride(),light->PreUpdateLambda});
        light->PreUpdateLambda={};light->IsShadowCaster=light==s.Target;
    }
    for(auto* object:jObject::GetStaticObject())if(object){s.Objects.emplace_back(object,object->IsPostUpdate);object->IsPostUpdate=false;}
    s.Lease=jRenderTrace::BeginCapture();s.Plan=ex::MakePlan(s.RunningDefinition);s.TaskIndex=0;s.RenderedFrames=0;
    s.Info.TaskCount=s.Plan.size();s.Started=Clock::now();PhaseLabel(Phase::Configure);
}
bool SceneUnchanged()
{
    auto& s=Get();
    if(!g_Editor||!g_Engine||g_Engine->Game.MainCamera!=s.Camera||SCR_WIDTH!=s.Width||SCR_HEIGHT!=s.Height||
        g_Editor->GetDocument().Revision()!=s.DocumentRevision)return false;
    const auto& lights=jLight::GetLights();const auto& objects=jObject::GetStaticObject();
    if(lights.size()!=s.Lights.size()||objects.size()!=s.Objects.size())return false;
    for(const auto& light:s.Lights)if(std::find(lights.begin(),lights.end(),light.Light)==lights.end())return false;
    for(const auto& [object,unused]:s.Objects)if(std::find(objects.begin(),objects.end(),object)==objects.end())return false;
    return true;
}
void CollectTimings()
{
    auto& s=Get();auto& result=s.Results.back();
    while(!s.Timings.empty())
    {
        const auto pending=s.Timings.front();const auto frame=jRenderTrace::CopyFrame(pending.Frame);
        if(!frame)throw std::runtime_error("A measured frame was evicted before GPU timing completed");
        if(!frame->Ended||frame->PendingGpu)break;
        std::optional<double> scene,shadow;
        for(const auto& pass:frame->Passes)
        {
            if(pass.Name=="RenderingLabScene")scene=pass.GpuMilliseconds;
            if(pass.Name=="AdvancedShadows")shadow=pass.GpuMilliseconds;
        }
        if(!scene||!shadow||!std::isfinite(*scene)||!std::isfinite(*shadow))throw std::runtime_error("Required GPU timestamp scopes were not produced");
        result.SceneGpu.push_back(*scene);result.ShadowGpu.push_back(*shadow);result.Cpu.push_back(pending.Cpu);s.Timings.pop_front();
    }
}
}

bool IsVisible(){return Get().Visible;}
void SetVisible(bool visible){Get().Visible=visible;}
bool IsRunning(){return Get().Info.Running;}
const Status& GetStatus(){return Get().Info;}
float CameraAspect(){return IsRunning()&&Get().RestorePending?Get().Pose.Aspect:0.f;}
bool Start(const ex::Definition& definition,const fs::path& outputRoot,std::string& error)
{
    auto& s=Get();if(s.Info.Running){error="An experiment is already running";return false;}
    if(!ex::Validate(definition,error))return false;
    if(outputRoot.empty()){error="Choose an output directory";return false;}
    fs::path absolute;
    try { absolute=fs::absolute(outputRoot); }
    catch(const std::exception& e){error=e.what();return false;}
    s.RunningDefinition=definition;s.Definition=definition;std::snprintf(s.Name,sizeof(s.Name),"%s",definition.Name.c_str());
    s.OutputRoot=std::move(absolute);s.Info={};s.Info.Running=true;
    s.Visible=true;s.CancelRequested=false;s.StartRequested=true;s.Failure.clear();PhaseLabel(Phase::Queued);error.clear();return true;
}
void Cancel(){if(IsRunning())Get().CancelRequested=true;}
void BeforeFrame()
{
    auto& s=Get();if(!s.Info.Running)return;
    try
    {
        if(s.StartRequested){s.StartRequested=false;BeginSession();}
        if(s.Step==Phase::Reporting)
        {
            if(s.Writer.valid()&&s.Writer.wait_for(std::chrono::seconds(0))==std::future_status::ready)
            {s.Writer.get();s.CompletedReport=Json::parse(ReadText(s.Info.Directory/"results.json",16*1024*1024));s.Info.Running=false;s.ShowResults=true;PhaseLabel(Phase::Idle);LoadPreviews();}
            return;
        }
        if(s.CancelRequested){if(s.Writer.valid())s.Writer.get();ReportAndStop("cancelled","Cancelled by user");return;}
        if(!s.Failure.empty()){const auto error=s.Failure;s.Failure.clear();ReportAndStop("failed",error);return;}
        if(!SceneUnchanged())throw std::runtime_error("Scene membership, document revision or render size changed during the experiment");
        if(std::chrono::duration<double>(Clock::now()-s.Started).count()>600||s.RenderedFrames>60000)
            throw std::runtime_error("Experiment exceeded its time/frame budget");
        if(s.Step==Phase::Saving)
        {
            if(s.Writer.wait_for(std::chrono::seconds(0))==std::future_status::ready)
            {s.Writer.get();if(s.Results.back().State=="running")s.Results.back().State="completed";NextTask();}
            return;
        }
        if(s.Step==Phase::Configure)
        {
            g_rhi->Flush();jAdvancedShadows::ReleaseResources();
            const auto task=s.Plan[s.TaskIndex];const auto& variant=s.RunningDefinition.Variants[task.VariantIndex];
            const auto parameters=ex::ResolvedParameters(s.RunningDefinition,variant);
            const auto shadow=ShadowSettings(parameters,variant.AlgorithmId);
            s.Target->SetShadowOverride(shadow);gOptions.AdvancedShadows=shadow;
            gOptions.ShadowFilterMode=1;gOptions.ShadowFilterRadiusTexels=float(parameters.at("pcfRadius"));
            s.Results.push_back({variant.Id,variant.Label,variant.AlgorithmId,"running","",task.Repetition});
            s.Info.Task=s.TaskIndex+1;PhaseLabel(Phase::Warmup);
        }
    }
    catch(const std::exception& e)
    {
        if(s.Writer.valid()){try{s.Writer.get();}catch(...){}}
        if(s.Info.Directory.empty()||!fs::is_directory(s.Info.Directory))
        {RestoreWorld();s.Info.Error=e.what();s.Info.Running=false;PhaseLabel(Phase::Idle);}
        else if(s.Step==Phase::Reporting){RestoreWorld();s.Info.Error=e.what();s.Info.Running=false;PhaseLabel(Phase::Idle);}
        else ReportAndStop("failed",e.what());
    }
}
bool ControlledUpdate()
{
    auto& s=Get();if(!s.Info.Running)return false;
    if(!s.RestorePending)return true;
    s.FrameStarted=Clock::now();g_timeDeltaSecond=float(s.RunningDefinition.FixedStep);
    const double progress=s.Step==Phase::Measure?double(s.PhaseFrames)/std::max(1u,s.RunningDefinition.MeasuredFrames-1):0.;
    PlaceCamera(progress);g_Engine->Game.Update(g_timeDeltaSecond);jObject::FlushDirtyState();return true;
}
void AfterFrame()
{
    auto& s=Get();if(!s.Info.Running||!s.RestorePending||s.Results.empty())return;
    const double cpu=std::chrono::duration<double,std::milli>(Clock::now()-s.FrameStarted).count();
    try
    {
        ++s.RenderedFrames;CollectTimings();
        const auto& stats=jAdvancedShadows::GetLastFrameStats().Lights;
        const auto found=std::find_if(stats.begin(),stats.end(),[&](const auto& light){return light.Source==s.Target;});
        auto& result=s.Results.back();
        const auto& expected=*s.Target->GetShadowOverride();
        if(s.Step==Phase::Warmup||s.Step==Phase::Measure)
        {
            if(found!=stats.end()){result.AtlasWidth=found->AtlasWidth;result.AtlasHeight=found->AtlasHeight;}
            if(found==stats.end()||!found->Active||found->ProjectionFallback||found->Requested!=expected||
                found->AtlasWidth!=2*int(expected.MapResolution)||found->AtlasHeight!=2*int(expected.MapResolution))
            {
                result.State="unavailable";result.Reason=found==stats.end()?"No output from the selected shadow adapter":found->Diagnostic;
                if(result.Reason.empty())result.Reason="Applied method, projection or resolution did not match the experiment";
                NextTask();return;
            }
            result.TargetBytes=std::max(result.TargetBytes,found->AdditionalTargetBytes);result.AtlasWidth=found->AtlasWidth;result.AtlasHeight=found->AtlasHeight;
        }
        if(s.Step==Phase::Warmup)
        {s.Info.Frame=++s.PhaseFrames;if(s.PhaseFrames>=s.RunningDefinition.WarmupFrames)PhaseLabel(Phase::Measure);}
        else if(s.Step==Phase::Measure)
        {
            s.Timings.push_back({g_rhi->GetCurrentFrameNumber(),cpu});s.Info.Frame=++s.PhaseFrames;
            if(s.PhaseFrames>=s.RunningDefinition.MeasuredFrames)PhaseLabel(Phase::Drain);
        }
        else if(s.Step==Phase::Drain)
        {
            if(++s.PhaseFrames>24)throw std::runtime_error("Timed out collecting GPU timestamps");
            if(s.Timings.empty())
            {
                if(result.SceneGpu.size()!=s.RunningDefinition.MeasuredFrames)throw std::runtime_error("Measured frame count did not match GPU samples");
                if(s.Plan[s.TaskIndex].Repetition==0)PhaseLabel(Phase::Capture);
                else {result.State="completed";NextTask();}
            }
        }
        else if(s.Step==Phase::Capture)
        {
            if(!s.CaptureFrame){if(++s.PhaseFrames>8)throw std::runtime_error("The raster HDR capture hook did not run");return;}
            g_rhi->Flush();ex::FloatImage image;image.Width=uint32_t(s.Width);image.Height=uint32_t(s.Height);
            image.Rgba.resize(uint64_t(s.Width)*s.Height*4);const auto* bytes=static_cast<const uint8_t*>(s.CaptureReadback->Map());
            if(!bytes)throw std::runtime_error("HDR readback mapping failed");
            for(int y=0;y<s.Height;++y)std::memcpy(image.Rgba.data()+size_t(y)*s.Width*4,bytes+size_t(y)*s.CapturePitch,size_t(s.Width)*16);
            s.CaptureReadback->Unmap();const auto nonfinite=ex::NonFinitePixels(image);
            if(nonfinite){result.State="invalid-output";result.Reason="HDR capture contains nonfinite pixels";s.Nonfinite+=nonfinite;}
            const auto directory=s.Info.Directory;const auto id=result.Id;const double exposure=s.RunningDefinition.DisplayExposure;
            s.Writer=std::async(std::launch::async,[directory,id,exposure,image=std::move(image)]{
                ex::SavePfm(directory/(id+".pfm"),image);ex::SavePng(directory/(id+".png"),ex::Display(image,exposure));
            });
            PhaseLabel(Phase::Saving);
        }
    }
    catch(const std::exception& e){ReportAndStop("failed",e.what());}
}
void CaptureHDR(const std::shared_ptr<jRenderFrameContext>& frame,jTexture& source) try
{
    auto& s=Get();if(!s.Info.Running||s.Step!=Phase::Capture||s.CaptureFrame)return;
    if(source.SampleCount!=EMSAASamples::COUNT_1||source.Type!=ETextureType::TEXTURE_2D)return;
    if(source.Width!=s.Width||source.Height!=s.Height)return;
    SCOPE_GPU_PROFILE(frame,RenderingLabCapture);
    if(!s.CaptureTarget)
    {
        s.CaptureTarget=g_rhi->CreateRenderTarget({.Format=ETextureFormat::RGBA32F,.Width=s.Width,.Height=s.Height,
            .TextureCreateFlag=ETextureCreateFlag::RTV|ETextureCreateFlag::TransferSrc,.ResourceName=jNameStatic("RenderingLabHDR")});
        s.CapturePitch=(uint32_t(s.Width)*16+255)&~255u;
        s.CaptureReadback=g_rhi->CreateRawBuffer(uint64_t(s.CapturePitch)*s.Height,512,EBufferCreateFlag::Readback,
            EResourceLayout::TRANSFER_DST,nullptr,0,jNameStatic("RenderingLabHDRReadback"));
    }
    if(!s.CaptureTarget||!s.CaptureReadback)throw std::runtime_error("Could not allocate bounded HDR capture resources");
    jLabCaptureUniform data;data.InvSize=Vector4(1.f/s.Width,1.f/s.Height,0,0);
    auto uniform=std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("RenderingLabCapture"),jLifeTimeType::OneFrame,sizeof(data)));
    uniform->UpdateBufferData(&data,sizeof(data));auto* command=frame->GetActiveCommandBuffer();const auto restore=source.GetLayout();
    g_rhi->TransitionLayout(command,&source,EResourceLayout::SHADER_READ_ONLY);
    jRHIUtil::DrawFullScreen(frame,s.CaptureTarget,
        [&](const auto&,jShaderBindingArray& bindings,jShaderBindingResourceInlineAllocator& allocator){
            jLabCaptureParameters p;p.LabSource={&source,TSamplerStateInfo<ETextureFilter::NEAREST,ETextureFilter::NEAREST>::Create()};p.LabCapture.Buffer=uniform;
            jShaderParameterSet::BuildShaderBindings(p,EShaderAccessStageFlag::FRAGMENT,bindings,allocator);
        },[](const auto&){
            jShaderInfo info;info.SetName(jNameStatic("RenderingLabHDRPS"));info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RenderingLabHDR_ps.hlsl"));
            info.SetShaderType(EShaderAccessStageFlag::FRAGMENT);jShaderParameterSet::AppendToShaderInfo<jLabCaptureParameters>(info,0);return g_rhi->CreateShader(info);
        });
    g_rhi->TransitionLayout(command,&source,restore);g_rhi->TransitionLayout(command,s.CaptureTarget->GetTexture(),EResourceLayout::TRANSFER_SRC);command->FlushBarrierBatch();
    jTextureCopyRegion region;region.Width=s.Width;region.Height=s.Height;region.DestRowPitchOverride=s.CapturePitch;
    g_rhi->CopyTextureRegionToBuffer(command,s.CaptureTarget->GetTexture(),region,s.CaptureReadback.get(),s.CaptureReadback->GetOffset());
    if(IsUseVulkan())
    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(command->GetHandle()),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    }
    s.CaptureFrame=g_rhi->GetCurrentFrameNumber();
}
catch(const std::exception& error){Get().Failure=error.what();}

void Render(jEditor& editor)
{
    auto& s=Get();if(!s.Visible&&!s.Info.Running)return;
    ImGui::SetNextWindowSize(ImVec2(980,760),ImGuiCond_FirstUseEver);ImGui::SetNextWindowBgAlpha(1.f);
    bool* open=s.Info.Running?nullptr:&s.Visible;
    if(!ImGui::Begin("Rendering Lab",open)){ImGui::End();return;}
    if(s.Info.Running)
    {
        ImGui::Text("Shadow Lab | %s | task %zu / %zu",s.Info.Phase.c_str(),s.Info.Task,s.Info.TaskCount);
        if(s.TaskIndex<s.Plan.size())ImGui::TextUnformatted(s.RunningDefinition.Variants[s.Plan[s.TaskIndex].VariantIndex].Label.c_str());
        ImGui::ProgressBar(s.Info.FrameCount?float(s.Info.Frame)/s.Info.FrameCount:0.f,ImVec2(-1,0));
        if(ImGui::Button("Cancel experiment"))Cancel();
        ImGui::TextWrapped("Scene editing and automatic object/light updates are paused. Settings are restored when the run finishes or is cancelled.");
        ImGui::TextWrapped("The engine renders the same scene sequentially. Captures and encoding run outside measured frames.");
        if(ImGui::IsKeyPressed(ImGuiKey_Escape))Cancel();
        ImGui::End();return;
    }
    if(!s.Info.Error.empty())ImGui::TextWrapped("%s",s.Info.Error.c_str());
    if(ImGui::BeginTabBar("LabTabs"))
    {
        if(ImGui::BeginTabItem("Experiment"))
        {
            ImGui::InputText("Name",s.Name,sizeof(s.Name));s.Definition.Name=s.Name;
            if(ImGui::Button("Store current camera in experiment")&&g_Engine&&g_Engine->Game.MainCamera)
            {
                const auto* camera=g_Engine->Game.MainCamera;ex::CameraSettings c;
                c.Position={camera->Pos.x,camera->Pos.y,camera->Pos.z};c.Target={camera->Target.x,camera->Target.y,camera->Target.z};
                c.FovDegrees=camera->FOVRad*180/3.141592653589793;c.NearClip=camera->Near;c.FarClip=camera->Far;c.Aspect=camera->ProjectionAspectOverride;
                s.Definition.Camera=c;
            }
            ImGui::SameLine();if(ImGui::Button("Use camera at run start"))s.Definition.Camera.reset();
            ImGui::TextDisabled("%s",s.Definition.Camera?"Saved camera will be restored for every variant":"Current camera will be captured when the run starts");
            ImGui::TextWrapped("Shadow Lab: first directional light, one-cascade CSM (2x2 atlas), current raster scene. Production moment methods also fit receiver/caster bounds. Other lights keep illumination but do not cast shadows during the experiment.");
            const fs::path sample=fs::path("..")/"examples"/"rendering-lab"/"project.jproject";
            if(fs::exists(sample)&&ImGui::Button("Open Shadow Lab sample"))
            {
                const auto absolute=fs::absolute(sample);
                RequestAuthoringSceneReplacement([absolute]{std::string error;if(!OpenAuthoringProject(*g_Editor,absolute,error))Get().Info.Error=error;});
            }
            int warm=int(s.Definition.WarmupFrames),measure=int(s.Definition.MeasuredFrames),repeat=int(s.Definition.Repetitions),motion=int(s.Definition.Motion);
            if(ImGui::InputInt("Warmup frames",&warm))s.Definition.WarmupFrames=uint32_t(std::clamp(warm,1,600));
            if(ImGui::InputInt("Measured frames",&measure))s.Definition.MeasuredFrames=uint32_t(std::clamp(measure,4,3600));
            if(ImGui::SliderInt("Repetitions",&repeat,1,5))s.Definition.Repetitions=uint32_t(repeat);
            const char* scenarios[]={"Static","Camera arc","Light sweep"};
            if(ImGui::Combo("Scenario",&motion,scenarios,3))s.Definition.Motion=ex::Scenario(motion);
            float degrees=float(s.Definition.MotionDegrees);if(ImGui::SliderFloat("Motion amplitude",&degrees,0,60,"%.1f deg"))s.Definition.MotionDegrees=degrees;
            float exposure=float(s.Definition.DisplayExposure);if(ImGui::SliderFloat("Display exposure",&exposure,-8,8,"%.1f EV"))s.Definition.DisplayExposure=exposure;
            float region[4];for(size_t i=0;i<4;++i)region[i]=float(s.Definition.Region[i]);
            if(ImGui::InputFloat4("Difference ROI (x,y,w,h)",region))for(size_t i=0;i<4;++i)s.Definition.Region[i]=region[i];
            auto resolutionControl=[](const char* label,double& value){int index=0;const int values[]={64,128,256,512,1024};const char* names[]={"64","128","256","512","1024"};for(int i=0;i<5;++i)if(value==values[i])index=i;if(ImGui::Combo(label,&index,names,5))value=values[index];};
            resolutionControl("Common shadow resolution",s.Definition.Common["resolution"]);
            auto number=[](const char* label,double& value,float lo,float hi,const char* format="%.3f"){float v=float(value);if(ImGui::SliderFloat(label,&v,lo,hi,format))value=v;};
            number("Maximum shadow distance",s.Definition.Common["maxDistance"],1,20000,"%.0f");
            number("Caster padding",s.Definition.Common["casterPadding"],0,5000,"%.0f");
            number("PCF radius (texels)",s.Definition.Common["pcfRadius"],0,4);
            int blur=int(s.Definition.Common["blurRadius"]);if(ImGui::SliderInt("Moment blur radius",&blur,0,8))s.Definition.Common["blurRadius"]=blur;
            number("Positive exponent",s.Definition.Common["positiveExponent"],.1f,40);
            number("Negative exponent",s.Definition.Common["negativeExponent"],.1f,40);
            number("Bleed reduction",s.Definition.Common["bleedReduction"],0,.999f);
            ImGui::SeparatorText("Algorithm variants");
            size_t duplicate=SIZE_MAX,remove=SIZE_MAX;
            for(size_t i=0;i<s.Definition.Variants.size();++i)
            {
                auto& v=s.Definition.Variants[i];ImGui::PushID(int(i));
                ImGui::Checkbox("##enabled",&v.Enabled);ImGui::SameLine();
                if(ImGui::TreeNode(v.Id.c_str(),"%s (%s)",v.Label.c_str(),v.AlgorithmId.c_str()))
                {
                    char label[129]{};std::snprintf(label,sizeof(label),"%s",v.Label.c_str());if(ImGui::InputText("Label",label,sizeof(label)))v.Label=label;
                    bool own=v.Overrides.contains("resolution");if(ImGui::Checkbox("Override resolution",&own)){if(own)v.Overrides["resolution"]=s.Definition.Common["resolution"];else v.Overrides.erase("resolution");}
                    if(own)resolutionControl("Resolution",v.Overrides["resolution"]);
                    if(s.Definition.Variants.size()<8&&ImGui::Button("Duplicate for parameter sweep"))duplicate=i;
                    if(s.Definition.Variants.size()>1){ImGui::SameLine();if(ImGui::Button("Remove"))remove=i;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(duplicate!=SIZE_MAX){auto v=s.Definition.Variants[duplicate];for(int n=1;;++n){v.Id=s.Definition.Variants[duplicate].Id+"-"+std::to_string(n);if(std::none_of(s.Definition.Variants.begin(),s.Definition.Variants.end(),[&](const auto& other){return v.Id==other.Id;}))break;}v.Label+=" copy";s.Definition.Variants.push_back(std::move(v));}
            if(remove!=SIZE_MAX)s.Definition.Variants.erase(s.Definition.Variants.begin()+remove);
            if(ImGui::BeginCombo("Comparison baseline",s.Definition.Baseline.c_str()))
            {
                for(const auto& v:s.Definition.Variants)if(v.Enabled&&ImGui::Selectable(v.Label.c_str(),s.Definition.Baseline==v.Id))s.Definition.Baseline=v.Id;
                ImGui::EndCombo();
            }
            if(!*s.OutputPath)
            {
                const auto project=GetAuthoringProjectFile();
                const auto root=(project.empty()?fs::current_path():project.parent_path())/".jeditor"/"lab-runs";
                std::snprintf(s.OutputPath,sizeof(s.OutputPath),"%s",jEditorAssetPath::ToUtf8(root).c_str());
            }
            ImGui::InputText("Results directory",s.OutputPath,sizeof(s.OutputPath));
            ImGui::InputTextWithHint("Experiment file","Absolute path to a .jlab JSON file",s.SpecPath,sizeof(s.SpecPath));
            if(ImGui::Button("Save experiment"))try
            {
                std::string error;if(!ex::Save(jEditorAssetPath::FromUtf8(s.SpecPath),s.Definition,error))throw std::runtime_error(error);s.Info.Error.clear();
            }catch(const std::exception& e){s.Info.Error=e.what();}
            ImGui::SameLine();if(ImGui::Button("Load experiment"))try
            {
                ex::Definition parsed;std::string error;
                if(!ex::Load(jEditorAssetPath::FromUtf8(s.SpecPath),parsed,error))throw std::runtime_error(error);
                s.Definition=std::move(parsed);std::snprintf(s.Name,sizeof(s.Name),"%s",s.Definition.Name.c_str());s.Info.Error.clear();
            }catch(const std::exception& e){s.Info.Error=e.what();}
            std::string error;const bool valid=ex::Validate(s.Definition,error);
            if(!valid)ImGui::TextWrapped("%s",error.c_str());
            ImGui::BeginDisabled(!valid||editor.GetDocument().IsPlaying());
            if(ImGui::Button("Run experiment"))Start(s.Definition,jEditorAssetPath::FromUtf8(s.OutputPath),s.Info.Error);
            ImGui::EndDisabled();ImGui::EndTabItem();
        }
        const auto resultFlags=s.ShowResults?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;
        if(ImGui::BeginTabItem("Results",nullptr,resultFlags))
        {
            s.ShowResults=false;
            if(s.CompletedReport.is_null())ImGui::TextWrapped("Run an experiment to compare its actual output and GPU measurements.");
            else
            {
                ImGui::Text("Status: %s",s.CompletedReport.value("status","unknown").c_str());
                ImGui::TextWrapped("Results: %s",jEditorAssetPath::ToUtf8(s.Info.Directory).c_str());
                if(ImGui::Button("Copy report path"))ImGui::SetClipboardText(jEditorAssetPath::ToUtf8(s.Info.Directory/"report.html").c_str());
                if(ImGui::BeginTable("LabSummary",5,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable))
                {
                    for(const char* title:{"Variant","GPU median / P95","Shadow median","HDR RMSE","Target MiB"})ImGui::TableSetupColumn(title);
                    ImGui::TableHeadersRow();
                    for(const auto& row:s.CompletedReport["summary"])
                    {
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(row["label"].get<std::string>().c_str());
                        ImGui::TableNextColumn();if(row["sceneGpu"].is_object())ImGui::Text("%.3f / %.3f",row["sceneGpu"]["medianMs"].get<double>(),row["sceneGpu"]["p95Ms"].get<double>());else ImGui::TextDisabled("Unavailable");
                        ImGui::TableNextColumn();if(row["shadowGpu"].is_object())ImGui::Text("%.3f",row["shadowGpu"]["medianMs"].get<double>());else ImGui::TextDisabled("Unavailable");
                        ImGui::TableNextColumn();if(row["baselineDifference"].is_object()&&row["baselineDifference"].value("valid",false))ImGui::Text("%.5g",row["baselineDifference"]["rmse"].get<double>());else ImGui::TextDisabled("Unavailable");
                        ImGui::TableNextColumn();ImGui::Text("%.2f",row["estimatedShadowTargetBytes"].get<double>()/1048576);
                    }
                    ImGui::EndTable();
                }
                ImGui::TextWrapped("RMSE measures difference from the selected baseline, not physical correctness. Target bytes are the selected shadow adapter's estimate, not total VRAM.");
                if(!s.Images.empty())
                {
                    auto choose=[&](const char* label,int& selected){bool changed=false;if(ImGui::BeginCombo(label,s.ImageLabels[size_t(selected)].c_str())){for(size_t i=0;i<s.ImageLabels.size();++i)if(ImGui::Selectable(s.ImageLabels[i].c_str(),selected==int(i))){selected=int(i);changed=true;}ImGui::EndCombo();}return changed;};
                    bool changed=choose("A",s.CompareA);changed|=choose("B",s.CompareB);
                    const char* modes[]={"Side by side","Swipe","Difference"};
                    if(ImGui::Combo("Compare mode",&s.CompareMode,modes,3))changed=true;
                    if(s.CompareMode==1)ImGui::SliderFloat("Split",&s.Split,0,1);
                    if((changed||!s.Difference.Id)&&s.CompareMode==2)try
                    {
                        if(s.Difference.Id){g_rhi->Flush();Unregister(s.Difference);}
                        auto a=ex::LoadPfm(s.Info.Directory/(s.ImageIds[size_t(s.CompareA)]+".pfm"));
                        auto b=ex::LoadPfm(s.Info.Directory/(s.ImageIds[size_t(s.CompareB)]+".pfm"));
                        s.Difference=Upload(ex::Heatmap(a,b,4,768));s.PreviewError.clear();
                    }catch(const std::exception& e){s.PreviewError=e.what();}
                    const float aspect=s.Pose.Aspect>0?s.Pose.Aspect:float(s.Width)/std::max(1,s.Height);
                    auto size=ImGui::GetContentRegionAvail();float width=std::max(1.f,size.x);
                    if(s.CompareMode==0)width=(width-12)*.5f;
                    const float height=std::min(380.f,width/aspect);width=height*aspect;
                    if(s.CompareMode==0){ImGui::Image(s.Images[size_t(s.CompareA)].Id,ImVec2(width,height));ImGui::SameLine();ImGui::Image(s.Images[size_t(s.CompareB)].Id,ImVec2(width,height));}
                    else if(s.CompareMode==1)
                    {
                        auto start=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
                        ImGui::Image(s.Images[size_t(s.CompareA)].Id,ImVec2(width,height));
                        draw->PushClipRect(start,ImVec2(start.x+width*s.Split,start.y+height),true);
                        draw->AddImage(s.Images[size_t(s.CompareB)].Id,start,ImVec2(start.x+width,start.y+height));draw->PopClipRect();
                        draw->AddLine(ImVec2(start.x+width*s.Split,start.y),ImVec2(start.x+width*s.Split,start.y+height),IM_COL32(255,210,50,255),2);
                    }
                    else if(s.Difference.Id)ImGui::Image(s.Difference.Id,ImVec2(width,height));
                }
                if(!s.PreviewError.empty())ImGui::TextWrapped("%s",s.PreviewError.c_str());
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
void Shutdown()
{
    auto& s=Get();if(s.Writer.valid()){try{s.Writer.get();}catch(...){}}
    RestoreWorld();ClearImages();s.Info.Running=false;s.StartRequested=false;s.Step=Phase::Idle;
}
int RunFromCommandLine(const char* definitionFile,const char* outputRoot)
{
    auto& s=Get();try
    {
        ex::Definition definition;std::string error;
        if(!ex::Parse(ReadText(jEditorAssetPath::FromUtf8(definitionFile),65536),definition,error))throw std::runtime_error(error);
        if(!Start(definition,jEditorAssetPath::FromUtf8(outputRoot),error))throw std::runtime_error(error);
        while(IsRunning())
        {
            g_Engine->ProcessInput(float(definition.FixedStep));g_rhi->IncrementFrameNumber();
            g_Engine->Update(float(definition.FixedStep));g_Engine->Draw();g_Engine->EndOfFrame();
#ifdef _WIN32
            if(IsUseDX12())
            {
                MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE))
                {if(message.message==WM_QUIT)Cancel();TranslateMessage(&message);DispatchMessage(&message);}
            }
            else
#endif
            {auto* window=static_cast<GLFWwindow*>(g_rhi->GetWindow());glfwPollEvents();if(glfwWindowShouldClose(window))Cancel();}
        }
        const bool success=s.CompletedReport.is_object()&&s.CompletedReport.value("status","")=="completed";
        std::cout<<"Rendering Lab: "<<(success?"completed":"not completed")<<"; report="<<jEditorAssetPath::ToUtf8(s.Info.Directory/"report.html")<<std::endl;
        if(!s.Info.Error.empty())std::cerr<<s.Info.Error<<std::endl;return success?0:1;
    }
    catch(const std::exception& e){Shutdown();std::cerr<<"Rendering Lab: "<<e.what()<<std::endl;return 1;}
}
int ExportShaderFixtures(const char* directory)
{
    try
    {
        const fs::path root(directory);
        WriteText(root/"lab-hdr.hlsl",jShaderParameterSet::GenerateHLSL<jLabCaptureParameters>(0)+"\n#include \"RenderingLabHDR_ps.hlsl\"\n");
        Json fields=Json::object();for(const auto& f:jLabCaptureUniform::GetShaderUniformBufferFieldMembers())fields[f.Name]=f.ByteOffset;
        WriteText(root/"lab-cpu-layout.json",Json{{jLabCaptureUniform::GetShaderUniformBufferTypeName(),{{"size",sizeof(jLabCaptureUniform)},{"fields",fields}}}}.dump(2));
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
}
