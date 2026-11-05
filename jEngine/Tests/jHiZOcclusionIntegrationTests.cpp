#include "pch.h"
#include "jHiZOcclusionIntegrationTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "FileLoader/jEditorAssets.h"
#include "Renderer/jHiZOcclusionPass.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "RHI/jRenderTarget.h"
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"
#endif
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "jEngine.h"
#include "jOptions.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>

std::string CaptureSceneFeaturePixels(const char* pngPath);

namespace
{
namespace fs=std::filesystem;
using Json=nlohmann::json;
using namespace jEditorAuthoring;
using Clock=std::chrono::steady_clock;
using Arguments=jHiZOcclusion::IndexedArguments;
constexpr std::uint32_t FrameBudget=70;

void Write(const fs::path& path,const Json& value)
{std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);file<<value.dump(2)<<'\n';}
struct Restore
{
    std::vector<std::function<void()>> Actions;
    template<class T>void Keep(T& value){const T previous=value;Actions.emplace_back([pointer=&value,previous]{*pointer=previous;});}
    void Apply(){for(auto i=Actions.rbegin();i!=Actions.rend();++i)(*i)();Actions.clear();}
    ~Restore(){Apply();}
};
void FlushBarriers(jCommandBuffer* command)
{
#if USE_RESOURCE_BARRIER_BATCHER
    if(auto* global=g_rhi->GetGlobalBarrierBatcher())global->Flush(command);
#endif
    command->FlushBarrierBatch();
}
void HostBarrier(jCommandBuffer* command,jBuffer* buffer,std::uint64_t bytes)
{
    if(!IsUseVulkan())return;
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer=static_cast<jBuffer_Vulkan*>(buffer)->Buffer;barrier.offset=buffer->GetOffset();barrier.size=bytes;
    vkCmdPipelineBarrier(static_cast<jCommandBuffer_Vulkan*>(command)->GetRef(),VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
}
std::vector<unsigned char> ReadBuffer(jBuffer* source,std::size_t bytes)
{
    if(!source||!bytes||bytes>jHiZOcclusion::MaximumResourceBytes||source->GetBufferSize()<bytes)throw std::runtime_error("Hi-Z readback buffer bounds invalid");
    g_rhi->Flush();const auto previous=source->GetLayout();
    auto target=g_rhi->CreateRawBuffer(bytes,512,EBufferCreateFlag::Readback,IsUseVulkan()?EResourceLayout::UNDEFINED:EResourceLayout::TRANSFER_DST);
    if(!target)throw std::runtime_error("Hi-Z readback allocation failed");
    auto* command=g_rhi->BeginSingleTimeCommands();if(!command)throw std::runtime_error("Hi-Z readback command allocation failed");
    g_rhi->TransitionLayout(command,source,EResourceLayout::TRANSFER_SRC);g_rhi->TransitionLayout(command,target.get(),EResourceLayout::TRANSFER_DST);FlushBarriers(command);
    if(IsUseVulkan())
    {
        const VkBufferCopy copy{source->GetOffset(),target->GetOffset(),bytes};
        vkCmdCopyBuffer(static_cast<jCommandBuffer_Vulkan*>(command)->GetRef(),static_cast<jBuffer_Vulkan*>(source)->Buffer,
            static_cast<jBuffer_Vulkan*>(target.get())->Buffer,1,&copy);
    }
#ifdef _WIN32
    else static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyBufferRegion(static_cast<ID3D12Resource*>(target->GetHandle()),target->GetOffset(),
        static_cast<ID3D12Resource*>(source->GetHandle()),source->GetOffset(),bytes);
#endif
    HostBarrier(command,target.get(),bytes);g_rhi->TransitionLayout(command,source,previous);FlushBarriers(command);g_rhi->EndSingleTimeCommands(command);
    const void* mapped=target->Map();if(!mapped)throw std::runtime_error("Hi-Z readback map failed");
    std::vector<unsigned char> result(bytes);std::memcpy(result.data(),mapped,bytes);target->Unmap();return result;
}
std::vector<std::uint32_t> ReadTextureWords(const std::shared_ptr<jTexture>& texture)
{
    if(!texture||texture->Width<=0||texture->Height<=0||texture->Width>4096||texture->Height>4096||
        texture->SampleCount!=EMSAASamples::COUNT_1||(texture->Format!=ETextureFormat::D32&&texture->Format!=ETextureFormat::R11G11B10F))
        throw std::runtime_error("Hi-Z texture readback requires bounded D32 or R11G11B10F");
    const auto width=std::uint32_t(texture->Width),height=std::uint32_t(texture->Height);
    std::uint32_t pitch=width*4;std::uint64_t bytes=std::uint64_t(pitch)*height;
#ifdef _WIN32
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    if(IsUseDX12())
    {
        const auto description=static_cast<jTexture_DX12*>(texture.get())->Texture->Get()->GetDesc();
        std::uint32_t rows=0;std::uint64_t rowBytes=0;
        g_rhi_dx12->Device->GetCopyableFootprints(&description,0,1,0,&footprint,&rows,&rowBytes,&bytes);pitch=footprint.Footprint.RowPitch;
    }
#endif
    g_rhi->Flush();auto target=g_rhi->CreateRawBuffer(bytes,512,EBufferCreateFlag::Readback,IsUseVulkan()?EResourceLayout::UNDEFINED:EResourceLayout::TRANSFER_DST);
    if(!target)throw std::runtime_error("Hi-Z texture readback allocation failed");
    auto* command=g_rhi->BeginSingleTimeCommands();if(!command)throw std::runtime_error("Hi-Z texture copy command allocation failed");
    const auto previous=texture->GetLayout();g_rhi->TransitionLayout(command,texture.get(),EResourceLayout::TRANSFER_SRC);
    g_rhi->TransitionLayout(command,target.get(),EResourceLayout::TRANSFER_DST);FlushBarriers(command);
    if(IsUseVulkan())
    {
        VkBufferImageCopy copy{};copy.bufferOffset=target->GetOffset();
        copy.imageSubresource={texture->Format==ETextureFormat::D32?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
        vkCmdCopyImageToBuffer(static_cast<jCommandBuffer_Vulkan*>(command)->GetRef(),static_cast<jTexture_Vulkan*>(texture.get())->Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,static_cast<jBuffer_Vulkan*>(target.get())->Buffer,1,&copy);
    }
#ifdef _WIN32
    else
    {
        D3D12_TEXTURE_COPY_LOCATION source{},destination{};
        source.pResource=static_cast<ID3D12Resource*>(texture->GetHandle());source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource=static_cast<ID3D12Resource*>(target->GetHandle());destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint=footprint;destination.PlacedFootprint.Offset+=target->GetOffset();
        static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
    }
#endif
    HostBarrier(command,target.get(),bytes);g_rhi->TransitionLayout(command,texture.get(),previous);FlushBarriers(command);g_rhi->EndSingleTimeCommands(command);
    const auto* mapped=static_cast<const unsigned char*>(target->Map());if(!mapped)throw std::runtime_error("Hi-Z texture map failed");
    std::vector<std::uint32_t> result(std::size_t(width)*height);
    for(std::uint32_t y=0;y<height;++y)std::memcpy(result.data()+std::size_t(y)*width,mapped+std::size_t(y)*pitch,std::size_t(width)*4);
    target->Unmap();return result;
}
float UnsignedFloat(std::uint32_t bits,std::uint32_t mantissaBits)
{
    const auto exponent=bits>>mantissaBits,mantissa=bits&((1u<<mantissaBits)-1u);
    if(exponent==31)return mantissa?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
    return exponent?std::ldexp(1.f+float(mantissa)/float(1u<<mantissaBits),int(exponent)-15):
        std::ldexp(float(mantissa)/float(1u<<mantissaBits),-14);
}
struct HdrImage
{
    std::uint32_t Width=0,Height=0;
    std::vector<std::uint32_t> Words;
    std::vector<float> RGB;
};
HdrImage ReadHDR()
{
    const auto texture=jHiZOcclusion::AcquireLastSceneColor();HdrImage result;
    result.Words=ReadTextureWords(texture);result.Width=std::uint32_t(texture->Width);result.Height=std::uint32_t(texture->Height);
    result.RGB.reserve(result.Words.size()*3);
    for(const auto pixel:result.Words)
    {result.RGB.push_back(UnsignedFloat(pixel&2047u,6));result.RGB.push_back(UnsignedFloat((pixel>>11)&2047u,6));result.RGB.push_back(UnsignedFloat(pixel>>22,5));}
    return result;
}
void Edit(jEditor& editor,EntityId id,const std::function<void(Entity&)>& update)
{
    const auto* old=editor.GetDocument().Find(id);if(!old)throw std::runtime_error("Hi-Z fixture entity disappeared");
    auto entity=*old;update(entity);
    if(!(entity==*old)&&!editor.GetDocument().Update(entity,"Hi-Z fixture"))throw std::runtime_error(editor.GetDocument().LastError());
}
jRenderObject* Mesh(jEditor& editor,EntityId id)
{auto* object=editor.ObjectForEntity(id);return object&&!object->RenderObjects.empty()?object->RenderObjects.front():nullptr;}
struct Run
{
    fs::path Output,Fixture;
    Json Report{{"schemaVersion",1},{"test","hiz-occlusion-native"},{"checks",Json::array()},{"cases",Json::object()},{"profiles",Json::object()}};
    std::uint32_t Frames=0;int Failures=0;bool Ready=false;
    EntityId Occluder=0,Hidden=0,Witness=0,Alpha=0,Water=0,Near=0;
    Vector Eye{0,0,-200},Target{0,0,50};
    void Save(){Report["renderedFrames"]=Frames;Report["failureCount"]=Failures;Write(Output/"hiz-progress.json",Report);}
    void Expect(bool pass,const std::string& name,Json detail=Json::object())
    {Report["checks"].push_back({{"name",name},{"pass",pass},{"frame",Frames},{"detail",detail}});Failures+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<"Hi-Z: "<<name<<std::endl;Save();}
    double Frame()
    {
        if(Frames>=FrameBudget)throw std::runtime_error("Hi-Z native frame budget exceeded");
        const auto start=Clock::now();ResetAllKeyState();ResetMouseAllState();g_timeDeltaSecond=1.f/60.f;
        g_Engine->ProcessInput(g_timeDeltaSecond);g_rhi->IncrementFrameNumber();g_Engine->Update(g_timeDeltaSecond);
        if(Ready)
        {
            auto* camera=g_Engine->Game.MainCamera;if(!camera)throw std::runtime_error("Hi-Z fixture camera missing");
            jCamera::SetCamera(camera,Eye,Target,Eye+Vector::UpVector,DegreeToRadian(60),1,1200,SCR_WIDTH,SCR_HEIGHT,true,250);
            camera->UpdateCamera();gOptions.CameraPos=Eye;
        }
        g_Engine->Draw();g_Engine->EndOfFrame();++Frames;
#ifdef _WIN32
        if(IsUseDX12())
        {MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)throw std::runtime_error("Hi-Z window closed");TranslateMessage(&message);DispatchMessage(&message);}}
        else
#endif
        {auto* window=static_cast<GLFWwindow*>(g_rhi->GetWindow());if(!window||glfwWindowShouldClose(window))throw std::runtime_error("Hi-Z window closed");glfwPollEvents();}
        return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }
    void Warm(int count=4){while(count--)Frame();}
    void Profile(const std::string& name)
    {
        Warm(2);std::vector<double> times;for(int i=0;i<6;++i)times.push_back(Frame());
        double sum=0;for(double value:times)sum+=value;
        Report["profiles"][name]={{"frameCpuMilliseconds",times},{"meanMilliseconds",sum/times.size()},
            {"scope","Same executable ProcessInput/Update/Draw/EndOfFrame/present/event polling. Readback, captures, warmup and edits excluded. No GPU-only timing or speedup claim."}};Save();
    }
    std::map<std::uint32_t,Arguments> ReadArguments(const std::string& name)
    {
        jHiZOcclusion::DebugOutput output;const bool actual=jHiZOcclusion::GetLastDebugOutput(output);
        Expect(actual&&output.Arguments&&output.RenderObjectIds.size()==output.CandidateCount,name+" publishes actual per-view GPU argument records");
        std::map<std::uint32_t,Arguments> result;if(!actual)return result;
        const auto bytes=ReadBuffer(output.Arguments.get(),std::size_t(output.CandidateCount)*sizeof(Arguments));
        Json records=Json::array();
        for(std::size_t i=0;i<output.CandidateCount;++i)
        {
            Arguments args;std::memcpy(&args,bytes.data()+i*sizeof(args),sizeof(args));result[output.RenderObjectIds[i]]=args;
            records.push_back({{"renderObjectId",output.RenderObjectIds[i]},{"indexCount",args.IndexCount},{"instanceCount",args.InstanceCount},
                {"firstIndex",args.FirstIndex},{"baseVertex",args.BaseVertex},{"firstInstance",args.FirstInstance}});
        }
        const auto& stats=jHiZOcclusion::GetLastFrameStats();
        Report["cases"][name]={{"active",stats.Active},{"candidateCount",stats.CandidateCount},{"occluderDrawCount",stats.OccluderDrawCount},
            {"additionalResourceBytes",stats.AdditionalResourceBytes},{"computePasses",stats.ComputePasses},{"records",records},{"diagnostic",stats.Diagnostic}};Save();return result;
    }
    void CompareHDR(const std::string& name,const HdrImage& before)
    {
        const auto after=ReadHDR();const bool sized=before.Width==after.Width&&before.Height==after.Height&&
            before.RGB.size()==after.RGB.size()&&!before.RGB.empty();
        double maximum=0,sum=0;std::size_t lit=0,nonfiniteBefore=0,nonfiniteAfter=0,finitePairs=0;Json badPixels=Json::array();
        const auto classify=[](float value){return std::isnan(value)?"NaN":std::isinf(value)?"infinity":"finite";};
        for(std::size_t i=0;i<before.RGB.size()&&i<after.RGB.size();++i)
        {
            const bool finiteBefore=std::isfinite(before.RGB[i]),finiteAfter=std::isfinite(after.RGB[i]);
            nonfiniteBefore+=!finiteBefore;nonfiniteAfter+=!finiteAfter;lit+=finiteBefore&&before.RGB[i]>.01f;
            if(!finiteBefore||!finiteAfter)
            {
                const auto pixel=i/3;
                if(badPixels.size()<12)badPixels.push_back({{"x",pixel%before.Width},{"y",pixel/before.Width},{"channel",i%3},
                    {"before",classify(before.RGB[i])},{"after",classify(after.RGB[i])},
                    {"beforeR11G11B10Bits",before.Words[pixel]},{"afterR11G11B10Bits",after.Words[pixel]}});
                continue;
            }
            ++finitePairs;const auto delta=std::abs(double(before.RGB[i])-after.RGB[i]);maximum=std::max(maximum,delta);sum+=delta;
        }
        // Nonfinite values always fail parity. Finite-only deltas are diagnostic,
        // and never substitute for the required zero nonfinite channel counts.
        const bool finite=sized&&nonfiniteBefore==0&&nonfiniteAfter==0;
        Expect(finite&&lit>100&&maximum<=1.e-4,name+" OFF/ON preserves actual nonblack HDR pixels",
            {{"channels",before.RGB.size()},{"litChannels",lit},{"nonfiniteBefore",nonfiniteBefore},{"nonfiniteAfter",nonfiniteAfter},
                {"nonfinitePixelSamples",badPixels},{"finiteComparisonChannels",finitePairs},{"maximumAbsoluteDifference",maximum},
                {"meanAbsoluteDifference",finite&&finitePairs?Json(sum/finitePairs):Json(nullptr)}});
        const auto png=Output/(name+".png");Report["cases"][name]["capture"]=Json::parse(CaptureSceneFeaturePixels(jEditorAssetPath::ToUtf8(png).c_str()));Save();
    }
};
void Configure(Restore& restore)
{
    restore.Keep(gOptions.HiZOcclusion);restore.Keep(gOptions.Subsurface);restore.Keep(gOptions.RequestedMSAASamples);
    restore.Keep(gOptions.AdvancedShadows);restore.Keep(gOptions.SceneFeatures);restore.Keep(gOptions.CameraPos);restore.Keep(gOptions.UseDeferredRenderer);
    restore.Keep(gOptions.UseSubpass);restore.Keep(gOptions.UseMemoryless);restore.Keep(gOptions.UseHWRTDirectLighting);
    restore.Keep(gOptions.UseSurfelGI);restore.Keep(gOptions.UseSSGI);restore.Keep(gOptions.UseSSGITemporalAccumulation);
    restore.Keep(gOptions.AOType);restore.Keep(gOptions.AOIntensity);restore.Keep(gOptions.ShowAOOnly);restore.Keep(gOptions.ShowSSGIOnly);
    restore.Keep(gOptions.BloomEyeAdaptation);restore.Keep(gOptions.UseAtmosphericShadowing);restore.Keep(gOptions.UseVRS);
    restore.Keep(gOptions.ShowVRSArea);restore.Keep(gOptions.ShowGrid);restore.Keep(gOptions.ShowDebugObject);restore.Keep(gOptions.ShowDebugRT);restore.Keep(gOptions.EarthQuake);
    gOptions.HiZOcclusion={};gOptions.Subsurface={};gOptions.RequestedMSAASamples=1;gOptions.AdvancedShadows={};gOptions.SceneFeatures={};
    gOptions.UseDeferredRenderer=true;gOptions.UseSubpass=false;gOptions.UseMemoryless=false;gOptions.UseHWRTDirectLighting=false;
    gOptions.UseSurfelGI=false;gOptions.UseSSGI=false;gOptions.UseSSGITemporalAccumulation=false;gOptions.AOType=0;gOptions.AOIntensity=0;
    gOptions.ShowAOOnly=false;gOptions.ShowSSGIOnly=false;gOptions.BloomEyeAdaptation=false;gOptions.UseAtmosphericShadowing=false;
    gOptions.UseVRS=false;gOptions.ShowVRSArea=false;gOptions.ShowGrid=false;gOptions.ShowDebugObject=false;gOptions.ShowDebugRT=false;gOptions.EarthQuake=false;
}
void Fixture(Run& run,jEditor& editor)
{
    editor.CancelPendingImports();editor.ResetWorldBindings(false);g_Engine->Game.ClearImportedSceneForAuthoring();
    if(!editor.GetDocument().NewScene("Hi-Z current-frame visibility"))throw std::runtime_error(editor.GetDocument().LastError());
    run.Fixture=run.Output/("fixture-"+std::to_string(Clock::now().time_since_epoch().count()));jEditorProject project;std::string error;
    if(!project.Create(run.Fixture,"Hi-Z visibility",error))throw std::runtime_error(error);
    Document empty;if(!empty.Save(run.Fixture/"Scenes/start.jscene"))throw std::runtime_error(empty.LastError());
    project.EditConfig().StartupScene="Scenes/start.jscene";project.EditConfig().Settings["render.surfelgi"]="false";project.EditConfig().Settings["render.ssgi"]="false";
    if(!project.Save(error)||!OpenAuthoringProject(editor,project.GetProjectFile(),error))throw std::runtime_error(error);
    SetAuthoringRuntimeMode(false);SetAuthoringWorkspaceVisible(true);editor.SetAuthoringEnabled(true);editor.Placement.EnablePlacementMode=true;editor.SetSelectedEntities({});
    run.Occluder=editor.CreateEntity(EntityKind::Cube,"Opaque mesh occluder");run.Hidden=editor.CreateEntity(EntityKind::Cube,"Hidden shared-geometry cube");
    run.Witness=editor.CreateEntity(EntityKind::Cube,"Visible shared-geometry witness");run.Alpha=editor.CreateEntity(EntityKind::Cube,"Fractional alpha always direct");
    run.Water=editor.CreateEntity(EntityKind::Water,"Deformed water always direct");run.Near=editor.CreateEntity(EntityKind::Cube,"Near-plane crossing always direct");
    const auto sun=editor.CreateEntity(EntityKind::DirectionalLight,"Unshadowed fixture light");
    Edit(editor,run.Occluder,[](Entity& entity){entity.Local={};entity.Local.Scale={8,8,.5f};entity.Material.OverrideEnabled=true;
        entity.Material.BaseColor={.12f,.3f,.7f};entity.Material.Emissive={.03f,.06f,.12f};});
    Edit(editor,run.Hidden,[](Entity& entity){entity.Local={};entity.Local.Position={0,0,50};entity.Material.OverrideEnabled=true;
        entity.Material.BaseColor={.8f,.1f,.1f};entity.Material.Emissive={.15f,.01f,.01f};});
    Edit(editor,run.Witness,[](Entity& entity){entity.Local={};entity.Local.Position={100,0,0};entity.Material.OverrideEnabled=true;
        entity.Material.BaseColor={.1f,.8f,.2f};entity.Material.Emissive={.01f,.15f,.02f};});
    Edit(editor,run.Alpha,[](Entity& entity){entity.Local={};entity.Local.Position={100,50,40};entity.Visible=false;
        entity.Material.OverrideEnabled=true;entity.Material.Opacity=.4f;entity.Material.BaseColor={.8f,.5f,.1f};});
    Edit(editor,run.Water,[](Entity& entity){entity.Local={};entity.Local.Position={-100,-55,20};entity.Visible=false;
        entity.Water.Resolution=9;entity.Water.SizeX=60;entity.Water.SizeZ=60;entity.Material.OverrideEnabled=true;entity.Material.BaseColor={.1f,.4f,.8f};});
    Edit(editor,run.Near,[](Entity& entity){entity.Local={};entity.Local.Position={0,0,-198.9f};entity.Local.Scale={.02f,.02f,.02f};entity.Visible=false;
        entity.Material.OverrideEnabled=true;entity.Material.BaseColor={.6f,.2f,.7f};});
    Edit(editor,sun,[](Entity& entity){entity.Local={};entity.Local.RotationDegrees={-30,0,0};entity.Light.CastShadows=false;entity.Light.Intensity=1;});
    run.Ready=true;run.Report["fixtureProject"]=project.GetProjectFile().generic_string();
    if(!editor.GetDocument().Save(run.Fixture/"Scenes/visibility.jscene"))throw std::runtime_error(editor.GetDocument().LastError());
    project.EditConfig().StartupScene="Scenes/visibility.jscene";if(!project.Save(error))throw std::runtime_error(error);
}
std::optional<Arguments> For(const std::map<std::uint32_t,Arguments>& records,jEditor& editor,EntityId entity)
{
    const auto* mesh=Mesh(editor,entity);if(!mesh)return {};
    const auto found=records.find(mesh->RenderObjectID);return found==records.end()?std::optional<Arguments>{}:found->second;
}
void CheckPyramid(Run& run)
{
    jHiZOcclusion::DebugOutput output;if(!jHiZOcclusion::GetLastDebugOutput(output)){run.Expect(false,"actual GPU pyramid available");return;}
    const auto words=ReadTextureWords(output.Depth->GetTexturePtr());std::vector<float> depth(words.size());std::memcpy(depth.data(),words.data(),words.size()*4);
    const auto actual=ReadBuffer(output.Pyramid.get(),std::size_t(output.Layout.ElementCount)*sizeof(float));std::vector<float> expected;std::string error;
    const bool built=jHiZOcclusion::BuildPyramid(depth,output.Layout,expected,error);
    run.Expect(built&&actual.size()==expected.size()*sizeof(float)&&std::memcmp(actual.data(),expected.data(),actual.size())==0,
        "every actual GPU hierarchy texel matches max reduction of its same-frame D32 input",{{"levels",output.Layout.LevelCount},{"elements",output.Layout.ElementCount},{"error",error}});
}
}
#endif

int RunHiZOcclusionIntegrationGpuTests(const char* ownedOutputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    Run run;Restore restore;const auto keys=g_KeyState;const auto mouse=g_MouseState;const auto delta=g_timeDeltaSecond;
    const bool oldWorkspace=IsAuthoringWorkspaceVisible(),oldRuntime=IsAuthoringRuntimeMode(),oldAuthoring=g_Editor&&g_Editor->IsAuthoringEnabled();
    try
    {
        if(!ownedOutputDirectory||!*ownedOutputDirectory||!g_Engine||!g_rhi||!g_Editor)throw std::runtime_error("Hi-Z native test requires initialized engine/RHI/editor and owned output");
        run.Output=fs::absolute(jEditorAssetPath::FromUtf8(ownedOutputDirectory));fs::create_directories(run.Output);
        run.Report["frameBudget"]=FrameBudget;run.Report["rhi"]=g_rhi->GetRHIName().ToStr();
        run.Report["validationScope"]="Caller collects actual Vulkan/DX12/Metal validation errors. Test-only synchronous readback occurs outside all frame timing samples.";
        if(!g_Engine->Game.WaitForSceneImportForTesting())throw std::runtime_error("Hi-Z startup imports did not complete");
        Configure(restore);restore.Keep(g_Editor->Placement.EnablePlacementMode);Fixture(run,*g_Editor);
        // Real project open applies settings, so restore the same controlled
        // runtime values without replacing the original restore snapshots.
        gOptions.Subsurface={};gOptions.RequestedMSAASamples=1;gOptions.AdvancedShadows={};gOptions.SceneFeatures={};
        gOptions.UseSurfelGI=false;gOptions.UseSSGI=false;gOptions.UseHWRTDirectLighting=false;
        auto& editor=*g_Editor;
        for(const bool forward:{false,true})
        {
            const std::string name=forward?"forward":"deferred";std::string error;
            const auto mode=forward?jGame::ESceneRenderPipeline::Forward:jGame::ESceneRenderPipeline::Deferred;
            if(!g_Engine->Game.RequestSceneRenderPipeline(mode,error))throw std::runtime_error(error);
            gOptions.HiZOcclusion.Enabled=false;run.Warm(4);
            run.Expect(!jHiZOcclusion::GetLastFrameStats().Active,name+" OFF retains the ordinary raster draw path");
            const auto before=ReadHDR();if(!forward)run.Profile("disabled");
            gOptions.HiZOcclusion.Enabled=true;run.Warm(4);
            const auto& stats=jHiZOcclusion::GetLastFrameStats();run.Expect(stats.Active&&stats.CandidateCount>=3&&stats.AdditionalResourceBytes<=jHiZOcclusion::MaximumResourceBytes,
                name+" builds current-frame depth and bounded GPU culling",{{"diagnostic",stats.Diagnostic},{"candidates",stats.CandidateCount},{"bytes",stats.AdditionalResourceBytes}});
            const auto records=run.ReadArguments(name);const auto hidden=For(records,editor,run.Hidden),witness=For(records,editor,run.Witness),occluder=For(records,editor,run.Occluder);
            run.Expect(hidden&&hidden->InstanceCount==0&&witness&&witness->InstanceCount==1&&occluder&&occluder->InstanceCount==1,
                name+" GPU suppresses the hidden draw while visible witnesses retain their complete indexed commands");
            const auto* a=Mesh(editor,run.Hidden);const auto* b=Mesh(editor,run.Witness);
            run.Expect(a&&b&&a->GeometryDataPtr==b->GeometryDataPtr&&!a->GeometryDataPtr->IndirectCommandBufferPtr,
                name+" shared cube geometry is retained without a global indirect-buffer mutation");
            run.CompareHDR(name,before);if(!forward){CheckPyramid(run);run.Profile("enabled");}
        }
        run.Eye={500,0,-200};run.Warm(3);auto records=run.ReadArguments("camera-disocclusion");auto hidden=For(records,editor,run.Hidden);
        run.Expect(hidden&&hidden->InstanceCount==1,"camera disocclusion restores the hidden draw using current-frame depth");
        gOptions.HiZOcclusion.Enabled=false;run.Warm(2);auto before=ReadHDR();gOptions.HiZOcclusion.Enabled=true;run.Warm(2);run.CompareHDR("camera-disocclusion",before);
        run.Eye={0,0,-200};
        if(!editor.GetDocument().Delete(run.Occluder))throw std::runtime_error(editor.GetDocument().LastError());
        run.Warm(3);records=run.ReadArguments("scene-edit-disocclusion");hidden=For(records,editor,run.Hidden);
        run.Expect(hidden&&hidden->InstanceCount==1&&!Mesh(editor,run.Occluder),"deleting the actual occluder restores draw visibility without stale history");
        for(const auto id:{run.Alpha,run.Water,run.Near})Edit(editor,id,[](Entity& entity){entity.Visible=true;});
        run.Warm(3);records=run.ReadArguments("conservative-exclusions");
        run.Expect(Mesh(editor,run.Alpha)&&Mesh(editor,run.Water)&&Mesh(editor,run.Near)&&!For(records,editor,run.Alpha)&&!For(records,editor,run.Water)&&!For(records,editor,run.Near),
            "fractional alpha, deformed water and near-plane crossing render through their retained direct draws");
        gOptions.HiZOcclusion.Enabled=false;run.Warm(2);before=ReadHDR();gOptions.HiZOcclusion.Enabled=true;run.Warm(2);run.CompareHDR("conservative-exclusions",before);
        gOptions.HiZOcclusion.MaxCandidates=1;run.Warm(2);
        const auto& fallback=jHiZOcclusion::GetLastFrameStats();run.Expect(!fallback.Active&&fallback.Diagnostic.find("budget")!=std::string::npos,
            "candidate-budget failure retains all ordinary draws with an explicit reason",{{"diagnostic",fallback.Diagnostic}});
        run.CompareHDR("budget-fallback",before);
    }
    catch(const std::exception& exception){++run.Failures;run.Report["exception"]=exception.what();std::cerr<<"FAIL Hi-Z native integration: "<<exception.what()<<std::endl;}
    if(g_rhi)g_rhi->Flush();
    if(g_Editor)
    {
        g_Editor->SetAuthoringEnabled(oldAuthoring);
        if(run.Ready){g_Editor->CancelPendingImports();g_Editor->ResetWorldBindings(false);g_Engine->Game.ClearImportedSceneForAuthoring();g_Editor->GetDocument().NewScene();}
    }
    SetAuthoringRuntimeMode(oldRuntime);SetAuthoringWorkspaceVisible(oldWorkspace);restore.Apply();g_KeyState=keys;g_MouseState=mouse;g_timeDeltaSecond=delta;
    run.Report["renderedFrames"]=run.Frames;run.Report["failureCount"]=run.Failures;run.Report["status"]=run.Failures?"failed":"passed";
    if(!run.Output.empty())Write(run.Output/"hiz-gpu-report.json",run.Report);
    std::cout<<"Hi-Z native: "<<run.Frames<<" actual frames, "<<run.Failures<<" failures"<<std::endl;return run.Failures?EXIT_FAILURE:EXIT_SUCCESS;
#else
    (void)ownedOutputDirectory;return EXIT_FAILURE;
#endif
}
