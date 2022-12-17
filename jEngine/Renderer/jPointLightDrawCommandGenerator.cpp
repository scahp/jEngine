#include "pch.h"
#include "jPointLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"

jObject* jPointLightDrawCommandGenerator::PointLightSphere = nullptr;

jPointLightDrawCommandGenerator::jPointLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances)
    : ShaderBindingInstances(InShaderBindingInstances)
{
    if (!PointLightSphere)
        PointLightSphere = jPrimitiveUtil::CreateSphere(Vector::ZeroVector, 1.0, 16, Vector(1.0f), Vector4::OneVector);
}

void jPointLightDrawCommandGenerator::Initialize(int32 InRTWidth, int32 InRTHeight)
{
    MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    // PointLight 의 경우 카메라에서 가장 먼쪽에 있는 Mesh 면을 렌더링하고, 그 면 부터 카메라 사이에 있는 공간에 대해서만 라이트를 적용함.
    RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false>::Create();
    DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create();

    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)InRTWidth, (float)InRTHeight), jScissor(0, 0, InRTWidth, InRTHeight), gOptions.UseVRS);

    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("PointLightShaderVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/pointlight_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        Shader.VertexShader = g_rhi->CreateShader(shaderInfo);
    }
}

void jPointLightDrawCommandGenerator::GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex)
{
    jPushConstant* PushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(
        jPointLightPushConstant(InView->Camera->Projection * InView->Camera->View * (*InLightView.Light->GetLightWorldMatrix())), EShaderAccessStageFlag::ALL);

    jShaderPointLight::ShaderPermutation ShaderPermutation;
    ShaderPermutation.SetIndex<jShaderPointLight::USE_SUBPASS>(gOptions.UseSubpass);
    ShaderPermutation.SetIndex<jShaderPointLight::USE_SHADOW_MAP>(InLightView.ShadowMapPtr ? 1 : 0);
    Shader.PixelShader = jShaderPointLight::CreateShader(ShaderPermutation);

    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, PointLightSphere->RenderObjects[0], InRenderPass
        , Shader, &PipelineStateFixedInfo, ShaderBindingInstances, PushConstant, nullptr, InSubpassIndex);
    OutDestDrawCommand->PrepareToDraw(false);
}
