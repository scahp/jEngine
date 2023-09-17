#include "pch.h"
#include "jDirectionalLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"

jObject* jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive = nullptr;

jDirectionalLightDrawCommandGenerator::jDirectionalLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances)
    : ShaderBindingInstances(InShaderBindingInstances)
{
    if (!GlobalFullscreenPrimitive)
        GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
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
    
    DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)InRTWidth, (float)InRTHeight), jScissor(0, 0, InRTWidth, InRTHeight), gOptions.UseVRS);

    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("DirectionalLightShaderVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        Shader.VertexShader = g_rhi->CreateShader(shaderInfo);
    }
}

void jDirectionalLightDrawCommandGenerator::GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex)
{
    jShaderDirectionalLightPixelShader::ShaderPermutation ShaderPermutation;
    ShaderPermutation.SetIndex<jShaderDirectionalLightPixelShader::USE_SUBPASS>(gOptions.UseSubpass);
    ShaderPermutation.SetIndex<jShaderDirectionalLightPixelShader::USE_SHADOW_MAP>(InLightView.ShadowMapPtr ? 1 : 0);
    Shader.PixelShader = jShaderDirectionalLightPixelShader::CreateShader(ShaderPermutation);

    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, GlobalFullscreenPrimitive->RenderObjects[0], InRenderPass
        , Shader, &PipelineStateFixedInfo, ShaderBindingInstances, {}, nullptr, InSubpassIndex);
    OutDestDrawCommand->PrepareToDraw(false);
}
