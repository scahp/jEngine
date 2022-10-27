#include "pch.h"
#include "jRenderer.h"
#include "Scene/jLight.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "jOptions.h"
#include "RHI/Vulkan/jUniformBufferBlock_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"

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

    View.SetupUniformBuffer();

    // View 별로 저장 할 수 있어야 함
    if (View.DirectionalLight)
    {
        View.DirectionalLight->ShadowMapPtr = RenderFrameContextPtr->SceneRenderTarget->DirectionalLightShadowMapPtr;
        View.DirectionalLight->PrepareShaderBindingInstance();
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

    // Prepare shadowpass pipeline
    auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
    jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(EMSAASamples::COUNT_1);
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

    jPipelineStateFixedInfo ShadpwPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), false);

    {
        const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

        jAttachment depth = jAttachment(View.DirectionalLight->ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
            , ClearColor, ClearDepth, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);

        // Setup attachment
        jRenderPassInfo renderPassInfo;
        renderPassInfo.Attachments.push_back(depth);

        // Setup subpass of ShadowPass
        jSubpass subpass;
        subpass.SourceSubpassIndex = 0;
        subpass.DestSubpassIndex = 1;
        subpass.OutputDepthAttachment = 0;
        subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
        renderPassInfo.Subpasses.push_back(subpass);

        ShadowMapRenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

        // This is an example of creating RenderPass without setting subpasses
        // ShadowMapRenderPass = g_rhi->GetOrCreateRenderPass({}, depth, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }

    ShadowView.Camera = View.DirectionalLight->GetLightCamra();
    ShadowView.DirectionalLight = View.DirectionalLight;

    jShader* ShadowShader = nullptr;
    {
        jShaderInfo shaderInfo;
        shaderInfo.name = jNameStatic("shadow_test");
        shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/shadow_vs.hlsl");
        shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/shadow_fs.hlsl");
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
    ShadowPasses.resize(jObject::GetStaticObject().size());
    jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetStaticObject()
        , [&](size_t InIndex, const jObject* InObject)
    {
        new (&ShadowPasses[InIndex]) jDrawCommand(RenderFrameContextPtr, &ShadowView, InObject->RenderObject, ShadowMapRenderPass
            , (InObject->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, {}, nullptr);
        ShadowPasses[InIndex].PrepareToDraw(true);
    });
#else
    ShadowPasses.resize(jObject::GetStaticObject().size());
    int32 i = 0;
    for (auto iter : jObject::GetStaticObject())
    {
        new (&ShadowPasses[i]) jDrawCommand(RenderFrameContextPtr, &ShadowView, iter->RenderObject, ShadowMapRenderPass
            , (iter->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, {}, nullptr);
        ShadowPasses[i].PrepareToDraw(true);
        ++i;
    }
#endif
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

    jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
        , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTarget->DepthPtr, EAttachmentLoadStoreOp::CLEAR_DONTCARE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
        , EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
    {
        resolve = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
            , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT, true);
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    renderPassInfo.Attachments.push_back(color);
    renderPassInfo.Attachments.push_back(depth);
    if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        renderPassInfo.Attachments.push_back(resolve);

    // Setup subpass of ShadowPass
    jSubpass subpass;
    subpass.SourceSubpassIndex = 0;
    subpass.DestSubpassIndex = 1;
    subpass.OutputColorAttachments.push_back(0);
    subpass.OutputDepthAttachment = 1;
    if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        subpass.OutputResolveAttachment = 2;
    subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
    renderPassInfo.Subpasses.push_back(subpass);

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
        shaderInfo.vs = jNameStatic("Resource/Shaders/hlsl/shader_vs.hlsl");
        shaderInfo.fs = jNameStatic("Resource/Shaders/hlsl/shader_fs.hlsl");
        if (gOptions.UseVRS)
            shaderInfo.fsPreProcessor = jNameStatic("#define USE_VARIABLE_SHADING_RATE 1");
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

    {
        SCOPE_CPU_PROFILE(ShadowPass);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, ShadowPass);

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, View.DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

        if (ShadowMapRenderPass && ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
        {
            ShadowpassOcclusionTest.BeginQuery(RenderFrameContextPtr->CommandBuffer);
            for (const auto& command : ShadowPasses)
            {
                command.Draw();
            }
            ShadowpassOcclusionTest.EndQuery(RenderFrameContextPtr->CommandBuffer);
            ShadowMapRenderPass->EndRenderPass();
        }

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, View.DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
    }
}

void jRenderer::OpaquePass()
{
    if (BasePassSetupFinishEvent.valid())
        BasePassSetupFinishEvent.wait();

    {
        SCOPE_CPU_PROFILE(OpaquePass);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BasePass);

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);

        if (OpaqueRenderPass && OpaqueRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
        {
            BasepassOcclusionTest.BeginQuery(RenderFrameContextPtr->CommandBuffer);
            for (const auto& command : BasePasses)
            {
                command.Draw();
            }
            BasepassOcclusionTest.EndQuery(RenderFrameContextPtr->CommandBuffer);
            OpaqueRenderPass->EndRenderPass();
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
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, CopyCS);

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
        shaderInfo.cs = jNameStatic("Resource/Shaders/hlsl/copy_cs.hlsl");
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

        ShadowpassOcclusionTest.Init();
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
