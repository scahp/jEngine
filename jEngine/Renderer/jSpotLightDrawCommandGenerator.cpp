﻿#include "pch.h"
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
    MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

    // SpotLight 의 경우 카메라에서 가장 먼쪽에 있는 Mesh 면을 렌더링하고, 그 면 부터 카메라 사이에 있는 공간에 대해서만 라이트를 적용함.
    RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false>::Create();
    DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create();

    PipelineStateFixedInfo = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
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
    
    const auto lightDir = -spotLight->LightData.Direction;
    const auto directionToRot = lightDir.GetEulerAngleFrom();
    const auto spotLightPos = spotLight->LightData.Position + lightDir * (-spotLight->LightData.MaxDistance / 2.0f);
    const auto umbraRadius = tanf(spotLight->LightData.UmbraRadian) * spotLight->LightData.MaxDistance;
    Matrix WorldMat = Matrix::MakeTranslate(spotLightPos) * Matrix::MakeRotate(directionToRot) * Matrix::MakeScale(Vector(umbraRadius, spotLight->LightData.MaxDistance, umbraRadius));

    jPushConstant* PushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(
        jSpotLightPushConstant(InView->Camera->Projection * InView->Camera->View * WorldMat), EShaderAccessStageFlag::ALL);

    jShaderSpotLight::ShaderPermutation ShaderPermutation;
    ShaderPermutation.SetIndex<jShaderSpotLight::USE_SUBPASS>(gOptions.UseSubpass);
    ShaderPermutation.SetIndex<jShaderSpotLight::USE_SHADOW_MAP>(InLightView.ShadowMapPtr ? 1 : 0);
    Shader.PixelShader = jShaderSpotLight::CreateShader(ShaderPermutation);

    check(OutDestDrawCommand);
    new (OutDestDrawCommand) jDrawCommand(InRenderFrameContextPtr, &InLightView, SpotLightCone->RenderObjects[0], InRenderPass
        , Shader, &PipelineStateFixedInfo, ShaderBindingInstances, PushConstant, InSubpassIndex);
    OutDestDrawCommand->PrepareToDraw(false);
}