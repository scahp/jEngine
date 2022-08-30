#include "pch.h"
#include "jForwardRenderer.h"
#include "RHI/jRHI.h"
#include "Scene/jLight.h"
#include "RHI/jRenderTargetPool.h"
#include "Scene/jObject.h"
#include "jDrawCommand.h"
#include "jSceneRenderTargets.h"
#include "RHI/Vulkan/jShader_Vulkan.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/Vulkan/jUniformBufferBlock_Vulkan.h"
#include "RHI/jShaderBindingsLayout.h"
#include "RHI/jRHIType.h"

void jForwardRenderer::Setup()
{
    View.SetupUniformBuffer();

    // View 별로 저장 할 수 있어야 함
    if (View.DirectionalLight)
    {
        View.DirectionalLight->ShadowMapPtr = RenderFrameContextPtr->SceneRenderTarget->DirectionalLightShadowMapPtr;
        View.DirectionalLight->PrepareShaderBindingInstance();
    }

    ShadowPassSetupFinishEvent = std::async(std::launch::async, &jForwardRenderer::SetupShadowPass, this);
    BasePassSetupFinishEvent = std::async(std::launch::async, &jForwardRenderer::SetupBasePass, this);
}

void jForwardRenderer::SetupShadowPass()
{
    // Prepare shadowpass pipeline
    auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
    jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(EMSAASamples::COUNT_1);
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

    jPipelineStateFixedInfo ShadpwPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));

    {
        const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

        jAttachment depth = jAttachment(View.DirectionalLight->ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
            , ClearColor, ClearDepth, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);

        ShadowMapRenderPass = g_rhi->GetOrCreateRenderPass({}, depth, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }

    ShadowView.Camera = View.DirectionalLight->GetLightCamra();
    ShadowView.DirectionalLight = View.DirectionalLight;

    jShaderInfo shaderInfo;
    shaderInfo.name = jName("shadow_test");
    shaderInfo.vs = jName("Resource/Shaders/hlsl/shadow_instancing_vs.hlsl");
    shaderInfo.fs = jName("Resource/Shaders/hlsl/shadow_fs.hlsl");
    jShader* ShadowShader = g_rhi->CreateShader(shaderInfo);

    for (auto iter : jObject::GetStaticObject())
    {
        auto newCommand = jDrawCommand(RenderFrameContextPtr, &ShadowView, iter->RenderObject, ShadowMapRenderPass, ShadowShader, &ShadpwPipelineStateFixed, { });
        newCommand.PrepareToDraw(true);
        ShadowPasses.push_back(newCommand);
    }
}

void jForwardRenderer::SetupBasePass()
{
    // Prepare basepass pipeline
    auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
    jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(g_rhi->GetSelectedMSAASamples());
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo BasePassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));

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
            , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
    }

    if (resolve.IsValid())
    {
        OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass({ color }, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }
    else
    {
        OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass({ color }, depth, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }

    jShaderInfo shaderInfo;
    shaderInfo.name = jName("default_test");
    shaderInfo.vs = jName("Resource/Shaders/hlsl/shader_instancing_vs.hlsl");
    shaderInfo.fs = jName("Resource/Shaders/hlsl/shader_fs.hlsl");
    jShader* BasePassShader = g_rhi->CreateShader(shaderInfo);

    for (auto iter : jObject::GetStaticObject())
    {
        auto newCommand = jDrawCommand(RenderFrameContextPtr, &View, iter->RenderObject, OpaqueRenderPass, BasePassShader, &BasePassPipelineStateFixed, { });
        newCommand.PrepareToDraw(false);
        BasePasses.push_back(newCommand);
    }
}

void jForwardRenderer::Render()
{
    check(RenderFrameContextPtr->CommandBuffer);
    ensure(RenderFrameContextPtr->CommandBuffer->Begin());
    if (g_rhi->GetQueryPool())
    {
        g_rhi->GetQueryPool()->ResetQueryPool(RenderFrameContextPtr->CommandBuffer);
    }

    __super::Render();

    jImGUI_Vulkan::Get().Draw(RenderFrameContextPtr);
    ensure(RenderFrameContextPtr->CommandBuffer->End());
}

void jForwardRenderer::ShadowPass()
{
    ShadowPassSetupFinishEvent.wait();

    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, ShadowPass);

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, View.DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

        if (ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
        {
            for (auto& command : ShadowPasses)
            {
                command.Draw();
            }
            ShadowMapRenderPass->EndRenderPass();
        }

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, View.DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
    }
}

void jForwardRenderer::OpaquePass()
{
    BasePassSetupFinishEvent.wait();

    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BasePass);

        g_rhi->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);

        if (OpaqueRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
        {
            for (auto command : BasePasses)
            {
                command.Draw();
            }

            OpaqueRenderPass->EndRenderPass();
        }
    }

    // 여기 까지 렌더 로직 끝
    //////////////////////////////////////////////////////////////////////////
    {
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, ComputePass);
        
        const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
        jCommandBuffer* CommandBuffer = RenderFrameContextPtr->CommandBuffer;
        jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTarget;

        //////////////////////////////////////////////////////////////////////////
        // Compute Pipeline
        g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->FinalColorPtr->GetTexture(), EImageLayout::GENERAL);

        jShaderBindingInstance* CurrentBindingInstance = nullptr;
        int32 BindingPoint = 0;
        std::vector<jShaderBinding> ShaderBindings;

        // Binding 0 : Source Image
        if (ensure(SceneRT->ColorPtr))
        {
            ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , std::make_shared<jTextureResource>(SceneRT->ColorPtr->GetTexture(), nullptr)));
        }

        // Binding 1 : Target Image
        if (ensure(SceneRT->FinalColorPtr))
        {
            ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , std::make_shared<jTextureResource>(SceneRT->FinalColorPtr->GetTexture(), nullptr)));
        }

        // Binding 2 : CommonComputeUniformBuffer
        struct jCommonComputeUniformBuffer
        {
            float Width;
            float Height;
            Vector2 Padding;
        };
        jCommonComputeUniformBuffer CommonComputeUniformBuffer;
        CommonComputeUniformBuffer.Width = (float)SCR_WIDTH;
        CommonComputeUniformBuffer.Height = (float)SCR_HEIGHT;

        jUniformBufferBlock_Vulkan OneFrameUniformBuffer(jName("CommonComputeUniformBuffer"));
        OneFrameUniformBuffer.Init(sizeof(CommonComputeUniformBuffer));
        OneFrameUniformBuffer.UpdateBufferData(&CommonComputeUniformBuffer, sizeof(CommonComputeUniformBuffer));
        {
            ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::UNIFORMBUFFER
                , EShaderAccessStageFlag::COMPUTE, std::make_shared<jUniformBufferResource>(&OneFrameUniformBuffer)));
        }

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindings);

        jShaderInfo shaderInfo;
        shaderInfo.name = jName("emboss");
        shaderInfo.cs = jName("Resource/Shaders/hlsl/emboss_cs.hlsl");
        static jShader_Vulkan* Shader = (jShader_Vulkan*)g_rhi->CreateShader(shaderInfo);

        jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, { CurrentBindingInstance->ShaderBindingsLayouts });

        computePipelineStateInfo->Bind(RenderFrameContextPtr);
        // vkCmdBindPipeline((VkCommandBuffer)CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline[imageIndex]);

        CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

        Vector2i SwapchainExtent = g_rhi->GetSwapchain()->GetExtent();

        check(g_rhi->GetSwapchain());
        vkCmdDispatch((VkCommandBuffer)CommandBuffer->GetHandle()
            , SwapchainExtent.x / 16 + ((SwapchainExtent.x % 16) ? 1 : 0)
            , SwapchainExtent.y / 16 + ((SwapchainExtent.y % 16) ? 1 : 0), 1);
    }
    // End compute pipeline
}

void jForwardRenderer::TranslucentPass()
{

}
