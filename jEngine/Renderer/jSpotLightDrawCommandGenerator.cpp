#include "pch.h"
#include "jSpotLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"

jObject* jSpotLightDrawCommandGenerator::SpotLightCone = nullptr;

jSpotLightDrawCommandGenerator::jSpotLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances)
    : ShaderBindingInstances(InShaderBindingInstances)
{
    if (!SpotLightCone)
        SpotLightCone = jPrimitiveUtil::CreateCone(Vector::ZeroVector, 1.0, 1.0, 20, Vector::OneVector, Vector4::OneVector, false, false);
}

void jSpotLightDrawCommandGenerator::Initialize(int32 InRTWidth, int32 InRTHeight)
{
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    // SpotLight 의 경우 카메라에서 가장 먼쪽에 있는 Mesh 면을 렌더링하고, 그 면 부터 카메라 사이에 있는 공간에 대해서만 라이트를 적용함.
    switch (g_rhi->GetSelectedMSAASamples())
    {
    case EMSAASamples::COUNT_1:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_2:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_4:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_8:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
        break;
    default:
        check(0);
        break;
    }
    DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create();

    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)InRTWidth, (float)InRTHeight), jScissor(0, 0, InRTWidth, InRTHeight), gOptions.UseVRS);

    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("SpotLightShaderVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/spotlight_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        Shader.VertexShader = g_rhi->CreateShader(shaderInfo);
    }

    ScreenSize.x = (float)InRTWidth;
    ScreenSize.y = (float)InRTHeight;
}

void jSpotLightDrawCommandGenerator::GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex)
{
    jSpotLight* spotLight = (jSpotLight*)InLightView.Light;
    const jSpotLightUniformBufferData& LightData = spotLight->GetLightData();

    const auto lightDir = -LightData.Direction;
    const auto directionToRot = lightDir.GetEulerAngleFrom();
    const auto spotLightPos = LightData.Position + lightDir * (-LightData.MaxDistance / 2.0f);
    const auto umbraRadius = tanf(LightData.UmbraRadian) * LightData.MaxDistance;
    Matrix WorldMat = Matrix::MakeTranslate(spotLightPos) * Matrix::MakeRotate(directionToRot) * Matrix::MakeScale(Vector(umbraRadius, LightData.MaxDistance, umbraRadius));

    jPushConstant* PushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(
        jSpotLightPushConstant(InView->Camera->Projection * InView->Camera->View * WorldMat), EShaderAccessStageFlag::ALL);

    jShaderSpotLightPixelShader::ShaderPermutation ShaderPermutation;
    ShaderPermutation.SetIndex<jShaderSpotLightPixelShader::USE_SUBPASS>(gOptions.UseSubpass);
    ShaderPermutation.SetIndex<jShaderSpotLightPixelShader::USE_SHADOW_MAP>(InLightView.ShadowMapPtr ? 1 : 0);
    ShaderPermutation.SetIndex<jShaderSpotLightPixelShader::USE_REVERSEZ>(USE_REVERSEZ_PERSPECTIVE_SHADOW);
    Shader.PixelShader = jShaderSpotLightPixelShader::CreateShader(ShaderPermutation);

    jShaderBindingInstanceArray CopyShaderBindingInstances = ShaderBindingInstances;
    CopyShaderBindingInstances.Add(InLightView.ShaderBindingInstance);

    //////////////////////////////////////////////////////////////////////////
    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

    auto SpotLightPushConstant = g_rhi->CreateUniformBufferBlock(
        jNameStatic("jSpotLightPushConstant"), jLifeTimeType::OneFrame, sizeof(jSpotLightPushConstant));
    auto PushConstantData = jSpotLightPushConstant(InView->Camera->Projection * InView->Camera->View * WorldMat);
    SpotLightPushConstant->UpdateBufferData(&PushConstantData, sizeof(jSpotLightPushConstant));

    // todo : 인라인 아닌것도 지원해야 됨
    ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_GRAPHICS
        , ResourceInlineAllactor.Alloc<jUniformBufferResource>(SpotLightPushConstant), true);

    CopyShaderBindingInstances.Add(g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame));
    //////////////////////////////////////////////////////////////////////////

    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, SpotLightCone->RenderObjects[0], InRenderPass
        , Shader, &PipelineStateFixedInfo, SpotLightCone->RenderObjects[0]->MaterialPtr.get(), CopyShaderBindingInstances, PushConstant, nullptr, InSubpassIndex);
    OutDestDrawCommand->Test = true;
    OutDestDrawCommand->PrepareToDraw(false);
}
