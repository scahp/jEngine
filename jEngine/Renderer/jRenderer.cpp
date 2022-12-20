#include "pch.h"
#include "jRenderer.h"
#include "Scene/Light/jDirectionalLight.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "jOptions.h"
#include "RHI/Vulkan/jUniformBufferBlock_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"
#include "jPrimitiveUtil.h"
#include "Scene/jRenderObject.h"
#include "RHI/jShaderBindingsLayout.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "jDirectionalLightDrawCommandGenerator.h"
#include "jPointLightDrawCommandGenerator.h"
#include "jSpotLightDrawCommandGenerator.h"
#include "RHI/jRenderTargetPool.h"

#define ASYNC_WITH_SETUP 1
#define PARALLELFOR_WITH_PASSSETUP 1

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

    FrameIndex = g_rhi_vk->CurrentFrameIndex;
    UseForwardRenderer = RenderFrameContextPtr->UseForwardRenderer;

    // View 별로 저장 할 수 있어야 함
    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];

        if (ViewLight.Light)
        {
            if (ViewLight.Light->IsShadowCaster)
            {
                switch (ViewLight.Light->Type)
                {
                case ELightType::DIRECTIONAL:
                    ViewLight.ShadowMapPtr = RenderFrameContextPtr->SceneRenderTarget->DirectionalLightShadowMapPtr;
                    break;
                case ELightType::POINT:
                    ViewLight.ShadowMapPtr = RenderFrameContextPtr->SceneRenderTarget->CubeShadowMapPtr;
                    break;
                case ELightType::SPOT:
                    ViewLight.ShadowMapPtr = RenderFrameContextPtr->SceneRenderTarget->SpotLightShadowMapPtr;
                    break;
                }
            }

            ViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(ViewLight.ShadowMapPtr ? ViewLight.ShadowMapPtr->GetTexture() : nullptr);
        }
    }

#if ASYNC_WITH_SETUP
    ShadowPassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupShadowPass, this);
#else
    jRenderer::SetupShadowPass();
    jRenderer::SetupBasePass();
#endif
}

void jRenderer::SetupShadowPass()
{
    SCOPE_CPU_PROFILE(SetupShadowPass);

    ShadowDrawInfo.reserve(View.Lights.size());

    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];
        if (!ViewLight.Light->IsShadowCaster)
            continue;

        ShadowDrawInfo.push_back(jShadowDrawInfo());
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[ShadowDrawInfo.size() - 1];

        // Prepare shadowpass pipeline
        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
        jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(EMSAASamples::COUNT_1);
        auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE
            , EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

        const int32 RTWidth = ViewLight.ShadowMapPtr->Info.Width;
        const int32 RTHeight = ViewLight.ShadowMapPtr->Info.Height;

        jPipelineStateFixedInfo ShadpwPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), false);

        {
            const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

            jAttachment depth = jAttachment(ViewLight.ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
                , ClearColor, ClearDepth, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);

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
                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace : nullptr);

                new (&ShadowPasses.DrawCommands[InIndex]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, InRenderObject, ShadowPasses.ShadowMapRenderPass
                    , (InRenderObject->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, {}, nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[InIndex].PrepareToDraw(true);
            });
#else
        ShadowPasses.DrawCommands.resize(ShadowCaterRenderObjects.size());
        int32 i = 0;
        for (auto iter : ShadowCaterRenderObjects)
        {
            new (&ShadowPasses.DrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, iter->RenderObject, ShadowMapRenderPass
                , (iter->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, {}, nullptr);
            ShadowPasses.DrawCommands[i].PrepareToDraw(true);
            ++i;
        }
#endif
    }
}

void jRenderer::SetupBasePass()
{
    SCOPE_CPU_PROFILE(SetupBasePass);

    // Prepare basepass pipeline
    auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
    jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo BasePassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    auto TranslucentBlendingState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jPipelineStateFixedInfo TranslucentPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, TranslucentBlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTarget->DepthPtr, EAttachmentLoadStoreOp::CLEAR_STORE
        , EAttachmentLoadStoreOp::CLEAR_STORE, ClearColor, ClearDepth
        , EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT, true);
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    if (!UseForwardRenderer)
    {
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTarget->GBuffer); ++i)
        {
            jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTarget->GBuffer[i], EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);
        }
    }

    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    if (UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
            , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
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

    jGraphicsPipelineShader BasePassShader;
    jShaderInfo shaderInfo;
    if (UseForwardRenderer)
    {
        shaderInfo.SetName(jNameStatic("default_testVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        BasePassShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        shaderInfo.SetName(jNameStatic("default_testPS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
#if USE_VARIABLE_SHADING_RATE_TIER2
        if (gOptions.UseVRS)
            shaderInfo.PreProcessors = jNameStatic("#define USE_VARIABLE_SHADING_RATE 1");
#endif
        BasePassShader.PixelShader = g_rhi->CreateShader(shaderInfo);
    }
    else
    {
        shaderInfo.SetName(jNameStatic("default_testVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gbuffer_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        BasePassShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        jShaderGBuffer::ShaderPermutation ShaderPermutation;
        ShaderPermutation.SetIndex<jShaderGBuffer::USE_VERTEX_COLOR>(0);
        ShaderPermutation.SetIndex<jShaderGBuffer::USE_ALBEDO_TEXTURE>(1);
        ShaderPermutation.SetIndex<jShaderGBuffer::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
        BasePassShader.PixelShader = jShaderGBuffer::CreateShader(ShaderPermutation);
    }

    jGraphicsPipelineShader TranslucentPassShader;
    {
        shaderInfo.SetName(jNameStatic("default_testVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/gbuffer_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        TranslucentPassShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        jShaderGBuffer::ShaderPermutation ShaderPermutation;
        ShaderPermutation.SetIndex<jShaderGBuffer::USE_VERTEX_COLOR>(0);
        ShaderPermutation.SetIndex<jShaderGBuffer::USE_ALBEDO_TEXTURE>(1);
        ShaderPermutation.SetIndex<jShaderGBuffer::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
        TranslucentPassShader.PixelShader = jShaderGBuffer::CreateShader(ShaderPermutation);
    }

    jGraphicsPipelineShader BasePassInstancingShader;
    {
        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("default_instancing_testVS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_instancing_vs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
        BasePassInstancingShader.VertexShader = g_rhi->CreateShader(shaderInfo);

        shaderInfo.SetName(jNameStatic("default_instancing_testPS"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
        BasePassInstancingShader.PixelShader = g_rhi->CreateShader(shaderInfo);
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
        new (&BasePasses[InIndex]) jDrawCommand(RenderFrameContextPtr, &View, InRenderObject, BaseRenderPass
            , (InRenderObject->HasInstancing() ? BasePassInstancingShader : BasePassShader), &BasePassPipelineStateFixed, {}, SimplePushConstant);
        BasePasses[InIndex].PrepareToDraw(false);
    });
#else
    BasePasses.resize(jObject::GetStaticObject().size());
    int32 i = 0;
    for (auto iter : jObject::GetStaticObject())
    {
        new (&BasePasses[i]) jDrawCommand(RenderFrameContextPtr, &View, iter->RenderObject, BaseRenderPass
            , (iter->HasInstancing() ? BasePassInstancingShader : BasePassShader), &BasePassPipelineStateFixed, {}, SimplePushConstant);
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

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

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

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
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
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
        }
        else
        {
            for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTarget->GBuffer); ++i)
            {
                g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTarget->GBuffer[i]->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
            }
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
}

void jRenderer::DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass)
{
    SCOPE_CPU_PROFILE(LightingPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, LightingPass);
    DEBUG_EVENT(RenderFrameContextPtr, "LightingPass");

    if (!gOptions.UseSubpass)
    {
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTarget->GBuffer); ++i)
        {
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTarget->GBuffer[i]->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // GBuffer Input attachment 추가
    jShaderBindingInstance* GBufferShaderBindingInstance
        = RenderFrameContextPtr->SceneRenderTarget->PrepareGBufferShaderBindingInstance(gOptions.UseSubpass);

    jShaderBindingInstanceArray DefaultLightPassShaderBindingInstances;
    DefaultLightPassShaderBindingInstances.Add(GBufferShaderBindingInstance);
    DefaultLightPassShaderBindingInstances.Add(View.ViewUniformBufferShaderBindingInstance);
    //////////////////////////////////////////////////////////////////////////

    std::vector<jDrawCommand> LightPasses;
    LightPasses.resize(View.Lights.size());

    const int32 RTWidth = RenderFrameContextPtr->SceneRenderTarget->ColorPtr->Info.Width;
    const int32 RTHeight = RenderFrameContextPtr->SceneRenderTarget->ColorPtr->Info.Height;
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
            const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

            jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTarget->DepthPtr, EAttachmentLoadStoreOp::LOAD_DONTCARE
                , EAttachmentLoadStoreOp::LOAD_DONTCARE, ClearColor, ClearDepth
                , RenderFrameContextPtr->SceneRenderTarget->DepthPtr->GetLayout(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
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

        for (int32 i = 0; i < (int32)View.Lights.size(); ++i)
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
        , jName VertexShader, jName PixelShader, bool IsBloom = false)
    {
        DEBUG_EVENT(RenderFrameContextPtr, InDebugName);

        static jFullscreenQuadPrimitive* GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
        jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
        auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

        const int32 RTWidth = InRenderTargetPtr->Info.Width;
        const int32 RTHeight = InRenderTargetPtr->Info.Height;

        // Create fixed pipeline states
        jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, MultisampleState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

        jRenderPassInfo renderPassInfo;
        jAttachment color = jAttachment(InRenderTargetPtr, EAttachmentLoadStoreOp::DONTCARE_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth, EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
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
        };
        jBloomUniformBuffer ubo;
        ubo.BufferSizeAndInvSize.x = (float)RTWidth;
        ubo.BufferSizeAndInvSize.y = (float)RTHeight;
        ubo.BufferSizeAndInvSize.z = 1.0f / (float)RTWidth;
        ubo.BufferSizeAndInvSize.w = 1.0f / (float)RTHeight;

        jUniformBufferBlock_Vulkan OneFrameUniformBuffer(jNameStatic("BloomUniformBuffer"), jLifeTimeType::OneFrame);
        if (IsBloom)
        {
            OneFrameUniformBuffer.Init(sizeof(ubo));
            OneFrameUniformBuffer.UpdateBufferData(&ubo, sizeof(ubo));
        }

        {
            if (IsBloom)
            {
                ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
                    , ResourceInlineAllactor.Alloc<jUniformBufferResource>(&OneFrameUniformBuffer));
            }

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), true, ECompareOp::LESS>::Create();

            for (int32 i = 0; i < (int32)InShaderInputs.size(); ++i)
            {
                ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                    , ResourceInlineAllactor.Alloc<jTextureResource>(InShaderInputs[i], SamplerState));
            }

            ShaderBindingInstanceArray.Add(g_rhi->CreateShaderBindingInstance(ShaderBindingArray));
        }

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

        jDrawCommand DrawCommand(RenderFrameContextPtr, GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
            , Shader, &PostProcessPassPipelineStateFixed, ShaderBindingInstanceArray, nullptr);
        DrawCommand.Test = true;
        DrawCommand.PrepareToDraw(false);

        if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            DrawCommand.Draw();
            RenderPass->EndRenderPass();
        }
    };

    SCOPE_CPU_PROFILE(PostProcess);
    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, PostProcess);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "PostProcess", Vector4(0.0f, 0.8f, 0.8f, 1.0f));

        const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
        jCommandBuffer* CommandBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();
        jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTarget;

        //////////////////////////////////////////////////////////////////////////
        // Todo remove this hardcode
        if (!g_EyeAdaptationARTPtr)
        {
            g_EyeAdaptationARTPtr = jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi_vk->GetSelectedMSAASamples() });
        }
        if (!g_EyeAdaptationBRTPtr)
        {
            g_EyeAdaptationBRTPtr = jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi_vk->GetSelectedMSAASamples() });
        }

        static bool FlipEyeAdaptation = false;
        FlipEyeAdaptation = !FlipEyeAdaptation;

        jTexture* EyeAdaptationTextureOld = FlipEyeAdaptation ? g_EyeAdaptationARTPtr->GetTexture() : g_EyeAdaptationBRTPtr->GetTexture();
        jTexture* EyeAdaptationTextureCurrent = FlipEyeAdaptation ? g_EyeAdaptationBRTPtr->GetTexture() : g_EyeAdaptationARTPtr->GetTexture();

        g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureOld, EImageLayout::SHADER_READ_ONLY);
        //////////////////////////////////////////////////////////////////////////

        g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);

        jTexture* SourceRT = SceneRT->ColorPtr->GetTexture();

        char szDebugEventTemp[1024] = { 0, };
        sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomEyeAdaptationSetup %dx%d", SceneRT->BloomSetup->Info.Width, SceneRT->BloomSetup->Info.Height);
        AddFullQuadPass(szDebugEventTemp, { SourceRT, EyeAdaptationTextureOld }, SceneRT->BloomSetup
            , jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_and_eyeadaptation_setup_ps.hlsl"));
        SourceRT = SceneRT->BloomSetup->GetTexture();
        g_rhi->TransitionImageLayout(CommandBuffer, SourceRT, EImageLayout::SHADER_READ_ONLY);

        for (int32 i = 0; i < _countof(SceneRT->DownSample); ++i)
        {
            const auto& RTInfo = SceneRT->DownSample[i]->Info;
            sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomDownsample %dx%d", RTInfo.Width, RTInfo.Height);
            AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->DownSample[i]
                , jNameStatic("Resource/Shaders/hlsl/bloom_down_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_down_ps.hlsl"), true);
            SourceRT = SceneRT->DownSample[i]->GetTexture();
            g_rhi->TransitionImageLayout(CommandBuffer, SourceRT, EImageLayout::SHADER_READ_ONLY);
        }

        g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureOld, EImageLayout::SHADER_READ_ONLY);
        g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureCurrent, EImageLayout::GENERAL);

        // Todo make a function for each postprocess steps
        // 여기서 EyeAdaptation 계산하는 Compute shader 추가
        {
            sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "EyeAdaptationCS %dx%d", EyeAdaptationTextureCurrent->Width, EyeAdaptationTextureCurrent->Height);
            DEBUG_EVENT(RenderFrameContextPtr, szDebugEventTemp);
            //////////////////////////////////////////////////////////////////////////
            // Compute Pipeline
            jShaderBindingInstance* CurrentBindingInstance = nullptr;
            int32 BindingPoint = 0;
            jShaderBindingArray ShaderBindingArray;
            jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

            // Binding 0 : Source Log2Average Image
            if (ensure(SourceRT))
            {
                ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(SourceRT, nullptr));
            }

            // Binding 1 : Prev frame EyeAdaptation Image
            if (ensure(EyeAdaptationTextureOld))
            {
                ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureOld, nullptr));
            }

            // Binding 2 : Current frame EyeAdaptation Image
            if (ensure(EyeAdaptationTextureCurrent))
            {
                ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureCurrent, nullptr));
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

            jUniformBufferBlock_Vulkan OneFrameUniformBuffer(jNameStatic("EyeAdaptationUniformBuffer"), jLifeTimeType::OneFrame);
            OneFrameUniformBuffer.Init(sizeof(EyeAdaptationUniformBuffer));
            OneFrameUniformBuffer.UpdateBufferData(&EyeAdaptationUniformBuffer, sizeof(EyeAdaptationUniformBuffer));
            {
                ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                    , ResourceInlineAllactor.Alloc<jUniformBufferResource>(&OneFrameUniformBuffer));
            }

            CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray);

            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("eyeadaptation"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/eyeadaptation_cs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            static jShader* Shader = g_rhi->CreateShader(shaderInfo);

            jShaderBindingsLayoutArray ShaderBindingLayoutArray;
            ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

            jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

            computePipelineStateInfo->Bind(RenderFrameContextPtr);

            CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

            vkCmdDispatch((VkCommandBuffer)CommandBuffer->GetHandle(), 1, 1, 1);
        }
        //////////////////////////////////////////////////////////////////////////

        for (int32 i = 0; i < _countof(SceneRT->UpSample); ++i)
        {
            const auto& RTInfo = SceneRT->UpSample[i]->Info;
            sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomUpsample %dx%d", RTInfo.Width, RTInfo.Height);
            AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->UpSample[i]
                , jNameStatic("Resource/Shaders/hlsl/bloom_up_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_up_ps.hlsl"), true);
            SourceRT = SceneRT->UpSample[i]->GetTexture();
            g_rhi->TransitionImageLayout(CommandBuffer, SourceRT, EImageLayout::SHADER_READ_ONLY);
        }

        g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureCurrent, EImageLayout::SHADER_READ_ONLY);

        sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "Tonemap %dx%d", SceneRT->FinalColorPtr->Info.Width, SceneRT->FinalColorPtr->Info.Height);
        AddFullQuadPass(szDebugEventTemp, { SourceRT, SceneRT->ColorPtr->GetTexture(), EyeAdaptationTextureCurrent }, SceneRT->FinalColorPtr
            , jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/tonemap_ps.hlsl"));

        return;


        //////////////////////////////////////////////////////////////////////////
        // Compute Pipeline
        g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        g_rhi->TransitionImageLayout(CommandBuffer, EyeAdaptationTextureCurrent, EImageLayout::SHADER_READ_ONLY);
        g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->FinalColorPtr->GetTexture(), EImageLayout::GENERAL);

        jShaderBindingInstance* CurrentBindingInstance = nullptr;
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        // Binding 0 : Source Image
        if (ensure(SceneRT->ColorPtr))
        {
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(SceneRT->ColorPtr->GetTexture(), nullptr));
        }

        // Binding 1 : Source Image
        if (ensure(EyeAdaptationTextureCurrent))
        {
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureCurrent, nullptr));
        }

        // Binding 2 : Target Image
        if (ensure(SceneRT->FinalColorPtr))
        {
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(SceneRT->FinalColorPtr->GetTexture(), nullptr));
        }

        // Binding 3 : CommonComputeUniformBuffer
        struct jCommonComputeUniformBuffer
        {
            float Width;
            float Height;
            float UseWaveIntrinsics;
            float Padding;
        };
        jCommonComputeUniformBuffer CommonComputeUniformBuffer;
        CommonComputeUniformBuffer.Width = (float)SCR_WIDTH;
        CommonComputeUniformBuffer.Height = (float)SCR_HEIGHT;
        CommonComputeUniformBuffer.UseWaveIntrinsics = gOptions.UseWaveIntrinsics;

        jUniformBufferBlock_Vulkan OneFrameUniformBuffer(jNameStatic("CommonComputeUniformBuffer"), jLifeTimeType::OneFrame);
        OneFrameUniformBuffer.Init(sizeof(CommonComputeUniformBuffer));
        OneFrameUniformBuffer.UpdateBufferData(&CommonComputeUniformBuffer, sizeof(CommonComputeUniformBuffer));
        {
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jUniformBufferResource>(&OneFrameUniformBuffer));
        }

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray);

        jShaderInfo shaderInfo;
        shaderInfo.SetName(jNameStatic("tonemap cs"));
        shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/tonemap_cs.hlsl"));
        shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        static jShader* Shader = g_rhi->CreateShader(shaderInfo);

        jShaderBindingsLayoutArray ShaderBindingLayoutArray;
        ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

        jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

        computePipelineStateInfo->Bind(RenderFrameContextPtr);

        CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

        Vector2i SwapchainExtent = g_rhi->GetSwapchain()->GetExtent();

        check(g_rhi->GetSwapchain());
        vkCmdDispatch((VkCommandBuffer)CommandBuffer->GetHandle()
            , SwapchainExtent.x / 16 + ((SwapchainExtent.x % 16) ? 1 : 0)
            , SwapchainExtent.y / 16 + ((SwapchainExtent.y % 16) ? 1 : 0), 1);
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
    auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
    jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo DebugPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTarget->DepthPtr, EAttachmentLoadStoreOp::LOAD_DONTCARE
        , EAttachmentLoadStoreOp::LOAD_DONTCARE, ClearColor, ClearDepth
        , RenderFrameContextPtr->SceneRenderTarget->DepthPtr->GetLayout(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT, true);
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    if (UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTarget->FinalColorPtr, EAttachmentLoadStoreOp::LOAD_STORE
            , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
            , RenderFrameContextPtr->SceneRenderTarget->FinalColorPtr->GetLayout(), EImageLayout::COLOR_ATTACHMENT);
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
            , DebugObjectShader, &DebugPassPipelineStateFixed, {}, nullptr);
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
        ensure(RenderFrameContextPtr->GetActiveCommandBuffer()->Begin());
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

    Setup();
    ShadowPass();

    // Queue submit to prepare shadowmap for basepass 
    if (gOptions.QueueSubmitAfterShadowPass)
    {
        SCOPE_CPU_PROFILE(QueueSubmitAfterShadowPass);
        RenderFrameContextPtr->GetActiveCommandBuffer()->End();
        RenderFrameContextPtr->QueueSubmitCurrentActiveCommandBuffer(g_rhi_vk->Swapchain->Images[FrameIndex]->RenderFinishedAfterShadow);
        RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    }

    BasePass();

    // Queue submit to prepare scenecolor RT for postprocess
    if (gOptions.QueueSubmitAfterBasePass)
    {
        SCOPE_CPU_PROFILE(QueueSubmitAfterBasePass);
        RenderFrameContextPtr->GetActiveCommandBuffer()->End();
        RenderFrameContextPtr->QueueSubmitCurrentActiveCommandBuffer(g_rhi_vk->Swapchain->Images[FrameIndex]->RenderFinishedAfterBasePass);
        RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
    }

    PostProcess();
    DebugPasses();

    jImGUI_Vulkan::Get().Draw(RenderFrameContextPtr);
    ensure(RenderFrameContextPtr->GetActiveCommandBuffer()->End());

}
