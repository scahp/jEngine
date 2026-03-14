#include "pch.h"
#include "jDirectionalLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "Scene/jRenderObject.h"
#include "Shader/jLightingShaderParameters.h"
#include "jSceneRenderTargets.h"

jObject* jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive = nullptr;

jDirectionalLightDrawCommandGenerator::jDirectionalLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances)
    : ShaderBindingInstances(InShaderBindingInstances)
{
}

void jDirectionalLightDrawCommandGenerator::Initialize(int32 InRTWidth, int32 InRTHeight)
{
    switch(g_rhi->GetSelectedMSAASamples())
    {
    case EMSAASamples::COUNT_1:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_2:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_4:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_8:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
        break;
    default:
        check(0);
        break;
    }
    
    DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::ALWAYS, false, false, 0.0f, 1.0f>::Create();
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)InRTWidth, (float)InRTHeight), jScissor(0, 0, InRTWidth, InRTHeight), gOptions.UseVRS);

    {
        Shader.VertexShader = jShaderFullscreenQuadVertexShader::CreateShader(jShaderFullscreenQuadVertexShader::ShaderPermutation());
    }
}

void jDirectionalLightDrawCommandGenerator::GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex)
{
    jShaderDirectionalLightPixelShader::ShaderPermutation ShaderPermutation;
    ShaderPermutation.SetIndex<jShaderDirectionalLightPixelShader::USE_SUBPASS>(gOptions.UseSubpass);
    ShaderPermutation.SetIndex<jShaderDirectionalLightPixelShader::USE_SHADOW_MAP>(InLightView.ShadowMapPtr ? 1 : 0);
    ShaderPermutation.SetIndex<jShaderDirectionalLightPixelShader::USE_PBR>(ENABLE_PBR);
    Shader.PixelShader = jShaderDirectionalLightPixelShader::CreateShader(ShaderPermutation);

    jShaderBindingInstanceArray CopyShaderBindingInstances = ShaderBindingInstances;
    CopyShaderBindingInstances.Add(InLightView.ShaderBindingInstance.get());

    const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::NEAREST_MIPMAP_LINEAR, ETextureFilter::NEAREST_MIPMAP_LINEAR
        , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    jDirectionalLightIBLShaderParameters Parameters;
    Parameters.IrradianceMap = { jSceneRenderTarget::IrradianceMap2, ShadowSamplerStateInfo };
    Parameters.PrefilteredEnvMap = { jSceneRenderTarget::FilteredEnvMap2, ShadowSamplerStateInfo };
    temp = jShaderParameterSet::CreateShaderBindingInstance(Parameters, EShaderAccessStageFlag::FRAGMENT, jShaderBindingInstanceType::SingleFrame);
    CopyShaderBindingInstances.Add(temp.get());

    SceneTextureShaderBindingInstance = InRenderFrameContextPtr->SceneRenderTargetPtr->PrepareGBufferShaderBindingInstance(gOptions.UseSubpass);
    CopyShaderBindingInstances.Add(SceneTextureShaderBindingInstance.get());

    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, GlobalFullscreenPrimitive->RenderObjects[0], InRenderPass
        , Shader, &PipelineStateFixedInfo, GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), CopyShaderBindingInstances, {}, nullptr, InSubpassIndex);
    OutDestDrawCommand->Test = true;
    OutDestDrawCommand->PrepareToDraw(false);
}
