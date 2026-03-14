#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "Material/jMaterial.h"
#include "RHI/jShaderBindingLayout.h"
#include "RHI/jTexture.h"
#include "Scene/jRenderObject.h"
#include "Profiler/jPerformanceProfile.h"
#include "Scene/Light/jDirectionalLight.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/jRHIUtil.h"
#include "FileLoader/jImageFileLoader.h"
#include "RHI/jRenderTargetPool.h"
#include "Shader/jAOShaderParameters.h"
#include "Shader/jShaderParameterSet.h"

static float RTScale = 0.0f;
static int32 RayRTWidth = 0;
static int32 RayRTHeight = 0;

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jGaussianBlurCommonComputeUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, KernelSize)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jGaussianBlurKernelUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, Data, 20)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jGaussianBlurCSParameters)
    SHADER_RW_TEXTURE2D(resultImage)
    SHADER_TEXTURE2D(inputImage)
    SHADER_UNIFORM_BUFFER(jGaussianBlurCommonComputeUniformBuffer, ComputeCommon)
    SHADER_UNIFORM_BUFFER(jGaussianBlurKernelUniformBuffer, KernelBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSSGIAccumulateUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_Height)
    SHADER_UNIFORM_BUFFER_MEMBER(float, g_SSGIAccumBlendFactor)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_Padding)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSSGIAccumulateCSParameters)
    SHADER_RW_TEXTURE2D(OutSSGIAccum)
    SHADER_TEXTURE2D(InSSGI)
    SHADER_TEXTURE2D(InPrevSSGIAccum)
    SHADER_UNIFORM_BUFFER(jSSGIAccumulateUniformBuffer, SSGIAccumUniformBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jAOApplyCommonUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(float, AOIntensity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jAOApplyCSParameters)
    SHADER_RW_TEXTURE2D(resultImage)
    SHADER_TEXTURE2D(inputImage)
    SHADER_UNIFORM_BUFFER(jAOApplyCommonUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jAOApplyPSParameters)
    SHADER_TEXTURE2D(AOTexture)
    SHADER_UNIFORM_BUFFER(jAOApplyCommonUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jAOReprojectionCommonUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(float, InvScaleToOriginBuffer)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jAOReprojectionCSParameters)
    SHADER_RW_TEXTURE2D(resultImage)
    SHADER_TEXTURE2D(HistoryBuffer)
    SHADER_TEXTURE2D(VelocityBuffer)
    SHADER_TEXTURE2D(DepthBuffer)
    SHADER_RW_TEXTURE2D_FLOAT(HistoryDepthBuffer)
    SHADER_UNIFORM_BUFFER(jAOReprojectionCommonUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jAOReprojectionPSParameters)
    SHADER_TEXTURE2D(CurrentTexture)
    SHADER_TEXTURE2D(HistoryBuffer)
    SHADER_TEXTURE2D(VelocityBuffer)
    SHADER_TEXTURE2D(DepthBuffer)
    SHADER_TEXTURE2D(HistoryDepthBuffer)
    SHADER_UNIFORM_BUFFER(jAOReprojectionCommonUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSSAOCommonUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, V)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, P)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Radius)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Bias)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, NoiseUVScale)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, CameraPos)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding1)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSSAOKernelUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, Data, 64)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSSAOCSParameters)
    SHADER_RW_TEXTURE2D(Result)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_TEXTURE2D(GBuffer0_Normal)
    SHADER_TEXTURE2D(Noise)
    SHADER_UNIFORM_BUFFER(jSSAOCommonUniformBuffer, ComputeCommon)
    SHADER_UNIFORM_BUFFER(jSSAOKernelUniformBuffer, Kernel)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSSGICommonUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, V)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, P)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvV)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Radius)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Bias)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, NoiseUVScale)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SSGI_MaxSteps)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, CameraPos)
    SHADER_UNIFORM_BUFFER_MEMBER(float, SSGI_MaxDistance)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SSGI_RayCount)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, UseAttenuation)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding1)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding2)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSSGICSParameters)
    SHADER_RW_TEXTURE2D(Result)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_TEXTURE2D(GBuffer0)
    SHADER_TEXTURE2D(GBuffer1)
    SHADER_TEXTURE2D(GBuffer2)
    SHADER_TEXTURE2D(ColorTexture)
    SHADER_TEXTURE2D(Noise)
    SHADER_UNIFORM_BUFFER(jSSGICommonUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSSGIReprojectionUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(float, BlendFactor)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSSGIReprojectionCSParameters)
    SHADER_RW_TEXTURE2D(resultImage)
    SHADER_TEXTURE2D(HistoryBuffer)
    SHADER_TEXTURE2D(VelocityBuffer)
    SHADER_TEXTURE2D(DepthBuffer)
    SHADER_RW_TEXTURE2D_FLOAT(HistoryDepthBuffer)
    SHADER_UNIFORM_BUFFER(jSSGIReprojectionUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSSGIReprojectionPSParameters)
    SHADER_TEXTURE2D(CurrentTexture)
    SHADER_TEXTURE2D(HistoryBuffer)
    SHADER_TEXTURE2D(VelocityBuffer)
    SHADER_TEXTURE2D(DepthBuffer)
    SHADER_TEXTURE2D(HistoryDepthBuffer)
    SHADER_UNIFORM_BUFFER(jSSGIReprojectionUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jATrousUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_StepSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, g_Sigma_Color)
    SHADER_UNIFORM_BUFFER_MEMBER(float, g_Sigma_Normal)
    SHADER_UNIFORM_BUFFER_MEMBER(float, g_Sigma_Depth)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_KernelSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, padding)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSSGIATrousCSParameters)
    SHADER_RW_TEXTURE2D(OutTexture)
    SHADER_TEXTURE2D(InTexture)
    SHADER_TEXTURE2D(NormalTexture)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_UNIFORM_BUFFER(jATrousUniformBuffer, A_TrousUniformBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jRTAOSceneConstantBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, projectionToWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, ViewRect)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, cameraPosition)
    SHADER_UNIFORM_BUFFER_MEMBER(float, AORadius)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Clear)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, RayPerPixel)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, HaltonJitter)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jRTAOGlobalParameters)
    SHADER_ACCELERATION_STRUCTURE(Scene)
    SHADER_RW_TEXTURE2D_FLOAT2(RenderTarget)
    SHADER_TEXTURE2D_SRV(DepthTexture)
    SHADER_TEXTURE2D_SRV(GBuffer0_Normal)
    SHADER_UNIFORM_BUFFER(jRTAOSceneConstantBuffer, g_sceneCB)
    SHADER_SAMPLER(AlbedoTextureSampler)
    SHADER_SAMPLER(PBRSamplerState)
END_SHADER_PARAMETER_SET()

namespace
{
    struct jRTAOBindlessUInt2
    {
    };

    template <>
    struct TShaderParameterHLSLTypeInfo<jRTAOBindlessUInt2>
    {
        static constexpr const char* GetTypeName() { return "uint2"; }
        static void AppendTypeDeclaration(std::string&) {}
    };
}

BEGIN_SHADER_BINDLESS_SET(jRTAOBindlessParameters)
    // space0 is reserved for jRTAOGlobalParameters; bindless tables start at space1.
    SHADER_BINDLESS_TEXTURECUBE(IrradianceMapArray, 1)
    SHADER_BINDLESS_TEXTURECUBE(PrefilteredEnvMapArray, 2)
    SHADER_BINDLESS_STRUCTURED_BUFFER(jRTAOBindlessUInt2, VertexIndexOffsetArray, 3)
    SHADER_BINDLESS_BUFFER(uint32, IndexBindlessArray, 4)
    SHADER_BINDLESS_STRUCTURED_BUFFER(RenderObjectUniformBuffer, RenderObjParamArray, 5)
    SHADER_BINDLESS_BYTEADDRESS_BUFFER(VerticesBindlessArray, 6)
    SHADER_BINDLESS_TEXTURE2D(AlbedoTextureArray, 7)
    SHADER_BINDLESS_TEXTURE2D(NormalTextureArray, 8)
    SHADER_BINDLESS_TEXTURE2D(RMTextureArray, 9)
END_SHADER_BINDLESS_SET()

namespace
{
struct jRTAORaytracingShaderBase : public jShader
{
    using jShader::jShader;

    DECLARE_SHADER_PARAMETER_SETS(jRTAOGlobalParameters)

    DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

    using ShaderPermutation = jPermutation<USE_BINDLESS_RESOURCE>;
    ShaderPermutation Permutation;

    static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
    {
        if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
            InOutBinder.AddBindless<jRTAOBindlessParameters>();
    }
};

struct jShaderRTAOMissShader : public jRTAORaytracingShaderBase
{
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderRTAOMissShader, jRTAORaytracingShaderBase, Permutation)
};

struct jShaderRTAORaygenShader : public jRTAORaytracingShaderBase
{
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderRTAORaygenShader, jRTAORaytracingShaderBase, Permutation)
};

struct jShaderRTAOClosestHitShader : public jRTAORaytracingShaderBase
{
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderRTAOClosestHitShader, jRTAORaytracingShaderBase, Permutation)
};

struct jShaderRTAOAnyHitShader : public jRTAORaytracingShaderBase
{
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderRTAOAnyHitShader, jRTAORaytracingShaderBase, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderRTAOMissShader
    , "Miss"
    , "Resource/Shaders/hlsl/RTAO.hlsl"
    , ""
    , "MyMissShader"
    , EShaderAccessStageFlag::RAYTRACING_MISS)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderRTAORaygenShader
    , "Raygen"
    , "Resource/Shaders/hlsl/RTAO.hlsl"
    , ""
    , "MyRaygenShader"
    , EShaderAccessStageFlag::RAYTRACING_RAYGEN)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderRTAOClosestHitShader
    , "ClosestHit"
    , "Resource/Shaders/hlsl/RTAO.hlsl"
    , ""
    , "MyClosestHitShader"
    , EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderRTAOAnyHitShader
    , "AnyHit"
    , "Resource/Shaders/hlsl/RTAO.hlsl"
    , ""
    , "MyAnyHitShader"
    , EShaderAccessStageFlag::RAYTRACING_ANYHIT)

struct jShaderShowAOOnlyBase : public jShader
{
    using jShader::jShader;

    DECLARE_DEFINE(SHOW_AO_ONLY, 0, 1);

    using ShaderPermutation = jPermutation<SHOW_AO_ONLY>;
    ShaderPermutation Permutation;
};

struct jShaderAOApplyComputeShader : public jShaderShowAOOnlyBase
{
    DECLARE_SHADER_PARAMETER_SETS(jAOApplyCSParameters)
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderAOApplyComputeShader, jShaderShowAOOnlyBase, Permutation)
};

struct jShaderAOApplyPixelShader : public jShaderShowAOOnlyBase
{
    DECLARE_SHADER_PARAMETER_SETS(jAOApplyPSParameters)
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderAOApplyPixelShader, jShaderShowAOOnlyBase, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderAOApplyComputeShader
    , "AOApplyCS"
    , "Resource/Shaders/hlsl/AOApply_cs.hlsl"
    , ""
    , "AOApplyCS"
    , EShaderAccessStageFlag::COMPUTE)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderAOApplyPixelShader
    , "AOApplyPS"
    , "Resource/Shaders/hlsl/AOApply_cs.hlsl"
    , ""
    , "AOApplyPS"
    , EShaderAccessStageFlag::FRAGMENT)

struct jShaderDiscontinuityWeightBase : public jShader
{
    using jShader::jShader;

    DECLARE_DEFINE(USE_DISCONTINUITY_WEIGHT, 0, 1);

    using ShaderPermutation = jPermutation<USE_DISCONTINUITY_WEIGHT>;
    ShaderPermutation Permutation;
};

struct jShaderAOReprojectionComputeShader : public jShaderDiscontinuityWeightBase
{
    DECLARE_SHADER_PARAMETER_SETS(jAOReprojectionCSParameters)
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderAOReprojectionComputeShader, jShaderDiscontinuityWeightBase, Permutation)
};

struct jShaderAOReprojectionPixelShader : public jShaderDiscontinuityWeightBase
{
    DECLARE_SHADER_PARAMETER_SETS(jAOReprojectionPSParameters)
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderAOReprojectionPixelShader, jShaderDiscontinuityWeightBase, Permutation)
};

struct jShaderSSGIReprojectionComputeShader : public jShaderDiscontinuityWeightBase
{
    DECLARE_SHADER_PARAMETER_SETS(jSSGIReprojectionCSParameters)
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderSSGIReprojectionComputeShader, jShaderDiscontinuityWeightBase, Permutation)
};

struct jShaderSSGIReprojectionPixelShader : public jShaderDiscontinuityWeightBase
{
    DECLARE_SHADER_PARAMETER_SETS(jSSGIReprojectionPSParameters)
    DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderSSGIReprojectionPixelShader, jShaderDiscontinuityWeightBase, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderAOReprojectionComputeShader
    , "ReProjectionAOCS"
    , "Resource/Shaders/hlsl/AOReprojection_cs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::COMPUTE)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderAOReprojectionPixelShader
    , "ReProjectionAOPS"
    , "Resource/Shaders/hlsl/AOReprojection_cs.hlsl"
    , ""
    , "AOReprojectionPS"
    , EShaderAccessStageFlag::FRAGMENT)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderSSGIReprojectionComputeShader
    , "ReProjectionSSGICS"
    , "Resource/Shaders/hlsl/SSGIReprojection_cs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::COMPUTE)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderSSGIReprojectionPixelShader
    , "ReProjectionSSGIPS"
    , "Resource/Shaders/hlsl/SSGIReprojection_cs.hlsl"
    , ""
    , "SSGIReprojectionPS"
    , EShaderAccessStageFlag::FRAGMENT)

template <typename TShaderParameters>
void DispatchShaderParameterComputePass(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , jName InShaderName, jName InShaderFilePath, jName InEntryPoint, const TShaderParameters& InParameters
    , uint32 NumGroupsX, uint32 NumGroupsY, uint32 NumGroupsZ, const std::string& InPreProcessors = {})
{
    auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
        InParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

    jShaderInfo ShaderInfo;
    ShaderInfo.SetName(InShaderName);
    ShaderInfo.SetShaderFilepath(InShaderFilePath);
    ShaderInfo.SetEntryPoint(InEntryPoint);
    ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    if (!InPreProcessors.empty())
        ShaderInfo.SetPreProcessors(jName(InPreProcessors));
    jShaderParameterSet::AppendToShaderInfo<TShaderParameters>(ShaderInfo, 0);
    jShader* Shader = g_rhi->CreateShader(ShaderInfo);

    jShaderBindingLayoutArray ShaderBindingLayoutArray;
    ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

    jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});
    ComputePipelineStateInfo->Bind(InRenderFrameContextPtr);

    jShaderBindingInstanceArray ShaderBindingInstanceArray;
    ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

    jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
        const std::vector<uint32>* DynamicOffsets = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
        if (DynamicOffsets && DynamicOffsets->size())
        {
            ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
        }
    }
    ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
    g_rhi->DispatchCompute(InRenderFrameContextPtr, NumGroupsX, NumGroupsY, NumGroupsZ);
}

template <typename TShaderClass, typename TShaderParameters>
void DispatchShaderParameterComputePass(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const TShaderParameters& InParameters, const typename TShaderClass::ShaderPermutation& InPermutation
    , uint32 NumGroupsX, uint32 NumGroupsY, uint32 NumGroupsZ)
{
    auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
        InParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

    jShader* Shader = TShaderClass::CreateShader(InPermutation);

    jShaderBindingLayoutArray ShaderBindingLayoutArray;
    ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

    jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});
    ComputePipelineStateInfo->Bind(InRenderFrameContextPtr);

    jShaderBindingInstanceArray ShaderBindingInstanceArray;
    ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

    jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
        const std::vector<uint32>* DynamicOffsets = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
        if (DynamicOffsets && DynamicOffsets->size())
        {
            ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
        }
    }
    ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
    g_rhi->DispatchCompute(InRenderFrameContextPtr, NumGroupsX, NumGroupsY, NumGroupsZ);
}

void DispatchGaussianBlurPass(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jTexture* InDestTexture, jTexture* InSourceTexture
    , const std::shared_ptr<IUniformBufferBlock>& InCommonUniformBuffer, const std::shared_ptr<IUniformBufferBlock>& InKernelUniformBuffer
    , jName InShaderName, jName InEntryPoint)
{
    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InSourceTexture, EResourceLayout::SHADER_READ_ONLY);

    jGaussianBlurCSParameters Parameters;
    Parameters.resultImage = { InDestTexture };
    Parameters.inputImage = { InSourceTexture, nullptr };
    Parameters.ComputeCommon.Buffer = InCommonUniformBuffer;
    Parameters.KernelBuffer.Buffer = InKernelUniformBuffer;

    const uint32 GroupX = InDestTexture->Width / 8 + ((InDestTexture->Width % 8) ? 1 : 0);
    const uint32 GroupY = InDestTexture->Height / 8 + ((InDestTexture->Height % 8) ? 1 : 0);
    DispatchShaderParameterComputePass(InRenderFrameContextPtr, InShaderName
        , jNameStatic("Resource/Shaders/hlsl/gaussianblur_cs.hlsl"), InEntryPoint, Parameters, GroupX, GroupY, 1);
}
}

std::shared_ptr<jTexture> ReprojectionAO(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const std::shared_ptr<jTexture>& InTexture)
{
	if (gOptions.UseAOReprojection)
	{
		jAOReprojectionCommonUniformBuffer CommonComputeData;
		CommonComputeData.Width = InTexture->Width;
		CommonComputeData.Height = InTexture->Height;
		CommonComputeData.FrameNumber = g_rhi->GetCurrentFrameNumber();
		CommonComputeData.InvScaleToOriginBuffer = 1.0f / RTScale;

		auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
			jNameStatic("ReprojectionAOUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
		OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

		// Temporal Previous Frame AO Reprojection
		{
			DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Reprojection AO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
			SCOPE_CPU_PROFILE(ReprojectionAO);
			SCOPE_GPU_PROFILE(InRenderFrameContextPtr, ReprojectionAO);

            bool IsAOReprojectionCompute = false;
            if (IsAOReprojectionCompute)
            {
                auto* CommandBuffer = InRenderFrameContextPtr->GetActiveCommandBuffer();
                g_rhi->TransitionLayout(CommandBuffer, jSceneRenderTarget::AOProjection->GetTexture(), EResourceLayout::UAV);
                g_rhi->TransitionLayout(CommandBuffer, jSceneRenderTarget::HistoryBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
                g_rhi->TransitionLayout(CommandBuffer, InTexture.get(), EResourceLayout::SHADER_READ_ONLY);
                g_rhi->TransitionLayout(CommandBuffer, InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                g_rhi->TransitionLayout(CommandBuffer, jSceneRenderTarget::HistoryDepthBuffer.get(), EResourceLayout::UAV);
                g_rhi->TransitionLayout(CommandBuffer, InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                jAOReprojectionCSParameters Parameters;
                Parameters.resultImage = { jSceneRenderTarget::AOProjection->GetTexture() };
                Parameters.HistoryBuffer = { jSceneRenderTarget::HistoryBuffer.get(), nullptr };
                Parameters.VelocityBuffer = { InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), nullptr };
                Parameters.DepthBuffer = { InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr };
                Parameters.HistoryDepthBuffer = { jSceneRenderTarget::HistoryDepthBuffer.get() };
                Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

                jShaderAOReprojectionComputeShader::ShaderPermutation Permutation;
                Permutation.SetIndex<jShaderAOReprojectionComputeShader::USE_DISCONTINUITY_WEIGHT>(gOptions.UseDiscontinuityWeight ? 1 : 0);
                const uint32 GroupX = jSceneRenderTarget::AOProjection->GetTexture()->Width / 8 + ((jSceneRenderTarget::AOProjection->GetTexture()->Width % 8) ? 1 : 0);
                const uint32 GroupY = jSceneRenderTarget::AOProjection->GetTexture()->Height / 8 + ((jSceneRenderTarget::AOProjection->GetTexture()->Height % 8) ? 1 : 0);
                DispatchShaderParameterComputePass<jShaderAOReprojectionComputeShader>(InRenderFrameContextPtr
                    , Parameters
                    , Permutation
                    , GroupX, GroupY, 1);
            }
            else
            {
                const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                jAOReprojectionPSParameters Parameters;
                Parameters.CurrentTexture = { InTexture.get(), SamplerState };
                Parameters.HistoryBuffer = { jSceneRenderTarget::HistoryBuffer.get(), SamplerState };
                Parameters.VelocityBuffer = { InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), SamplerState };
                Parameters.DepthBuffer = { InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
                Parameters.HistoryDepthBuffer = { jSceneRenderTarget::HistoryDepthBuffer.get(), SamplerState };
                Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

                jRHIUtil::DrawFullScreen(InRenderFrameContextPtr, jSceneRenderTarget::AOProjection
                    , [&, Parameters](const std::shared_ptr<jRenderFrameContext>& InInRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                    {
                        g_rhi->TransitionLayout(InInRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HistoryBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
                        g_rhi->TransitionLayout(InInRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);
                        g_rhi->TransitionLayout(InInRenderFrameContextPtr->GetActiveCommandBuffer(), InInRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                        g_rhi->TransitionLayout(InInRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HistoryDepthBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
                        g_rhi->TransitionLayout(InInRenderFrameContextPtr->GetActiveCommandBuffer(), InInRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                        jShaderParameterSet::BuildShaderBindings(Parameters, EShaderAccessStageFlag::FRAGMENT, InOutShaderBindingArray, InOutResourceInlineAllactor);
                    }
                    , [](const std::shared_ptr<jRenderFrameContext>&)
                    {
                        jShaderAOReprojectionPixelShader::ShaderPermutation Permutation;
                        Permutation.SetIndex<jShaderAOReprojectionPixelShader::USE_DISCONTINUITY_WEIGHT>(gOptions.UseDiscontinuityWeight ? 1 : 0);
                        return jShaderAOReprojectionPixelShader::CreateShader(Permutation);
                    });
            }
		}
		return jSceneRenderTarget::AOProjection->GetTexturePtr();
	}

	return InTexture;
}



std::shared_ptr<jTexture> jRenderer::Denoise(const std::shared_ptr<jTexture>& InTexture, const char* InDenoiser, int32 InKernelSize, float InKernelSigma, float InBilateralSigma)
{
    if (gOptions.IsDenoiserGuassianSeparable())
	{
		DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "GaussianSeparable", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
		SCOPE_CPU_PROFILE(GaussianSeparable);
		SCOPE_GPU_PROFILE(RenderFrameContextPtr, GaussianSeparable);

		auto createGaussianKernel = [](int32 kernelSize, float sigma) -> std::vector<float>
			{
				std::vector<float> kernel(kernelSize);
				int32 center = kernelSize / 2;
				float sum = 0.0;

				for (int32 i = 0; i < kernelSize; ++i)
				{
					float x = (float)(i - center);
					kernel[i] = exp(-(x * x) / (2 * sigma * sigma)) / (sqrt(2 * PI) * sigma);
					sum += kernel[i];
				}

				// Normalize the kernel
				for (int32 i = 0; i < kernelSize; ++i)
				{
					kernel[i] /= sum;
				}

				return kernel;
			};

		std::vector<float> GaussianKernel = createGaussianKernel(InKernelSize, InKernelSigma);

		// Create GaussianBlurKernel uniformbuffer
		jGaussianBlurKernelUniformBuffer KernelData;
		check(sizeof(KernelData.Data) >= GaussianKernel.size() * sizeof(float));
		memcpy(KernelData.Data, GaussianKernel.data(), GaussianKernel.size() * sizeof(float));

		auto OneFrameGaussianKernelUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
			jNameStatic("GaussianKernel"), jLifeTimeType::OneFrame, sizeof(KernelData)));
		OneFrameGaussianKernelUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

		// Create common uniformbuffer
		jGaussianBlurCommonComputeUniformBuffer CommonComputeData;
		CommonComputeData.Width = RayRTWidth;
		CommonComputeData.Height = RayRTHeight;
		CommonComputeData.KernelSize = (int32)GaussianKernel.size();

		auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
			jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
		OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

		g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get());
		{
			DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "Vertical", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
			SCOPE_CPU_PROFILE(Vertical);
			SCOPE_GPU_PROFILE(RenderFrameContextPtr, Vertical);

			DispatchGaussianBlurPass(RenderFrameContextPtr, jSceneRenderTarget::GaussianV.get(), InTexture.get()
				, OneFrameUniformBuffer, OneFrameGaussianKernelUniformBuffer, jNameStatic("GaussianV"), jNameStatic("Vertical"));
		}

		g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::GaussianV.get());

		{
			DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "Horizon", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
			SCOPE_CPU_PROFILE(Horizon);
			SCOPE_GPU_PROFILE(RenderFrameContextPtr, Horizon);

			auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
				jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
			OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

			DispatchGaussianBlurPass(RenderFrameContextPtr, jSceneRenderTarget::GaussianH.get(), jSceneRenderTarget::GaussianV.get()
				, OneFrameUniformBuffer, OneFrameGaussianKernelUniformBuffer, jNameStatic("GaussianH"), jNameStatic("Horizon"));
		}
		return jSceneRenderTarget::GaussianH;
	}
	else if (gOptions.IsDenoiserGuassian() || gOptions.IsDenoiserBilateral())
	{
		DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, gOptions.IsDenoiserBilateral() ? "Bilateral" : "Gaussian", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

		auto createGaussian2DKernel = [](int32 kernelSize, float sigma) -> std::vector<float>
			{
				std::vector<float> kernel(kernelSize * kernelSize);
				int32 center = kernelSize / 2;
				float sum = 0.0;

				int32 Index = 0;
				for (int32 j = 0; j < kernelSize; ++j)
				{
					for (int32 i = 0; i < kernelSize; ++i)
					{
						float x = (float)(i - center);
						float y = (float)(j - center);
						kernel[Index] = exp(-0.5f * (x * x + y * y) / (sigma * sigma)) / (2 * PI * sigma * sigma);
						sum += kernel[Index];
						++Index;
					}
				}

				// Normalize the kernel
				for (int32 i = 0; i < (int32)kernel.size(); ++i)
				{
					kernel[i] /= sum;
				}

				return kernel;
			};

		std::vector<float> GaussianKernel = createGaussian2DKernel(InKernelSize, InKernelSigma);

		jName ProfileTitle = gOptions.IsDenoiserBilateral() ? jNameStatic("Bilateral") : jNameStatic("Gaussian");
		SCOPE_CPU_PROFILE(ProfileTitle);
		SCOPE_GPU_PROFILE_NAME(RenderFrameContextPtr, ProfileTitle);
		g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get());
		{
			jBilateralCommonComputeUniformBuffer CommonComputeData;
			CommonComputeData.Width = jSceneRenderTarget::GaussianH->Width;
			CommonComputeData.Height = jSceneRenderTarget::GaussianH->Height;
			CommonComputeData.Sigma = InKernelSigma;
			CommonComputeData.KernelSize = InKernelSize;
			CommonComputeData.SigmaForBilateral = InBilateralSigma;

			auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
				jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
			OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

			jBilateralKernelUniformBuffer KernelData;
			check(sizeof(KernelData.Data) >= GaussianKernel.size() * sizeof(float));
			memcpy(KernelData.Data, GaussianKernel.data(), GaussianKernel.size() * sizeof(float));

			auto OneFrameGaussianKernelUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
				jNameStatic("GaussianKernel"), jLifeTimeType::OneFrame, sizeof(KernelData)));
			OneFrameGaussianKernelUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            jBilateralFilteringCSParameters Parameters;
            Parameters.resultImage = { jSceneRenderTarget::GaussianH.get() };
            Parameters.inputImage = { InTexture.get(), nullptr };
            Parameters.DepthBuffer = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
            Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
            Parameters.Kernal.Buffer = OneFrameGaussianKernelUniformBuffer;

            jShaderBilateralComputeShader::ShaderPermutation Permutation;
            Permutation.SetIndex<jShaderBilateralComputeShader::USE_GAUSSIAN_INSTEAD>(gOptions.IsDenoiserGuassian() ? 1 : 0);
            DispatchShaderParameterComputePass<jShaderBilateralComputeShader>(RenderFrameContextPtr
                , Parameters
                , Permutation
                , jSceneRenderTarget::GaussianH->Width / 8 + ((jSceneRenderTarget::GaussianH->Width % 8) ? 1 : 0)
                , jSceneRenderTarget::GaussianH->Height / 8 + ((jSceneRenderTarget::GaussianH->Height % 8) ? 1 : 0)
                , 1);
		}

		g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::GaussianH.get());
		return jSceneRenderTarget::GaussianH;
	}

	return InTexture;
}

std::shared_ptr<jTexture> UpdateHistoryBuffer(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const std::shared_ptr<jTexture>& InTexture)
{
	// Copy HistoryBuffer
	DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Copy HistoryBuffer", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
	SCOPE_CPU_PROFILE(CopyHistoryBuffer);
	SCOPE_GPU_PROFILE(InRenderFrameContextPtr, CopyHistoryBuffer);

	jRHIUtil::CopyTexture2D(InRenderFrameContextPtr, jSceneRenderTarget::HistoryBuffer.get(), InTexture.get());

	return InTexture;
}

std::shared_ptr<jTexture> jRenderer::SSAO()
{
    SCOPE_CPU_PROFILE(SSAO);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SSAO);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SSAO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

	auto SSAO_RT = jRenderTargetPool::GetRenderTargetForOneFrame({
		.Type = ETextureType::TEXTURE_2D,
		.Format = ETextureFormat::R16F,
		.Width = RayRTWidth,
		.Height = RayRTHeight,
		.LayerCount = 1,
		.IsGenerateMipmap = false,
		.SampleCount = EMSAASamples::COUNT_1,
		.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
		.TextureCreateFlag = ETextureCreateFlag::UAV
	});

	// 1. SSAO
    jSSAOCommonUniformBuffer CommonComputeData;

	auto mainCamera = jCamera::GetMainCamera();
	CommonComputeData.InvP = mainCamera->Projection.GetInverse();
	CommonComputeData.V = mainCamera->View;
	CommonComputeData.P = mainCamera->Projection;
	CommonComputeData.Radius = gOptions.AORadius;
	CommonComputeData.Bias = gOptions.SSAOBias;
	CommonComputeData.NoiseUVScale.x = (float)RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width / (float)GNoiseTexture->Width;
	CommonComputeData.NoiseUVScale.y = (float)RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height / (float)GNoiseTexture->Height;
	CommonComputeData.Width = SSAO_RT->Info.Width;
	CommonComputeData.Height = SSAO_RT->Info.Height;
	CommonComputeData.FrameNumber = (int32)g_rhi->GetCurrentFrameNumber();
	CommonComputeData.CameraPos = mainCamera->Pos;

	auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
		jNameStatic("OnFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
	OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

    // Sample kernel
    #define SSAO_KERNEL_SIZE 64
    static std::vector<Vector4> ssaoKernelData;
    if (ssaoKernelData.empty())
    {
		std::default_random_engine rndEngine(0);
		std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

        ssaoKernelData.resize(SSAO_KERNEL_SIZE);
        for (uint32 i = 0; i < (uint32)SSAO_KERNEL_SIZE; ++i)
        {
            Vector sample(rndDist(rndEngine) * 2.0f - 1.0f, rndDist(rndEngine) * 2.0f - 1.0f, rndDist(rndEngine));
            sample.SetNormalize();
            sample *= rndDist(rndEngine);
            float scale = float(i) / float(SSAO_KERNEL_SIZE);
            scale = Lerp(0.1f, 1.0f, scale * scale);
            ssaoKernelData[i] = Vector4(sample * scale, 0.0f);
        }
    }
    jSSAOKernelUniformBuffer KernelData;
    check(sizeof(KernelData.Data) >= ssaoKernelData.size() * sizeof(Vector4));
    memcpy(KernelData.Data, ssaoKernelData.data(), ssaoKernelData.size() * sizeof(Vector4));

    auto SSAOKernelOneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("SSAOKernelOneFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(KernelData)));
    SSAOKernelOneFrameUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SSAO_RT->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::DEPTH_STENCIL_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GNoiseTexture.get(), EResourceLayout::SHADER_READ_ONLY);

    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::REPEAT
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    const jSamplerStateInfo* RepeatSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    jSSAOCSParameters Parameters;
    Parameters.Result = { SSAO_RT->GetTexture() };
    Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
    Parameters.GBuffer0_Normal = { RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), SamplerState };
    Parameters.Noise = { GNoiseTexture.get(), RepeatSamplerState };
    Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
    Parameters.Kernel.Buffer = SSAOKernelOneFrameUniformBuffer;

    std::string ShaderPreProcessors;
    ShaderPreProcessors += "#define SSAO_KERNEL_SIZE ";
    ShaderPreProcessors += std::to_string(SSAO_KERNEL_SIZE);

    const uint32 GroupX = SSAO_RT->GetTexture()->Width / 8 + ((SSAO_RT->GetTexture()->Width % 8) ? 1 : 0);
    const uint32 GroupY = SSAO_RT->GetTexture()->Height / 8 + ((SSAO_RT->GetTexture()->Height % 8) ? 1 : 0);
    DispatchShaderParameterComputePass(RenderFrameContextPtr
        , jNameStatic("SSAO_CS")
        , jNameStatic("Resource/Shaders/hlsl/SSAO_cs.hlsl")
        , jNameStatic("main")
        , Parameters
        , GroupX, GroupY, 1
        , ShaderPreProcessors);

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), SSAO_RT->GetTexture());

	// 2. Denosing
	return Denoise(SSAO_RT->GetTexturePtr(), gOptions.GetDenoiseName(gOptions.Denoiser), gOptions.GaussianKernelSize, gOptions.GaussianKernelSigma, gOptions.BilateralKernelSigma);
}

std::shared_ptr<jTexture> jRenderer::RTAO()
{
	SCOPE_CPU_PROFILE(RaytracingAO);
	SCOPE_GPU_PROFILE(RenderFrameContextPtr, RaytracingAO);
	DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "RaytracingAO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

	// Create Persistent Resources
	if (!jSceneRenderTarget::AOProjection || jSceneRenderTarget::AOProjection->Info.Width != (int32)RayRTWidth || jSceneRenderTarget::AOProjection->Info.Height != (int32)RayRTHeight)
	{
		jSceneRenderTarget::AOProjection = g_rhi->CreateRenderTarget({
			.Type = ETextureType::TEXTURE_2D,
			.Format = ETextureFormat::R16F,
			.Width = RayRTWidth,
			.Height = RayRTHeight,
			.LayerCount = 1,
			.IsGenerateMipmap = false,
			.SampleCount = g_rhi->GetSelectedMSAASamples(),
			.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
			.TextureCreateFlag = ETextureCreateFlag::UAV
		});
	}
	if (!jSceneRenderTarget::HistoryBuffer || jSceneRenderTarget::HistoryBuffer->Width != (int32)RayRTWidth || jSceneRenderTarget::HistoryBuffer->Height != (int32)RayRTHeight)
	{
		jSceneRenderTarget::HistoryBuffer = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
			, ETextureFormat::R16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
	}

	// 1. RTAO ray shoot
	{
		auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

		const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
			, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
			, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();
		const jSamplerStateInfo* PBRSamplerStateInfo = TSamplerStateInfo<ETextureFilter::NEAREST_MIPMAP_LINEAR, ETextureFilter::NEAREST_MIPMAP_LINEAR
			, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
			, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

		jRTAOSceneConstantBuffer m_sceneCB;
		auto mainCamera = jCamera::GetMainCamera();
		m_sceneCB.cameraPosition = mainCamera->Pos;
		m_sceneCB.projectionToWorld = mainCamera->GetInverseViewProjectionMatrix();
		m_sceneCB.ViewRect = Vector4(0.0f, 0.0f, (float)RayRTWidth, (float)RayRTHeight);
		m_sceneCB.FrameNumber = (int32)g_rhi->GetCurrentFrameNumber();
		m_sceneCB.AORadius = gOptions.AORadius;
		m_sceneCB.RayPerPixel = (uint32)Max(gOptions.RayPerPixel, 0);

		static Vector2 HaltonJitter[] = {
			Vector2(0.0f,      -0.333334f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.5f,     0.333334f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.5f,      -0.777778f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.75f,    -0.111112f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.25f,     0.555556f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.25f,    -0.555556f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.75f,     0.111112f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.875f,   0.777778f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.125f,    -0.925926f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.375f,   -0.259260f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.625f,    0.407408f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.625f,   -0.703704f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.375f,    -0.037038f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.125f,   0.629630f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(0.875f,    -0.481482f) / Vector2((float)RayRTWidth, (float)RayRTHeight),
			Vector2(-0.9375f,  0.185186f) / Vector2((float)RayRTWidth, (float)RayRTHeight)
		};

		if (gOptions.UseHaltonJitter)
		{
			static int32 index = 0;
			m_sceneCB.HaltonJitter = HaltonJitter[index % _countof(HaltonJitter)];
			++index;
		}
		else
		{
			m_sceneCB.HaltonJitter = Vector2::ZeroVector;
		}

		static jOptions OldOptions = gOptions;
		static uint64 OldRTSceneUpdateIndex = 0;
		const uint64 CurrentRTSceneUpdateIndex = (RenderFrameContextPtr->RaytracingScene) ? RenderFrameContextPtr->RaytracingScene->GetSceneUpdateIndex() : 0;
		static auto OldMatrix = m_sceneCB.projectionToWorld;
		if (!gOptions.UseAccumulateRay || OldMatrix != m_sceneCB.projectionToWorld || OldOptions != gOptions || OldRTSceneUpdateIndex != CurrentRTSceneUpdateIndex)
		{
			OldMatrix = m_sceneCB.projectionToWorld;
			memcpy(&OldOptions, &gOptions, sizeof(gOptions));
			OldRTSceneUpdateIndex = CurrentRTSceneUpdateIndex;
			m_sceneCB.Clear = true;
		}
		else
		{
			m_sceneCB.Clear = false;
		}

		if (!RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr
			|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Width != (int32)RayRTWidth
			|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Height != (int32)RayRTHeight
			|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Format != ETextureFormat::RG16F)
		{
			RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
				, ETextureFormat::RG16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
		}

		if (jObject::GetStaticRenderObject().size() > 0)
		{
			DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "DispatchRayAO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

			auto SceneUniformBufferPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("SceneData"), jLifeTimeType::OneFrame, sizeof(m_sceneCB));
			SceneUniformBufferPtr->UpdateBufferData(&m_sceneCB, sizeof(m_sceneCB));

			// Normal resource
			// Record normal resources
			jShaderBindingArray ShaderBindingArray;
			jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
			jRTAOGlobalParameters GlobalParameters;
			GlobalParameters.Scene.Buffer = RenderFrameContextPtr->RaytracingScene->TLASBufferPtr.get();
			GlobalParameters.RenderTarget.Texture = RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get();
			GlobalParameters.DepthTexture.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture();
			GlobalParameters.GBuffer0_Normal.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture();
			GlobalParameters.g_sceneCB.Buffer = SceneUniformBufferPtr;
			GlobalParameters.AlbedoTextureSampler.SamplerState = SamplerState;
			GlobalParameters.PBRSamplerState.SamplerState = PBRSamplerStateInfo;
			jShaderParameterSet::BuildShaderBindings(GlobalParameters, EShaderAccessStageFlag::ALL_RAYTRACING, ShaderBindingArray, ResourceInlineAllactor);

#define TURN_ON_BINDLESS 1

#if TURN_ON_BINDLESS
			// Bindless resources
			std::vector<jTextureResourceBindless::jTextureBindData> IrradianceMapTextures;
			jTextureResourceBindless::jTextureBindData IrradianceTextureBindData;
			IrradianceTextureBindData.Texture = jSceneRenderTarget::IrradianceMap2;
			IrradianceMapTextures.push_back(IrradianceTextureBindData);

			std::vector<jTextureResourceBindless::jTextureBindData> FilteredEnvMapTextures;
			jTextureResourceBindless::jTextureBindData FilteredEnvMapBindData;
			FilteredEnvMapBindData.Texture = jSceneRenderTarget::FilteredEnvMap2;
			FilteredEnvMapTextures.push_back(FilteredEnvMapBindData);

			std::vector<const jBuffer*> VertexAndInexOffsetBuffers;
			std::vector<const jBuffer*> IndexBuffers;
			std::vector<const jBuffer*> TestUniformBuffers;
			std::vector<const jBuffer*> VertexBuffers;
			std::vector<jTextureResourceBindless::jTextureBindData> AlbedoTextures;
			std::vector<jTextureResourceBindless::jTextureBindData> NormalTextures;
			std::vector<jTextureResourceBindless::jTextureBindData> MetallicTextures;

			const int32 NumOfStaticRenderObjects = (int32)jObject::GetStaticRenderObject().size();
			VertexAndInexOffsetBuffers.reserve(NumOfStaticRenderObjects);
			IndexBuffers.reserve(NumOfStaticRenderObjects);
			TestUniformBuffers.reserve(NumOfStaticRenderObjects);
			VertexBuffers.reserve(NumOfStaticRenderObjects);
			AlbedoTextures.reserve(NumOfStaticRenderObjects);
			NormalTextures.reserve(NumOfStaticRenderObjects);
			MetallicTextures.reserve(NumOfStaticRenderObjects);

			for (int32 i = 0; i < NumOfStaticRenderObjects; ++i)
			{
				jRenderObject* RObj = jObject::GetStaticRenderObject()[i];
				RObj->CreateShaderBindingInstance();

				VertexAndInexOffsetBuffers.push_back(RObj->VertexAndIndexOffsetBuffer.get());
				IndexBuffers.push_back(RObj->GeometryDataPtr->IndexBufferPtr->GetBuffer());
				TestUniformBuffers.push_back(RObj->TestUniformBuffer.get());
				VertexBuffers.push_back(RObj->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

				auto Material = RObj->MaterialPtr ? RObj->MaterialPtr : GDefaultMaterial;
				check(Material);
                AlbedoTextures.push_back({.Texture = Material->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Albedo), .SamplerState = nullptr});
				NormalTextures.push_back({.Texture = Material->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Normal), .SamplerState = nullptr});
				MetallicTextures.push_back({.Texture = Material->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Metallic), .SamplerState = nullptr});
			}

			jRTAOBindlessParameters BindlessParameters;
			BindlessParameters.IrradianceMapArray.Textures = IrradianceMapTextures;
			BindlessParameters.PrefilteredEnvMapArray.Textures = FilteredEnvMapTextures;
			BindlessParameters.VertexIndexOffsetArray.Buffers = VertexAndInexOffsetBuffers;
			BindlessParameters.IndexBindlessArray.Buffers = IndexBuffers;
			BindlessParameters.RenderObjParamArray.Buffers = TestUniformBuffers;
			BindlessParameters.VerticesBindlessArray.Buffers = VertexBuffers;
			BindlessParameters.AlbedoTextureArray.Textures = AlbedoTextures;
			BindlessParameters.NormalTextureArray.Textures = NormalTextures;
			BindlessParameters.RMTextureArray.Textures = MetallicTextures;
#endif // TURN_ON_BINDLESS

			// Create ShaderBindingLayout and ShaderBindingInstance Instance for this draw call
			std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance;
			GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
#if TURN_ON_BINDLESS
			std::vector<std::shared_ptr<jShaderBindingInstance>> BindlessShaderBindingInstances =
				jShaderBindlessSet::CreateShaderBindingInstances(BindlessParameters, EShaderAccessStageFlag::ALL_RAYTRACING, jShaderBindingInstanceType::SingleFrame);
#endif // TURN_ON_BINDLESS

			jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
			GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);
#if TURN_ON_BINDLESS
			for (const auto& BindlessShaderBindingInstance : BindlessShaderBindingInstances)
			{
				GlobalShaderBindingLayoutArray.Add(BindlessShaderBindingInstance->ShaderBindingsLayouts);
			}
#endif // TURN_ON_BINDLESS

			// Create RaytracingShaders
			std::vector<jRaytracingPipelineShader> RaytracingShaders;
			{
				jRaytracingPipelineShader NewShader;

				// First hit gorup
                jRTAORaytracingShaderBase::ShaderPermutation RaytracingPermutation;
                RaytracingPermutation.SetIndex<jRTAORaytracingShaderBase::USE_BINDLESS_RESOURCE>(TURN_ON_BINDLESS ? 1 : 0);

				NewShader.MissShader = jShaderRTAOMissShader::CreateShader(RaytracingPermutation);
				NewShader.MissEntryPoint = TEXT("MyMissShader");

				NewShader.RaygenShader = jShaderRTAORaygenShader::CreateShader(RaytracingPermutation);
				NewShader.RaygenEntryPoint = TEXT("MyRaygenShader");

				NewShader.ClosestHitShader = jShaderRTAOClosestHitShader::CreateShader(RaytracingPermutation);
				NewShader.ClosestHitEntryPoint = TEXT("MyClosestHitShader");

				NewShader.AnyHitShader = jShaderRTAOAnyHitShader::CreateShader(RaytracingPermutation);
				NewShader.AnyHitEntryPoint = TEXT("MyAnyHitShader");

				NewShader.HitGroupName = TEXT("DefaultHit");

				RaytracingShaders.push_back(NewShader);
			}

			// Create RaytracingPipelineState
			jRaytracingPipelineData RaytracingPipelineData;
			RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);	                // float2 barycentrics
			RaytracingPipelineData.MaxPayloadSize = 4 * sizeof(float);		                    // float shadow
			RaytracingPipelineData.MaxTraceRecursionDepth = 1;
			auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData
				, GlobalShaderBindingLayoutArray, nullptr);

			// Binding RaytracingShader resources
			jShaderBindingInstanceArray ShaderBindingInstanceArray;
			ShaderBindingInstanceArray.Add(GlobalShaderBindingInstance.get());
#if TURN_ON_BINDLESS
			for (const auto& BindlessShaderBindingInstance : BindlessShaderBindingInstances)
			{
				ShaderBindingInstanceArray.Add(BindlessShaderBindingInstance.get());
			}
#endif // TURN_ON_BINDLESS

			jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
			for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
			{
				ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
				const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
				if (pDynamicOffsetTest && pDynamicOffsetTest->size())
				{
					ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
				}
			}
			ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;
			g_rhi->BindRaytracingShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), RaytracingPipelineState, ShaderBindingInstanceCombiner, 0);

			// Binding Raytracing Pipeline State
			RaytracingPipelineState->Bind(RenderFrameContextPtr);

			g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), EResourceLayout::UAV);
			g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
			g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

			// Dispatch Rays
			jRaytracingDispatchData TracingData;
			TracingData.Width = RayRTWidth;
			TracingData.Height = RayRTHeight;
			TracingData.Depth = 1;
			TracingData.PipelineState = RaytracingPipelineState;
			g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

			g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), EResourceLayout::SHADER_READ_ONLY);
		}
	}

	// 2. Reprojection AO
	std::shared_ptr<jTexture> AfterReprojection = ReprojectionAO(RenderFrameContextPtr, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr);

	// 3. Denosing
	std::shared_ptr<jTexture> AfterDenoising = Denoise(AfterReprojection, gOptions.GetDenoiseName(gOptions.Denoiser), gOptions.GaussianKernelSize, gOptions.GaussianKernelSigma, gOptions.BilateralKernelSigma);
	g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), AfterDenoising.get());

	// 4. UpdateHistoryBuffer
	return UpdateHistoryBuffer(RenderFrameContextPtr, AfterDenoising);
}

void ApplyAOToFinalColor(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, std::shared_ptr<jTexture> InTexture)
{
	check(InTexture);

	// 5. Apply AO to Final color 
    DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Apply AO", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
    SCOPE_CPU_PROFILE(ApplyAO);
    SCOPE_GPU_PROFILE(InRenderFrameContextPtr, ApplyAO);

    jAOApplyCommonUniformBuffer CommonComputeData;
    CommonComputeData.Width = InTexture->Width;
    CommonComputeData.Height = InTexture->Height;
    CommonComputeData.AOIntensity = gOptions.AOIntensity;

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("OnFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
    OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

    bool IsApplyAOCompute = false;
    if (IsApplyAOCompute)
	{
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);

        jAOApplyCSParameters Parameters;
        Parameters.resultImage = { InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture() };
        Parameters.inputImage = { InTexture.get(), nullptr };
        Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

        jShaderAOApplyComputeShader::ShaderPermutation Permutation;
        Permutation.SetIndex<jShaderAOApplyComputeShader::SHOW_AO_ONLY>(gOptions.ShowAOOnly ? 1 : 0);
        const uint32 GroupX = InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Width / 8 + ((InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Width % 8) ? 1 : 0);
        const uint32 GroupY = InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Height / 8 + ((InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Height % 8) ? 1 : 0);
        DispatchShaderParameterComputePass<jShaderAOApplyComputeShader>(InRenderFrameContextPtr
            , Parameters
            , Permutation
            , GroupX, GroupY, 1);
	}
    else
    {
        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        jAOApplyPSParameters Parameters;
        Parameters.AOTexture = { InTexture.get(), SamplerState };
        Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

        jRHIUtil::DrawFullScreen(InRenderFrameContextPtr, InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr
        , [&, Parameters](const std::shared_ptr<jRenderFrameContext>& InInRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
        {
            g_rhi->TransitionLayout(InInRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get(), EResourceLayout::SHADER_READ_ONLY);
            jShaderParameterSet::BuildShaderBindings(Parameters, EShaderAccessStageFlag::ALL_GRAPHICS, InOutShaderBindingArray, InOutResourceInlineAllactor);
        }
        , [](const std::shared_ptr<jRenderFrameContext>&)
            {
                jShaderAOApplyPixelShader::ShaderPermutation Permutation;
                Permutation.SetIndex<jShaderAOApplyPixelShader::SHOW_AO_ONLY>(gOptions.ShowAOOnly ? 1 : 0);
                return jShaderAOApplyPixelShader::CreateShader(Permutation);
            }
        , [](jRasterizationStateInfo*& OutRasterState, jBlendingStateInfo*& OutBlendState, jDepthStencilStateInfo*& OutDepthStencilState)
            {
                OutRasterState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
                OutDepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
                    
                if (gOptions.ShowAOOnly)
                    OutBlendState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();
                else
                    OutBlendState = TBlendingStateInfo<true, EBlendFactor::ZERO, EBlendFactor::SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();
            });

    }
}

void jRenderer::AOPass()
{
	RTScale = (float)(atof(gOptions.UseResolution) / 100.0f);
	RayRTWidth = (int32)(SCR_WIDTH * RTScale);
	RayRTHeight = (int32)(SCR_HEIGHT * RTScale);

    // Create Persistent Resources
	if (!jSceneRenderTarget::GaussianV || jSceneRenderTarget::GaussianV->Width != (int32)RayRTWidth || jSceneRenderTarget::GaussianV->Height != (int32)RayRTHeight)
	{
		jSceneRenderTarget::GaussianV = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
			, ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
	}
	if (!jSceneRenderTarget::GaussianH || jSceneRenderTarget::GaussianH->Width != (int32)RayRTWidth || jSceneRenderTarget::GaussianH->Height != (int32)RayRTHeight)
	{
		jSceneRenderTarget::GaussianH = g_rhi->Create2DTexture((uint32)RayRTWidth, (uint32)RayRTHeight, (uint32)1, (uint32)1
			, ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
	}

	std::shared_ptr<jTexture> AOResult;
    if (GSupportRaytracing && gOptions.IsRTAO())
	{
		AOResult = RTAO();
	}
	else if (gOptions.IsSSAO())
	{
		AOResult = SSAO();
	}
	else
	{
		return;
	}
	if (gOptions.ShowDebugRT)
		DebugRTs.push_back(AOResult);

	ApplyAOToFinalColor(RenderFrameContextPtr, AOResult);
}

void jRenderer::SSGIPass()
{
    if (!gOptions.UseSSGI)
        return;

    SCOPE_CPU_PROFILE(SSGIPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SSGIPass);

    const float RTScale = gOptions.SSGIResolutionScale;
    const int32 RayRTWidth = (int32)(SCR_WIDTH * RTScale);
    const int32 RayRTHeight = (int32)(SCR_HEIGHT * RTScale);

    char eventName[128];
    sprintf_s(eventName, "SSGIPass (Res:%.0f%%, RPP:%d)", RTScale * 100.0f, gOptions.SSGIRayCount);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, eventName, Vector4(0.0f, 0.8f, 0.5f, 1.0f));

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

    auto SSGI_RT = jRenderTargetPool::GetRenderTargetForOneFrame({
		.Type = ETextureType::TEXTURE_2D,
		.Format = ETextureFormat::RGBA16F,
		.Width = RayRTWidth,
		.Height = RayRTHeight,
		.LayerCount = 1,
		.IsGenerateMipmap = false,
		.SampleCount = EMSAASamples::COUNT_1,
		.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
		.TextureCreateFlag = ETextureCreateFlag::UAV
	});
	jSceneRenderTarget::SSGI_RT = SSGI_RT;
    
    if (!jSceneRenderTarget::GIProjection || jSceneRenderTarget::GIProjection->Info.Width != RayRTWidth || jSceneRenderTarget::GIProjection->Info.Height != RayRTHeight)
    {
        jSceneRenderTarget::GIProjection = jRenderTargetPool::GetRenderTarget({
			.Type = ETextureType::TEXTURE_2D,
			.Format = ETextureFormat::RGBA16F,
			.Width = RayRTWidth,
			.Height = RayRTHeight,
			.LayerCount = 1,
			.IsGenerateMipmap = false,
			.SampleCount = EMSAASamples::COUNT_1,
			.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
			.TextureCreateFlag = ETextureCreateFlag::UAV
		});
    }

    jSSGICommonUniformBuffer CommonComputeData;

    auto mainCamera = jCamera::GetMainCamera();
    CommonComputeData.InvP = mainCamera->Projection.GetInverse();
    CommonComputeData.V = mainCamera->View;
    CommonComputeData.P = mainCamera->Projection;
    CommonComputeData.InvV = mainCamera->View.GetInverse();
    CommonComputeData.Radius = 50.0f; // temp
    CommonComputeData.Bias = 0.01f; // temp
    CommonComputeData.NoiseUVScale.x = (float)RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width / (float)GNoiseTexture->Width;
    CommonComputeData.NoiseUVScale.y = (float)RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height / (float)GNoiseTexture->Height;
    CommonComputeData.Width = SSGI_RT->Info.Width;
    CommonComputeData.Height = SSGI_RT->Info.Height;
    CommonComputeData.FrameNumber = (int32)g_rhi->GetCurrentFrameNumber();
    CommonComputeData.SSGI_MaxSteps = gOptions.SSGIMaxSteps;
    CommonComputeData.CameraPos = mainCamera->Pos;
    CommonComputeData.SSGI_MaxDistance = gOptions.SSGIMaxDistance;
    CommonComputeData.SSGI_RayCount = gOptions.SSGIRayCount;
    CommonComputeData.UseAttenuation = gOptions.UseSSGIAttenuation ? 1 : 0;
    CommonComputeData.Padding1 = 0;
    CommonComputeData.Padding2 = 0;

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("SSGI_OnFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
    OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SSGI_RT->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::REPEAT
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    const jSamplerStateInfo* RepeatSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    jSSGICSParameters Parameters;
    Parameters.Result = { SSGI_RT->GetTexture() };
    Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
    Parameters.GBuffer0 = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState };
    Parameters.GBuffer1 = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), SamplerState };
    Parameters.GBuffer2 = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), SamplerState };
    Parameters.ColorTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), SamplerState };
    Parameters.Noise = { GNoiseTexture.get(), RepeatSamplerState };
    Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

    const uint32 GroupX = SSGI_RT->GetTexture()->Width / 8 + ((SSGI_RT->GetTexture()->Width % 8) ? 1 : 0);
    const uint32 GroupY = SSGI_RT->GetTexture()->Height / 8 + ((SSGI_RT->GetTexture()->Height % 8) ? 1 : 0);
    DispatchShaderParameterComputePass(RenderFrameContextPtr
        , jNameStatic("SSGI_CS")
        , jNameStatic("Resource/Shaders/hlsl/SSGI_cs.hlsl")
        , jNameStatic("main")
        , Parameters
        , GroupX, GroupY, 1);

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), SSGI_RT->GetTexture());
}

void jRenderer::SSGIAccumulatePass()
{
    if (!gOptions.UseSSGI || !gOptions.UseSSGITemporalAccumulation)
        return;

    SCOPE_CPU_PROFILE(SSGIAccumulatePass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SSGIAccumulatePass);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SSGIAccumulatePass", Vector4(0.0f, 0.8f, 0.5f, 1.0f));

    const float RTScale = gOptions.SSGIResolutionScale;
    const int32 RayRTWidth = (int32)(SCR_WIDTH * RTScale);
    const int32 RayRTHeight = (int32)(SCR_HEIGHT * RTScale);

    if (!jSceneRenderTarget::SSGI_Accum_RT[0] || jSceneRenderTarget::SSGI_Accum_RT[0]->Info.Width != RayRTWidth || jSceneRenderTarget::SSGI_Accum_RT[0]->Info.Height != RayRTHeight)
    {
        jRenderTargetInfo Info = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::RGBA16F,
            .Width = RayRTWidth,
            .Height = RayRTHeight,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = EMSAASamples::COUNT_1,
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .TextureCreateFlag = ETextureCreateFlag::UAV,
            .ResourceName = jNameStatic("SSGI_Accum_0")
        };
        jSceneRenderTarget::SSGI_Accum_RT[0] = g_rhi->CreateRenderTarget(Info);
        Info.ResourceName = jNameStatic("SSGI_Accum_1");
        jSceneRenderTarget::SSGI_Accum_RT[1] = g_rhi->CreateRenderTarget(Info);
        Info.ResourceName = jNameStatic("SSGI_Accum_2");
        jSceneRenderTarget::SSGI_Accum_RT[2] = g_rhi->CreateRenderTarget(Info);
    }

    const int32 RTIndex = RenderFrameContextPtr->FrameIndex % 3;
    const int32 PrevRTIndex = (RenderFrameContextPtr->FrameIndex + 2) % 3;

    auto SSGI_Accum_RT_Dest = jSceneRenderTarget::SSGI_Accum_RT[RTIndex];
    auto SSGI_Accum_RT_Prev = jSceneRenderTarget::SSGI_Accum_RT[PrevRTIndex];

    if (gOptions.UseSSGIReprojection)
    {
        jSSGIReprojectionUniformBuffer CommonComputeData;
        CommonComputeData.Width = SSGI_Accum_RT_Dest->Info.Width;
        CommonComputeData.Height = SSGI_Accum_RT_Dest->Info.Height;
        CommonComputeData.FrameNumber = g_rhi->GetCurrentFrameNumber();
        CommonComputeData.BlendFactor = gOptions.SSGIAccumBlendFactor;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("ReprojectionSSGIUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        bool IsSSGIReprojectionCompute = false;
        if (IsSSGIReprojectionCompute)
        {
            auto* CommandBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();
            g_rhi->TransitionLayout(CommandBuffer, SSGI_Accum_RT_Dest->GetTexture(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(CommandBuffer, SSGI_Accum_RT_Prev->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(CommandBuffer, jSceneRenderTarget::SSGI_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(CommandBuffer, RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(CommandBuffer, jSceneRenderTarget::HistoryDepthBuffer.get(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(CommandBuffer, RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            jSSGIReprojectionCSParameters Parameters;
            Parameters.resultImage = { SSGI_Accum_RT_Dest->GetTexture() };
            Parameters.HistoryBuffer = { SSGI_Accum_RT_Prev->GetTexture(), nullptr };
            Parameters.VelocityBuffer = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), nullptr };
            Parameters.DepthBuffer = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr };
            Parameters.HistoryDepthBuffer = { jSceneRenderTarget::HistoryDepthBuffer.get() };
            Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

            jShaderSSGIReprojectionComputeShader::ShaderPermutation Permutation;
            Permutation.SetIndex<jShaderSSGIReprojectionComputeShader::USE_DISCONTINUITY_WEIGHT>(gOptions.UseDiscontinuityWeightForSSGI ? 1 : 0);
            const uint32 GroupX = SSGI_Accum_RT_Dest->GetTexture()->Width / 8 + ((SSGI_Accum_RT_Dest->GetTexture()->Width % 8) ? 1 : 0);
            const uint32 GroupY = SSGI_Accum_RT_Dest->GetTexture()->Height / 8 + ((SSGI_Accum_RT_Dest->GetTexture()->Height % 8) ? 1 : 0);
            DispatchShaderParameterComputePass<jShaderSSGIReprojectionComputeShader>(RenderFrameContextPtr
                , Parameters
                , Permutation
                , GroupX, GroupY, 1);
        }
        else
        {
            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            jSSGIReprojectionPSParameters Parameters;
            Parameters.CurrentTexture = { jSceneRenderTarget::SSGI_RT->GetTexture(), SamplerState };
            Parameters.HistoryBuffer = { SSGI_Accum_RT_Prev->GetTexture(), SamplerState };
            Parameters.VelocityBuffer = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), SamplerState };
            Parameters.DepthBuffer = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
            Parameters.HistoryDepthBuffer = { jSceneRenderTarget::HistoryDepthBuffer.get(), SamplerState };
            Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

            jRHIUtil::DrawFullScreen(RenderFrameContextPtr, SSGI_Accum_RT_Dest
                , [&, Parameters](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), SSGI_Accum_RT_Prev->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SSGI_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::VELOCITY)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HistoryDepthBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    jShaderParameterSet::BuildShaderBindings(Parameters, EShaderAccessStageFlag::FRAGMENT, InOutShaderBindingArray, InOutResourceInlineAllactor);
                }
            , [](const std::shared_ptr<jRenderFrameContext>&)
                {
                    jShaderSSGIReprojectionPixelShader::ShaderPermutation Permutation;
                    Permutation.SetIndex<jShaderSSGIReprojectionPixelShader::USE_DISCONTINUITY_WEIGHT>(gOptions.UseDiscontinuityWeightForSSGI ? 1 : 0);
                    return jShaderSSGIReprojectionPixelShader::CreateShader(Permutation);
                });
        }
    }
    else
    {
        jSSGIAccumulateUniformBuffer CommonComputeData;
        CommonComputeData.g_Width = SSGI_Accum_RT_Dest->Info.Width;
        CommonComputeData.g_Height = SSGI_Accum_RT_Dest->Info.Height;
        CommonComputeData.g_SSGIAccumBlendFactor = gOptions.SSGIAccumBlendFactor;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("SSGIAccum_OnFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SSGI_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SSGI_Accum_RT_Prev->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

        jSSGIAccumulateCSParameters Parameters;
        Parameters.OutSSGIAccum = { SSGI_Accum_RT_Dest->GetTexture() };
        Parameters.InSSGI = { jSceneRenderTarget::SSGI_RT->GetTexture(), SamplerState };
        Parameters.InPrevSSGIAccum = { SSGI_Accum_RT_Prev->GetTexture(), SamplerState };
        Parameters.SSGIAccumUniformBuffer.Buffer = OneFrameUniformBuffer;

        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("SSGI_Accumulate_CS")
            , jNameStatic("Resource/Shaders/hlsl/SSGI_Accumulate_cs.hlsl")
            , jNameStatic("main")
            , Parameters
            , SSGI_Accum_RT_Dest->GetTexture()->Width / 8 + ((SSGI_Accum_RT_Dest->GetTexture()->Width % 8) ? 1 : 0)
            , SSGI_Accum_RT_Dest->GetTexture()->Height / 8 + ((SSGI_Accum_RT_Dest->GetTexture()->Height % 8) ? 1 : 0)
            , 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), SSGI_Accum_RT_Dest->GetTexture());
}

std::shared_ptr<jRenderTarget> SeparableGaussianBlur(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const std::shared_ptr<jTexture>& InTexture, int32 InKernelSize, float InKernelSigma)
{
    DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "GaussianSeparable", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
    SCOPE_CPU_PROFILE(GaussianSeparable);
    SCOPE_GPU_PROFILE(InRenderFrameContextPtr, GaussianSeparable);

    auto createGaussianKernel = [](int32 kernelSize, float sigma) -> std::vector<float>
    {
        std::vector<float> kernel(kernelSize);
        int32 center = kernelSize / 2;
        float sum = 0.0;

        for (int32 i = 0; i < kernelSize; ++i)
        {
            float x = (float)(i - center);
            kernel[i] = exp(-(x * x) / (2 * sigma * sigma)) / (sqrt(2 * PI) * sigma);
            sum += kernel[i];
        }

        // Normalize the kernel
        for (int32 i = 0; i < kernelSize; ++i)
        {
            kernel[i] /= sum;
        }

        return kernel;
    };

    std::vector<float> GaussianKernel = createGaussianKernel(InKernelSize, InKernelSigma);

    // Create GaussianBlurKernel uniformbuffer
    jGaussianBlurKernelUniformBuffer KernelData;
    check(sizeof(KernelData.Data) >= GaussianKernel.size() * sizeof(float));
    memcpy(KernelData.Data, GaussianKernel.data(), GaussianKernel.size() * sizeof(float));

    auto OneFrameGaussianKernelUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("GaussianKernel"), jLifeTimeType::OneFrame, sizeof(KernelData)));
    OneFrameGaussianKernelUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

    // Create common uniformbuffer
    jGaussianBlurCommonComputeUniformBuffer CommonComputeData;
    CommonComputeData.Width = InTexture->Width;
    CommonComputeData.Height = InTexture->Height;
    CommonComputeData.KernelSize = (int32)GaussianKernel.size();

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
    OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

    auto GaussianV = jRenderTargetPool::GetRenderTargetForOneFrame({
		.Type = ETextureType::TEXTURE_2D,
		.Format = InTexture->Format,
		.Width = InTexture->Width,
		.Height = InTexture->Height,
		.LayerCount = 1,
		.IsGenerateMipmap = false,
		.SampleCount = EMSAASamples::COUNT_1,
		.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
		.TextureCreateFlag = ETextureCreateFlag::UAV
	});
    auto GaussianH = jRenderTargetPool::GetRenderTargetForOneFrame({
		.Type = ETextureType::TEXTURE_2D,
		.Format = InTexture->Format,
		.Width = InTexture->Width,
		.Height = InTexture->Height,
		.LayerCount = 1,
		.IsGenerateMipmap = false,
		.SampleCount = EMSAASamples::COUNT_1,
		.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
		.TextureCreateFlag = ETextureCreateFlag::UAV
	});

    g_rhi->UAVBarrier(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture.get());
    {
        DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Vertical", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(Vertical);
        SCOPE_GPU_PROFILE(InRenderFrameContextPtr, Vertical);

        DispatchGaussianBlurPass(InRenderFrameContextPtr, GaussianV->GetTexture(), InTexture.get()
            , OneFrameUniformBuffer, OneFrameGaussianKernelUniformBuffer, jNameStatic("GaussianV"), jNameStatic("Vertical"));
    }

    g_rhi->UAVBarrier(InRenderFrameContextPtr->GetActiveCommandBuffer(), GaussianV->GetTexture());

    {
        DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "Horizon", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(Horizon);
        SCOPE_GPU_PROFILE(InRenderFrameContextPtr, Horizon);

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        DispatchGaussianBlurPass(InRenderFrameContextPtr, GaussianH->GetTexture(), GaussianV->GetTexture()
            , OneFrameUniformBuffer, OneFrameGaussianKernelUniformBuffer, jNameStatic("GaussianH"), jNameStatic("Horizon"));
    }
    return GaussianH;
}

std::shared_ptr<jTexture> jRenderer::BlurSSGI(const std::shared_ptr<jRenderTarget>& InRenderTarget)
{
    if (gOptions.SSGIDenoiser == EDenoiser::NONE)
        return InRenderTarget->GetTexturePtr();

    if (gOptions.IsSSGIDenoiserGuassian() || gOptions.IsSSGIDenoiserBilateral())
    {
        const jName ProfileTitle = gOptions.IsSSGIDenoiserBilateral() ? jNameStatic("BilateralSSGI") : jNameStatic("GaussianSSGI");
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, ProfileTitle.ToStr(), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(ProfileTitle);
        SCOPE_GPU_PROFILE_NAME(RenderFrameContextPtr, ProfileTitle);

        auto createGaussian2DKernel = [](int32 kernelSize, float sigma) -> std::vector<float>
        {
            std::vector<float> kernel(kernelSize * kernelSize);
            int32 center = kernelSize / 2;
            float sum = 0.0;

            int32 Index = 0;
            for (int32 j = 0; j < kernelSize; ++j)
            {
                for (int32 i = 0; i < kernelSize; ++i)
                {
                    float x = (float)(i - center);
                    float y = (float)(j - center);
                    kernel[Index] = exp(-0.5f * (x * x + y * y) / (sigma * sigma)) / (2 * PI * sigma * sigma);
                    sum += kernel[Index];
                    ++Index;
                }
            }

            // Normalize the kernel
            for (int32 i = 0; i < (int32)kernel.size(); ++i)
            {
                kernel[i] /= sum;
            }

            return kernel;
        };

        std::vector<float> GaussianKernel = createGaussian2DKernel(gOptions.SSGIDenoiserKernelSize, gOptions.SSGIDenoiserKernelSigma);
        auto DenoiseTarget = jRenderTargetPool::GetRenderTargetForOneFrame(InRenderTarget->Info);

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTarget->GetTexture());
        {
            jBilateralCommonComputeUniformBuffer CommonComputeData;
            CommonComputeData.Width = DenoiseTarget->Info.Width;
            CommonComputeData.Height = DenoiseTarget->Info.Height;
            CommonComputeData.Sigma = gOptions.SSGIDenoiserKernelSigma;
            CommonComputeData.KernelSize = gOptions.SSGIDenoiserKernelSize;
            CommonComputeData.SigmaForBilateral = gOptions.SSGIDenoiserBilateralKernelSigma;

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
            OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

            jBilateralKernelUniformBuffer KernelData;
            check(sizeof(KernelData.Data) >= GaussianKernel.size() * sizeof(float));
            memcpy(KernelData.Data, GaussianKernel.data(), GaussianKernel.size() * sizeof(float));

            auto OneFrameGaussianKernelUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                jNameStatic("GaussianKernel"), jLifeTimeType::OneFrame, sizeof(KernelData)));
            OneFrameGaussianKernelUniformBuffer->UpdateBufferData(&KernelData, sizeof(KernelData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTarget->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            jBilateralFilteringCSParameters Parameters;
            Parameters.resultImage = { DenoiseTarget->GetTexture() };
            Parameters.inputImage = { InRenderTarget->GetTexture(), nullptr };
            Parameters.DepthBuffer = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
            Parameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
            Parameters.Kernal.Buffer = OneFrameGaussianKernelUniformBuffer;

            jShaderBilateralComputeShader::ShaderPermutation Permutation;
            Permutation.SetIndex<jShaderBilateralComputeShader::USE_GAUSSIAN_INSTEAD>(gOptions.IsSSGIDenoiserGuassian() ? 1 : 0);
            DispatchShaderParameterComputePass<jShaderBilateralComputeShader>(RenderFrameContextPtr
                , Parameters
                , Permutation
                , DenoiseTarget->Info.Width / 8 + ((DenoiseTarget->Info.Width % 8) ? 1 : 0)
                , DenoiseTarget->Info.Height / 8 + ((DenoiseTarget->Info.Height % 8) ? 1 : 0)
                , 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), DenoiseTarget->GetTexture());
        return DenoiseTarget->GetTexturePtr();
    }
    else if (gOptions.IsSSGIDenoiserBilateralPS())
    {
        const jName ProfileTitle = jNameStatic("BilateralPSSGI");
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, ProfileTitle.ToStr(), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(ProfileTitle);
        SCOPE_GPU_PROFILE_NAME(RenderFrameContextPtr, ProfileTitle);

        auto DenoiseTarget = jRenderTargetPool::GetRenderTargetForOneFrame(InRenderTarget->Info);

        jBilateralCommonComputeUniformBuffer CommonComputeData;
        CommonComputeData.Width = DenoiseTarget->Info.Width;
        CommonComputeData.Height = DenoiseTarget->Info.Height;
        CommonComputeData.Sigma = gOptions.SSGIDenoiserKernelSigma;
        CommonComputeData.KernelSize = gOptions.SSGIDenoiserKernelSize;
        CommonComputeData.SigmaForBilateral = gOptions.SSGIDenoiserBilateralKernelSigma;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        jRHIUtil::DrawFullScreen(RenderFrameContextPtr, DenoiseTarget,
            [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
            {
                g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTarget->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                jBilateralPSParameters Parameters;
                Parameters.Param.Buffer = OneFrameUniformBuffer;
                Parameters.InTexture = { InRenderTarget->GetTexture(), SamplerState };
                Parameters.DepthTexture = { InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
                jShaderParameterSet::BuildShaderBindings(Parameters, EShaderAccessStageFlag::FRAGMENT, InOutShaderBindingArray, InOutResourceInlineAllactor);
            },
            [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
            {
                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("BilateralPS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/bilateral_ps.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                shaderInfo.SetEntryPoint(jNameStatic("PS"));
                jShaderParameterSet::AppendToShaderInfo<jBilateralPSParameters>(shaderInfo, 0);
                return g_rhi->CreateShader(shaderInfo);
            }
        );
        return DenoiseTarget->GetTexturePtr();
    }
    else if (gOptions.IsSSGIDenoise_A_Trous())
    {
        const jName ProfileTitle = jNameStatic("A-Trous_SSGI");
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, ProfileTitle.ToStr(), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(ProfileTitle);
        SCOPE_GPU_PROFILE_NAME(RenderFrameContextPtr, ProfileTitle);

        auto RT1 = jRenderTargetPool::GetRenderTargetForOneFrame(InRenderTarget->Info);
        auto RT2 = jRenderTargetPool::GetRenderTargetForOneFrame(InRenderTarget->Info);

        std::shared_ptr<jTexture> CurrentInput = InRenderTarget->GetTexturePtr();
        std::shared_ptr<jRenderTarget> CurrentOutput = RT1;

        for (int32 i = 0; i < gOptions.SSGIBlurQuality; ++i)
        {
            g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), CurrentInput.get());

            jATrousUniformBuffer UniformData;
            UniformData.g_StepSize = 1 << i;
            UniformData.g_Sigma_Color = gOptions.SSGIATrousSigmaColor;
            UniformData.g_Sigma_Normal = gOptions.SSGIATrousSigmaNormal;
            UniformData.g_Sigma_Depth = gOptions.SSGIATrousSigmaDepth;
            UniformData.g_KernelSize = gOptions.SSGIDenoiserKernelSize;

            auto UniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("A_TrousUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformData)));
            UniformBuffer->UpdateBufferData(&UniformData, sizeof(UniformData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), CurrentOutput->GetTexture(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), CurrentInput.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            const jSamplerStateInfo* LinearSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            jSSGIATrousCSParameters Parameters;
            Parameters.OutTexture = { CurrentOutput->GetTexture() };
            Parameters.InTexture = { CurrentInput.get(), nullptr };
            Parameters.NormalTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), LinearSamplerState };
            Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), LinearSamplerState };
            Parameters.A_TrousUniformBuffer.Buffer = UniformBuffer;

            const uint32 GroupX = CurrentOutput->GetTexture()->Width / 8 + ((CurrentOutput->GetTexture()->Width % 8) ? 1 : 0);
            const uint32 GroupY = CurrentOutput->GetTexture()->Height / 8 + ((CurrentOutput->GetTexture()->Height % 8) ? 1 : 0);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SSGI_ATrous_CS")
                , jNameStatic("Resource/Shaders/hlsl/SSGI_ATrous_cs.hlsl")
                , jNameStatic("main")
                , Parameters
                , GroupX, GroupY, 1);

            CurrentInput = CurrentOutput->GetTexturePtr();
            CurrentOutput = (CurrentOutput == RT1) ? RT2 : RT1;
        }
        return CurrentInput;
    }
    else if (gOptions.IsSSGIDenoiserGuassianSeparable())
    {
        if (gOptions.SSGIBlurQuality <= 0)
            return InRenderTarget->GetTexturePtr();

        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "BlurSSGI", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(BlurSSGI);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BlurSSGI);

        auto Downsample = [&](std::shared_ptr<jRenderTarget> InRT, const Vector2i& InSize) -> std::shared_ptr<jRenderTarget>
        {
            auto DownsampleRT = jRenderTargetPool::GetRenderTargetForOneFrame({
				.Type = ETextureType::TEXTURE_2D,
				.Format = InRT->Info.Format,
				.Width = InSize.x,
				.Height = InSize.y,
				.LayerCount = 1,
				.IsGenerateMipmap = false,
				.SampleCount = EMSAASamples::COUNT_1,
				.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
				.TextureCreateFlag = ETextureCreateFlag::UAV
			});
            jRHIUtil::DrawQuad(RenderFrameContextPtr, DownsampleRT, { 0, 0, InSize.x, InSize.y },
                [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                    jRHIUtil::BuildSingleTextureFragmentBindings(InRT->GetTexture(), SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
                },
                [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo shaderInfo;
                    shaderInfo.SetName(jNameStatic("CopyPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
                    return g_rhi->CreateShader(shaderInfo);
                }
            );
            return DownsampleRT;
        };

        // Create a copy of the input render target to avoid modifying the original accumulation buffer.
        auto BlurredResultRT = Downsample(InRenderTarget, { InRenderTarget->Info.Width, InRenderTarget->Info.Height });

        std::vector<std::shared_ptr<jRenderTarget>> DownsampleChain;
        DownsampleChain.push_back(BlurredResultRT);

        // Downsample
        std::shared_ptr<jRenderTarget> LastRT = BlurredResultRT;
        for (int32 i = 0; i < gOptions.SSGIBlurQuality; ++i)
        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SSGI Downsample", Vector4(0.8f, 0.5f, 0.0f, 1.0f));
            Vector2i NextSize(LastRT->Info.Width / 2, LastRT->Info.Height / 2);
            if (NextSize.x <= 0 || NextSize.y <= 0)
                break;

            auto Downsampled = Downsample(LastRT, NextSize);
            auto Blurred = SeparableGaussianBlur(RenderFrameContextPtr, Downsampled->GetTexturePtr(), gOptions.SSGIDenoiserKernelSize, gOptions.SSGIDenoiserKernelSigma);
            DownsampleChain.push_back(Blurred);
            LastRT = Blurred;
        }

        // Upsample
        for (int32 i = (int32)DownsampleChain.size() - 1; i > 0; --i)
        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SSGI Upsample", Vector4(0.0f, 0.8f, 0.2f, 1.0f));
            auto Src = DownsampleChain[i];
            auto Dst = DownsampleChain[i - 1];
            Vector2i DstSize(Dst->Info.Width, Dst->Info.Height);

            auto Upsampled = Downsample(Src, DstSize);
            // Additive Blending
            {
                struct jUniformBuffer
                {
                    Vector4 DstTextureSize;
                };
                jUniformBuffer UniformData;
                UniformData.DstTextureSize.x = (float)Dst->Info.Width;
                UniformData.DstTextureSize.y = (float)Dst->Info.Height;
                UniformData.DstTextureSize.z = 1.0f / Dst->Info.Width;
                UniformData.DstTextureSize.w = 1.0f / Dst->Info.Height;

                auto UniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("SSGIBlurUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformData));
                UniformBuffer->UpdateBufferData(&UniformData, sizeof(UniformData));

                jRHIUtil::DrawQuad(RenderFrameContextPtr, Dst, { 0, 0, Dst->Info.Width, Dst->Info.Height },
                    [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                    {
                        g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), Upsampled->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                        jRHIUtil::BuildSingleTextureFragmentBindings(Upsampled->GetTexture(), SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
                        InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::FRAGMENT
                            , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(UniformBuffer.get())));
                    },
                    [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                    {
                        jShaderInfo shaderInfo;
                        shaderInfo.SetName(jNameStatic("CopyPS"));
                        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                        jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
                        return g_rhi->CreateShader(shaderInfo);
                    },
                    [](jRasterizationStateInfo*& OutRasterState, jBlendingStateInfo*& OutBlendState, jDepthStencilStateInfo*& OutDepthStencilState)
                    {
                        //OutBlendState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

                        OutRasterState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
                        OutDepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
                        OutBlendState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

                    }
                );
            }
        }
        return DownsampleChain[0]->GetTexturePtr();
    }
    return InRenderTarget->GetTexturePtr();
}
