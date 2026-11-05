#include "pch.h"
#include "jSceneFeatureTools.h"
#include "jEditorProjectSettings.h"
#include "Code/Engine/jEditor.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jImageFileLoader.h"
#include "Renderer/Features/jEnvironmentLighting.h"
#include "External/stb/stb_image.h"
#include "External/loadpng/lodepng.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "jOptions.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <thread>

namespace jSceneFeatureTools
{
namespace
{
namespace fs=std::filesystem;
constexpr std::uint64_t MaximumPixels=4194304,MaximumInputBytes=64*1024*1024;
constexpr const char* Marker="horizon-bake.json";
struct Progress {std::atomic<bool> Cancel{false};std::atomic<int> Phase{0};};
struct Job
{
    std::string ProjectId,AssetRoot;
    fs::path Root,Source,Output;
    BakeRequest Request;
    std::string EnvironmentAssetBefore,EnvironmentEnabledBefore;
};
struct Result
{
    Job Snapshot;
    bool Succeeded=false,Cancelled=false;
    std::string Message;
    std::vector<std::string> Outputs;
};
struct PendingSettings {std::string Id;fs::path Root;jEditorProjectConfig Config;};
struct Panel
{
    const std::thread::id Owner=std::this_thread::get_id();
    bool Visible=false,Initialized=false,Changed=false;
    std::string ProjectId,Asset;
    fs::path Root;
    jEditorProjectConfig Draft;
    jSceneFeatures::Settings Features;
    char EnvironmentSource[4097]{},EnvironmentOutput[4097]{},EnvironmentAsset[4097]{};
    char HorizonSource[4097]{},HorizonOutput[4097]{};
    int Resolution=128,Radius=16;
    float HeightScale=1,TexelSize=1;
    bool Wrap=true;
    std::future<Result> Future;
    std::shared_ptr<Progress> Work;
    BakeStatus Status;
    std::optional<PendingSettings> Pending;
};
Panel& State(){static Panel state;return state;}
bool Owner(){return State().Owner==std::this_thread::get_id();}
bool Matches(const jEditorProject* project,const std::string& id,const fs::path& root)
{return project && project->GetConfig().Id==id && project->GetRoot()==root;}
bool ChildOf(const fs::path& root,const fs::path& path)
{
    const auto relative=path.lexically_relative(root);
    return !relative.empty() && relative!="." && *relative.begin()!="..";
}
struct BakeCancelled : std::exception
{const char* what()const noexcept override{return "Bake cancelled.";}};
void CheckCancel(const std::shared_ptr<Progress>& progress)
{if(progress->Cancel.load())throw BakeCancelled{};}
bool ValidateJob(const jEditorProject& project,const BakeRequest& request,Job& out,std::string& error)
{
    if(project.GetRoot().empty() || project.GetConfig().Id.empty()){error="Open a project before baking.";return false;}
    if(request.Kind!=BakeKind::EnvironmentSH && request.Kind!=BakeKind::Horizon){error="Unknown bake operation.";return false;}
    if(request.SourcePath.size()>4096 || request.OutputPath.size()>4096){error="Bake path exceeds 4096 bytes.";return false;}
    const auto source=jEditorProject::ResolveSafe(project.GetRoot(),request.SourcePath,error,true);
    if(!source)return false;
    const auto output=jEditorProject::ResolveSafe(project.GetRoot(),request.OutputPath,error,false);
    if(!output)return false;
    const auto assetRoot=jEditorProject::ResolveSafe(project.GetRoot(),project.GetConfig().AssetRoot,error,true);
    if(!assetRoot)return false;
    if(!fs::is_regular_file(*source) || !ChildOf(*assetRoot,*output) || *source==*output)
    {error="Choose an existing source image and a separate output inside the project's asset folder.";return false;}
    if(request.Kind==BakeKind::EnvironmentSH && output->extension()!=".jsh")
    {error="Environment output must be a project-relative .jsh file.";return false;}
    if(request.Kind==BakeKind::Horizon && (request.HorizonResolution<32 || request.HorizonResolution>512
        || request.RadiusTexels<1 || request.RadiusTexels>64 || !std::isfinite(request.HeightScale)
        || request.HeightScale<0 || request.HeightScale>1e6f || !std::isfinite(request.TexelSize)
        || request.TexelSize<=0 || request.TexelSize>1e6f
        || (request.Edges!=jHorizonMapping::Boundary::Clamp && request.Edges!=jHorizonMapping::Boundary::Wrap)))
    {error="Horizon requires resolution32..512, radius1..64 and finite physical scales.";return false;}
    out={project.GetConfig().Id,project.GetConfig().AssetRoot,project.GetRoot(),*source,*output,request};
    const auto value=[&](const char* key,const char* fallback)
    {const auto found=project.GetConfig().Settings.find(key);return found==project.GetConfig().Settings.end()?std::string(fallback):found->second;};
    out.EnvironmentAssetBefore=value("render.environment.shAsset","");
    out.EnvironmentEnabledBefore=value("render.environment.enabled","false");
    return true;
}
std::vector<unsigned char> ReadImage(const fs::path& path)
{
    const auto size=fs::file_size(path);
    if(!size || size>MaximumInputBytes)throw std::runtime_error("Source image must be nonempty and at most 64 MiB.");
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read source image.");
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(size));
    if(static_cast<std::uint64_t>(stream.gcount())!=size || stream.bad())throw std::runtime_error("Source image changed or could not be read.");
    char extra;
    if(stream.get(extra))throw std::runtime_error("Source image grew while being read.");
    return data;
}
void CheckDimensions(int width,int height)
{
    if(width<=0 || height<=0 || width>16384 || height>16384
        || std::uint64_t(width)*height>MaximumPixels)
        throw std::runtime_error("Source image must contain at most four million pixels with dimensions at most 16384.");
}
struct DirectoryCleanup
{
    fs::path Path;
    ~DirectoryCleanup(){if(!Path.empty()){std::error_code ignored;fs::remove_all(Path,ignored);}}
};
fs::path UniqueSibling(const fs::path& output,const char* role)
{
    static std::atomic<std::uint64_t> sequence{0};
    auto path=output.parent_path()/fs::path(".jengine-bake-");
    path+=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        +"-"+std::to_string(sequence.fetch_add(1))+"-"+role;
    return path;
}
void WriteBytes(const fs::path& path,const std::vector<unsigned char>& bytes)
{
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    stream.flush();if(!stream)throw std::runtime_error("Cannot finish baked image staging write.");
    stream.close();if(!stream)throw std::runtime_error("Cannot close baked image staging file.");
}
void WriteText(const fs::path& path,const std::string& text)
{WriteBytes(path,{text.begin(),text.end()});}
bool ManagedDirectory(const fs::path& path)
{
    if(!fs::exists(path))return true;
    if(!fs::is_directory(path) || fs::is_symlink(fs::symlink_status(path)))return false;
    const std::set<std::string> allowed{"layer0.png","layer1.png","ao.png",Marker,
        "layer0.png.jasset","layer1.png.jasset","ao.png.jasset",std::string(Marker)+".jasset"};
    std::size_t count=0;
    for(const auto& entry:fs::directory_iterator(path))
    {
        if(++count>allowed.size() || !entry.is_regular_file() || entry.is_symlink()
            || !allowed.count(jEditorAssetPath::ToUtf8(entry.path().filename())))return false;
    }
    const auto marker=path/Marker;
    if(!fs::is_regular_file(marker) || fs::file_size(marker)>65536)return false;
    std::ifstream stream(marker);
    const auto json=nlohmann::json::parse(stream);
    return json.is_object() && json.value("format",std::string())=="jEngine.HorizonBake" && json.value("version",0)==1;
}
void RevalidateOutput(const Job& job)
{
    std::string error;
    const auto output=jEditorProject::ResolveSafe(job.Root,job.Request.OutputPath,error,false);
    const auto assets=jEditorProject::ResolveSafe(job.Root,job.AssetRoot,error,true);
    if(!output || !assets || *output!=job.Output || !ChildOf(*assets,*output))
        throw std::runtime_error("Bake output path changed or escaped the active asset folder.");
}
void PublishHorizon(const Job& job,const jHorizonMapping::EncodedLayers& layers,const std::shared_ptr<Progress>& progress)
{
    RevalidateOutput(job);
    fs::create_directories(job.Output.parent_path());
    if(!ManagedDirectory(job.Output))throw std::runtime_error("Horizon output already contains non-bake files; choose a new output folder.");
    DirectoryCleanup staging;
    for(int attempt=0;attempt<16;++attempt)
    {
        const auto path=UniqueSibling(job.Output,"stage");
        if(fs::create_directory(path)){staging.Path=path;break;}
    }
    if(staging.Path.empty())throw std::runtime_error("Cannot create horizon staging directory.");
    const auto count=std::size_t(layers.Width)*layers.Height;
    std::vector<unsigned char> pixels(count*4),encoded;
    const auto quantize=[](float value){return static_cast<unsigned char>(std::lround(std::clamp(value,0.f,1.f)*255.f));};
    for(int layer=0;layer<2;++layer)
    {
        CheckCancel(progress);
        const auto& source=layer==0?layers.Layer0:layers.Layer1;
        for(std::size_t i=0;i<count;++i)for(std::size_t c=0;c<4;++c)pixels[i*4+c]=quantize(source[i][c]);
        if(const unsigned code=LodePNG::encode(encoded,pixels,layers.Width,layers.Height,6,8);code)
            throw std::runtime_error("PNG encoding failed: "+std::to_string(code));
        WriteBytes(staging.Path/(layer==0?"layer0.png":"layer1.png"),encoded);
    }
    pixels.resize(count);
    for(std::size_t i=0;i<count;++i)pixels[i]=quantize(layers.AmbientVisibility[i]);
    if(const unsigned code=LodePNG::encode(encoded,pixels,layers.Width,layers.Height,0,8);code)
        throw std::runtime_error("AO PNG encoding failed: "+std::to_string(code));
    WriteBytes(staging.Path/"ao.png",encoded);
    nlohmann::json metadata={{"format","jEngine.HorizonBake"},{"version",1},{"source",job.Request.SourcePath},
        {"resolution",layers.Width},{"radiusTexels",job.Request.RadiusTexels},{"heightScale",job.Request.HeightScale},
        {"texelSize",job.Request.TexelSize},{"wrap",job.Request.Edges==jHorizonMapping::Boundary::Wrap}};
    WriteText(staging.Path/Marker,metadata.dump(2));
    // Registry sidecars own the stable asset IDs. Carry their exact bytes to
    // the replacement directory; the registry refreshes fingerprints/revisions
    // against the new PNG files without breaking material dependency IDs.
    for(const char* file:{"layer0.png.jasset","layer1.png.jasset","ao.png.jasset","horizon-bake.json.jasset"})
    {
        CheckCancel(progress);
        const auto previous=job.Output/file;
        if(!fs::exists(previous))continue;
        std::ifstream input(previous,std::ios::binary);
        if(!input)throw std::runtime_error("Cannot preserve existing bake asset metadata.");
        std::vector<unsigned char> bytes;
        std::array<char,16384> chunk{};
        do
        {
            input.read(chunk.data(),static_cast<std::streamsize>(chunk.size()));
            const auto count=static_cast<std::size_t>(input.gcount());
            if(bytes.size()+count>16*1024*1024)throw std::runtime_error("Existing bake metadata exceeds its size limit.");
            bytes.insert(bytes.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
            if(input.bad())throw std::runtime_error("Cannot preserve existing bake asset metadata.");
        }while(!input.eof() && input);
        if(!input.eof())throw std::runtime_error("Cannot finish reading existing bake asset metadata.");
        WriteBytes(staging.Path/file,bytes);
    }
    CheckCancel(progress);
    RevalidateOutput(job);
    // Only generated, marked bake folders can be replaced. A failed publish
    // restores the previous folder; arbitrary user folders are never removed.
    progress->Phase.store(2);
    CheckCancel(progress);
    fs::path backup;
    if(fs::exists(job.Output)){backup=UniqueSibling(job.Output,"previous");fs::rename(job.Output,backup);}
    std::error_code ec;fs::rename(staging.Path,job.Output,ec);
    if(ec)
    {
        if(!backup.empty())
        {
            std::error_code rollback;fs::rename(backup,job.Output,rollback);
            if(rollback)throw std::runtime_error("Bake publication failed. Previous output preserved at "+jEditorAssetPath::ToUtf8(backup));
        }
        throw std::runtime_error("Cannot publish horizon output: "+ec.message());
    }
    staging.Path.clear();
    if(!backup.empty())
    {
        // Publication has committed. Cleanup cannot turn that success into a
        // failure, and an unexpected user-added backup file is retained.
        try{if(ManagedDirectory(backup)){std::error_code ignored;fs::remove_all(backup,ignored);}}catch(...){}
    }
}
Result RunJob(Job job,const std::shared_ptr<Progress>& progress)
{
    Result result;result.Snapshot=std::move(job);
    try
    {
        auto& snapshot=result.Snapshot;
        CheckCancel(progress);
        std::string error;
        const auto source=jEditorProject::ResolveSafe(snapshot.Root,snapshot.Request.SourcePath,error,true);
        if(!source || *source!=snapshot.Source)throw std::runtime_error("Bake source path changed.");
        auto data=ReadImage(snapshot.Source);
        int width=0,height=0,channels=0;
        if(!stbi_info_from_memory(data.data(),static_cast<int>(data.size()),&width,&height,&channels))
            throw std::runtime_error("Unsupported or invalid source image.");
        CheckDimensions(width,height);CheckCancel(progress);
        progress->Phase.store(1);
        if(snapshot.Request.Kind==BakeKind::EnvironmentSH)
        {
            std::unique_ptr<float,decltype(&stbi_image_free)> decoded(stbi_loadf_from_memory(data.data(),
                static_cast<int>(data.size()),&width,&height,&channels,3),stbi_image_free);
            if(!decoded)throw std::runtime_error("Cannot decode source HDR/linear environment image.");
            CheckDimensions(width,height);CheckCancel(progress);
            std::vector<jEnvironmentLighting::RGB> pixels(std::size_t(width)*height);
            for(int y=0;y<height;++y)
            {
                CheckCancel(progress);
                for(int x=0;x<width;++x)
                {const auto i=std::size_t(y)*width+x;pixels[i]={decoded.get()[3*i],decoded.get()[3*i+1],decoded.get()[3*i+2]};}
            }
            decoded.reset();
            jEnvironmentLighting::Coefficients coefficients;
            jEnvironmentLighting::ProjectionOptions options;options.Cancelled=[progress]{return progress->Cancel.load();};
            if(!jEnvironmentLighting::ProjectEquirectangular({std::uint32_t(width),std::uint32_t(height),pixels.data(),pixels.size()},
                coefficients,error,options)){CheckCancel(progress);throw std::runtime_error(error);}
            jSceneFeatures::Settings check;check.Environment.Radiance=coefficients;
            jSceneFeatures::Settings validated;
            if(!jSceneFeatures::ValidateSettings(check,validated,error))throw std::runtime_error(error);
            result.Outputs={snapshot.Request.OutputPath};
            CheckCancel(progress);RevalidateOutput(snapshot);
            fs::create_directories(snapshot.Output.parent_path());
            progress->Phase.store(2);CheckCancel(progress);
            if(!jEnvironmentLighting::SaveAsset(snapshot.Output,coefficients,error))throw std::runtime_error(error);
        }
        else
        {
            std::unique_ptr<unsigned char,decltype(&stbi_image_free)> decoded(stbi_load_from_memory(data.data(),
                static_cast<int>(data.size()),&width,&height,&channels,4),stbi_image_free);
            if(!decoded)throw std::runtime_error("Cannot decode source height image.");
            CheckDimensions(width,height);CheckCancel(progress);
            const auto resolution=snapshot.Request.HorizonResolution;
            std::vector<float> samples(std::size_t(resolution)*resolution);
            const auto coordinate=[&](int value,int size)
            {
                if(snapshot.Request.Edges==jHorizonMapping::Boundary::Clamp)return std::clamp(value,0,size-1);
                value%=size;return value<0?value+size:value;
            };
            const auto read=[&](int x,int y){return decoded.get()[(std::size_t(coordinate(y,height))*width+coordinate(x,width))*4]/255.f;};
            for(std::uint32_t y=0;y<resolution;++y)
            {
                CheckCancel(progress);
                for(std::uint32_t x=0;x<resolution;++x)
                {
                    const double sx=(x+.5)*width/resolution-.5,sy=(y+.5)*height/resolution-.5;
                    const int x0=int(std::floor(sx)),y0=int(std::floor(sy));
                    const float fx=float(sx-x0),fy=float(sy-y0);
                    samples[std::size_t(y)*resolution+x]=
                        (read(x0,y0)*(1-fx)+read(x0+1,y0)*fx)*(1-fy)+(read(x0,y0+1)*(1-fx)+read(x0+1,y0+1)*fx)*fy;
                }
            }
            decoded.reset();
            jHorizonMapping::BakeSettings options;options.RadiusTexels=snapshot.Request.RadiusTexels;
            options.Edges=snapshot.Request.Edges;options.Cancelled=[progress]{return progress->Cancel.load();};
            jHorizonMapping::HorizonMap baked;jHorizonMapping::EncodedLayers layers;
            if(!jHorizonMapping::Bake({resolution,resolution,samples.data(),samples.size(),snapshot.Request.HeightScale,
                snapshot.Request.TexelSize,snapshot.Request.TexelSize},options,baked,error)
                || !jHorizonMapping::EncodeLayers(baked,layers,error)){CheckCancel(progress);throw std::runtime_error(error);}
            for(const char* file:{"layer0.png","layer1.png","ao.png"})
                result.Outputs.push_back(jEditorAssetPath::ToUtf8(jEditorAssetPath::FromUtf8(snapshot.Request.OutputPath)/file));
            PublishHorizon(snapshot,layers,progress);
        }
        result.Succeeded=true;result.Message="Bake completed.";
    }
    catch(const BakeCancelled& exception){result.Outputs.clear();result.Cancelled=true;result.Message=exception.what();}
    catch(const std::exception& exception){result.Outputs.clear();result.Message=exception.what();}
    return result;
}
void LoadDraft(const jEditorProject& project,bool resetPaths)
{
    auto& state=State();state.Draft=project.GetConfig();state.ProjectId=state.Draft.Id;state.Root=project.GetRoot();
    std::string error;
    if(!jEditorProjectSettings::ReadSceneFeatures(state.Draft,state.Root,state.Features,state.Asset,error))state.Status.Message=error;
    std::snprintf(state.EnvironmentAsset,sizeof(state.EnvironmentAsset),"%s",state.Asset.c_str());
    if(resetPaths)
    {
        const auto& assets=state.Draft.AssetRoot;
        std::snprintf(state.EnvironmentSource,sizeof(state.EnvironmentSource),"%s/environment.hdr",assets.c_str());
        std::snprintf(state.EnvironmentOutput,sizeof(state.EnvironmentOutput),"%s/Baked/environment.jsh",assets.c_str());
        std::snprintf(state.HorizonSource,sizeof(state.HorizonSource),"%s/height.png",assets.c_str());
        std::snprintf(state.HorizonOutput,sizeof(state.HorizonOutput),"%s/Baked/horizon",assets.c_str());
    }
    state.Initialized=true;state.Changed=false;
}
void SyncDraft()
{
    auto& state=State();std::string error;
    if(!jEditorProjectSettings::WriteSceneFeatures(state.Draft,state.Features,state.EnvironmentAsset,error))
        state.Status.Message=error;
}
}

void SetVisible(bool visible){if(Owner())State().Visible=visible;}
bool IsVisible(){return Owner() && State().Visible;}
bool StartBake(const jEditorProject& project,const BakeRequest& request,std::string& error)
{
    error.clear();
    if(!Owner()){error="Bake requests belong to the editor owner thread.";return false;}
    auto& state=State();
    if(state.Future.valid()){error="Wait for the current bake to finish or cancel it.";return false;}
    try
    {
        Job job;if(!ValidateJob(project,request,job,error))return false;
        if(!state.Initialized || !Matches(&project,state.ProjectId,state.Root))LoadDraft(project,true);
        auto progress=std::make_shared<Progress>();
        auto future=std::async(std::launch::async,[job,progress]{return RunJob(job,progress);});
        state.Work=std::move(progress);state.Future=std::move(future);
        state.Status={true,false,false,false,"Baking on a CPU worker.",{}};return true;
    }
    catch(const std::exception& exception){error=exception.what();return false;}
}
void CancelBake()
{
    if(!Owner())return;
    auto& state=State();
    if(state.Work && state.Work->Phase.load()<2){state.Work->Cancel.store(true);state.Status.Message="Cancelling bake...";}
}
BakeStatus GetBakeStatus()
{
    if(!Owner())return {};
    auto status=State().Status;status.Running=State().Future.valid();
    status.Saving=State().Work && State().Work->Phase.load()>=2;return status;
}
void Tick(jEditor& editor,jEditorProject* project)
{
    if(!Owner())return;
    auto& state=State();
    const bool changedProject=!Matches(project,state.ProjectId,state.Root);
    if(changedProject)
    {
        CancelBake();state.Pending.reset();state.Initialized=false;state.ProjectId.clear();state.Root.clear();
        if(!state.Future.valid())state.Status={};
        if(project && !project->GetRoot().empty())LoadDraft(*project,true);
    }
    if(state.Future.valid() && state.Future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)
    {
        auto result=state.Future.get();state.Work.reset();
        state.Status={false,false,result.Succeeded,result.Cancelled,result.Message,result.Outputs};
        if(result.Succeeded && Matches(project,result.Snapshot.ProjectId,result.Snapshot.Root))
        {
            for(const auto& output:result.Outputs)
            {
                std::string error;const auto absolute=jEditorProject::ResolveSafe(project->GetRoot(),output,error,true);
                if(absolute)jImageFileLoader::GetInstance().InvalidateImageData(jName(jEditorAssetPath::ToUtf8(*absolute)));
                editor.ReimportAsset(output);
            }
            if(result.Snapshot.Request.Kind==BakeKind::EnvironmentSH && result.Snapshot.Request.UseEnvironmentAfterBake)
            {
                auto config=project->GetConfig();
                const auto value=[&](const char* key,const char* fallback)
                {const auto found=config.Settings.find(key);return found==config.Settings.end()?std::string(fallback):found->second;};
                const bool environmentChanged=value("render.environment.shAsset","")!=result.Snapshot.EnvironmentAssetBefore
                    || value("render.environment.enabled","false")!=result.Snapshot.EnvironmentEnabledBefore || bool(state.Pending);
                if(environmentChanged)
                {state.Status.Message="SH asset saved. Environment settings changed during the bake; select the new asset when ready.";}
                else
                {
                config.Settings["render.environment.shAsset"]=result.Snapshot.Request.OutputPath;
                config.Settings["render.environment.enabled"]="true";
                std::string error;
                if(!jEditorProjectSettings::Save(*project,editor,config,error))state.Status.Message="SH asset saved; settings were retained: "+error;
                else if(!state.Changed)LoadDraft(*project,false);
                else
                {
                    state.Asset=result.Snapshot.Request.OutputPath;
                    std::snprintf(state.EnvironmentAsset,sizeof(state.EnvironmentAsset),"%s",state.Asset.c_str());
                    state.Features.Environment.Enabled=true;
                    jEnvironmentLighting::LoadAsset(result.Snapshot.Output,state.Features.Environment.Radiance,error);
                    SyncDraft();
                }
                }
            }
        }
        else if(result.Succeeded)state.Status.Message="Bake saved for the previous project; active project settings retained.";
    }
    if(state.Pending)
    {
        auto pending=std::move(*state.Pending);state.Pending.reset();std::string error;
        if(!Matches(project,pending.Id,pending.Root))state.Status.Message="Project changed; queued settings were discarded.";
        else
        {
            jSceneFeatures::Settings features;std::string asset;
            auto merged=project->GetConfig();
            if(jEditorProjectSettings::ReadSceneFeatures(pending.Config,project->GetRoot(),features,asset,error)
                && jEditorProjectSettings::WriteSceneFeatures(merged,features,asset,error)
                && jEditorProjectSettings::Save(*project,editor,merged,error))
            {LoadDraft(*project,false);state.Status.Message="Scene feature settings saved and applied.";}
            else state.Status.Message="Scene feature settings were retained: "+error;
        }
    }
}
void Shutdown()
{
    if(!Owner())return;
    auto& state=State();if(state.Work)state.Work->Cancel.store(true);
    if(state.Future.valid()){try{state.Future.get();}catch(...){}}
    state.Work.reset();state.Pending.reset();state.Status.Running=false;state.Visible=false;state.Initialized=false;
    state.ProjectId.clear();state.Root.clear();
}
void Render(jEditor& editor,jEditorProject* project)
{
    (void)editor;
    if(!Owner() || !State().Visible || !ImGui::GetCurrentContext())return;
    auto& state=State();
    ImGui::SetNextWindowSize(ImVec2(580,720),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Scene Features and Baking",&state.Visible)){ImGui::End();return;}
    if(!project || project->GetRoot().empty()){ImGui::TextWrapped("Open a project to configure raster scene features and bake assets.");ImGui::End();return;}
    if(!state.Initialized || !Matches(project,state.ProjectId,state.Root))LoadDraft(*project,true);
    ImGui::TextWrapped("Raster scene features. Save and Apply publishes validated settings at the next frame boundary.");
    auto& e=state.Features.Environment;auto& p=state.Features.Reflection;auto& a=state.Features.Atmosphere;
    bool changed=false;
    if(ImGui::CollapsingHeader("SH diffuse environment",ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed|=ImGui::Checkbox("Enable SH environment",&e.Enabled);
        changed|=ImGui::SliderFloat("Environment intensity",&e.Intensity,0,100);
        changed|=ImGui::Checkbox("Combine with diffuse GI",&e.CombineWithDiffuseGI);
        changed|=ImGui::InputText("SH asset (.jsh)",state.EnvironmentAsset,sizeof(state.EnvironmentAsset));
        ImGui::TextWrapped("Empty asset uses uniform linear radiance 0.1. Diffuse GI replaces SH by default.");
    }
    if(ImGui::CollapsingHeader("Pixel projected reflection"))
    {
        changed|=ImGui::Checkbox("Enable PPR",&p.Enabled);
        ImGui::TextWrapped("Requires Deferred, stored GBuffer attachments and single-sample scene inputs.");
        changed|=ImGui::InputFloat4("Plane nx,ny,nz,d",p.Plane.data());
        changed|=ImGui::DragFloat("Plane thickness",&p.PlaneThickness,.1f,.0001f,100000.f);
        changed|=ImGui::SliderFloat("Reflection intensity",&p.Intensity,0,10);
        changed|=ImGui::SliderFloat("Roughness cutoff",&p.RoughnessThreshold,0,1);
        changed|=ImGui::SliderFloat("Normal threshold",&p.NormalThreshold,0,1);
        changed|=ImGui::Checkbox("Fill reflection holes",&p.FillHoles);
        changed|=ImGui::SliderFloat("Reflection edge fade",&p.EdgeFade,0,.5f);
    }
    if(ImGui::CollapsingHeader("Rayleigh / Mie atmosphere"))
    {
        changed|=ImGui::Checkbox("Enable atmosphere",&a.Enabled);
        changed|=ImGui::SliderFloat("Atmosphere intensity",&a.Intensity,0,100);
        changed|=ImGui::DragFloat("Meters per world unit",&a.MetersPerWorldUnit,.001f,.000001f,100000.f,"%.6g");
        changed|=ImGui::DragFloat("Ground height",&a.GroundHeight,.1f,-1e7f,1e7f);
        changed|=ImGui::Checkbox("Follow scene directional light",&a.UseSceneDirectionalLight);
        changed|=ImGui::InputFloat3("Fallback sun direction",a.SunDirection.data());
        changed|=ImGui::InputFloat3("Solar irradiance",a.SolarIrradiance.data());
        int view=int(a.ViewSamples),light=int(a.LightSamples);
        if(ImGui::SliderInt("View samples",&view,4,64)){a.ViewSamples=std::uint32_t(view);changed=true;}
        if(ImGui::SliderInt("Light samples",&light,1,16)){a.LightSamples=std::uint32_t(light);changed=true;}
        if(ImGui::TreeNode("Atmosphere density"))
        {
            changed|=ImGui::InputFloat("Atmosphere height (m)",&a.AtmosphereHeightMeters);
            changed|=ImGui::InputFloat("Max distance (m)",&a.MaxDistanceMeters);
            changed|=ImGui::InputFloat("Rayleigh scale height (m)",&a.RayleighScaleHeightMeters);
            changed|=ImGui::InputFloat("Mie scale height (m)",&a.MieScaleHeightMeters);
            changed|=ImGui::InputFloat3("Rayleigh RGB",a.RayleighScattering.data(),"%.8g");
            changed|=ImGui::InputFloat3("Mie RGB",a.MieScattering.data(),"%.8g");
            changed|=ImGui::SliderFloat("Mie anisotropy",&a.MieAnisotropy,-.95f,.95f);
            ImGui::TreePop();
        }
    }
    if(changed){state.Changed=true;SyncDraft();}
    if(state.Changed)ImGui::TextUnformatted("Unsaved scene feature settings");
    if(ImGui::Button("Save and Apply"))
    {
        std::string error;
        if(jEditorProjectSettings::WriteSceneFeatures(state.Draft,state.Features,state.EnvironmentAsset,error))
            state.Pending=PendingSettings{project->GetConfig().Id,project->GetRoot(),state.Draft};
        else state.Status.Message=error;
    }
    ImGui::SameLine();if(ImGui::Button("Revert edits"))LoadDraft(*project,false);
    const auto status=GetBakeStatus();
    if(ImGui::CollapsingHeader("Bake project assets",ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Import source images into the project first. Input is bounded to 4M pixels/64 MiB. Decoding and baking run on one cancellable CPU worker.");
        ImGui::BeginDisabled(status.Running);
        ImGui::InputText("HDR source",state.EnvironmentSource,sizeof(state.EnvironmentSource));
        ImGui::InputText("SH output file",state.EnvironmentOutput,sizeof(state.EnvironmentOutput));
        if(ImGui::Button("Project HDR and use SH"))
        {
            BakeRequest request;request.SourcePath=state.EnvironmentSource;request.OutputPath=state.EnvironmentOutput;
            std::string error;if(!StartBake(*project,request,error))state.Status.Message=error;
        }
        ImGui::Separator();
        ImGui::InputText("Height source (R)",state.HorizonSource,sizeof(state.HorizonSource));
        ImGui::InputText("Horizon output folder",state.HorizonOutput,sizeof(state.HorizonOutput));
        ImGui::SliderInt("Bake resolution",&state.Resolution,32,512);
        ImGui::SliderInt("Horizon radius (texels)",&state.Radius,1,64);
        ImGui::DragFloat("Height scale (world units)",&state.HeightScale,.01f,0,1e6f);
        ImGui::DragFloat("Baked texel size (world units)",&state.TexelSize,.01f,.000001f,1e6f);
        ImGui::Checkbox("Wrap height field",&state.Wrap);
        if(ImGui::Button("Bake horizon layers and AO"))
        {
            BakeRequest request;request.Kind=BakeKind::Horizon;request.SourcePath=state.HorizonSource;request.OutputPath=state.HorizonOutput;
            request.HorizonResolution=std::uint32_t(state.Resolution);request.RadiusTexels=std::uint32_t(state.Radius);
            request.HeightScale=state.HeightScale;request.TexelSize=state.TexelSize;
            request.Edges=state.Wrap?jHorizonMapping::Boundary::Wrap:jHorizonMapping::Boundary::Clamp;
            std::string error;if(!StartBake(*project,request,error))state.Status.Message=error;
        }
        ImGui::EndDisabled();
        if(status.Running)
        {
            ImGui::TextUnformatted(status.Saving?"Saving baked assets...":"Baking...");
            ImGui::BeginDisabled(status.Saving);if(ImGui::Button("Cancel bake"))CancelBake();ImGui::EndDisabled();
        }
        ImGui::TextWrapped("Horizon outputs: layer0.png, layer1.png and ao.png. Assign the generated linear data textures to the material Horizon node.");
        for(const auto& path:state.Status.Outputs)ImGui::TextWrapped("%s",path.c_str());
    }
    if(!state.Status.Message.empty())ImGui::TextWrapped("%s",state.Status.Message.c_str());
    ImGui::End();
}
}
