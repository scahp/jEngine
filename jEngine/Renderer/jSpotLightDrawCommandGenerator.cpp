#include "pch.h"
#include "jSpotLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"

jObject* jSpotLightDrawCommandGenerator::SpotLightUIQuad = nullptr;

jSpotLightDrawCommandGenerator::jSpotLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances)
    : ShaderBindingInstances(InShaderBindingInstances)
{
    if (!SpotLightUIQuad)
        SpotLightUIQuad = jPrimitiveUtil::CreateUIQuad(Vector2(), Vector2(), nullptr);
}

void jSpotLightDrawCommandGenerator::Initialize(int32 InRTWidth, int32 InRTHeight)
{
    RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false>::Create();
    MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    // SpotLight 의 경우 카메라에서 가장 먼쪽에 있는 Mesh 면을 렌더링하고(Quad 를 렌더링하기 때문에 MaxPos.z 로 컨트롤), 그 면 부터 카메라 사이에 있는 공간에 대해서만 라이트를 적용함.
    DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create();
    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)InRTWidth, (float)InRTHeight), jScissor(0, 0, InRTWidth, InRTHeight), gOptions.UseVRS);

    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("SpotLightShaderVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/spotlight_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

        shaderInfo.SetName(jNameStatic("SpotLightShaderPS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/spotlight_fs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
        if (gOptions.UseSubpass)
            shaderInfo.SetPreProcessors(jNameStatic("#define USE_SUBPASS 1"));
        Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
    }

    ScreenSize.x = (float)InRTWidth;
    ScreenSize.y = (float)InRTHeight;
}

void jSpotLightDrawCommandGenerator::GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex)
{
    jPushConstant* PushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(jSpotLightPushConstant(), EShaderAccessStageFlag::ALL);
    jSpotLightPushConstant& SpotLightPushConstant = PushConstant->Get<jSpotLightPushConstant>();

    Vector MinPos;
    Vector MaxPos;
    const jLight* SpotLight = InLightView.Light;
    if (SpotLight && SpotLight->GetLightCamra())
    {
        SpotLight->GetLightCamra()->GetRectInScreenSpace(MinPos, MaxPos, InView->Camera->GetViewProjectionMatrix(), ScreenSize);
    }
    SpotLightPushConstant.Pos = Vector2(MinPos.x, MinPos.y);
    SpotLightPushConstant.Size = Vector2(MaxPos.x - MinPos.x, MaxPos.y - MinPos.y);
    SpotLightPushConstant.PixelSize = Vector2(1.0f) / ScreenSize;
    SpotLightPushConstant.Depth = MaxPos.z;

    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, SpotLightUIQuad->RenderObject, InRenderPass
        , Shader, &PipelineStateFixedInfo, ShaderBindingInstances, PushConstant, InSubpassIndex);
    OutDestDrawCommand->PrepareToDraw(false);
}
