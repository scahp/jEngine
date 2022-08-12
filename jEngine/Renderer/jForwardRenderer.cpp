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

void jForwardRenderer::Setup()
{
    SetupShadowPass();
    SetupBasePass();
}

void jForwardRenderer::SetupShadowPass()
{
    // View 별로 저장 할 수 있어야 함
    View.DirectionalLight->ShadowMapPtr = RenderFrameContextPtr->SceneRenderTarget->DirectionalLightShadowMapPtr;
    View.DirectionalLight->PrepareShaderBindingInstance();

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
    shaderInfo.vs = jName("Resource/Shaders/hlsl/shadow_vs.hlsl");
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
    shaderInfo.vs = jName("Resource/Shaders/hlsl/shader_vs.hlsl");
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

void jForwardRenderer::OpaquePass()
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

    // 여기 까지 렌더 로직 끝
    //////////////////////////////////////////////////////////////////////////
    const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
    jCommandBuffer* CommandBuffer = RenderFrameContextPtr->CommandBuffer;
    jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTarget;

    g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);
    g_rhi->TransitionImageLayout(CommandBuffer, SceneRT->FinalColorPtr->GetTexture(), EImageLayout::GENERAL);

    static jShaderBindingInstance* ShaderBindingInstance[3] = { nullptr };
    jShaderBindingInstance*& CurrentBindingInstance = ShaderBindingInstance[RenderFrameContextPtr->FrameIndex];
    if (!CurrentBindingInstance)
    {
        int32 BindingPoint = 0;
        std::vector<jShaderBinding> ShaderBindings;
        if (ensure(SceneRT->ColorPtr))
        {
            ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                , std::make_shared<jTextureResource>(SceneRT->ColorPtr->GetTexture(), nullptr)));
        }
        if (ensure(SceneRT->FinalColorPtr))
        {
            ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
                , std::make_shared<jTextureResource>(SceneRT->FinalColorPtr->GetTexture(), nullptr)));
        }

        CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindings);
        CurrentBindingInstance->UpdateShaderBindings(ShaderBindings);
    }

    //static VkPipeline pipeline[3] = { nullptr };
    //static VkPipelineLayout pipelineLayout[3] = { nullptr };
    //if (!pipeline[imageIndex])
    //{
    //    pipelineLayout[imageIndex] = (VkPipelineLayout)g_rhi->CreatePipelineLayout({ CurrentBindingInstance->ShaderBindings });

    //    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    //    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    //    computePipelineCreateInfo.layout = (VkPipelineLayout)pipelineLayout[imageIndex];
    //    computePipelineCreateInfo.flags = 0;

    //    jShaderInfo shaderInfo;
    //    shaderInfo.name = jName("emboss");
    //    shaderInfo.cs = jName("Resource/Shaders/hlsl/emboss_cs.hlsl");
    //    static jShader_Vulkan* shader = (jShader_Vulkan*)g_rhi->CreateShader(shaderInfo);
    //    computePipelineCreateInfo.stage = shader->ShaderStages[0];

    //    verify(VK_SUCCESS == vkCreateComputePipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache
    //        , 1, &computePipelineCreateInfo, nullptr, &pipeline[imageIndex]));
    //}

    jShaderInfo shaderInfo;
    shaderInfo.name = jName("emboss");
    shaderInfo.cs = jName("Resource/Shaders/hlsl/emboss_cs.hlsl");
    static jShader_Vulkan* Shader = (jShader_Vulkan*)g_rhi->CreateShader(shaderInfo);

    jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, { CurrentBindingInstance->ShaderBindings });

    computePipelineStateInfo->Bind(RenderFrameContextPtr);
    // vkCmdBindPipeline((VkCommandBuffer)CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline[imageIndex]);

    CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

    check(g_rhi->GetSwapchain());
    vkCmdDispatch((VkCommandBuffer)CommandBuffer->GetHandle()
        , g_rhi->GetSwapchain()->GetExtent().x / 16, g_rhi->GetSwapchain()->GetExtent().y / 16, 1);
}

void jForwardRenderer::TranslucentPass()
{

}
