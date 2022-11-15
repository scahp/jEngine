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
    RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false>::Create();
    MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)InRTWidth, (float)InRTHeight), jScissor(0, 0, InRTWidth, InRTHeight), gOptions.UseVRS);

    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("DirectionalLightShaderVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        Shader.VertexShader = g_rhi->CreateShader(shaderInfo);
    }
    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("DirectionalLightShaderPS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/directionallight_fs.hlsl"));
        if (gOptions.UseSubpass)
            shaderInfo.SetPreProcessors(jNameStatic("#define USE_SUBPASS 1"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
        Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
    }
}

void jDirectionalLightDrawCommandGenerator::GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex)
{
    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, GlobalFullscreenPrimitive->RenderObject, InRenderPass
        , Shader, &PipelineStateFixedInfo, ShaderBindingInstances, {}, InSubpassIndex);
    OutDestDrawCommand->PrepareToDraw(false);
}
