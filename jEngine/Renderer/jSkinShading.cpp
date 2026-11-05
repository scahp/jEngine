#include "pch.h"
#include "jSkinShading.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "Shader/jSkinShadingShaderParameters.h"

namespace jSkinShading
{
namespace
{
FrameStats LastStats;
struct BuildResources
{
    std::shared_ptr<jTexture> Texture;
    std::shared_ptr<IUniformBufferBlock> Uniform;
    std::shared_ptr<jShaderBindingInstance> Binding;
};
// One owner-thread transaction. Retain even failed allocations until the owner
// flushes and calls ReleaseResources: backend creation may enqueue barriers.
BuildResources Resources;
bool Built=false,Failed=false;
const jRHI* OwnerRHI=nullptr;

void Flush(jCommandBuffer* command)
{
#if USE_RESOURCE_BARRIER_BATCHER
    if(auto* global=g_rhi->GetGlobalBarrierBatcher())global->Flush(command);
    command->FlushBarrierBatch();
#else
    (void)command;
#endif
}
std::shared_ptr<jTexture> Fail(const char* message)
{
    LastStats.Ready=false;LastStats.Diagnostic=message;Failed=true;
    return {};
}
}

std::shared_ptr<jTexture> AcquireEnergyLUT(const std::shared_ptr<jRenderFrameContext>& frame)
{
    LastStats.GeneratedThisCall=false;LastStats.Dispatches=0;
    if(!g_rhi||!frame||!frame->GetActiveCommandBuffer())
    {LastStats.Ready=false;LastStats.Diagnostic="Skin energy requires an active owner-thread graphics frame.";return {};}
    if(OwnerRHI&&OwnerRHI!=g_rhi)
    {LastStats.Ready=false;LastStats.Diagnostic="Release skin resources after flushing the previous RHI before changing devices.";return {};}
    if(Built)
    {LastStats.Ready=true;LastStats.Diagnostic.clear();return Resources.Texture;}
    if(Failed)return {};
    OwnerRHI=g_rhi;

    // Compile first, before creating any resource with queued initialization.
    jShaderInfo info;info.SetName(jNameStatic("SkinEnergyBuildCS"));
    info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/skin_energy_cs.hlsl"));
    info.SetEntryPoint(jNameStatic("main"));info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    jShaderParameterSet::AppendToShaderInfo<jSkinEnergyBuildParameters>(info,0);
    auto* shader=g_rhi->CreateShader(info);
    if(!shader||!shader->GetCompiledShader())return Fail("Skin energy shader compilation failed; standard PBR remains available.");

    Resources.Texture=g_rhi->Create2DTexture(EnergyLUTResolution,EnergyLUTResolution,1,1,ETextureFormat::R32F,
        ETextureCreateFlag::UAV|ETextureCreateFlag::TransferSrc,EResourceLayout::UAV,{},jRTClearValue::Invalid,jNameStatic("SkinEnergyLUT"));
    if(!Resources.Texture)return Fail("Skin energy R32F texture allocation failed.");
    LastStats.AdditionalTextureBytes=std::size_t(EnergyLUTResolution)*EnergyLUTResolution*sizeof(float);
    jSkinEnergyBuildData data;data.DimensionsAndRoughness=Vector4(float(EnergyLUTResolution),float(IntegrationSteps),
        float(MinimumRoughness),float(MaximumRoughness));
    Resources.Uniform=g_rhi->CreateUniformBufferBlock(jNameStatic("SkinEnergyBuild"),jLifeTimeType::OneFrame,sizeof(data));
    if(!Resources.Uniform)return Fail("Skin energy constants allocation failed.");
    Resources.Uniform->UpdateBufferData(&data,sizeof(data));
    jSkinEnergyBuildParameters parameters;parameters.SkinEnergyBuild.Buffer=Resources.Uniform;
    parameters.SkinEnergyResult={Resources.Texture.get()};
    Resources.Binding=jShaderParameterSet::CreateShaderBindingInstance(parameters,EShaderAccessStageFlag::COMPUTE,
        jShaderBindingInstanceType::SingleFrame);
    if(!Resources.Binding)return Fail("Skin energy resource binding creation failed.");
    jShaderBindingInstanceGroup bindings;bindings.Add(Resources.Binding);
    auto* pipeline=g_rhi->CreateComputePipelineStateInfo(shader,bindings.GetLayoutArray(),{});
    if(!pipeline)return Fail("Skin energy compute pipeline creation failed.");

    auto* command=frame->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command,Resources.Texture.get(),EResourceLayout::UAV);Flush(command);
    pipeline->Bind(frame);
    g_rhi->BindComputeShaderBindingInstances(command,pipeline,bindings.GetCombiner(),0);
    g_rhi->DispatchCompute(frame,(EnergyLUTResolution+7)/8,(EnergyLUTResolution+7)/8,1);
    g_rhi->UAVBarrier(command,Resources.Texture.get());
    g_rhi->TransitionLayout(command,Resources.Texture.get(),EResourceLayout::SHADER_READ_ONLY);Flush(command);
    Built=true;LastStats.Ready=true;LastStats.GeneratedThisCall=true;LastStats.Dispatches=1;LastStats.Diagnostic.clear();
    return Resources.Texture;
}

const FrameStats& GetLastStats(){return LastStats;}

void ReleaseResources()
{
    // The renderer owns the GPU flush and all outstanding frame leases.
    Resources={};Built=false;Failed=false;OwnerRHI=nullptr;LastStats={};
}
}
