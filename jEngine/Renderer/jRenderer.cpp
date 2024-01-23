#include "pch.h"
#include "FileLoader/jImageFileLoader.h"
#include "Material/jMaterial.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jShaderBindingLayout.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "dxcapi.h"
#include "jDirectionalLightDrawCommandGenerator.h"
#include "jGame.h"
#include "jOptions.h"
#include "jPointLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "jSpotLightDrawCommandGenerator.h"

#define ASYNC_WITH_SETUP 0
#define PARALLELFOR_WITH_PASSSETUP 0

struct jSimplePushConstant
{
    Vector4 Color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

    bool ShowVRSArea = false;
    bool Padding[3];

    bool ShowGrid = true;
    bool Padding2[3];
};

void jRenderer::Setup()
{
    SCOPE_CPU_PROFILE(Setup);

    FrameIndex = g_rhi->GetCurrentFrameIndex();
    UseForwardRenderer = RenderFrameContextPtr->UseForwardRenderer;

    View.ShadowCasterLights.reserve(View.Lights.size());

    // View 별로 저장 할 수 있어야 함
    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];

        if (ViewLight.Light)
        {
            if (ViewLight.Light->IsShadowCaster)
            {
                ViewLight.ShadowMapPtr = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(ViewLight.Light);
            }

            ViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(ViewLight.ShadowMapPtr ? ViewLight.ShadowMapPtr->GetTexture() : nullptr);

            if (ViewLight.Light->IsShadowCaster)
            {
                jViewLight NewViewLight = ViewLight;
                NewViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(nullptr);
                View.ShadowCasterLights.push_back(NewViewLight);
            }
        }
    }

#if ASYNC_WITH_SETUP
    ShadowPassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupShadowPass, this);
#else
    jRenderer::SetupShadowPass();
    jRenderer::SetupBasePass();
#endif

#if SUPPORT_RAYTRACING
    if (g_rhi->RaytracingScene && g_rhi->RaytracingScene->ShouldUpdate())
    {
        SCOPE_CPU_PROFILE(UpdateTLAS);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, UpdateTLAS);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "UpdateTLAS", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

        auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();
        jRatracingInitializer InInitializer;
        InInitializer.CommandBuffer = CmdBuffer;
        InInitializer.RenderObjects = jObject::GetStaticRenderObject();
        g_rhi->RaytracingScene->CreateOrUpdateTLAS(InInitializer);
    }
#endif // SUPPORT_RAYTRACING
}

void jRenderer::SetupShadowPass()
{
    SCOPE_CPU_PROFILE(SetupShadowPass);

    ShadowDrawInfo.reserve(View.Lights.size());

    for (int32 i = 0; i < View.ShadowCasterLights.size(); ++i)
    {
        jViewLight& ViewLight = View.ShadowCasterLights[i];
        if (!ViewLight.Light->IsShadowCaster)
            continue;

        ShadowDrawInfo.push_back(jShadowDrawInfo());
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[ShadowDrawInfo.size() - 1];

        const bool IsUseReverseZShadow = USE_REVERSEZ_PERSPECTIVE_SHADOW && (ViewLight.Light->IsUseRevereZPerspective());

        // Prepare shadowpass pipeline
        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f
            , false, false, EMSAASamples::COUNT_1, true, 0.2f, false, false>::Create();
        auto DepthStencilState = IsUseReverseZShadow ? TDepthStencilStateInfo<true, true, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create()
            : TDepthStencilStateInfo<true, true, ECompareOp::LEQUAL, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE
            , EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

        const int32 RTWidth = ViewLight.ShadowMapPtr->Info.Width;
        const int32 RTHeight = ViewLight.ShadowMapPtr->Info.Height;

        jPipelineStateFixedInfo ShadpwPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), false);

        {
            const jRTClearValue ClearDepth = IsUseReverseZShadow ? jRTClearValue(0.0f, 0) : jRTClearValue(1.0f, 0);

            jAttachment depth = jAttachment(ViewLight.ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
                , ClearDepth, EResourceLayout::UNDEFINED, EResourceLayout::DEPTH_STENCIL_ATTACHMENT);

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            renderPassInfo.Attachments.push_back(depth);

            // Setup subpass of ShadowPass
            jSubpass subpass;
            subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
            subpass.OutputDepthAttachment = 0;
            renderPassInfo.Subpasses.push_back(subpass);

            ShadowPasses.ShadowMapRenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { RTWidth, RTHeight });
        }

        // Shadow 기준 View 를 생성, 현재 Light 가 가진 Shadow Camera 와 ViewLight를 설정해줌
        ShadowPasses.ViewLight = ViewLight;

        jGraphicsPipelineShader ShadowShader;
        {
            jShaderInfo shaderInfo;

            if (ViewLight.Light->IsOmnidirectional())
            {
                shaderInfo.SetName(jNameStatic("shadow_testVS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/omni_shadow_vs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                ShadowShader.VertexShader = g_rhi->CreateShader(shaderInfo);

                shaderInfo.SetName(jNameStatic("shadow_testPS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/omni_shadow_fs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                ShadowShader.PixelShader = g_rhi->CreateShader(shaderInfo);
            }
            else
            {
                if (ViewLight.Light->Type == ELightType::SPOT)
                {
                    shaderInfo.SetName(jNameStatic("shadow_testVS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/spot_shadow_vs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                    ShadowShader.VertexShader = g_rhi->CreateShader(shaderInfo);

                    shaderInfo.SetName(jNameStatic("shadow_testPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/spot_shadow_fs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    ShadowShader.PixelShader = g_rhi->CreateShader(shaderInfo);
                }
                else
                {
                    shaderInfo.SetName(jNameStatic("shadow_testVS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/shadow_vs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                    ShadowShader.VertexShader = g_rhi->CreateShader(shaderInfo);

                    shaderInfo.SetName(jNameStatic("shadow_testPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("resource/shaders/hlsl/shadow_fs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    ShadowShader.PixelShader = g_rhi->CreateShader(shaderInfo);
                }
            }
        }
        jGraphicsPipelineShader ShadowInstancingShader;
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("shadow_testVS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shadow_instancing_vs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            ShadowInstancingShader.VertexShader = g_rhi->CreateShader(shaderInfo);

            shaderInfo.SetName(jNameStatic("shadow_testGS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shadow_fs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            ShadowInstancingShader.PixelShader = g_rhi->CreateShader(shaderInfo);
        }

#if PARALLELFOR_WITH_PASSSETUP
        ShadowPasses.DrawCommands.resize(jObject::GetShadowCasterRenderObject().size());
        jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetShadowCasterRenderObject()
            , [&](size_t InIndex, jRenderObject* InRenderObject)
            {
                jMaterial* Material = nullptr;

                // todo : Masked material need to set Material for jDrawCommand
                // iter->MaterialPtr;

                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace.get() : nullptr);

                new (&ShadowPasses.DrawCommands[InIndex]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, InRenderObject, ShadowPasses.ShadowMapRenderPass
                    , (InRenderObject->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, Material, {}, nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[InIndex].PrepareToDraw(true);
            });
#else
        ShadowPasses.DrawCommands.resize(jObject::GetShadowCasterRenderObject().size());
        {
            int32 i = 0;
            for (auto iter : jObject::GetShadowCasterRenderObject())
            {
                jMaterial* Material = nullptr;

                // todo : Masked material need to set Material for jDrawCommand
                // iter->MaterialPtr;

                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace.get() : nullptr);

                new (&ShadowPasses.DrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, iter, ShadowPasses.ShadowMapRenderPass
                    , (iter->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, Material, {}, nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[i].PrepareToDraw(true);
                ++i;
            }
        }
#endif
    }
}

void jRenderer::SetupBasePass()
{
    SCOPE_CPU_PROFILE(SetupBasePass);

    // Prepare basepass pipeline
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
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo BasePassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    auto TranslucentBlendingState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jPipelineStateFixedInfo TranslucentPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, TranslucentBlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr, EAttachmentLoadStoreOp::CLEAR_STORE
        , EAttachmentLoadStoreOp::CLEAR_STORE, ClearDepth
        , EResourceLayout::UNDEFINED, EResourceLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , EResourceLayout::UNDEFINED, EResourceLayout::COLOR_ATTACHMENT, true);
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    if (!UseForwardRenderer)
    {
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
        {
            jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i], EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , EResourceLayout::UNDEFINED, EResourceLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);
        }
    }

    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    if (UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
            , EResourceLayout::UNDEFINED, EResourceLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);
    }

    const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
    renderPassInfo.Attachments.push_back(depth);

    const int32 ResolveAttachemntIndex = (int32)renderPassInfo.Attachments.size();
    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            renderPassInfo.Attachments.push_back(resolve);
    }

    //////////////////////////////////////////////////////////////////////////
    // Setup subpass of BasePass
    {
        // First subpass, Geometry pass
        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);

        if (UseForwardRenderer)
        {
            subpass.OutputColorAttachments.push_back(0);
        }
        else
        {
            const int32 GBufferCount = LightPassAttachmentIndex;
            for (int32 i = 0; i < GBufferCount; ++i)
            {
                subpass.OutputColorAttachments.push_back(i);
            }
        }

        subpass.OutputDepthAttachment = DepthAttachmentIndex;
        if (UseForwardRenderer)
        {
            if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
                subpass.OutputResolveAttachment = ResolveAttachemntIndex;
        }
        renderPassInfo.Subpasses.push_back(subpass);
    }
    if (!UseForwardRenderer && gOptions.UseSubpass)
    {
        // Second subpass, Lighting pass
        jSubpass subpass;
        subpass.Initialize(1, 2, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);

        const int32 GBufferCount = LightPassAttachmentIndex;
        for (int32 i = 0; i < GBufferCount; ++i)
        {
            subpass.InputAttachments.push_back(i);
        }

        subpass.OutputColorAttachments.push_back(LightPassAttachmentIndex);
        subpass.OutputDepthAttachment = DepthAttachmentIndex;

        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            subpass.OutputResolveAttachment = ResolveAttachemntIndex;

        renderPassInfo.Subpasses.push_back(subpass);
    }
    //////////////////////////////////////////////////////////////////////////
    BaseRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    auto GetOrCreateShaderFunc = [UseForwardRenderer = this->UseForwardRenderer](const jRenderObject* InRenderObject)
        {
            jGraphicsPipelineShader Shaders;
            jShaderInfo shaderInfo;

            if (InRenderObject->HasInstancing())
            {
                check(UseForwardRenderer);

                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("default_instancing_testVS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_instancing_vs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                Shaders.VertexShader = g_rhi->CreateShader(shaderInfo);

                shaderInfo.SetName(jNameStatic("default_instancing_testPS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                Shaders.PixelShader = g_rhi->CreateShader(shaderInfo);
                return Shaders;
            }

            if (UseForwardRenderer)
            {
                shaderInfo.SetName(jNameStatic("default_testVS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_vs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                Shaders.VertexShader = g_rhi->CreateShader(shaderInfo);

                jShaderForwardPixelShader::ShaderPermutation ShaderPermutation;
                ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
                ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_REVERSEZ>(USE_REVERSEZ_PERSPECTIVE_SHADOW);
                Shaders.PixelShader = jShaderForwardPixelShader::CreateShader(ShaderPermutation);
            }
            else
            {
                const bool IsUseSphericalMap = InRenderObject->MaterialPtr && InRenderObject->MaterialPtr->IsUseSphericalMap();
                const bool HasAlbedoTexture = InRenderObject->MaterialPtr && InRenderObject->MaterialPtr->HasAlbedoTexture();
                const bool IsUseSRGBAlbedoTexture = InRenderObject->MaterialPtr && InRenderObject->MaterialPtr->IsUseSRGBAlbedoTexture();
                const bool HasVertexColor = InRenderObject->GeometryDataPtr && InRenderObject->GeometryDataPtr->HasVertexColor();
                const bool HasVertexBiTangent = InRenderObject->GeometryDataPtr && InRenderObject->GeometryDataPtr->HasVertexBiTangent();

                jShaderGBufferVertexShader::ShaderPermutation ShaderPermutationVS;
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_COLOR>(HasVertexColor);
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_BITANGENT>(HasVertexBiTangent);
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_ALBEDO_TEXTURE>(HasAlbedoTexture);
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_SPHERICAL_MAP>(IsUseSphericalMap);
                Shaders.VertexShader = jShaderGBufferVertexShader::CreateShader(ShaderPermutationVS);

                jShaderGBufferPixelShader::ShaderPermutation ShaderPermutationPS;
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(HasVertexColor);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(HasAlbedoTexture);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_SRGB_ALBEDO_TEXTURE>(IsUseSRGBAlbedoTexture);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_PBR>(ENABLE_PBR);
                Shaders.PixelShader = jShaderGBufferPixelShader::CreateShader(ShaderPermutationPS);
            }
            return Shaders;
        };

    jShaderInfo shaderInfo;
    jGraphicsPipelineShader TranslucentPassShader;
    {
        shaderInfo.SetName(jNameStatic("default_testVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gbuffer_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        TranslucentPassShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        jShaderGBufferPixelShader::ShaderPermutation ShaderPermutation;
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(0);
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(1);
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
        TranslucentPassShader.PixelShader = jShaderGBufferPixelShader::CreateShader(ShaderPermutation);
    }

    jSimplePushConstant SimplePushConstantData;
    SimplePushConstantData.ShowVRSArea = gOptions.ShowVRSArea;
    SimplePushConstantData.ShowGrid = gOptions.ShowGrid;

    jPushConstant* SimplePushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(SimplePushConstantData, EShaderAccessStageFlag::FRAGMENT);

#if PARALLELFOR_WITH_PASSSETUP
    BasePasses.resize(jObject::GetStaticRenderObject().size());
    jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetStaticRenderObject()
        , [&](size_t InIndex, jRenderObject* InRenderObject)
        {
            jMaterial* Material = nullptr;
            if (InRenderObject->MaterialPtr)
            {
                Material = InRenderObject->MaterialPtr.get();
            }
            else
            {
                if (GDefaultMaterial)
                {
                    Material = GDefaultMaterial.get();
                }
            }

            new (&BasePasses[InIndex]) jDrawCommand(RenderFrameContextPtr, &View, InRenderObject, BaseRenderPass
                , GetOrCreateShaderFunc(InRenderObject), &BasePassPipelineStateFixed, Material, {}, SimplePushConstant);
            BasePasses[InIndex].PrepareToDraw(false);
        });
#else
    BasePasses.resize(jObject::GetStaticRenderObject().size());
    int32 i = 0;
    for (auto iter : jObject::GetStaticRenderObject())
    {
        jMaterial* Material = nullptr;
        if (iter->MaterialPtr)
        {
            Material = iter->MaterialPtr.get();
        }
        else
        {
            if (GDefaultMaterial)
            {
                Material = GDefaultMaterial.get();
            }
        }

        new (&BasePasses[i]) jDrawCommand(RenderFrameContextPtr, &View, iter, BaseRenderPass
            , GetOrCreateShaderFunc(iter), &BasePassPipelineStateFixed, Material, {}, SimplePushConstant);
        BasePasses[i].PrepareToDraw(false);
        ++i;
    }
#endif
}

void jRenderer::ShadowPass()
{
#if ASYNC_WITH_SETUP
    if (ShadowPassSetupCompleteEvent.valid())
        ShadowPassSetupCompleteEvent.wait();

    BasePassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupBasePass, this);
#endif

    SCOPE_CPU_PROFILE(ShadowPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, ShadowPass);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "ShadowPass", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

    for (int32 i = 0; i < ShadowDrawInfo.size(); ++i)
    {
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[i];

        const char* ShadowPassEventName = nullptr;
        switch (ShadowPasses.ViewLight.Light->Type)
        {
        case ELightType::DIRECTIONAL:
            ShadowPassEventName = "DirectionalLight";
            break;
        case ELightType::POINT:
            ShadowPassEventName = "PointLight";
            break;
        case ELightType::SPOT:
            ShadowPassEventName = "SpotLight";
            break;
        }

        DEBUG_EVENT(RenderFrameContextPtr, ShadowPassEventName);

        const std::shared_ptr<jRenderTarget>& ShadowMapPtr = ShadowPasses.GetShadowMapPtr();

        {
            auto NewLayout = ShadowMapPtr->GetTexture()->IsDepthOnlyFormat() ? EResourceLayout::DEPTH_ATTACHMENT : EResourceLayout::DEPTH_STENCIL_ATTACHMENT;
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), NewLayout);
        }

        if (ShadowPasses.ShadowMapRenderPass && ShadowPasses.ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            //ShadowpassOcclusionTest.BeginQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
            for (const auto& command : ShadowPasses.DrawCommands)
            {
                command.Draw();
            }
            //ShadowpassOcclusionTest.EndQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
            ShadowPasses.ShadowMapRenderPass->EndRenderPass();
        }

        {
            auto NewLayout = ShadowMapPtr->GetTexture()->IsDepthOnlyFormat() ? EResourceLayout::DEPTH_READ_ONLY : EResourceLayout::DEPTH_STENCIL_READ_ONLY;
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), NewLayout);
        }
    }
}

void jRenderer::BasePass()
{
    if (BasePassSetupCompleteEvent.valid())
        BasePassSetupCompleteEvent.wait();

    {
        SCOPE_CPU_PROFILE(BasePass);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BasePass);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "BasePass", Vector4(0.8f, 0.8f, 0.0f, 1.0f));

        if (UseForwardRenderer)
        {
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        }
        else
        {
            for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
            {
                g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i]->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
            }
        }

        {
            auto NewLayout = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture()->IsDepthOnlyFormat() ? EResourceLayout::DEPTH_ATTACHMENT : EResourceLayout::DEPTH_STENCIL_ATTACHMENT;
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), NewLayout);
        }

        //BasepassOcclusionTest.BeginQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
        if (BaseRenderPass && BaseRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            // Draw G-Buffer : subpass 0 
            for (const auto& command : BasePasses)
            {
                command.Draw();
            }

            // Draw Light : subpass 1
            if (!UseForwardRenderer && gOptions.UseSubpass)
            {
                g_rhi->NextSubpass(RenderFrameContextPtr->GetActiveCommandBuffer());
                DeferredLightPass_TodoRefactoring(BaseRenderPass);
            }

            BaseRenderPass->EndRenderPass();
        }

        if (!UseForwardRenderer && !gOptions.UseSubpass)
        {
            DeferredLightPass_TodoRefactoring(BaseRenderPass);
        }
        //BasepassOcclusionTest.EndQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
    }

    static bool EnableAtmosphericShadowing = true;
    if (EnableAtmosphericShadowing)
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
        check(DirectionalLight);

        jCamera* MainCamera = jCamera::GetMainCamera();
        check(MainCamera);

        SCOPE_CPU_PROFILE(AtmosphericShadowing);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, AtmosphericShadowing);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "AtmosphericShadowing", Vector4(0.8f, 0.8f, 0.8f, 1.0f));

        std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;
        int32 Width = AtmosphericShadowing->Info.Width;
        int32 Height = AtmosphericShadowing->Info.Height;

        struct jAtmosphericData
        {
            Matrix ShadowVP;
            Matrix VP;
            Vector CameraPos;
            float CameraNear;
            Vector LightCameraDirection;
            float CameraFar;
            float AnisoG;
            float SlopeOfDist;
            float InScatteringLambda;
            float Dummy;
            int TravelCount;
            int RTWidth;
            int RTHeight;
            int UseNoise;  // todo : change to #define USE_NOISE
        };

        auto LightCamera = DirectionalLight->GetLightCamra();
        auto ShadowVP = DirectionalLight->GetLightData().ShadowVP;
        auto LightCameraDirection = LightCamera->GetForwardVector();

        jAtmosphericData AtmosphericData;
        AtmosphericData.ShadowVP = ShadowVP;
        AtmosphericData.VP = MainCamera->GetViewProjectionMatrix();
        AtmosphericData.CameraPos = MainCamera->Pos;
        AtmosphericData.LightCameraDirection = LightCameraDirection;
        AtmosphericData.CameraFar = MainCamera->Far;
        AtmosphericData.CameraNear = MainCamera->Near;
        AtmosphericData.AnisoG = gOptions.AnisoG;
        AtmosphericData.SlopeOfDist = 0.25f;
        AtmosphericData.InScatteringLambda = 0.001f;
        AtmosphericData.TravelCount = 64;
        AtmosphericData.RTWidth = Width;
        AtmosphericData.RTHeight = Height;
        AtmosphericData.UseNoise = true;

        std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        auto ShadowMapTexture = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(DirectionalLight)->GetTexture();
        check(ShadowMapTexture);

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapTexture, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EResourceLayout::UAV);

        // Binding 0
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), nullptr)));
        }

        // Binding 1
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr)));
        }

        // Binding 2
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(ShadowMapTexture, nullptr)));
        }

        // Binding 3        
        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("AtmosphericDataUniformBuffer"), jLifeTimeType::OneFrame, sizeof(AtmosphericData)));
        OneFrameUniformBuffer->UpdateBufferData(&AtmosphericData, sizeof(AtmosphericData));
        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
            , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

        // Binding 4
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericShadowing->GetTexture(), nullptr)));
        }

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("GenIrradianceMap"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowing_cs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        static jShader* Shader = g_rhi->CreateShader(shaderInfo);

        jShaderBindingLayoutArray ShaderBindingLayoutArray;
        ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

        jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

        computePipelineStateInfo->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray ShaderBindingInstanceArray;
        ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

        jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
        for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
        {
            // Add ShaderBindingInstanceCombiner data : DescriptorSets, DynamicOffsets
            ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
            const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
            if (pDynamicOffsetTest && pDynamicOffsetTest->size())
            {
                ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
            }
        }
        ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);

        int32 X = (Width / 16) + ((Width % 16) ? 1 : 0);
        int32 Y = (Height / 16) + ((Height % 16) ? 1 : 0);
        g_rhi->DispatchCompute(RenderFrameContextPtr, X, Y, 1);

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    }

    if (EnableAtmosphericShadowing)
    {
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
        jAttachment color = jAttachment(RT, EAttachmentLoadStoreOp::LOAD_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, EResourceLayout::UNDEFINED, EResourceLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);

        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
        subpass.OutputColorAttachments.push_back(0);
        renderPassInfo.Subpasses.push_back(subpass);

        auto RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
            , ResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericShadowing->GetTexture(), SamplerState)));

        std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
        jShaderBindingInstanceArray ShaderBindingInstanceArray;
        ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());

        jGraphicsPipelineShader Shader;
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyVS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
            Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

            shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyPS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowingApply_ps.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
        }

        if (!jSceneRenderTarget::GlobalFullscreenPrimitive)
            jSceneRenderTarget::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
        jDrawCommand DrawCommand(RenderFrameContextPtr, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
            , Shader, &PostProcessPassPipelineStateFixed, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
        DrawCommand.Test = true;
        DrawCommand.PrepareToDraw(false);

        if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            DrawCommand.Draw();
            RenderPass->EndRenderPass();
        }
    }
}

void jRenderer::AOPass()
{
#if SUPPORT_RAYTRACING
    // RTAO
    // 해야 할 것
    // 1. RTAO Shader 작성, GBuffer 로 부터 Ray 발사.
    // 2. Geometry 인지 SkySpherer 인지 구분하여 Geometry 에서만 Ray 발사.
    // 3. SceneConstantBuffer 와 같은 데이터들 필요한 것 만 남기기
    // 4. Denoiser 를 구현하고 별도의 함수로 분리하여 다른 패스에서도 재활용 가능하게 함.
    //  - Denoiser 는 bilateral filter 고려중.
    {
        auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();
        const jSamplerStateInfo* PBRSamplerStateInfo = TSamplerStateInfo<ETextureFilter::NEAREST_MIPMAP_LINEAR, ETextureFilter::NEAREST_MIPMAP_LINEAR
            , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

        struct SceneConstantBuffer
        {
            Matrix projectionToWorld;
            Vector cameraPosition;
            float focalDistance;
            Vector lightPosition;
            float lensRadius;
            Vector lightAmbientColor;
            uint32 NumOfStartingRay;
            Vector lightDiffuseColor;
            float Padding0;                 // for 16 byte align
            Vector cameraDirection;
            float Padding1;                 // for 16 byte align
            Vector lightDirection;
            float Padding2;                 // for 16 byte align
        };

        SceneConstantBuffer m_sceneCB;

        auto mainCamera = jCamera::GetMainCamera();
        m_sceneCB.cameraPosition = mainCamera->Pos;
        m_sceneCB.projectionToWorld = mainCamera->GetViewProjectionMatrix().GetInverse();
        m_sceneCB.lightPosition = Vector(0.0f, 1.8f, -3.0f);
        m_sceneCB.lightAmbientColor = Vector(0.5f, 0.5f, 0.5f);
        m_sceneCB.lightDiffuseColor = Vector(0.5f, 0.3f, 0.3f);
        m_sceneCB.cameraDirection = (mainCamera->Target - mainCamera->Pos).GetNormalize();
        m_sceneCB.focalDistance = gOptions.FocalDistance;
        m_sceneCB.lensRadius = gOptions.LensRadius;
        m_sceneCB.NumOfStartingRay = 20;

        jDirectionalLight* DirectionalLight = nullptr;
        for (auto light : jLight::GetLights())
        {
            if (light->Type == ELightType::DIRECTIONAL)
            {
                DirectionalLight = (jDirectionalLight*)light;
                break;
            }
        }
        check(DirectionalLight);
        m_sceneCB.lightDirection = { DirectionalLight->GetLightData().Direction.x, DirectionalLight->GetLightData().Direction.y, DirectionalLight->GetLightData().Direction.z };

        auto SceneUniformBufferPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("SceneData"), jLifeTimeType::OneFrame, sizeof(m_sceneCB));
        SceneUniformBufferPtr->UpdateBufferData(&m_sceneCB, sizeof(m_sceneCB));

        if (!RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr)
        {
            RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)SCR_WIDTH, (uint32)SCR_HEIGHT, (uint32)1, (uint32)1
                , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
        }

        // Normal resource
        // Record normal resources
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
        ShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jBufferResource>(RenderFrameContextPtr->RaytracingScene->TLASBufferPtr.get()), true));
        ShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), nullptr), false));
        ShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jUniformBufferResource>(SceneUniformBufferPtr.get()), true));
        ShaderBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jSamplerResource>(SamplerState), false));
        ShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jSamplerResource>(PBRSamplerStateInfo), false));

#define TURN_ON_BINDLESS 1
#define TURN_ON_SHADOW 0

#if TURN_ON_BINDLESS
        // Bindless resources
        // Record bindless resources
        jShaderBindingArray BindlessShaderBindingArray[9];

        std::vector<jTextureResourceBindless::jTextureBindData> IrradianceMapTextures;
        jTextureResourceBindless::jTextureBindData IrradianceTextureBindData;
        IrradianceTextureBindData.Texture = jSceneRenderTarget::IrradianceMap2;
        IrradianceMapTextures.push_back(IrradianceTextureBindData);

        std::vector<jTextureResourceBindless::jTextureBindData> FilteredEnvMapTextures;
        jTextureResourceBindless::jTextureBindData FilteredEnvMapBindData;
        FilteredEnvMapBindData.Texture = jSceneRenderTarget::FilteredEnvMap2;
        FilteredEnvMapTextures.push_back(FilteredEnvMapBindData);

        // Below Two textures have only 1 texture each, but this is just test code for bindless resouces so I will remove this from bindless resource range.
        BindlessShaderBindingArray[0].Add(jShaderBinding::CreateBindless(0, (uint32)IrradianceMapTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jTextureResourceBindless>(IrradianceMapTextures), false));
        BindlessShaderBindingArray[1].Add(jShaderBinding::CreateBindless(0, (uint32)FilteredEnvMapTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jTextureResourceBindless>(FilteredEnvMapTextures), false));

        std::vector<const jBuffer*> VertexAndInexOffsetBuffers;
        std::vector<const jBuffer*> IndexBuffers;
        std::vector<const jBuffer*> TestUniformBuffers;
        std::vector<const jBuffer*> VertexBuffers;
        std::vector<jTextureResourceBindless::jTextureBindData> AlbedoTextures;
        std::vector<jTextureResourceBindless::jTextureBindData> NormalTextures;
        std::vector<jTextureResourceBindless::jTextureBindData> MetallicTextures;

        for (int32 i = 0; i < jObject::GetStaticRenderObject().size(); ++i)
        {
            jRenderObject* RObj = jObject::GetStaticRenderObject()[i];
            RObj->CreateShaderBindingInstance();

            VertexAndInexOffsetBuffers.push_back(RObj->VertexAndIndexOffsetBuffer.get());
            IndexBuffers.push_back(RObj->GeometryDataPtr->IndexBufferPtr->GetBuffer());
            TestUniformBuffers.push_back(RObj->TestUniformBuffer.get());
            VertexBuffers.push_back(RObj->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

            check(RObj->MaterialPtr);
            AlbedoTextures.push_back(jTextureResourceBindless::jTextureBindData(RObj->MaterialPtr->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Albedo), nullptr));
            NormalTextures.push_back(jTextureResourceBindless::jTextureBindData(RObj->MaterialPtr->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Normal), nullptr));
            MetallicTextures.push_back(jTextureResourceBindless::jTextureBindData(RObj->MaterialPtr->GetTexture<jTexture>(jMaterial::EMaterialTextureType::Metallic), nullptr));
        }
        BindlessShaderBindingArray[2].Add(jShaderBinding::CreateBindless(0, (uint32)VertexAndInexOffsetBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jBufferResourceBindless>(VertexAndInexOffsetBuffers), false));
        BindlessShaderBindingArray[3].Add(jShaderBinding::CreateBindless(0, (uint32)IndexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jBufferResourceBindless>(IndexBuffers), false));
        BindlessShaderBindingArray[4].Add(jShaderBinding::CreateBindless(0, (uint32)TestUniformBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jBufferResourceBindless>(TestUniformBuffers), false));
        BindlessShaderBindingArray[5].Add(jShaderBinding::CreateBindless(0, (uint32)VertexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jBufferResourceBindless>(VertexBuffers), false));
        BindlessShaderBindingArray[6].Add(jShaderBinding::CreateBindless(0, (uint32)AlbedoTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jTextureResourceBindless>(AlbedoTextures)));
        BindlessShaderBindingArray[7].Add(jShaderBinding::CreateBindless(0, (uint32)NormalTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jTextureResourceBindless>(NormalTextures)));
        BindlessShaderBindingArray[8].Add(jShaderBinding::CreateBindless(0, (uint32)MetallicTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
            ResourceInlineAllactor.Alloc<jTextureResourceBindless>(MetallicTextures)));
#endif // TURN_ON_BINDLESS

        // Create ShaderBindingLayout and ShaderBindingInstance Instance for this draw call
        std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance;
        GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
#if TURN_ON_BINDLESS
        std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstanceBindless[9];
        for (int32 i = 0; i < 9; ++i)
        {
            GlobalShaderBindingInstanceBindless[i] = g_rhi->CreateShaderBindingInstance(BindlessShaderBindingArray[i], jShaderBindingInstanceType::SingleFrame);
        }
#endif // TURN_ON_BINDLESS

        jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
        GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);
#if TURN_ON_BINDLESS
        for (int32 i = 0; i < 9; ++i)
        {
            GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstanceBindless[i]->ShaderBindingsLayouts);
        }
#endif // TURN_ON_BINDLESS

        // Create RaytracingShaders
        std::vector<jRaytracingPipelineShader> RaytracingShaders;
        {
            jRaytracingPipelineShader NewShader;
            jShaderInfo shaderInfo;

            // First hit gorup
            shaderInfo.SetName(jNameStatic("Miss"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
            shaderInfo.SetEntryPoint(jNameStatic("MissShader"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_MISS);
#if TURN_ON_BINDLESS
            shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
#endif // TURN_ON_BINDLESS
            NewShader.MissShader = g_rhi->CreateShader(shaderInfo);
            NewShader.MissEntryPoint = TEXT("MyMissShader");

            shaderInfo.SetName(jNameStatic("Raygen"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
            shaderInfo.SetEntryPoint(jNameStatic("RaygenShader"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_RAYGEN);
#if TURN_ON_BINDLESS
            shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
#endif
            NewShader.RaygenShader = g_rhi->CreateShader(shaderInfo);
            NewShader.RaygenEntryPoint = TEXT("MyRaygenShader");

            shaderInfo.SetName(jNameStatic("ClosestHit"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
            shaderInfo.SetEntryPoint(jNameStatic("ClosestHitShader"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT);
#if TURN_ON_BINDLESS
            shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
#endif // TURN_ON_BINDLESS
            NewShader.ClosestHitShader = g_rhi->CreateShader(shaderInfo);
            NewShader.ClosestHitEntryPoint = TEXT("MyClosestHitShader");

            shaderInfo.SetName(jNameStatic("AnyHit"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RTAO.hlsl"));
            shaderInfo.SetEntryPoint(jNameStatic("AnyHitShader"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_ANYHIT);
#if TURN_ON_BINDLESS
            shaderInfo.SetPreProcessors(jNameStatic("#define USE_BINDLESS_RESOURCE 1"));
#endif // TURN_ON_BINDLESS
            NewShader.AnyHitShader = g_rhi->CreateShader(shaderInfo);
            NewShader.AnyHitEntryPoint = TEXT("MyAnyHitShader");

            NewShader.HitGroupName = TEXT("DefaultHit");

            RaytracingShaders.push_back(NewShader);
        }

        // Create RaytracingPipelineState
        jRaytracingPipelineData RaytracingPipelineData;
        RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);	                // float2 barycentrics
        RaytracingPipelineData.MaxPayloadSize = 4 * sizeof(float) + sizeof(int32);		// float4 color
        RaytracingPipelineData.MaxTraceRecursionDepth = 10;
        auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData
            , GlobalShaderBindingLayoutArray, nullptr);

        // Binding RaytracingShader resources
        jShaderBindingInstanceArray ShaderBindingInstanceArray;
        ShaderBindingInstanceArray.Add(GlobalShaderBindingInstance.get());
#if TURN_ON_BINDLESS
        for (int32 i = 0; i < _countof(GlobalShaderBindingInstanceBindless); ++i)
        {
            ShaderBindingInstanceArray.Add(GlobalShaderBindingInstanceBindless[i].get());
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

        // Dispatch Rays
        jRaytracingDispatchData TracingData;
        TracingData.Width = SCR_WIDTH;
        TracingData.Height = SCR_HEIGHT;
        TracingData.Depth = 1;
        TracingData.PipelineState = RaytracingPipelineState;
        g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

        g_rhi->TransitionLayout(CmdBuffer, RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get(), EResourceLayout::SHADER_READ_ONLY);
    }

#else // SUPPORT_RAYTRACING
    // SSAO
#endif // SUPPORT_RAYTRACING
}

void jRenderer::DenoisePass()
{

}

void jRenderer::DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass)
{
    SCOPE_CPU_PROFILE(LightingPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, LightingPass);
    DEBUG_EVENT(RenderFrameContextPtr, "LightingPass");

    if (!gOptions.UseSubpass)
    {
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::DEPTH_STENCIL_READ_ONLY);
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
        {
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // GBuffer Input attachment 추가
    std::shared_ptr<jShaderBindingInstance> GBufferShaderBindingInstance
        = RenderFrameContextPtr->SceneRenderTargetPtr->PrepareGBufferShaderBindingInstance(gOptions.UseSubpass);

    jShaderBindingInstanceArray DefaultLightPassShaderBindingInstances;
    DefaultLightPassShaderBindingInstances.Add(GBufferShaderBindingInstance.get());
    DefaultLightPassShaderBindingInstances.Add(View.ViewUniformBufferShaderBindingInstance.get());
    //////////////////////////////////////////////////////////////////////////

    std::vector<jDrawCommand> LightPasses;
    LightPasses.resize(View.Lights.size());

    const int32 RTWidth = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Width;
    const int32 RTHeight = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Height;
    jDirectionalLightDrawCommandGenerator DirectionalLightPass(DefaultLightPassShaderBindingInstances);
    DirectionalLightPass.Initialize(RTWidth, RTHeight);

    jPointLightDrawCommandGenerator PointLightPass(DefaultLightPassShaderBindingInstances);
    PointLightPass.Initialize(RTWidth, RTHeight);

    jSpotLightDrawCommandGenerator SpotLightPass(DefaultLightPassShaderBindingInstances);
    SpotLightPass.Initialize(RTWidth, RTHeight);

    jDrawCommandGenerator* LightDrawCommandGenerator[(int32)ELightType::MAX] = { 0, };
    LightDrawCommandGenerator[(int32)ELightType::DIRECTIONAL] = &DirectionalLightPass;
    LightDrawCommandGenerator[(int32)ELightType::POINT] = &PointLightPass;
    LightDrawCommandGenerator[(int32)ELightType::SPOT] = &SpotLightPass;

    {
        const int32 SubpassIndex = (!UseForwardRenderer && gOptions.UseSubpass) ? 1 : 0;

        if (!gOptions.UseSubpass)
        {
            const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
            const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

            jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr, EAttachmentLoadStoreOp::LOAD_DONTCARE
                , EAttachmentLoadStoreOp::LOAD_STORE, ClearDepth
                , RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(), EResourceLayout::DEPTH_STENCIL_READ_ONLY);

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetLayout(), EResourceLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);

            const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
            renderPassInfo.Attachments.push_back(depth);

            // Setup subpass of LightingPass
            jSubpass subpass;
            subpass.SourceSubpassIndex = 0;
            subpass.DestSubpassIndex = 1;

            for (int32 i = 0; i < DepthAttachmentIndex; ++i)
            {
                subpass.OutputColorAttachments.push_back(i);
            }

            subpass.OutputDepthAttachment = DepthAttachmentIndex;
            subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
            renderPassInfo.Subpasses.push_back(subpass);

            InRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

            check(InRenderPass);
            InRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer());
        }

        check(View.Lights.size() == LightPasses.size());
        for (int32 i = 0; i < (int32)LightPasses.size(); ++i)
        {
            LightDrawCommandGenerator[(int32)View.Lights[i].Light->Type]->GenerateDrawCommand(&LightPasses[i], RenderFrameContextPtr, &View, View.Lights[i], InRenderPass, SubpassIndex);
        }

        for (int32 i = 0; i < (int32)LightPasses.size(); ++i)
        {
            LightPasses[i].Draw();
        }

        if (!gOptions.UseSubpass)
        {
            check(InRenderPass);
            InRenderPass->EndRenderPass();
        }
    }
}

void jRenderer::PostProcess()
{
    auto AddFullQuadPass = [&](const char* InDebugName, const std::vector<jTexture*> InShaderInputs, const std::shared_ptr<jRenderTarget>& InRenderTargetPtr
        , jName VertexShader, jName PixelShader, bool IsBloom = false, Vector InBloomTintA = Vector::ZeroVector, Vector InBloomTintB = Vector::ZeroVector)
        {
            DEBUG_EVENT(RenderFrameContextPtr, InDebugName);

            if (!jSceneRenderTarget::GlobalFullscreenPrimitive)
                jSceneRenderTarget::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

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
            auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

            const int32 RTWidth = InRenderTargetPtr->Info.Width;
            const int32 RTHeight = InRenderTargetPtr->Info.Height;

            // Create fixed pipeline states
            jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
                , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

            const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
            const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

            jRenderPassInfo renderPassInfo;
            jAttachment color = jAttachment(InRenderTargetPtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, EResourceLayout::UNDEFINED, EResourceLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);

            // Setup subpass of LightingPass
            jSubpass subpass;
            subpass.SourceSubpassIndex = 0;
            subpass.DestSubpassIndex = 1;
            subpass.OutputColorAttachments.push_back(0);
            subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
            renderPassInfo.Subpasses.push_back(subpass);

            // Create RenderPass
            jRenderPass* RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { RTWidth, RTHeight });

            int32 BindingPoint = 0;
            jShaderBindingArray ShaderBindingArray;
            jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
            jShaderBindingInstanceArray ShaderBindingInstanceArray;

            struct jBloomUniformBuffer
            {
                Vector4 BufferSizeAndInvSize;
                Vector4 TintA;
                Vector4 TintB;
                float BloomIntensity;
            };
            jBloomUniformBuffer ubo;
            ubo.BufferSizeAndInvSize.x = (float)RTWidth;
            ubo.BufferSizeAndInvSize.y = (float)RTHeight;
            ubo.BufferSizeAndInvSize.z = 1.0f / (float)RTWidth;
            ubo.BufferSizeAndInvSize.w = 1.0f / (float)RTHeight;
            ubo.TintA = Vector4(InBloomTintA, 0.0);
            ubo.TintB = Vector4(InBloomTintB, 0.0);
            ubo.BloomIntensity = 0.675f;

            auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("BloomUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ubo)));
            if (IsBloom)
            {
                OneFrameUniformBuffer->UpdateBufferData(&ubo, sizeof(ubo));
            }

            std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance;
            {
                if (IsBloom)
                {
                    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
                        , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
                }

                const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

                for (int32 i = 0; i < (int32)InShaderInputs.size(); ++i)
                {
                    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                        , ResourceInlineAllactor.Alloc<jTextureResource>(InShaderInputs[i], SamplerState)));

                    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InShaderInputs[i], EResourceLayout::SHADER_READ_ONLY);
                }

                ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
                ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());
            }

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTargetPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);

            jGraphicsPipelineShader Shader;
            {
                jShaderInfo shaderInfo;
                shaderInfo.SetName(VertexShader);
                shaderInfo.SetShaderFilepath(VertexShader);
                shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
                Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

                shaderInfo.SetName(PixelShader);
                shaderInfo.SetShaderFilepath(PixelShader);
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
            }

            jDrawCommand DrawCommand(RenderFrameContextPtr, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
                , Shader, &PostProcessPassPipelineStateFixed, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
            DrawCommand.Test = true;
            DrawCommand.PrepareToDraw(false);

            if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
            {
                DrawCommand.Draw();
                RenderPass->EndRenderPass();
            }
        };

    SCOPE_CPU_PROFILE(PostProcess);
    if (1)
    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, PostProcess);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "PostProcess", Vector4(0.0f, 0.8f, 0.8f, 1.0f));

        const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
        char szDebugEventTemp[1024] = { 0, };

        jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTargetPtr.get();
        jTexture* SourceRT = nullptr;
        jTexture* EyeAdaptationTextureCurrent = nullptr;
        if (gOptions.BloomEyeAdaptation)
        {
            //////////////////////////////////////////////////////////////////////////
            // Todo remove this hardcode
            if (!g_EyeAdaptationARTPtr)
            {
                jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue::Invalid, (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV) };
                Info.ResourceName = TEXT("g_EyeAdaptationARTPtr");
                g_EyeAdaptationARTPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            if (!g_EyeAdaptationBRTPtr)
            {
                jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue::Invalid, (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV) };
                Info.ResourceName = TEXT("g_EyeAdaptationBRTPtr");
                g_EyeAdaptationBRTPtr = jRenderTargetPool::GetRenderTarget(Info);
            }

            static bool FlipEyeAdaptation = false;
            FlipEyeAdaptation = !FlipEyeAdaptation;

            jTexture* EyeAdaptationTextureOld = FlipEyeAdaptation ? g_EyeAdaptationARTPtr->GetTexture() : g_EyeAdaptationBRTPtr->GetTexture();
            EyeAdaptationTextureCurrent = FlipEyeAdaptation ? g_EyeAdaptationBRTPtr->GetTexture() : g_EyeAdaptationARTPtr->GetTexture();

            if (1)
            {
                jCommandBuffer* CommandBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

                g_rhi->TransitionLayout(CommandBuffer, EyeAdaptationTextureOld, EResourceLayout::SHADER_READ_ONLY);
                //////////////////////////////////////////////////////////////////////////

                g_rhi->TransitionLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                SourceRT = SceneRT->ColorPtr->GetTexture();

                sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomEyeAdaptationSetup %dx%d", SceneRT->BloomSetup->Info.Width, SceneRT->BloomSetup->Info.Height);
                AddFullQuadPass(szDebugEventTemp, { SourceRT, EyeAdaptationTextureOld }, SceneRT->BloomSetup
                    , jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_and_eyeadaptation_setup_ps.hlsl"));
                SourceRT = SceneRT->BloomSetup->GetTexture();
                g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);

                for (int32 i = 0; i < _countof(SceneRT->DownSample); ++i)
                {
                    const auto& RTInfo = SceneRT->DownSample[i]->Info;
                    sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomDownsample %dx%d", RTInfo.Width, RTInfo.Height);
                    AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->DownSample[i]
                        , jNameStatic("Resource/Shaders/hlsl/bloom_down_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_down_ps.hlsl"), true);
                    SourceRT = SceneRT->DownSample[i]->GetTexture();
                    g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);
                }
            }

            // Todo make a function for each postprocess steps
            // 여기서 EyeAdaptation 계산하는 Compute shader 추가
            if (1)
            {
                sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "EyeAdaptationCS %dx%d", EyeAdaptationTextureCurrent->Width, EyeAdaptationTextureCurrent->Height);
                DEBUG_EVENT(RenderFrameContextPtr, szDebugEventTemp);
                //////////////////////////////////////////////////////////////////////////
                // Compute Pipeline
                g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SourceRT, EResourceLayout::SHADER_READ_ONLY);
                g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), EyeAdaptationTextureCurrent, EResourceLayout::UAV);

                std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
                int32 BindingPoint = 0;
                jShaderBindingArray ShaderBindingArray;
                jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

                // Binding 0 : Source Log2Average Image
                if (ensure(SourceRT))
                {
                    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jTextureResource>(SourceRT, nullptr)));
                }

                // Binding 1 : Prev frame EyeAdaptation Image
                if (ensure(EyeAdaptationTextureOld))
                {
                    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureOld, nullptr)));
                }

                // Binding 2 : Current frame EyeAdaptation Image
                if (ensure(EyeAdaptationTextureCurrent))
                {
                    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureCurrent, nullptr)));
                }

                // Binding 3 : CommonComputeUniformBuffer
                struct jEyeAdaptationUniformBuffer
                {
                    Vector2 ViewportMin;
                    Vector2 ViewportMax;
                    float MinLuminanceAverage;
                    float MaxLuminanceAverage;
                    float DeltaFrametime;
                    float AdaptationSpeed;
                    float ExposureCompensation;
                };
                jEyeAdaptationUniformBuffer EyeAdaptationUniformBuffer;
                EyeAdaptationUniformBuffer.ViewportMin = Vector2(0.0f, 0.0f);
                EyeAdaptationUniformBuffer.ViewportMax = Vector2((float)SourceRT->Width, (float)SourceRT->Height);
                EyeAdaptationUniformBuffer.MinLuminanceAverage = 0.03f;
                EyeAdaptationUniformBuffer.MaxLuminanceAverage = 8.0f;
                EyeAdaptationUniformBuffer.DeltaFrametime = 1.0f / 60.0f;
                EyeAdaptationUniformBuffer.AdaptationSpeed = 1.0f;
                EyeAdaptationUniformBuffer.ExposureCompensation = exp2(gOptions.AutoExposureKeyValueScale);

                auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
                    jNameStatic("EyeAdaptationUniformBuffer"), jLifeTimeType::OneFrame, sizeof(EyeAdaptationUniformBuffer)));
                OneFrameUniformBuffer->UpdateBufferData(&EyeAdaptationUniformBuffer, sizeof(EyeAdaptationUniformBuffer));
                {
                    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                        , ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
                }

                CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("eyeadaptation"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/eyeadaptation_cs.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                static jShader* Shader = g_rhi->CreateShader(shaderInfo);

                jShaderBindingLayoutArray ShaderBindingLayoutArray;
                ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

                jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

                computePipelineStateInfo->Bind(RenderFrameContextPtr);

                //CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

                jShaderBindingInstanceArray ShaderBindingInstanceArray;
                ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

                jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
                for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
                {
                    // Add ShaderBindingInstanceCombiner data : DescriptorSets, DynamicOffsets
                    ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
                    const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
                    if (pDynamicOffsetTest && pDynamicOffsetTest->size())
                    {
                        ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
                    }
                }
                ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

                g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
                g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
            }
            //////////////////////////////////////////////////////////////////////////

            if (1)
            {
                Vector UpscaleBloomTintA[3] = {
                    Vector(0.066f, 0.066f, 0.066f) * 0.675f,
                    Vector(0.1176f, 0.1176f, 0.1176f) * 0.675f,
                    Vector(0.138f, 0.138f, 0.138f) * 0.675f * 0.5f
                };

                Vector UpscaleBloomTintB[3] = {
                    Vector(0.066f, 0.066f, 0.066f) * 0.675f,
                    Vector::OneVector,
                    Vector::OneVector
                };

                auto CommandBuffer = (jCommandBuffer*)RenderFrameContextPtr->GetActiveCommandBuffer();
                for (int32 i = 0; i < _countof(SceneRT->UpSample); ++i)
                {
                    const auto& RTInfo = SceneRT->UpSample[i]->Info;
                    sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomUpsample %dx%d", RTInfo.Width, RTInfo.Height);
                    AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->UpSample[i]
                        , jNameStatic("Resource/Shaders/hlsl/bloom_up_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_up_ps.hlsl"), true, UpscaleBloomTintA[i], UpscaleBloomTintB[i]);
                    SourceRT = SceneRT->UpSample[i]->GetTexture();
                    g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);
                }

                g_rhi->TransitionLayout(CommandBuffer, EyeAdaptationTextureCurrent, EResourceLayout::SHADER_READ_ONLY);
            }
        }
        else
        {
            SourceRT = GBlackTexture.get();
            EyeAdaptationTextureCurrent = GWhiteTexture.get();
        }
        sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "Tonemap %dx%d", SceneRT->FinalColorPtr->Info.Width, SceneRT->FinalColorPtr->Info.Height);
        AddFullQuadPass(szDebugEventTemp, { SourceRT, SceneRT->ColorPtr->GetTexture(), EyeAdaptationTextureCurrent }, SceneRT->FinalColorPtr
            , jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/tonemap_ps.hlsl"));
    }
}

void jRenderer::DebugPasses()
{
    if (!gOptions.ShowDebugObject)
        return;

    SCOPE_CPU_PROFILE(DebugPasses);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, DebugPasses);
    DEBUG_EVENT(RenderFrameContextPtr, "DebugPasses");

    // Prepare basepass pipeline
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
    auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo DebugPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr, EAttachmentLoadStoreOp::LOAD_DONTCARE
        , EAttachmentLoadStoreOp::LOAD_DONTCARE, ClearDepth
        , RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(), EResourceLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
                , EResourceLayout::UNDEFINED, EResourceLayout::COLOR_ATTACHMENT, true);
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    //if (UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr, EAttachmentLoadStoreOp::LOAD_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor
            , RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr->GetLayout(), EResourceLayout::COLOR_ATTACHMENT);
        renderPassInfo.Attachments.push_back(color);
    }

    const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
    renderPassInfo.Attachments.push_back(depth);

    const int32 ResolveAttachemntIndex = (int32)renderPassInfo.Attachments.size();
    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            renderPassInfo.Attachments.push_back(resolve);
    }

    //////////////////////////////////////////////////////////////////////////
    // Setup subpass of BasePass
    {
        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
        subpass.OutputColorAttachments.push_back(0);
        subpass.OutputDepthAttachment = DepthAttachmentIndex;
        if (UseForwardRenderer)
        {
            if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
                subpass.OutputResolveAttachment = ResolveAttachemntIndex;
        }
        renderPassInfo.Subpasses.push_back(subpass);
    }
    jRenderPass* DebugRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    jGraphicsPipelineShader DebugObjectShader;
    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("default_debug_objectVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/debug_object_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        DebugObjectShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        shaderInfo.SetName(jNameStatic("default_debug_objectPS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/debug_object_fs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
        DebugObjectShader.PixelShader = g_rhi->CreateShader(shaderInfo);
    }

    std::vector<jDrawCommand> DebugDrawCommand;
    auto& DebugObjects = jObject::GetDebugObject();
    DebugDrawCommand.resize(DebugObjects.size());
    for (int32 i = 0; i < (int32)DebugObjects.size(); ++i)
    {
        new (&DebugDrawCommand[i]) jDrawCommand(RenderFrameContextPtr, &View, DebugObjects[i]->RenderObjects[0], DebugRenderPass
            , DebugObjectShader, &DebugPassPipelineStateFixed, DebugObjects[i]->RenderObjects[0]->MaterialPtr.get(), {}, nullptr);
        DebugDrawCommand[i].PrepareToDraw(false);
    }

    if (DebugRenderPass && DebugRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
    {
        for (auto& command : DebugDrawCommand)
        {
            command.Draw();
        }
        DebugRenderPass->EndRenderPass();
    }
}

void jRenderer::Render()
{
    SCOPE_CPU_PROFILE(Render);

    {
        SCOPE_CPU_PROFILE(PoolReset);
        check(RenderFrameContextPtr->GetActiveCommandBuffer());
        if (g_rhi->GetQueryTimePool())
        {
            g_rhi->GetQueryTimePool()->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
        }
        if (g_rhi->GetQueryOcclusionPool())
        {
            g_rhi->GetQueryOcclusionPool()->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
        }

        //ShadowpassOcclusionTest.Init();
        //BasepassOcclusionTest.Init();
    }



#if 1
    jSceneRenderTarget::CubeEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_cubemp.dds")).lock().get();
    jSceneRenderTarget::IrradianceMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_irradiancemap.dds")).lock().get();
    jSceneRenderTarget::FilteredEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_filteredenvmap.dds")).lock().get();
#else
    static jName FilePath = jName("Resource/stpeters_probe.hdr");
    static auto Cubemap = jRHIUtil::ConvertToCubeMap(jNameStatic("F:\\Cubemap.dds"), { 512, 512 }, RenderFrameContextPtr, FilePath);
    static auto IrradianceMap = jRHIUtil::GenerateIrradianceMap(jNameStatic("F:\\IrradianceMap.dds"), { 256, 256 }, RenderFrameContextPtr, Cubemap->GetTexture());
    static auto EnvironmentCubeMap = jRHIUtil::GenerateFilteredEnvironmentMap(jNameStatic("F:\\FilteredEnvMap.dds"), { 256, 256 }, RenderFrameContextPtr, Cubemap->GetTexture());

    jSceneRenderTarget::CubeEnvMap2 = Cubemap->GetTexture();
    jSceneRenderTarget::IrradianceMap2 = IrradianceMap->GetTexture();
    jSceneRenderTarget::FilteredEnvMap2 = EnvironmentCubeMap->GetTexture();
#endif

    {
        Setup();
        ShadowPass();

        // Queue submit to prepare shadowmap for basepass 
        if (gOptions.QueueSubmitAfterShadowPass)
        {
            SCOPE_CPU_PROFILE(QueueSubmitAfterShadowPass);
            //RenderFrameContextPtr->GetActiveCommandBuffer()->End();
            RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::ShadowPass);
            RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
        }

        BasePass();

        // Queue submit to prepare scenecolor RT for postprocess
        if (gOptions.QueueSubmitAfterBasePass)
        {
            SCOPE_CPU_PROFILE(QueueSubmitAfterBasePass);
            //RenderFrameContextPtr->GetActiveCommandBuffer()->End();
            RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass);
            RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
        }
    }

    {
        AOPass();
    }

    PostProcess();
    DebugPasses();

    check(g_ImGUI);
    g_ImGUI->NewFrame([]()
        {
            Vector4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

            char szTitle[128] = { 0, };
            sprintf_s(szTitle, sizeof(szTitle), "RHI : %s", g_rhi->GetRHIName().ToStr());

            ImGui::SetNextWindowPos(ImVec2(27.0f, 27.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350.0f, 682.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin(szTitle);

#if USE_VARIABLE_SHADING_RATE_TIER2
            ImGui::Checkbox("UseVRS", &gOptions.UseVRS);
#endif
            //ImGui::Checkbox("ShowVRSArea", &gOptions.ShowVRSArea);
            //ImGui::Checkbox("ShowGrid", &gOptions.ShowGrid);
            //ImGui::Checkbox("UseWaveIntrinsics", &gOptions.UseWaveIntrinsics);
            {
                if (IsUseVulkan())
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
                    ImGui::EndDisabled();
                    ImGui::Checkbox("UseSubpass", &gOptions.UseSubpass);
                    ImGui::Checkbox("UseMemoryless", &gOptions.UseMemoryless);
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
                    ImGui::Checkbox("[VulkanOnly]UseSubpass", &gOptions.UseSubpass);
                    ImGui::Checkbox("[VulkanOnly]UseMemoryless", &gOptions.UseMemoryless);
                    ImGui::EndDisabled();
                }
            }
            {
                ImGui::Checkbox("ShowDebugObject", &gOptions.ShowDebugObject);
                ImGui::Checkbox("BloomEyeAdaptation", &gOptions.BloomEyeAdaptation);

                ImGui::Checkbox("QueueSubmitAfterShadowPass", &gOptions.QueueSubmitAfterShadowPass);
                ImGui::Checkbox("QueueSubmitAfterBasePass", &gOptions.QueueSubmitAfterBasePass);
                ImGui::SliderFloat("AutoExposureKeyValueScale", &gOptions.AutoExposureKeyValueScale, -12.0f, 12.0f);
            }
            ImGui::Separator();
            //ImGui::Text("PBR properties");
            //ImGui::SliderFloat("Metallic", &gOptions.Metallic, 0.0f, 1.0f);
            //ImGui::SliderFloat("Roughness", &gOptions.Roughness, 0.0f, 1.0f);
            //ImGui::Separator();

            constexpr float IndentSpace = 10.0f;
            const std::thread::id CurrentThreadId = std::this_thread::get_id();
            const ImVec4 OtherThreadColor = { 0.2f, 0.6f, 0.2f, 1.0f };
            {
                ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            }
            ImGui::Separator();
            {
                ImGui::Text("[CPU]");
                const auto& CPUAvgProfileMap = jPerformanceProfile::GetInstance().GetCPUAvgProfileMap();
                double TotalPassesMS = 0.0;
                int32 MostLeastIndent = INT_MAX;
                for (auto& pair : CPUAvgProfileMap)
                {
                    const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
                    const float Indent = IndentSpace * (float)AvgProfile.Indent;
                    if (Indent > 0)
                        ImGui::Indent(Indent);

                    if (CurrentThreadId == AvgProfile.ThreadId)
                        ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
                    else
                        ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);

                    if (Indent > 0)
                        ImGui::Unindent(Indent);

                    // 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
                    // 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
                    if (CurrentThreadId == AvgProfile.ThreadId)
                    {
                        if (MostLeastIndent > AvgProfile.Indent)
                        {
                            MostLeastIndent = AvgProfile.Indent;
                            TotalPassesMS = AvgProfile.AvgElapsedMS;
                        }
                        else if (MostLeastIndent == AvgProfile.Indent)
                        {
                            TotalPassesMS += AvgProfile.AvgElapsedMS;
                        }
                    }
                }
                ImGui::Text("[CPU]Total Passes : %lf ms", TotalPassesMS);
            }
            ImGui::Separator();
            {
                ImGui::Text("[GPU]");
                const auto& GPUAvgProfileMap = jPerformanceProfile::GetInstance().GetGPUAvgProfileMap();
                double TotalPassesMS = 0.0;
                int32 MostLeastIndent = INT_MAX;
                for (auto& pair : GPUAvgProfileMap)
                {
                    const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
                    const float Indent = IndentSpace * (float)AvgProfile.Indent;
                    if (Indent > 0)
                        ImGui::Indent(Indent);

                    if (CurrentThreadId == AvgProfile.ThreadId)
                        ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
                    else
                        ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);

                    if (Indent > 0)
                        ImGui::Unindent(Indent);

                    // 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
                    // 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
                    if (CurrentThreadId == AvgProfile.ThreadId)
                    {
                        if (MostLeastIndent > AvgProfile.Indent)
                        {
                            MostLeastIndent = AvgProfile.Indent;
                            TotalPassesMS = AvgProfile.AvgElapsedMS;
                        }
                        else if (MostLeastIndent == AvgProfile.Indent)
                        {
                            TotalPassesMS += AvgProfile.AvgElapsedMS;
                        }
                    }
                }
                ImGui::Text("[GPU]Total Passes : %lf ms", TotalPassesMS);
            }
            ImGui::Separator();
            //for (auto& pair : CounterMap)
            //{
            //    ImGui::Text("%s : %lld", pair.first.ToStr(), pair.second);
            //}
            ImGui::Separator();
            ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);
            ImGui::End();

#if ENABLE_PBR
            ImGui::SetNextWindowPos(ImVec2(400.0f, 27.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(200.0f, 80.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Camera Options", 0, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderFloat("DirX", &gOptions.SunDir.x, -1.0f, 1.0f);
            ImGui::SliderFloat("DirY", &gOptions.SunDir.y, -1.0f, 1.0f);
            ImGui::SliderFloat("DirZ", &gOptions.SunDir.z, -1.0f, 1.0f);
            ImGui::SliderFloat("AnisoG", &gOptions.AnisoG, 0.0f, 1.0f);
            //ImGui::Checkbox("EarthQuake with TLAS update", &gOptions.EarthQuake);
            //ImGui::SliderFloat("Focal distance", &gOptions.FocalDistance, 3.0f, 40.0f);
            //ImGui::SliderFloat("Lens radius", &gOptions.LensRadius, 0.0f, 0.2f);
            ImGui::End();

            //ImGui::SetWindowFocus(szTitle);
#endif
        });
    g_ImGUI->Draw(RenderFrameContextPtr);
}