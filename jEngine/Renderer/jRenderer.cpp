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
static std::shared_ptr<TPushConstant<jSimplePushConstant>> PushConstantPtr;

void jRenderer::Setup()
{
    SCOPE_CPU_PROFILE(Setup);

    FrameIndex = g_rhi_vk->CurrenFrameIndex;
    UseForwardRenderer = RenderFrameContextPtr->UseForwardRenderer;

    // View 별로 저장 할 수 있어야 함
    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];

        if (ViewLight.Light)
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

            ViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(ViewLight.ShadowMapPtr->GetTexture());
        }
    }

#if ASYNC_WITH_SETUP
    ShadowPassSetupFinishEvent = std::async(std::launch::async, &jRenderer::SetupShadowPass, this);
#else
    jRenderer::SetupShadowPass();
    jRenderer::SetupBasePass();
#endif
}

void jRenderer::SetupShadowPass()
{
    SCOPE_CPU_PROFILE(SetupShadowPass);

    ShadowDrawInfo.resize(View.Lights.size());

    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[i];

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

        jShader* ShadowShader = nullptr;
        {
            jShaderInfo shaderInfo;

            shaderInfo.name = jNameStatic("shadow_test");
            if (ViewLight.Light->IsOmnidirectional())
            {
                shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/omni_shadow_vs.hlsl");
                shaderInfo.gs = jNameStatic("Resource/Shaders/hlsl/omni_shadow_gs.hlsl");
                shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/omni_shadow_fs.hlsl");
            }
            else
            {
                if (ViewLight.Light->Type == ELightType::SPOT)
                {
                    shaderInfo.vs = jNameStatic("resource/shaders/hlsl/spot_shadow_vs.hlsl");
                    shaderInfo.fs = jNameStatic("resource/shaders/hlsl/spot_shadow_fs.hlsl");
                }
                else
                {
                    shaderInfo.vs = jNameStatic("resource/shaders/hlsl/shadow_vs.hlsl");
                    shaderInfo.fs = jNameStatic("resource/shaders/hlsl/shadow_fs.hlsl");
                }
            }
            ShadowShader = g_rhi->CreateShader(shaderInfo);
        }
        jShader* ShadowInstancingShader = nullptr;
        {
            jShaderInfo shaderInfo;
            shaderInfo.name = jNameStatic("shadow_test");
            shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/shadow_instancing_vs.hlsl");
            shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/shadow_fs.hlsl");
            ShadowInstancingShader = g_rhi->CreateShader(shaderInfo);
        }

#if PARALLELFOR_WITH_PASSSETUP
        const auto& ShadowCaterObjects = jObject::GetShadowCasterObject();
        ShadowPasses.DrawCommands.resize(ShadowCaterObjects.size());
        jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, ShadowCaterObjects
            , [&](size_t InIndex, const jObject* InObject)
            {
                new (&ShadowPasses.DrawCommands[InIndex]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, InObject->RenderObject, ShadowPasses.ShadowMapRenderPass
                    , (InObject->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, {}, nullptr);
                ShadowPasses.DrawCommands[InIndex].PrepareToDraw(true);
            });
#else
        ShadowPasses.DrawCommands.resize(ShadowCaterObjects.size());
        int32 i = 0;
        for (auto iter : ShadowCaterObjects)
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
    OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    // This is an example of creating RenderPass without setting subpasses
    //if (resolve.IsValid())
    //{
    //    OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass({ color }, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    //}
    //else
    //{
    //    OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass({ color }, depth, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    //}

    jShader* BasePassShader = nullptr;
    {
        jShaderInfo shaderInfo;
        shaderInfo.name = jNameStatic("default_test");
        if (UseForwardRenderer)
        {
            shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/shader_vs.hlsl");
            shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl");
        }
        else
        {
            shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/gbuffer_vs.hlsl");
            shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/gbuffer_fs.hlsl");
        }
#if USE_VARIABLE_SHADING_RATE_TIER2
        if (gOptions.UseVRS)
            shaderInfo.fsPreProcessor = jNameStatic("#define USE_VARIABLE_SHADING_RATE 1");
#endif
        BasePassShader = g_rhi->CreateShader(shaderInfo);
    }
    jShader* BasePassInstancingShader = nullptr;
    {
        jShaderInfo shaderInfo;
        shaderInfo.name = jNameStatic("default_test");
        shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/shader_instancing_vs.hlsl");
        shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl");
        BasePassInstancingShader = g_rhi->CreateShader(shaderInfo);
    }
    
    if (!PushConstantPtr)
    {
        PushConstantPtr = std::make_shared<TPushConstant<jSimplePushConstant>>(jSimplePushConstant()
            , jPushConstantRange(EShaderAccessStageFlag::FRAGMENT, 0, sizeof(jSimplePushConstant)));
    }
    PushConstantPtr->Data.ShowVRSArea = gOptions.ShowVRSArea;
    PushConstantPtr->Data.ShowGrid = gOptions.ShowGrid;

#if PARALLELFOR_WITH_PASSSETUP
    BasePasses.resize(jObject::GetStaticObject().size());
    jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetStaticObject()
        , [&](size_t InIndex, const jObject* InObject)
    {
        new (&BasePasses[InIndex]) jDrawCommand(RenderFrameContextPtr, &View, InObject->RenderObject, OpaqueRenderPass
            , (InObject->HasInstancing() ? BasePassInstancingShader : BasePassShader), &BasePassPipelineStateFixed, {}, PushConstantPtr);
        BasePasses[InIndex].PrepareToDraw(false);
    });
#else
    BasePasses.resize(jObject::GetStaticObject().size());
    int32 i = 0;
    for (auto iter : jObject::GetStaticObject())
    {
        new (&BasePasses[i]) jDrawCommand(RenderFrameContextPtr, &View, iter->RenderObject, OpaqueRenderPass
            , (iter->HasInstancing() ? BasePassInstancingShader : BasePassShader), &BasePassPipelineStateFixed, {}, PushConstantPtr);
        BasePasses[i].PrepareToDraw(false);
        ++i;
    }
#endif
}

void jRenderer::ShadowPass()
{
#if ASYNC_WITH_SETUP
    if (ShadowPassSetupFinishEvent.valid())
        ShadowPassSetupFinishEvent.wait();

    BasePassSetupFinishEvent = std::async(std::launch::async, &jRenderer::SetupBasePass, this);
#endif

    SCOPE_CPU_PROFILE(ShadowPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, ShadowPass);

    for (int32 i = 0; i < ShadowDrawInfo.size(); ++i)
    {
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[i];
        const std::shared_ptr<jRenderTarget>& ShadowMapPtr = ShadowPasses.GetShadowMapPtr();

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

        if (ShadowPasses.ShadowMapRenderPass && ShadowPasses.ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
        {
            //ShadowpassOcclusionTest.BeginQuery(RenderFrameContextPtr->CommandBuffer);
            for (const auto& command : ShadowPasses.DrawCommands)
            {
                command.Draw();
            }
            //ShadowpassOcclusionTest.EndQuery(RenderFrameContextPtr->CommandBuffer);
            ShadowPasses.ShadowMapRenderPass->EndRenderPass();
        }

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
    }
}

void jRenderer::OpaquePass()
{
    if (BasePassSetupFinishEvent.valid())
        BasePassSetupFinishEvent.wait();

    {
        SCOPE_CPU_PROFILE(OpaquePass);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BasePass);

        if (UseForwardRenderer)
        {
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
        }
        else
        {
            for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTarget->GBuffer); ++i)
            {
                g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->GBuffer[i]->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
            }
        }

        BasepassOcclusionTest.BeginQuery(RenderFrameContextPtr->CommandBuffer);
        if (OpaqueRenderPass && OpaqueRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
        {
            for (const auto& command : BasePasses)
            {
                command.Draw();
            }

            if (!UseForwardRenderer && gOptions.UseSubpass)
            {
                g_rhi->NextSubpass(RenderFrameContextPtr->CommandBuffer);
                DeferredLightPass_TodoRefactoring(OpaqueRenderPass);
            }

            OpaqueRenderPass->EndRenderPass();
        }

        if (!UseForwardRenderer && !gOptions.UseSubpass)
        {
            DeferredLightPass_TodoRefactoring(OpaqueRenderPass);
        }
        BasepassOcclusionTest.EndQuery(RenderFrameContextPtr->CommandBuffer);
    }
}

void jRenderer::DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass)
{
    SCOPE_CPU_PROFILE(LightingPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, LightingPass);

    if (!gOptions.UseSubpass)
    {
        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTarget->GBuffer); ++i)
        {
            g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->GBuffer[i]->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        }
    }

    {
        const int32 SubpassIndex = (!UseForwardRenderer && gOptions.UseSubpass) ? 1 : 0;

        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false>::Create();
        jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
        auto DepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

        const int32 RTWidth = RenderFrameContextPtr->SceneRenderTarget->ColorPtr->Info.Width;
        const int32 RTHeight = RenderFrameContextPtr->SceneRenderTarget->ColorPtr->Info.Height;

        jPipelineStateFixedInfo PipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        //////////////////////////////////////////////////////////////////////////
        const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

        if (!gOptions.UseSubpass)
        {
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
        }

        jShader* DirectionalLightShader = nullptr;
        {
            jShaderInfo shaderInfo;
            shaderInfo.name = jNameStatic("DirectionalLightShader");
            shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl");
            shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/directionallight_fs.hlsl");
            if (gOptions.UseSubpass)
                shaderInfo.fsPreProcessor = jNameStatic("#define USE_SUBPASS 1");
            DirectionalLightShader = g_rhi->CreateShader(shaderInfo);
        }

        static jFullscreenQuadPrimitive* FullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

        jVertexBufferArray FullScreenQuadVertexBufferArray;
        FullScreenQuadVertexBufferArray.Add(FullscreenPrimitive->RenderObject->VertexBuffer);

        //////////////////////////////////////////////////////////////////////////
        jShaderBindingInstanceArray DirectionalLightShaderBindingInstanceArray;

        // GBuffer Input attachment 추가
        jShaderBindingInstance* GBufferShaderBindingInstance 
            = RenderFrameContextPtr->SceneRenderTarget->PrepareGBufferShaderBindingInstance(gOptions.UseSubpass);
        DirectionalLightShaderBindingInstanceArray.Add(GBufferShaderBindingInstance);
        DirectionalLightShaderBindingInstanceArray.Add(View.ViewUniformBufferShaderBindingInstance);

        for (int32 i = 0; i < View.Lights.size(); ++i)
        {
            if (View.Lights[i].Light->Type == ELightType::DIRECTIONAL)
            {
                DirectionalLightShaderBindingInstanceArray.Add(View.Lights[i].ShaderBindingInstance);
                break;
            }
        }
        //////////////////////////////////////////////////////////////////////////

        jShaderBindingsLayoutArray DirectionalLightShaderBindingLayoutArray;
        for (int32 i = 0; i < DirectionalLightShaderBindingInstanceArray.NumOfData; ++i)
        {
            DirectionalLightShaderBindingLayoutArray.Add(DirectionalLightShaderBindingInstanceArray[i]->ShaderBindingsLayouts);
        }

        // Directional light create pipeline
        jPipelineStateInfo_Vulkan* DirectionalLightPSO = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(&PipelineStateFixed, DirectionalLightShader
            , FullScreenQuadVertexBufferArray, InRenderPass, DirectionalLightShaderBindingLayoutArray, nullptr, SubpassIndex);

        //////////////////////////////////////////////////////////////////////////
        // Point light create pipeline
        static auto PointLightSphere = jPrimitiveUtil::CreateSphere(Vector::ZeroVector, 1.0, 16, Vector(1.0f), Vector4::OneVector);

        int32 PointLightIndex = 0;
        jPointLight* PointLight = nullptr;
        for (int32 i = 0; i < View.Lights.size(); ++i)
        {
            if (View.Lights[i].Light->Type == ELightType::POINT)
            {
                PointLight = (jPointLight*)View.Lights[i].Light;
                PointLightIndex = i;
                break;
            }
        }

        jShader* PointLightShader = nullptr;
        {
            jShaderInfo shaderInfo;
            shaderInfo.name = jNameStatic("PointLightShader");
            shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/pointlight_vs.hlsl");
            shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/pointlight_fs.hlsl");
            if (gOptions.UseSubpass)
                shaderInfo.fsPreProcessor = jNameStatic("#define USE_SUBPASS 1");
            PointLightShader = g_rhi->CreateShader(shaderInfo);
        }

        jVertexBufferArray PointLightSphereVertexBufferArray;
        PointLightSphereVertexBufferArray.Add(PointLightSphere->RenderObject->VertexBuffer);

        jShaderBindingInstanceArray PointLightShaderBindingInstanceArray;
        // ShaderBindingInstanceArray 에 필요한 내용을 여기 추가하자
        PointLightShaderBindingInstanceArray.Add(GBufferShaderBindingInstance);
        PointLightShaderBindingInstanceArray.Add(View.ViewUniformBufferShaderBindingInstance);
        PointLightShaderBindingInstanceArray.Add(View.Lights[PointLightIndex].ShaderBindingInstance);

        jShaderBindingsLayoutArray PointLightShaderBindingLayoutArray;
        for (int32 i = 0; i < PointLightShaderBindingInstanceArray.NumOfData; ++i)
        {
            PointLightShaderBindingLayoutArray.Add(PointLightShaderBindingInstanceArray[i]->ShaderBindingsLayouts);
        }

        // WorldMatrix 는 Push Constant 로 전달하는 것으로 하자
        struct jPointLightPushConstant
        {
            Matrix MVP;
        };
        static std::shared_ptr<TPushConstant<jPointLightPushConstant>> PointLightPushConstantPtr = std::make_shared<TPushConstant<jPointLightPushConstant>>(
            jPointLightPushConstant(), jPushConstantRange(EShaderAccessStageFlag::ALL, 0, sizeof(jPointLightPushConstant)));
        PointLightPushConstantPtr->Data.MVP = View.Camera->Projection * View.Camera->View
            * Matrix::MakeTranslate(PointLight->LightData.Position) * Matrix::MakeScale(Vector(PointLight->LightData.MaxDistance));

        // PointLight 의 경우 카메라에서 가장 먼쪽에 있는 Mesh 면을 렌더링하고, 그 면 부터 카메라 사이에 있는 공간에 대해서만 라이트를 적용함.
        auto PointLightRasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false>::Create();
        auto PointLightDepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create();

        jPipelineStateFixedInfo PointLightPipelineStateFixed = jPipelineStateFixedInfo(PointLightRasterizationState, MultisampleState, PointLightDepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        jPipelineStateInfo_Vulkan* PointLightPSO = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(&PointLightPipelineStateFixed, PointLightShader
            , PointLightSphereVertexBufferArray, InRenderPass, PointLightShaderBindingLayoutArray, PointLightPushConstantPtr.get(), SubpassIndex);
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // Spot light create pipeline
        static jUIQuadPrimitive* SpotLightUIQuad = jPrimitiveUtil::CreateUIQuad(Vector2(), Vector2(), nullptr);

        jSpotLight* SpotLight = nullptr;
        int32 SpotLightIndex = 0;
        for (int32 i = 0; i < View.Lights.size(); ++i)
        {
            if (View.Lights[i].Light->Type == ELightType::SPOT)
            {
                SpotLight = (jSpotLight*)View.Lights[i].Light;
                SpotLightIndex = i;
                break;
            }
        }

        jShader* SpotLightShader = nullptr;
        {
            jShaderInfo shaderInfo;
            shaderInfo.name = jNameStatic("SpotLightShader");
            shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/spotlight_vs.hlsl");
            shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/spotlight_fs.hlsl");
            if (gOptions.UseSubpass)
                shaderInfo.fsPreProcessor = jNameStatic("#define USE_SUBPASS 1");
            SpotLightShader = g_rhi->CreateShader(shaderInfo);
        }

        jVertexBufferArray SpotLightSphereVertexBufferArray;
        SpotLightSphereVertexBufferArray.Add(SpotLightUIQuad->RenderObject->VertexBuffer);

        jShaderBindingInstanceArray SpotLightShaderBindingInstanceArray;
        SpotLightShaderBindingInstanceArray.Add(GBufferShaderBindingInstance);
        SpotLightShaderBindingInstanceArray.Add(View.ViewUniformBufferShaderBindingInstance);
        SpotLightShaderBindingInstanceArray.Add(View.Lights[SpotLightIndex].ShaderBindingInstance);

        jShaderBindingsLayoutArray SpotLightShaderBindingLayoutArray;
        for (int32 i = 0; i < SpotLightShaderBindingInstanceArray.NumOfData; ++i)
        {
            SpotLightShaderBindingLayoutArray.Add(SpotLightShaderBindingInstanceArray[i]->ShaderBindingsLayouts);
        }

        // Rect 정보는 Push Constant 로 전달하는 것으로 하자
        struct jSpotLightPushConstant
        {
            Vector2 Pos;
            Vector2 Size;
            Vector2 PixelSize;
            float Depth;
            float padding;
        };
        static std::shared_ptr<TPushConstant<jSpotLightPushConstant>> SpotLightPushConstantPtr = std::make_shared<TPushConstant<jSpotLightPushConstant>>(
            jSpotLightPushConstant(), jPushConstantRange(EShaderAccessStageFlag::ALL, 0, sizeof(jSpotLightPushConstant)));

        Vector MinPos;
        Vector MaxPos;
        Vector2 ScreenSize;
        if (SpotLight && SpotLight->GetLightCamra())
        {
            ScreenSize.x = (float)RenderFrameContextPtr->SceneRenderTarget->ColorPtr->Info.Width;
            ScreenSize.y = (float)RenderFrameContextPtr->SceneRenderTarget->ColorPtr->Info.Height;
            SpotLight->GetLightCamra()->GetRectInScreenSpace(MinPos, MaxPos, View.Camera->GetViewProjectionMatrix(), ScreenSize);
        }
        SpotLightPushConstantPtr->Data.Pos = Vector2(MinPos.x, MinPos.y);
        SpotLightPushConstantPtr->Data.Size = Vector2(MaxPos.x - MinPos.x, MaxPos.y - MinPos.y);
        SpotLightPushConstantPtr->Data.PixelSize = Vector2(1.0f) / ScreenSize;
        SpotLightPushConstantPtr->Data.Depth = MaxPos.z;

        // SpotLight 의 경우 카메라에서 가장 먼쪽에 있는 Mesh 면을 렌더링하고(Quad 를 렌더링하기 때문에 MaxPos.z 로 컨트롤), 그 면 부터 카메라 사이에 있는 공간에 대해서만 라이트를 적용함.
        auto SpotLightDepthStencilState = TDepthStencilStateInfo<true, false, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create();
        jPipelineStateFixedInfo SpotLightPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, SpotLightDepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

        jPipelineStateInfo_Vulkan* SpotLightPSO = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(&SpotLightPipelineStateFixed, SpotLightShader
            , SpotLightSphereVertexBufferArray, InRenderPass, SpotLightShaderBindingLayoutArray, SpotLightPushConstantPtr.get(), SubpassIndex);
        //////////////////////////////////////////////////////////////////////////

        if (!gOptions.UseSubpass)
        {
            check(InRenderPass);
            InRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer);
        }

        {
            //////////////////////////////////////////////////////////////////////////
            // Directional light
            for (int32 i = 0; i < DirectionalLightShaderBindingInstanceArray.NumOfData; ++i)
                DirectionalLightShaderBindingInstanceArray[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)DirectionalLightPSO->GetPipelineLayoutHandle(), i);

            DirectionalLightPSO->Bind(RenderFrameContextPtr);

            FullscreenPrimitive->RenderObject->BindBuffers(RenderFrameContextPtr, false);
            FullscreenPrimitive->RenderObject->Draw(RenderFrameContextPtr, 0, -1, 1);
            //////////////////////////////////////////////////////////////////////////

            //////////////////////////////////////////////////////////////////////////
            // Point light
            for (int32 i = 0; i < PointLightShaderBindingInstanceArray.NumOfData; ++i)
                PointLightShaderBindingInstanceArray[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)PointLightPSO->GetPipelineLayoutHandle(), i);

            PointLightPSO->Bind(RenderFrameContextPtr);

            if (PointLightPushConstantPtr)
            {
                const std::vector<jPushConstantRange>* pushConstantRanges = PointLightPushConstantPtr->GetPushConstantRanges();
                if (ensure(pushConstantRanges))
                {
                    for (const auto& iter : *pushConstantRanges)
                    {
                        vkCmdPushConstants((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle(), PointLightPSO->vkPipelineLayout
                            , GetVulkanShaderAccessFlags(iter.AccessStageFlag), iter.Offset, iter.Size, PointLightPushConstantPtr->GetConstantData());
                    }
                }
            }

            PointLightSphere->RenderObject->BindBuffers(RenderFrameContextPtr, false);
            PointLightSphere->RenderObject->Draw(RenderFrameContextPtr, 0, -1, 1);
            //////////////////////////////////////////////////////////////////////////

            //////////////////////////////////////////////////////////////////////////
            // Spot light
            for (int32 i = 0; i < SpotLightShaderBindingInstanceArray.NumOfData; ++i)
                SpotLightShaderBindingInstanceArray[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)SpotLightPSO->GetPipelineLayoutHandle(), i);

            SpotLightPSO->Bind(RenderFrameContextPtr);

            if (SpotLightPushConstantPtr)
            {
                const std::vector<jPushConstantRange>* pushConstantRanges = SpotLightPushConstantPtr->GetPushConstantRanges();
                if (ensure(pushConstantRanges))
                {
                    for (const auto& iter : *pushConstantRanges)
                    {
                        vkCmdPushConstants((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle(), SpotLightPSO->vkPipelineLayout
                            , GetVulkanShaderAccessFlags(iter.AccessStageFlag), iter.Offset, iter.Size, SpotLightPushConstantPtr->GetConstantData());
                    }
                }
            }

            SpotLightUIQuad->RenderObject->BindBuffers(RenderFrameContextPtr, false);
            SpotLightUIQuad->RenderObject->Draw(RenderFrameContextPtr, 0, -1, 1);
            //////////////////////////////////////////////////////////////////////////
        }

        if (!gOptions.UseSubpass)
        {
            check(InRenderPass);
            InRenderPass->EndRenderPass();
        }
    }
}

void jRenderer::TranslucentPass()
{
    // SCOPE_CPU_PROFILE(TranslucentPass);
}

void jRenderer::PostProcess()
{
    SCOPE_CPU_PROFILE(PostProcess);
    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, TonemapCS);

        const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
        jCommandBuffer* CommandBuffer = RenderFrameContextPtr->CommandBuffer;
        jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTarget;

        //////////////////////////////////////////////////////////////////////////
        // Compute Pipeline
        g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);
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

        // Binding 1 : Target Image
        if (ensure(SceneRT->FinalColorPtr))
        {
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jTextureResource>(SceneRT->FinalColorPtr->GetTexture(), nullptr));
        }

        // Binding 2 : CommonComputeUniformBuffer
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
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::COMPUTE
                , ResourceInlineAllactor.Alloc<jUniformBufferResource>(&OneFrameUniformBuffer));
        }

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray);

        jShaderInfo shaderInfo;
        shaderInfo.name = jNameStatic("emboss");
        shaderInfo.cs = jNameStatic("Resource/Shaders/hlsl/tonemap_cs.hlsl");
        static jShader_Vulkan* Shader = (jShader_Vulkan*)g_rhi->CreateShader(shaderInfo);

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

void jRenderer::Render()
{
    SCOPE_CPU_PROFILE(Render);

    {
        SCOPE_CPU_PROFILE(PoolReset);
        check(RenderFrameContextPtr->CommandBuffer);
        ensure(RenderFrameContextPtr->CommandBuffer->Begin());
        if (g_rhi->GetQueryTimePool())
        {
            g_rhi->GetQueryTimePool()->ResetQueryPool(RenderFrameContextPtr->CommandBuffer);
        }
        if (g_rhi->GetQueryOcclusionPool())
        {
            g_rhi->GetQueryOcclusionPool()->ResetQueryPool(RenderFrameContextPtr->CommandBuffer);
        }

        //ShadowpassOcclusionTest.Init();
        BasepassOcclusionTest.Init();
    }

    Setup();
    ShadowPass();
    OpaquePass();
    TranslucentPass();
    PostProcess();

    jImGUI_Vulkan::Get().Draw(RenderFrameContextPtr);
    ensure(RenderFrameContextPtr->CommandBuffer->End());

}
