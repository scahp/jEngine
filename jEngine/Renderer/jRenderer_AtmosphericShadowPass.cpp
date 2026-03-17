#include "pch.h"
#include "jRenderer.h"
#include "Scene/Light/jLight.h"
#include "Scene/jCamera.h"
#include "jSceneRenderTargets.h"
#include "jDrawCommand.h"
#include "jPrimitiveUtil.h"
#include "Profiler/jPerformanceProfile.h"
#include "Scene/Light/jDirectionalLight.h"
#include "jOptions.h"
#include "Scene/jRenderObject.h"
#include "Shader/jShaderParameterSet.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jAtmosphericShadowingApplyData)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Intensity)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding1)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding2)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jAtmosphericShadowingApplyPSParameters)
    SHADER_TEXTURE2D(Texture)
    SHADER_UNIFORM_BUFFER(jAtmosphericShadowingApplyData, ApplyParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jAtmosphericData)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, ShadowVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, VP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, CameraPos)
    SHADER_UNIFORM_BUFFER_MEMBER(float, CameraNear)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, LightCameraDirection)
    SHADER_UNIFORM_BUFFER_MEMBER(float, CameraFar)
    SHADER_UNIFORM_BUFFER_MEMBER(float, AnisoG)
    SHADER_UNIFORM_BUFFER_MEMBER(float, SlopeOfDist)
    SHADER_UNIFORM_BUFFER_MEMBER(float, InScatteringLambda)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Dummy)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, TravelCount)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, RTWidth)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, RTHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, UseNoise)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jAtmosphericShadowingCSParameters)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_TEXTURE2D(ShadowMapTexture)
    SHADER_UNIFORM_BUFFER(jAtmosphericData, AtmosphericParam)
    SHADER_RW_TEXTURE2D(Result)
END_SHADER_PARAMETER_SET()

namespace
{
struct jShaderAtmosphericShadowingComputeShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jAtmosphericShadowingCSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderAtmosphericShadowingComputeShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderAtmosphericShadowingComputeShader
    , "AtmosphericShadowingCS"
    , "Resource/Shaders/hlsl/AtmosphericShadowing_cs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::COMPUTE)

struct jShaderAtmosphericShadowingApplyPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jAtmosphericShadowingApplyPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderAtmosphericShadowingApplyPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderAtmosphericShadowingApplyPixelShader
    , "AtmosphericShadowingApplyPS"
    , "Resource/Shaders/hlsl/AtmosphericShadowingApply_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)
}

void jRenderer::AtmosphericShadow()
{
    if (gOptions.ShowAOOnly)
        return;

    if (!gOptions.UseAtmosphericShadowing)
        return;

    {
        jDirectionalLight* DirectionalLight = nullptr;
        for (auto light : jLight::GetLights())
        {
            if (light->Type == ELightType::DIRECTIONAL)
            {
                DirectionalLight = (jDirectionalLight*)light;
                break;
            }
        }
        if (!DirectionalLight)
            return;

        jCamera* MainCamera = jCamera::GetMainCamera();
        check(MainCamera);

        SCOPE_CPU_PROFILE(AtmosphericShadowing);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, AtmosphericShadowing);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "AtmosphericShadowing", Vector4(0.8f, 0.8f, 0.8f, 1.0f));

        std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;
        int32 Width = AtmosphericShadowing->Info.Width;
        int32 Height = AtmosphericShadowing->Info.Height;

        auto LightCamera = DirectionalLight->GetLightCamra();
        auto ShadowVP = DirectionalLight->GetLightData().ShadowVP;
        auto LightCameraDirection = LightCamera->GetForwardVector();

        jAtmosphericData AtmosphericData;
        AtmosphericData.ShadowVP = ShadowVP;
        AtmosphericData.VP = MainCamera->GetViewProjectionMatrix();
        AtmosphericData.InvVP = MainCamera->GetInverseViewProjectionMatrix();
        AtmosphericData.CameraPos = MainCamera->Pos;
        AtmosphericData.LightCameraDirection = LightCameraDirection;
        AtmosphericData.CameraFar = MainCamera->Far;
        AtmosphericData.CameraNear = MainCamera->Near;
        AtmosphericData.AnisoG = gOptions.AnisoG;
        AtmosphericData.SlopeOfDist = gOptions.AtmosphericShadowSlopeOfDist;
        AtmosphericData.InScatteringLambda = gOptions.AtmosphericShadowInScatteringLambda;
        AtmosphericData.TravelCount = gOptions.AtmosphericShadowTravelCount;
        AtmosphericData.RTWidth = Width;
        AtmosphericData.RTHeight = Height;
        AtmosphericData.UseNoise = gOptions.AtmosphericShadowUseNoise ? 1 : 0;

        auto ShadowMapTexture = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(DirectionalLight)->GetTexture();
        check(ShadowMapTexture);

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapTexture, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EResourceLayout::UAV);

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("AtmosphericDataUniformBuffer"), jLifeTimeType::OneFrame, sizeof(AtmosphericData)));
        OneFrameUniformBuffer->UpdateBufferData(&AtmosphericData, sizeof(AtmosphericData));

        jAtmosphericShadowingCSParameters Parameters;
        Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
        Parameters.ShadowMapTexture = { ShadowMapTexture, SamplerState };
        Parameters.AtmosphericParam.Buffer = OneFrameUniformBuffer;
        Parameters.Result = { AtmosphericShadowing->GetTexture() };

        auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
            Parameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

        jShader* Shader = jShaderAtmosphericShadowingComputeShader::CreateShader(jShaderAtmosphericShadowingComputeShader::ShaderPermutation());

        jShaderBindingInstanceGroup ShaderBindingGroup;
        ShaderBindingGroup.Add(CurrentBindingInstance);
        jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingGroup.GetLayoutArray(), {});

        computePipelineStateInfo->Bind(RenderFrameContextPtr);

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingGroup.GetCombiner(), 0);

        int32 X = (Width / 8) + ((Width % 8) ? 1 : 0);
        int32 Y = (Height / 8) + ((Height % 8) ? 1 : 0);
        g_rhi->DispatchCompute(RenderFrameContextPtr, X, Y, 1);

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    }

    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, AtmosphericShadowingApply);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "AtmosphericShadowingApply", Vector4(0.6f, 0.8f, 1.0f, 1.0f));

        std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;

        auto RT = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr;
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RT->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);

        jRasterizationStateInfo* RasterizationState = nullptr;
        switch (g_rhi->GetSelectedMSAASamples())
        {
        case EMSAASamples::COUNT_1:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_2:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_4:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
            break;
        case EMSAASamples::COUNT_8:
            RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
            break;
        default:
            check(0);
            break;
        }
        auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

        const int32 RTWidth = RT->Info.Width;
        const int32 RTHeight = RT->Info.Height;

        // Create fixed pipeline states
        jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
        const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

        jRenderPassInfo renderPassInfo;
        jAttachment color = {
            .RenderTargetPtr = RT,
            .LoadStoreOp = EAttachmentLoadStoreOp::LOAD_STORE,
            .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
            .RTClearValue = ClearColor,
            .InitialLayout = RT->GetLayout(),
            .FinalLayout = EResourceLayout::COLOR_ATTACHMENT
        };
        renderPassInfo.Attachments.push_back(color);

        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
        subpass.OutputColorAttachments.push_back(0);
        renderPassInfo.Subpasses.push_back(subpass);

        auto RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        jAtmosphericShadowingApplyData ApplyData = {};
        ApplyData.Intensity = gOptions.AtmosphericShadowApplyIntensity;

        auto ApplyUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("AtmosphericShadowingApplyDataUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ApplyData)));
        ApplyUniformBuffer->UpdateBufferData(&ApplyData, sizeof(ApplyData));

        jAtmosphericShadowingApplyPSParameters Parameters;
        Parameters.Texture = { AtmosphericShadowing->GetTexture(), SamplerState };
        Parameters.ApplyParam.Buffer = ApplyUniformBuffer;

        auto ShaderBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
            Parameters, EShaderAccessStageFlag::FRAGMENT, jShaderBindingInstanceType::SingleFrame);

        RenderFrameContextPtr->GetActiveCommandBuffer()->GetBarrierBatcher()->Flush(RenderFrameContextPtr->GetActiveCommandBuffer());
        jShaderBindingInstanceGroup ShaderBindingGroup;
        ShaderBindingGroup.Add(ShaderBindingInstance);

        jGraphicsPipelineShader Shader;
        {
            Shader.VertexShader = jShaderFullscreenQuadVertexShader::CreateShader(jShaderFullscreenQuadVertexShader::ShaderPermutation());
            Shader.PixelShader = jShaderAtmosphericShadowingApplyPixelShader::CreateShader(jShaderAtmosphericShadowingApplyPixelShader::ShaderPermutation());
        }

        if (!jSceneRenderTarget::GlobalFullscreenPrimitive)
            jSceneRenderTarget::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
        jDrawCommand DrawCommand(RenderFrameContextPtr, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
            , Shader, &PostProcessPassPipelineStateFixed, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingGroup, nullptr, nullptr, 0, EDrawCommandBindingMode::Manual);
        DrawCommand.PrepareToDraw(false);

        if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            DrawCommand.Draw();
            RenderPass->EndRenderPass();
        }
    }
}
