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
    auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
    jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(EMSAASamples::COUNT_1);
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

    jPipelineStateFixedInfo CurrentPipelineStateFixed_Shadow = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));

    {
        const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

        jAttachment depth = jAttachment(View.DirectionalLight->ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
            , ClearColor, ClearDepth, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);

        ShadowMapRenderPass = (jRenderPass_Vulkan*)g_rhi_vk->GetOrCreateRenderPass({}, depth, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }
    
    ShadowView.Camera = View.DirectionalLight->GetLightCamra();
    ShadowView.DirectionalLight = View.DirectionalLight;

    // Shadow
    static jShader* Shader = nullptr;
    if (!Shader)
    {
        jShaderInfo shaderInfo;
        shaderInfo.name = jName("shadow_test");
        shaderInfo.vs = jName("Resource/Shaders/hlsl/shadow_vs.hlsl");
        shaderInfo.fs = jName("Resource/Shaders/hlsl/shadow_fs.hlsl");
        Shader = g_rhi->CreateShader(shaderInfo);
    }

    for (auto iter : jObject::GetStaticObject())
    {
        auto newCommand = jDrawCommand(&ShadowView, iter->RenderObject, ShadowMapRenderPass, Shader, &CurrentPipelineStateFixed_Shadow, { });
        newCommand.PrepareToDraw(true);
        ShadowPasses.push_back(newCommand);
    }

    //////////////////////////////////////////////////////////////////////////
    {
        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
        jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create((EMSAASamples)g_rhi_vk->MsaaSamples);
        auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

        g_rhi_vk->CurrentPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));
    }

    const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

    jAttachment color = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
        , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
    jAttachment depth = jAttachment(RenderFrameContextPtr->SceneRenderTarget->DepthPtr, EAttachmentLoadStoreOp::CLEAR_DONTCARE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
        , EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);
    jAttachment resolve;

    if (g_rhi_vk->MsaaSamples > 1)
    {
        resolve = jAttachment(RenderFrameContextPtr->SceneRenderTarget->ResolvePtr, EAttachmentLoadStoreOp::DONTCARE_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
            , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
    }

    if (resolve.IsValid())
    {
        OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi_vk->GetOrCreateRenderPass({ color }, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }
    else
    {
        OpaqueRenderPass = (jRenderPass_Vulkan*)g_rhi_vk->GetOrCreateRenderPass({ color }, depth, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
    }
}

void jForwardRenderer::Render()
{
    check(RenderFrameContextPtr->CommandBuffer);
    ensure(RenderFrameContextPtr->CommandBuffer->Begin());
    g_rhi_vk->QueryPool.ResetQueryPool(RenderFrameContextPtr->CommandBuffer);

    __super::Render();

    jImGUI_Vulkan::Get().Draw(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->FrameIndex);
    ensure(RenderFrameContextPtr->CommandBuffer->End());
}

void jForwardRenderer::ShadowPass()
{
    SCOPE_GPU_PROFILE(ShadowPass);

    g_rhi_vk->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, View.DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

    if (ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->CommandBuffer))
    {
        for (auto& command : ShadowPasses)
        {
            command.Draw();
        }
        ShadowMapRenderPass->EndRenderPass();
    }

    g_rhi_vk->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, View.DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
}

void jForwardRenderer::OpaquePass()
{
    SCOPE_GPU_PROFILE(BasePass);

    g_rhi_vk->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::COLOR_ATTACHMENT);

    static jShader* Shader = nullptr;
    if (!Shader)
    {
        jShaderInfo shaderInfo;
        shaderInfo.name = jName("default_test");
        shaderInfo.vs = jName("Resource/Shaders/hlsl/shader_vs.hlsl");
        shaderInfo.fs = jName("Resource/Shaders/hlsl/shader_fs.hlsl");
        Shader = g_rhi->CreateShader(shaderInfo);
    }

    std::vector<jDrawCommand> BasePasses;
    for (auto iter : jObject::GetStaticObject())
    {
        auto newCommand = jDrawCommand(&View, iter->RenderObject, OpaqueRenderPass, Shader, &g_rhi_vk->CurrentPipelineStateFixed, { });
        newCommand.PrepareToDraw(false);
        BasePasses.push_back(newCommand);
    }

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
    const int32 imageIndex = (int32)g_rhi_vk->CurrenFrameIndex;

    g_rhi_vk->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetTexture(), EImageLayout::SHADER_READ_ONLY);
    g_rhi_vk->TransitionImageLayout(RenderFrameContextPtr->CommandBuffer, RenderFrameContextPtr->SceneRenderTarget->FinalColorPtr->GetTexture(), EImageLayout::GENERAL);

    static VkDescriptorSetLayout descriptorSetLayout[3] = { nullptr };

    if (!descriptorSetLayout[imageIndex])
    {
        VkDescriptorSetLayoutBinding setLayoutBindingReadOnly{};
        setLayoutBindingReadOnly.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        setLayoutBindingReadOnly.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        setLayoutBindingReadOnly.binding = 0;
        setLayoutBindingReadOnly.descriptorCount = 1;

        VkDescriptorSetLayoutBinding setLayoutBindingWriteOnly{};
        setLayoutBindingWriteOnly.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        setLayoutBindingWriteOnly.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        setLayoutBindingWriteOnly.binding = 1;
        setLayoutBindingWriteOnly.descriptorCount = 1;

        std::vector<VkDescriptorSetLayoutBinding> setlayoutBindings = {
            setLayoutBindingReadOnly,		// Binding 0 : Input image (read-only)
            setLayoutBindingWriteOnly,		// Binding 1: Output image (write)
        };

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.pBindings = setlayoutBindings.data();
        descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setlayoutBindings.size());
        verify(VK_SUCCESS == vkCreateDescriptorSetLayout(g_rhi_vk->Device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout[imageIndex]));
    }

    static VkPipelineLayout pipelineLayout[3] = { nullptr };
    if (!pipelineLayout[imageIndex])
    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout[imageIndex];
        verify(VK_SUCCESS == vkCreatePipelineLayout(g_rhi_vk->Device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout[imageIndex]));
    }

    static VkDescriptorPool descriptorPool[3] = { nullptr };
    if (!descriptorPool[imageIndex])
    {
        VkDescriptorPoolSize descriptorPoolSize{};
        descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorPoolSize.descriptorCount = 3;

        VkDescriptorPoolSize descriptorPoolSize2{};
        descriptorPoolSize2.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorPoolSize2.descriptorCount = 3;

        std::vector<VkDescriptorPoolSize> poolSizes = {
            descriptorPoolSize,descriptorPoolSize2
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount = (uint32)poolSizes.size();
        descriptorPoolInfo.pPoolSizes = poolSizes.data();
        descriptorPoolInfo.maxSets = 3;
        verify(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &descriptorPoolInfo, nullptr, &descriptorPool[imageIndex]));
    }

    static VkDescriptorSet descriptorSet[3] = { nullptr };
    if (!descriptorSet[imageIndex])
    {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool[imageIndex];
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout[imageIndex];
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        verify(VK_SUCCESS == vkAllocateDescriptorSets(g_rhi_vk->Device, &descriptorSetAllocateInfo, &descriptorSet[imageIndex]));

        VkDescriptorImageInfo colorImageInfo{};
        colorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorImageInfo.imageView = (VkImageView)RenderFrameContextPtr->SceneRenderTarget->ColorPtr->GetViewHandle();
        colorImageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();

        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSet[imageIndex];
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pImageInfo = &colorImageInfo;
        writeDescriptorSet.descriptorCount = 1;

        VkDescriptorImageInfo targetImageInfo{};
        targetImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        targetImageInfo.imageView = (VkImageView)RenderFrameContextPtr->SceneRenderTarget->FinalColorPtr->GetViewHandle();
        targetImageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();

        VkWriteDescriptorSet writeDescriptorSet2{};
        writeDescriptorSet2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet2.dstSet = descriptorSet[imageIndex];
        writeDescriptorSet2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDescriptorSet2.dstBinding = 1;
        writeDescriptorSet2.pImageInfo = &targetImageInfo;
        writeDescriptorSet2.descriptorCount = 1;

        std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets = {
            writeDescriptorSet,
            writeDescriptorSet2
        };
        vkUpdateDescriptorSets(g_rhi_vk->Device, (uint32)computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, nullptr);
    }

    static VkPipeline pipeline[3] = { nullptr };
    if (!pipeline[imageIndex])
    {
        VkComputePipelineCreateInfo computePipelineCreateInfo{};
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.layout = pipelineLayout[imageIndex];
        computePipelineCreateInfo.flags = 0;

        jShaderInfo shaderInfo;
        shaderInfo.name = jName("emboss");
        shaderInfo.cs = jName("Resource/Shaders/hlsl/emboss_cs.hlsl");
        static jShader_Vulkan* shader = (jShader_Vulkan*)g_rhi_vk->CreateShader(shaderInfo);
        computePipelineCreateInfo.stage = shader->ShaderStages[0];

        verify(VK_SUCCESS == vkCreateComputePipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline[imageIndex]));
    }

    vkCmdBindPipeline((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline[imageIndex]);
    vkCmdBindDescriptorSets((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout[imageIndex], 0, 1, &descriptorSet[imageIndex], 0, 0);
    vkCmdDispatch((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle(), g_rhi_vk->Swapchain->Extent.x / 16, g_rhi_vk->Swapchain->Extent.y / 16, 1);
}

void jForwardRenderer::TranslucentPass()
{

}
