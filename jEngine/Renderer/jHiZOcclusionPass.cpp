#include "pch.h"
#include "jHiZOcclusionPass.h"
#include "jDrawCommand.h"
#include "jSceneRenderTargets.h"
#include "Material/jMaterial.h"
#include "Material/jMaterialWater.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRenderPass.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "Scene/jCamera.h"
#include "Scene/jMeshObject.h"
#include "Shader/jHiZOcclusionShaderParameters.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <typeinfo>

namespace jHiZOcclusion
{
namespace
{
FrameStats LastStats;
DebugOutput LastDebug;
std::shared_ptr<jTexture> LastSceneColor;
// BeginRenderFrame waits for the acquired swapchain slot before this slot can
// be replaced. Resources survive command recording and asynchronous execution.
struct AcquiredSlot
{
    std::uint32_t FrameNumber=~std::uint32_t(0);
    std::vector<std::shared_ptr<void>> Views;
};
std::map<std::uint32_t,AcquiredSlot> SlotResources;

void Flush(const std::shared_ptr<jRenderFrameContext>& frame)
{
#if USE_RESOURCE_BARRIER_BATCHER
    auto* command=frame->GetActiveCommandBuffer();
    if(auto* global=g_rhi->GetGlobalBarrierBatcher())global->Flush(command);
    command->FlushBarrierBatch();
#else
    (void)frame;
#endif
}
template<class T>std::shared_ptr<IUniformBufferBlock> Uniform(const char* name,const T& data)
{
    auto buffer=g_rhi->CreateUniformBufferBlock(jName(name),jLifeTimeType::OneFrame,sizeof(data));
    if(buffer)buffer->UpdateBufferData(&data,sizeof(data));
    return buffer;
}
Matrix4 CopyMatrix(const Matrix& matrix)
{Matrix4 result;for(std::size_t i=0;i<16;++i)result[i]=matrix.mm[i];return result;}

struct LocalBox { std::array<double,3> Minimum,Maximum; };
bool ScanPositions(const jRenderObject& object,LocalBox& result,std::size_t& scanned,bool& budgetExceeded)
{
    const auto& geometry=object.GeometryDataPtr;
    const auto stream=geometry?geometry->VertexStream_PositionOnlyPtr:nullptr;
    if(!stream||stream->ElementCount<=0||stream->Params.empty()||!stream->Params.front())return false;
    const auto& parameter=stream->Params.front();
    if(parameter->InstanceDivisor||parameter->Attributes.empty()||parameter->Attributes.front().UnderlyingType!=EBufferElementType::FLOAT||
        parameter->Attributes.front().Stride!=int(sizeof(float)*3))return false;
    const std::size_t count=std::size_t(stream->ElementCount),stride=parameter->Stride>0?std::size_t(parameter->Stride):sizeof(float)*3;
    if(count>MaximumScannedVertices-scanned){budgetExceeded=true;return false;}
    if(stride<sizeof(float)*3||parameter->GetBufferSize()<(count-1)*stride+sizeof(float)*3)return false;
    const auto* bytes=static_cast<const unsigned char*>(parameter->GetBufferData());if(!bytes)return false;
    scanned+=count;LocalBox box;box.Minimum.fill(std::numeric_limits<double>::infinity());box.Maximum.fill(-std::numeric_limits<double>::infinity());
    for(std::size_t i=0;i<count;++i)
    {
        float position[3];std::memcpy(position,bytes+i*stride,sizeof(position));
        for(std::size_t axis=0;axis<3;++axis)
        {
            if(!std::isfinite(position[axis])||std::abs(position[axis])>1.e8f)return false;
            box.Minimum[axis]=std::min(box.Minimum[axis],double(position[axis]));
            box.Maximum[axis]=std::max(box.Maximum[axis],double(position[axis]));
        }
    }
    result=box;return true;
}
bool WorldBounds(const jRenderObject& object,const LocalBox& local,BoxCorners& output)
{
    const auto world=CopyMatrix(object.World);
    if(!std::all_of(world.begin(),world.end(),[](double value){return std::isfinite(value)&&std::abs(value)<=1.e9;})||
        std::abs(world[3])>1.e-12||std::abs(world[7])>1.e-12||std::abs(world[11])>1.e-12||std::abs(world[15]-1.)>1.e-12)return false;
    std::array<double,3> minimum,maximum;minimum.fill(std::numeric_limits<double>::infinity());maximum.fill(-std::numeric_limits<double>::infinity());
    constexpr double gamma=32.*std::numeric_limits<float>::epsilon();
    for(std::size_t corner=0;corner<8;++corner)for(std::size_t row=0;row<3;++row)
    {
        double value=world[12+row],magnitude=std::abs(value);
        for(std::size_t axis=0;axis<3;++axis)
        {
            const double position=(corner&(1u<<axis))?local.Maximum[axis]:local.Minimum[axis];
            const double product=world[axis*4+row]*position;value+=product;magnitude+=std::abs(product);
        }
        const double margin=gamma*std::max(magnitude,1.e-20);
        minimum[row]=std::min(minimum[row],value-margin);maximum[row]=std::max(maximum[row],value+margin);
    }
    for(std::size_t corner=0;corner<8;++corner)for(std::size_t axis=0;axis<3;++axis)
        output[corner][axis]=(corner&(1u<<axis))?maximum[axis]:minimum[axis];
    return true;
}
bool OpaqueVertexColors(const jRenderObjectGeometryData& geometry,std::size_t& scanned,bool& exceeded,
    std::map<const jVertexStreamData*,bool>& cache)
{
    if(!geometry.HasVertexColor())return true;
    const auto stream=geometry.VertexStreamPtr;
    if(!stream||stream->StartLocation!=0||stream->ElementCount<=0)return false;
    if(const auto found=cache.find(stream.get());found!=cache.end())return found->second;
    std::uint32_t location=0;
    for(const auto& parameter:stream->Params)
    {
        if(!parameter)return false;
        std::size_t offset=0;
        for(const auto& attribute:parameter->Attributes)
        {
            if(location++==1)
            {
                const auto count=std::size_t(stream->ElementCount),stride=parameter->Stride>0?std::size_t(parameter->Stride):offset+std::size_t(attribute.Stride);
                if(parameter->InstanceDivisor||attribute.UnderlyingType!=EBufferElementType::FLOAT||attribute.Stride!=int(sizeof(float)*4)||
                    parameter->GetBufferSize()<sizeof(float)*4)return false;
                if(count>MaximumScannedVertices-scanned){exceeded=true;return false;}
                scanned+=count;const auto* bytes=static_cast<const unsigned char*>(parameter->GetBufferData());
                const bool opaque=bytes&&VertexAlphaIsOpaque({bytes,parameter->GetBufferSize()},count,stride,offset+sizeof(float)*3);
                cache[stream.get()]=opaque;return opaque;
            }
            if(attribute.Stride<=0)return false;offset+=std::size_t(attribute.Stride);
        }
    }
    cache[stream.get()]=false;return false;
}
bool OpaqueUndeformed(const jDrawCommand& draw,int width,int height,jHiZIndexedArguments& arguments,
    std::size_t& scanned,bool& exceeded,std::map<const jVertexStreamData*,bool>& alphaCache)
{
    const auto* object=draw.RenderObject;
    if(!object||!object->GeometryDataPtr||!draw.PipelineStateFixed||draw.OverrideInstanceData||object->HasInstancing()||
        (typeid(*object)!=typeid(jRenderObject)&&typeid(*object)!=typeid(jRenderObjectElement)))return false;
    const auto& geometry=*object->GeometryDataPtr;
    if(geometry.IndirectCommandBufferPtr||object->GetPrimitiveType()!=EPrimitiveType::TRIANGLES||!geometry.VertexBuffer_PositionOnlyPtr||
        !geometry.IndexBufferPtr||!geometry.IndexBufferPtr->IndexStreamData||!geometry.IndexBufferPtr->IndexStreamData->Param)return false;
    const auto& state=*draw.PipelineStateFixed;const auto* raster=state.RasterizationState;
    const auto* depth=state.DepthStencilState;
    if(!depth||!depth->DepthTestEnable||!depth->DepthWriteEnable||depth->DepthBoundsTestEnable||depth->StencilTestEnable||
        (depth->DepthCompareOp!=ECompareOp::LESS&&depth->DepthCompareOp!=ECompareOp::LEQUAL)||
        !state.BlendingState||state.BlendingState->BlendEnable)return false;
    if(!raster||state.IsUseVRS||raster->SampleCount!=EMSAASamples::COUNT_1||raster->PolygonMode!=EPolygonMode::FILL||
        raster->DepthClampEnable||raster->DepthBiasEnable||raster->RasterizerDiscardEnable||raster->AlphaToCoverageEnable||raster->AlphaToOneEnable||
        state.Viewports.size()!=1||state.Scissors.size()!=1||!state.DynamicStates.empty())return false;
    const auto& viewport=state.Viewports.front();const auto& scissor=state.Scissors.front();
    if(viewport.X!=0.f||viewport.Y!=0.f||viewport.Width!=float(width)||viewport.Height!=float(height)||viewport.MinDepth!=0.f||viewport.MaxDepth!=1.f||
        scissor.Offset.x!=0||scissor.Offset.y!=0||scissor.Extent.x!=width||scissor.Extent.y!=height)return false;
    const auto* material=draw.Material;
    if(!material||material->Water||material->IsUseSphericalMap()||
        (material->MappingProgram&&material->MappingProgram->Active)||
        (material->Surface&&material->Surface->AlphaMode!=jSurfaceAlphaMode::Opaque)||
        (material->UseMaterialProperties&&(!std::isfinite(material->BaseColorOpacity.w)||material->BaseColorOpacity.w<1.f)))return false;
    // RT alpha testing is also enabled for authored solid overrides, so it is
    // not a raster-coverage classification. Shared opaque SurfaceFlags or an
    // actual neutral white texture + validated vertex alpha prove full coverage.
    const bool sharedOpaque=material->UseMaterialProperties&&material->Surface&&
        material->Surface->AlphaMode==jSurfaceAlphaMode::Opaque&&!material->SubsurfaceOnlySurface;
    if(!sharedOpaque)
    {
        if(material->HasAlbedoTexture()&&material->GetTexture(jMaterial::EMaterialTextureType::Albedo)!=GWhiteTexture.get())return false;
        if(!OpaqueVertexColors(geometry,scanned,exceeded,alphaCache))return false;
    }
    if(material->MaterialDataPtr&&(!material->Surface||material->SubsurfaceOnlySurface))return false;
    const auto& index=*geometry.IndexBufferPtr->IndexStreamData;
    if(index.ElementCount<=0||index.Param->Attributes.empty())return false;
    const auto type=index.Param->Attributes.front().UnderlyingType;
    if(type!=EBufferElementType::UINT16&&type!=EBufferElementType::UINT32)return false;
    if(!geometry.VertexStream_PositionOnlyPtr||geometry.VertexStream_PositionOnlyPtr->ElementCount<=0)return false;
    IndexedRange range{0,index.ElementCount,0,geometry.VertexStream_PositionOnlyPtr->ElementCount};
    if(const auto* element=dynamic_cast<const jRenderObjectElement*>(object))
    {
        const auto& submesh=element->SubMesh;
        if(submesh.StartFace<0||submesh.EndFace<=submesh.StartFace||submesh.EndFace>index.ElementCount||submesh.StartVertex<0||
            !geometry.VertexStream_PositionOnlyPtr||submesh.EndVertex<=submesh.StartVertex||submesh.EndVertex>geometry.VertexStream_PositionOnlyPtr->ElementCount)return false;
        range={submesh.StartFace,submesh.EndFace,submesh.StartVertex,submesh.EndVertex};
    }
    std::string error;
    return BuildIndexedArguments(std::uint32_t(index.ElementCount),std::uint32_t(geometry.VertexStream_PositionOnlyPtr->ElementCount),range,arguments,error);
}
struct ComputeStep
{
    jPipelineStateInfo* Pipeline=nullptr;
    std::shared_ptr<jShaderBindingInstance> Binding;
    std::uint32_t X=0,Y=0;
};
template<class Parameters>bool PrepareCompute(const Parameters& parameters,int pass,std::uint32_t x,std::uint32_t y,ComputeStep& output)
{
    jShaderInfo info;info.SetName(jName(std::string("HiZOcclusionCS")+std::to_string(pass)));
    info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/hiz_occlusion_cs.hlsl"));info.SetEntryPoint(jNameStatic("main"));
    info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    info.SetPreProcessors(jName(std::string("#define JE_HIZ_PASS ")+std::to_string(pass)+"\n"));
    jShaderParameterSet::AppendToShaderInfo<Parameters>(info,0);
    auto* shader=g_rhi->CreateShader(info);if(!shader||!shader->GetCompiledShader())return false;
    output.Binding=jShaderParameterSet::CreateShaderBindingInstance(parameters,EShaderAccessStageFlag::COMPUTE,jShaderBindingInstanceType::SingleFrame);
    if(!output.Binding)return false;
    jShaderBindingInstanceGroup bindings;bindings.Add(output.Binding);
    output.Pipeline=g_rhi->CreateComputePipelineStateInfo(shader,bindings.GetLayoutArray(),{});output.X=x;output.Y=y;
    return output.Pipeline&&x&&y;
}
void RecordCompute(const std::shared_ptr<jRenderFrameContext>& frame,const ComputeStep& step)
{
    Flush(frame);step.Pipeline->Bind(frame);
    jShaderBindingInstanceGroup bindings;bindings.Add(step.Binding);
    g_rhi->BindComputeShaderBindingInstances(frame->GetActiveCommandBuffer(),step.Pipeline,bindings.GetCombiner(),0);
    g_rhi->DispatchCompute(frame,step.X,step.Y,1);
}
}

struct Frame::Impl
{
    FrameStats Stats;
    PyramidLayout Layout;
    std::shared_ptr<jRenderTarget> Depth;
    std::shared_ptr<jBuffer> Pyramid,Candidates,Arguments;
    std::map<const jRenderObject*,std::uint32_t> Indices;
    std::vector<jPipelineStateFixedInfo> DepthStates;
    std::vector<jDrawCommand> DepthDraws;
    std::vector<ComputeStep> Steps;
    jRenderPass* DepthPass=nullptr;
};
Frame::Frame():Data(std::make_shared<Impl>()){}
Frame::~Frame()=default;
const FrameStats& GetLastFrameStats(){return LastStats;}
const FrameStats& Frame::Stats()const{return Data->Stats;}
bool GetLastDebugOutput(DebugOutput& output)
{if(!LastStats.Active||!LastDebug.Depth||!LastDebug.Pyramid||!LastDebug.Arguments)return false;output=LastDebug;return true;}
std::shared_ptr<jTexture> AcquireLastSceneColor(){return LastSceneColor;}
void ReleaseResources(){LastSceneColor.reset();LastDebug={};SlotResources.clear();LastStats={};}
IndirectDraw Frame::ArgumentsFor(const jRenderObject* object)const
{
    const auto found=Data->Indices.find(object);
    return Data->Stats.Active&&found!=Data->Indices.end()?IndirectDraw{Data->Arguments,found->second}:IndirectDraw{};
}
bool Frame::BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame,const jView& view,
    std::span<const jDrawCommand> draws,const Settings& requested,std::string& error)
{
    // A failed rebuild must never leave a previous frame's arguments usable.
    Data->Stats.Active=false;Data->Indices.clear();
    try{Data=std::make_shared<Impl>();}
    catch(const std::exception& exception){error=std::string("Hi-Z frame allocation failed: ")+exception.what();LastStats={};LastDebug={};return false;}
    auto& data=*Data;data.Stats.Requested=requested.Enabled;
    LastSceneColor=frame&&frame->SceneRenderTargetPtr&&frame->SceneRenderTargetPtr->ColorPtr
        ?frame->SceneRenderTargetPtr->ColorPtr->GetTexturePtr():nullptr;
    // Disabling the feature also retires this completed slot's old resources;
    // other in-flight slots remain retained until their own acquisition fence.
    if(g_rhi&&frame&&frame->FrameIndex<=15)
    {
        auto found=SlotResources.find(frame->FrameIndex);
        if(found!=SlotResources.end()&&found->second.FrameNumber!=g_rhi->GetCurrentFrameNumber())
        {found->second.Views.clear();found->second.FrameNumber=g_rhi->GetCurrentFrameNumber();}
    }
    const auto fail=[&](const std::string& reason)
    {error=reason;data.Stats.Active=false;data.Stats.Diagnostic=reason;data.Indices.clear();LastStats=data.Stats;LastDebug={};return false;};
    try
    {
    Settings settings;if(!ValidateSettings(requested,settings,error))return fail(error);
    if(!settings.Enabled)return fail("Hi-Z occlusion is disabled.");
    if(!g_rhi||!frame||!frame->GetActiveCommandBuffer()||!frame->SceneRenderTargetPtr||!frame->SceneRenderTargetPtr->ColorPtr||!view.Camera)
        return fail("Hi-Z requires an active raster view and command buffer.");
    if(frame->FrameIndex>15)return fail("Hi-Z acquired frame slot is out of bounds.");
    if(g_rhi->GetRHIName()!=jNameStatic("Vulkan")&&g_rhi->GetRHIName()!=jNameStatic("DirectX12"))return fail("Hi-Z requires the Vulkan or DirectX12 indirect-draw backend.");
    const auto& color=*frame->SceneRenderTargetPtr->ColorPtr;
    if(color.Info.SampleCount!=EMSAASamples::COUNT_1||g_rhi->GetSelectedMSAASamples()!=EMSAASamples::COUNT_1)
        return fail("Hi-Z is inactive with MSAA: single-sample occluder coverage cannot conservatively represent sample edges.");
    if(draws.size()>MaximumCandidates)return fail("Hi-Z base draw list exceeds its admission budget.");
    const int width=color.Info.Width,height=color.Info.Height;
    if(width<=0||height<=0||!BuildLayout(std::uint32_t(width),std::uint32_t(height),data.Layout,error))return fail(error);
    data.Stats.DirectDrawCount=std::uint32_t(draws.size());
    std::vector<const jDrawCommand*> occluders;std::vector<jHiZCandidate> candidates;
    std::map<const jVertexStreamData*,LocalBox> localBounds;
    std::map<const jVertexStreamData*,bool> alphaCache;
    const auto vp=CopyMatrix(view.Camera->ViewProjection);
    const bool ndcYUp=!IsUseVulkan()||IsUse_VULKAN_NDC_Y_FLIP();
    for(const auto& draw:draws)
    {
        jHiZIndexedArguments arguments;
        bool coverageBudgetExceeded=false;
        if(!OpaqueUndeformed(draw,width,height,arguments,data.Stats.ScannedVertices,coverageBudgetExceeded,alphaCache))
        {if(coverageBudgetExceeded)return fail("Hi-Z current-frame vertex coverage scan exceeds its 4-million-vertex budget.");continue;}
        auto* object=draw.RenderObject;const auto stream=object->GeometryDataPtr->VertexStream_PositionOnlyPtr;
        if(!stream)continue;
        auto found=localBounds.find(stream.get());
        if(found==localBounds.end())
        {
            LocalBox box;bool exceeded=false;
            if(!ScanPositions(*object,box,data.Stats.ScannedVertices,exceeded))
            {if(exceeded)return fail("Hi-Z current-frame position scan exceeds its 4-million-vertex budget.");continue;}
            found=localBounds.emplace(stream.get(),box).first;
        }
        BoxCorners corners;ScreenBounds screen;
        if(!WorldBounds(*object,found->second,corners)||!ProjectBounds(corners,vp,std::uint32_t(width),std::uint32_t(height),ndcYUp,screen,error))continue;
        occluders.push_back(&draw);
        if(!screen.Testable)continue;
        if(candidates.size()==settings.MaxCandidates)return fail("Hi-Z candidate count exceeds the selected budget; all direct draws are retained.");
        jHiZCandidate candidate;candidate.MinimumX=screen.MinimumX;candidate.MinimumY=screen.MinimumY;
        candidate.MaximumX=screen.MaximumX;candidate.MaximumY=screen.MaximumY;
        candidate.NearestDepth=std::nextafter(float(screen.NearestDepth),-std::numeric_limits<float>::infinity());
        candidate.Testable=1;candidate.IndexCount=arguments.IndexCount;candidate.InstanceCount=arguments.InstanceCount;
        candidate.FirstIndex=arguments.FirstIndex;candidate.BaseVertex=arguments.BaseVertex;candidate.FirstInstance=arguments.FirstInstance;
        data.Indices.emplace(object,std::uint32_t(candidates.size()));candidates.push_back(candidate);
    }
    if(candidates.empty()||occluders.empty())return fail("Hi-Z has no validated opaque indexed bounds to test.");
    const std::size_t bufferBytes=candidates.size()*(sizeof(jHiZCandidate)+sizeof(jHiZIndexedArguments));
    data.Stats.AdditionalResourceBytes=data.Layout.ResourceBytes+bufferBytes;
    if(data.Stats.AdditionalResourceBytes>MaximumResourceBytes)return fail("Hi-Z buffers and depth exceed 128 MiB.");
    data.Depth=jRenderTargetPool::GetRenderTarget({.Type=ETextureType::TEXTURE_2D,.Format=ETextureFormat::D32,
        .Width=width,.Height=height,.SampleCount=EMSAASamples::COUNT_1,.RTClearValue=jRTClearValue(1.f,0),
        .TextureCreateFlag=ETextureCreateFlag::DSV|ETextureCreateFlag::TransferSrc,.ResourceName=jNameStatic("HiZOccluderDepth")});
    data.Pyramid=g_rhi->CreateStructuredBuffer(std::size_t(data.Layout.ElementCount)*sizeof(float),0,sizeof(float),EBufferCreateFlag::UAV,
        EResourceLayout::UAV,nullptr,0,jNameStatic("HiZPyramid"));
    data.Candidates=g_rhi->CreateStructuredBuffer(candidates.size()*sizeof(jHiZCandidate),0,sizeof(jHiZCandidate),EBufferCreateFlag::CPUAccess,
        EResourceLayout::READ_ONLY,candidates.data(),candidates.size()*sizeof(jHiZCandidate),jNameStatic("HiZCandidates"));
    data.Arguments=g_rhi->CreateStructuredBuffer(candidates.size()*sizeof(jHiZIndexedArguments),0,sizeof(jHiZIndexedArguments),
        EBufferCreateFlag::UAV|EBufferCreateFlag::IndirectCommand,EResourceLayout::UAV,nullptr,0,jNameStatic("HiZArguments"));
    // Retain even partially recorded transactions until the next completed slot.
    auto& slot=SlotResources[frame->FrameIndex];
    const auto frameNumber=g_rhi->GetCurrentFrameNumber();
    if(slot.FrameNumber!=frameNumber){slot.Views.clear();slot.FrameNumber=frameNumber;}
    if(slot.Views.size()>=4)return fail("Hi-Z supports at most four recorded views per acquired frame slot.");
    slot.Views.push_back(Data);
    if(!data.Depth||!data.Depth->GetTexture()||!data.Pyramid||!data.Candidates||!data.Arguments)return fail("Hi-Z resource allocation failed; direct draws are retained.");
    jRenderPassInfo passInfo;passInfo.SynchronizeDepthStencil=true;
    passInfo.Attachments.push_back({.RenderTargetPtr=data.Depth,.LoadStoreOp=EAttachmentLoadStoreOp::CLEAR_STORE,
        .StencilLoadStoreOp=EAttachmentLoadStoreOp::DONTCARE_DONTCARE,.RTClearValue=jRTClearValue(1.f,0),
        .InitialLayout=EResourceLayout::DEPTH_STENCIL_ATTACHMENT,.FinalLayout=EResourceLayout::DEPTH_STENCIL_ATTACHMENT});
    jSubpass subpass;subpass.OutputDepthAttachment=0;passInfo.Subpasses.push_back(subpass);
    data.DepthPass=g_rhi->GetOrCreateRenderPass(passInfo,{0,0},{width,height});
    if(!data.DepthPass)return fail("Hi-Z depth render pass creation failed.");
    jGraphicsPipelineShader shaders;
    for(int stage=0;stage<2;++stage)
    {
        jShaderInfo info;info.SetName(stage?jNameStatic("HiZDepthPS"):jNameStatic("HiZDepthVS"));
        info.SetShaderFilepath(stage?jNameStatic("Resource/Shaders/hlsl/hiz_occlusion_depth_ps.hlsl"):jNameStatic("Resource/Shaders/hlsl/hiz_occlusion_depth_vs.hlsl"));
        info.SetEntryPoint(jNameStatic("main"));info.SetShaderType(stage?EShaderAccessStageFlag::FRAGMENT:EShaderAccessStageFlag::VERTEX);
        jShaderParameterSet::AppendToShaderInfo<jHiZDepthParameters>(info,0);
        auto* shader=g_rhi->CreateShader(info);if(!shader||!shader->GetCompiledShader())return fail("Hi-Z depth shader compilation failed.");
        if(stage)shaders.PixelShader=shader;else shaders.VertexShader=shader;
    }
    auto* depthState=TDepthStencilStateInfo<true,true,ECompareOp::LEQUAL>::Create();
    auto* blend=TBlendingStateInfo<false,EBlendFactor::ONE,EBlendFactor::ZERO,EBlendOp::ADD,
        EBlendFactor::ONE,EBlendFactor::ZERO,EBlendOp::ADD,EColorMask::NONE>::Create();
    data.DepthStates.reserve(occluders.size());data.DepthDraws.reserve(occluders.size());
    for(const auto* source:occluders)
    {
        data.DepthStates.emplace_back(source->PipelineStateFixed->RasterizationState,depthState,blend,
            jViewport(0,0,width,height),jScissor(0,0,width,height),false);
        jHiZDepthData values;values.ViewProjection=view.Camera->ViewProjection;values.World=source->RenderObject->World;
        jHiZDepthParameters parameters;parameters.HiZDepth.Buffer=Uniform("HiZDepthDraw",values);
        if(!parameters.HiZDepth.Buffer)return fail("Hi-Z depth transform allocation failed.");
        auto binding=jShaderParameterSet::CreateShaderBindingInstance(parameters,EShaderAccessStageFlag::ALL_GRAPHICS,jShaderBindingInstanceType::SingleFrame);
        if(!binding)return fail("Hi-Z depth descriptor creation failed.");
        jShaderBindingInstanceGroup bindings;bindings.Add(binding);
        data.DepthDraws.emplace_back(frame,source->RenderObject,data.DepthPass,shaders,&data.DepthStates.back(),nullptr,
            bindings,nullptr,nullptr,0,EDrawCommandBindingMode::Manual);
        data.DepthDraws.back().PrepareToDraw(true);
        if(!data.DepthDraws.back().CurrentPipelineStateInfo)return fail("Hi-Z depth pipeline creation failed.");
    }
    jHiZDispatchData dispatch;dispatch.Viewport=Vector4(float(width),float(height),float(data.Layout.Levels[0].Width),float(data.Layout.Levels[0].Height));
    dispatch.Control=Vector4(float(candidates.size()),settings.DepthBias,float(data.Layout.LevelCount),1.f);
    for(std::uint32_t level=0;level<data.Layout.LevelCount;++level)
    {
        const auto& item=data.Layout.Levels[level];
        if(double(float(item.Offset))!=double(item.Offset))return fail("Hi-Z level offset cannot be represented exactly.");
        dispatch.Levels[level]=Vector4(float(item.Width),float(item.Height),float(item.Offset),0.f);
    }
    data.Steps.reserve(data.Layout.LevelCount+2);
    for(std::uint32_t level=0;level<data.Layout.LevelCount;++level)
    {
        const auto& destination=data.Layout.Levels[level];dispatch.Destination=dispatch.Levels[level];
        if(level==0)
        {
            jHiZSeedParameters parameters;parameters.HiZDispatch.Buffer=Uniform("HiZSeed",dispatch);
            parameters.HiZDepthInput={data.Depth->GetTexture()};parameters.HiZPyramidOutput={data.Pyramid.get()};
            if(!parameters.HiZDispatch.Buffer)return fail("Hi-Z seed constants allocation failed.");
            data.Steps.emplace_back();
            if(!PrepareCompute(parameters,0,(destination.Width+7)/8,(destination.Height+7)/8,data.Steps.back()))return fail("Hi-Z seed shader or pipeline creation failed.");
        }
        else
        {
            dispatch.Source=dispatch.Levels[level-1];jHiZReduceParameters parameters;
            parameters.HiZDispatch.Buffer=Uniform("HiZReduce",dispatch);parameters.HiZPyramidOutput={data.Pyramid.get()};
            if(!parameters.HiZDispatch.Buffer)return fail("Hi-Z reduction constants allocation failed.");
            data.Steps.emplace_back();
            if(!PrepareCompute(parameters,1,(destination.Width+7)/8,(destination.Height+7)/8,data.Steps.back()))return fail("Hi-Z reduction shader or pipeline creation failed.");
        }
    }
    // Initialize every record as visible before the occlusion dispatch. Both
    // pipelines and all descriptors are preflighted before recording any pass.
    for(int test=0;test<2;++test)
    {
        dispatch.Control.w=float(test);jHiZCullParameters parameters;
        parameters.HiZDispatch.Buffer=Uniform("HiZCull",dispatch);parameters.HiZPyramidInput={data.Pyramid.get()};
        parameters.HiZCandidates={data.Candidates.get()};parameters.HiZArguments={data.Arguments.get()};
        if(!parameters.HiZDispatch.Buffer)return fail("Hi-Z cull constants allocation failed.");
        data.Steps.emplace_back();
        if(!PrepareCompute(parameters,2,(std::uint32_t(candidates.size())+63)/64,1,data.Steps.back()))return fail("Hi-Z cull shader or pipeline creation failed.");
    }
    auto* command=frame->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command,data.Depth->GetTexture(),EResourceLayout::DEPTH_STENCIL_ATTACHMENT);Flush(frame);
    if(!data.DepthPass->BeginRenderPass(command))return fail("Hi-Z depth pass recording failed.");
    for(const auto& draw:data.DepthDraws)draw.Draw();
    data.DepthPass->EndRenderPass();
    data.Stats.DepthPasses=1;data.Stats.OccluderDrawCount=std::uint32_t(data.DepthDraws.size());
    g_rhi->TransitionLayout(command,data.Depth->GetTexture(),EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(command,data.Pyramid.get(),EResourceLayout::UAV);
    g_rhi->TransitionLayout(command,data.Arguments.get(),EResourceLayout::UAV);
    g_rhi->TransitionLayout(command,data.Candidates.get(),EResourceLayout::READ_ONLY);
    for(std::size_t i=0;i<data.Steps.size();++i)
    {
        if(i==data.Layout.LevelCount)g_rhi->TransitionLayout(command,data.Pyramid.get(),EResourceLayout::READ_ONLY);
        RecordCompute(frame,data.Steps[i]);++data.Stats.ComputePasses;
        g_rhi->UAVBarrier(command,i<data.Layout.LevelCount?data.Pyramid.get():data.Arguments.get());
    }
    g_rhi->TransitionLayout(command,data.Arguments.get(),EResourceLayout::READ_ONLY);Flush(frame);
    data.Stats.Active=true;data.Stats.CandidateCount=std::uint32_t(candidates.size());
    data.Stats.DirectDrawCount=std::uint32_t(draws.size())-data.Stats.CandidateCount;
    data.Stats.Diagnostic="Same-frame opaque depth; GPU indexed draw suppression; no visibility readback.";
    LastStats=data.Stats;LastDebug={data.Depth,data.Pyramid,data.Arguments,data.Layout,data.Stats.CandidateCount,{}};
    LastDebug.RenderObjectIds.resize(candidates.size());
    for(const auto& [object,index]:data.Indices)LastDebug.RenderObjectIds[index]=object->RenderObjectID;
    error.clear();return true;
    }
    catch(const std::exception& exception){return fail(std::string("Hi-Z preparation failed; direct draws retained: ")+exception.what());}
}
}
