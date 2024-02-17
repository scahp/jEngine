#include "pch.h"
#include "jRenderPass_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"

void jRenderPass_Vulkan::Release()
{
    if (FrameBuffer)
    {
        vkDestroyFramebuffer(g_rhi_vk->Device, FrameBuffer, nullptr);
        FrameBuffer = nullptr;
    }

    if (RenderPass)
    {
        vkDestroyRenderPass(g_rhi_vk->Device, RenderPass, nullptr);
        RenderPass = nullptr;
    }
}

bool jRenderPass_Vulkan::IsValidRenderTargets() const
{
    for (const jAttachment& iter : RenderPassInfo.Attachments)
    {
        if (iter.RenderTargetPtr.expired())
            return false;
    }
	return true;
}

bool jRenderPass_Vulkan::BeginRenderPass(const jCommandBuffer* commandBuffer)
{
    if (!ensure(commandBuffer))
        return false;

#if USE_RESOURCE_BARRIER_BATCHER
    g_rhi->GetGlobalBarrierBatcher()->Flush(commandBuffer);
    commandBuffer->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    CommandBuffer = commandBuffer;

    check(FrameBuffer);
    RenderPassBeginInfo.framebuffer = FrameBuffer;

    // 커맨드를 기록하는 명령어는 prefix로 모두 vkCmd 가 붙으며, 리턴값은 void 로 에러 핸들링은 따로 안함.
    // VK_SUBPASS_CONTENTS_INLINE : 렌더 패스 명령이 Primary 커맨드 버퍼에 포함되며, Secondary 커맨드 버퍼는 실행되지 않는다.
    // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : 렌더 패스 명령이 Secondary 커맨드 버퍼에서 실행된다.
    vkCmdBeginRenderPass((VkCommandBuffer)commandBuffer->GetHandle(), &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

void jRenderPass_Vulkan::EndRenderPass()
{
    ensure(CommandBuffer);

    // Finishing up
    vkCmdEndRenderPass((VkCommandBuffer)CommandBuffer->GetHandle());

    // Apply layout to attachments
    for(jAttachment& iter : RenderPassInfo.Attachments)
    {
        check(iter.IsValid());
        SetFinalLayoutToAttachment(iter);
    }

    CommandBuffer = nullptr;
}

void jRenderPass_Vulkan::SetFinalLayoutToAttachment(const jAttachment& attachment) const
{
    check(!attachment.RenderTargetPtr.expired());
    jTexture_Vulkan* texture_vk = (jTexture_Vulkan*)attachment.RenderTargetPtr.lock()->GetTexture();
    texture_vk->Layout = attachment.FinalLayout;
}

void jRenderPass_Vulkan::Initialize()
{
    CreateRenderPass();
}

bool jRenderPass_Vulkan::CreateRenderPass()
{
    int32 SampleCount = 0;
    int32 LayerCount = 0;

    // Create RenderPass
    {
        int32 DepthAttachmentIndex = -1;
        std::vector<VkAttachmentDescription> AttachmentDescs;
        AttachmentDescs.resize(RenderPassInfo.Attachments.size());
        for (int32 i = 0; i < (int32)RenderPassInfo.Attachments.size(); ++i)
        {
            const jAttachment& attachment = RenderPassInfo.Attachments[i];
            check(attachment.IsValid());

            const auto& RTInfo = attachment.RenderTargetPtr.lock()->Info;

            VkAttachmentDescription& attachmentDesc = AttachmentDescs[i];
            attachmentDesc.format = GetVulkanTextureFormat(RTInfo.Format);
            attachmentDesc.samples = (VkSampleCountFlagBits)RTInfo.SampleCount;
            GetVulkanAttachmentLoadStoreOp(attachmentDesc.loadOp, attachmentDesc.storeOp, attachment.LoadStoreOp);
            GetVulkanAttachmentLoadStoreOp(attachmentDesc.stencilLoadOp, attachmentDesc.stencilStoreOp, attachment.StencilLoadStoreOp);

            check((attachment.IsResolveAttachment() && ((int32)RTInfo.SampleCount > 1)) || (!attachment.IsResolveAttachment() && (int32)RTInfo.SampleCount == 1));
            const bool IsInvalidSampleCountAndLayerCount = (SampleCount == 0) || (LayerCount == 0);
            if (!attachment.IsDepthAttachment() || IsInvalidSampleCountAndLayerCount)
            {
                check(SampleCount == 0 || SampleCount == (int32)RTInfo.SampleCount);
                check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
                SampleCount = (int32)RTInfo.SampleCount;
                LayerCount = RTInfo.LayerCount;
            }
            attachmentDesc.initialLayout = GetVulkanImageLayout(attachment.InitialLayout);
            attachmentDesc.finalLayout = GetVulkanImageLayout(attachment.FinalLayout);

            const auto& RTClearColor = attachment.RTClearValue.GetCleraColor();
            VkClearValue clearValue = {};
            if (attachment.IsDepthAttachment())
                clearValue.depthStencil = { attachment.RTClearValue.GetCleraDepth(), attachment.RTClearValue.GetCleraStencil() };
            else
                clearValue.color = { RTClearColor[0], RTClearColor[1], RTClearColor[2], RTClearColor[3] };
            ClearValues.push_back(clearValue);

            check(Hash != 0);
            attachment.RenderTargetPtr.lock()->RelatedRenderPassHashes.push_back(Hash);
        }

        struct jSubpassAttachmentRefs
        {                
            std::vector<VkAttachmentReference> InputAttachmentRefs;
            std::vector<VkAttachmentReference> OutputColorAttachmentRefs;
            std::optional<VkAttachmentReference> OutputDepthAttachmentRef;
            std::optional<VkAttachmentReference> OutputResolveAttachmentRef;
        };

        check(RenderPassInfo.Subpasses.size());

        std::vector<jSubpassAttachmentRefs> subpassAttachmentRefs;
        subpassAttachmentRefs.resize(RenderPassInfo.Subpasses.size());

        std::vector<VkSubpassDescription> SubpassDescs;
        SubpassDescs.resize(RenderPassInfo.Subpasses.size());

        std::vector<VkSubpassDependency> SubpassDependencies;
        SubpassDependencies.resize(RenderPassInfo.Subpasses.size() + 1);

        const bool IsSubpassForExecuteInOrder = RenderPassInfo.IsSubpassForExecuteInOrder();

        int32 DependencyIndex = 0;
        SubpassDependencies[DependencyIndex].srcSubpass = VK_SUBPASS_EXTERNAL;
        SubpassDependencies[DependencyIndex].dstSubpass = 0;
        SubpassDependencies[DependencyIndex].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        SubpassDependencies[DependencyIndex].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        SubpassDependencies[DependencyIndex].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        SubpassDependencies[DependencyIndex].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        SubpassDependencies[DependencyIndex].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        ++DependencyIndex;

        for(int32 i=0;i<(int32)RenderPassInfo.Subpasses.size();++i)
        {
            const jSubpass& subPass = RenderPassInfo.Subpasses[i];

            std::vector<VkAttachmentReference>& InputAttachmentRefs = subpassAttachmentRefs[i].InputAttachmentRefs;
            std::vector<VkAttachmentReference>& OutputColorAttachmentRefs = subpassAttachmentRefs[i].OutputColorAttachmentRefs;
            std::optional<VkAttachmentReference>& OutputDepthAttachmentRef = subpassAttachmentRefs[i].OutputDepthAttachmentRef;
            std::optional<VkAttachmentReference>& OutputResolveAttachmentRef = subpassAttachmentRefs[i].OutputResolveAttachmentRef;

            for(const int32& attachmentIndex : subPass.InputAttachments)
            {
                const VkAttachmentReference attchmentRef = { (uint32)attachmentIndex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                InputAttachmentRefs.emplace_back(attchmentRef);
            }
            for (const int32& attachmentIndex : subPass.OutputColorAttachments)
            {
                const VkAttachmentReference attchmentRef = { (uint32)attachmentIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
                OutputColorAttachmentRefs.emplace_back(attchmentRef);
            }
            if (subPass.OutputDepthAttachment)
            {
                // If you need to Read-only shader resource and read-only depth attachment for the same depth texture. VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL will be used.
                const VkAttachmentReference attchmentRef = { (uint32)subPass.OutputDepthAttachment.value()
                    , subPass.DepthAttachmentReadOnly ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
                OutputDepthAttachmentRef = attchmentRef;
            }
            if (subPass.OutputResolveAttachment)
            {
                const VkAttachmentReference attchmentRef = { (uint32)subPass.OutputDepthAttachment.value(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
                OutputResolveAttachmentRef = attchmentRef;
            }

            SubpassDescs[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            if (InputAttachmentRefs.size() > 0)
            {
                SubpassDescs[i].inputAttachmentCount = (uint32)InputAttachmentRefs.size();
                SubpassDescs[i].pInputAttachments = InputAttachmentRefs.data();
            }
            if (OutputColorAttachmentRefs.size() > 0)
            {
                SubpassDescs[i].colorAttachmentCount = (uint32)OutputColorAttachmentRefs.size();
                SubpassDescs[i].pColorAttachments = OutputColorAttachmentRefs.data();
            }
            if (OutputDepthAttachmentRef)
            {
                SubpassDescs[i].pDepthStencilAttachment = &OutputDepthAttachmentRef.value();
            }
            if (OutputResolveAttachmentRef)
            {
                SubpassDescs[i].pResolveAttachments = &OutputResolveAttachmentRef.value();
            }

            // This dependency transitions the input attachment from color attachment to shader read
            if (IsSubpassForExecuteInOrder)
            {
                SubpassDependencies[DependencyIndex].srcSubpass = (uint32)(i);
                SubpassDependencies[DependencyIndex].dstSubpass = (uint32)(i + 1);
            }
            else
            {
                SubpassDependencies[DependencyIndex].srcSubpass = (uint32)subPass.SourceSubpassIndex;
                SubpassDependencies[DependencyIndex].dstSubpass = (uint32)subPass.DestSubpassIndex;
            }
            SubpassDependencies[DependencyIndex].srcStageMask = GetPipelineStageMask(subPass.AttachmentProducePipelineBit);
            SubpassDependencies[DependencyIndex].dstStageMask = GetPipelineStageMask(subPass.AttachmentComsumePipelineBit);
            SubpassDependencies[DependencyIndex].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            SubpassDependencies[DependencyIndex].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            SubpassDependencies[DependencyIndex].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            ++DependencyIndex;
        }

        --DependencyIndex;
        SubpassDependencies[DependencyIndex].srcSubpass = 0;
        SubpassDependencies[DependencyIndex].dstSubpass = VK_SUBPASS_EXTERNAL;
        SubpassDependencies[DependencyIndex].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        SubpassDependencies[DependencyIndex].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        SubpassDependencies[DependencyIndex].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        SubpassDependencies[DependencyIndex].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        SubpassDependencies[DependencyIndex].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo = {};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = static_cast<uint32>(AttachmentDescs.size());
        renderPassCreateInfo.pAttachments = AttachmentDescs.data();
        renderPassCreateInfo.subpassCount = (uint32)SubpassDescs.size();
        renderPassCreateInfo.pSubpasses = SubpassDescs.data();
        renderPassCreateInfo.dependencyCount = (uint32)SubpassDependencies.size();
        renderPassCreateInfo.pDependencies = SubpassDependencies.data();

        if (!ensure(vkCreateRenderPass(g_rhi_vk->GetDevice(), &renderPassCreateInfo, nullptr, &RenderPass) == VK_SUCCESS))
            return false;
    }

    // Create RenderPassBeginInfo
    RenderPassBeginInfo = {};
    RenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassBeginInfo.renderPass = RenderPass;

    // 렌더될 영역이며, 최상의 성능을 위해 attachment의 크기와 동일해야함.
    RenderPassBeginInfo.renderArea.offset = { RenderOffset.x, RenderOffset.y };
    RenderPassBeginInfo.renderArea.extent = { (uint32)RenderExtent.x, (uint32)RenderExtent.y };

    RenderPassBeginInfo.clearValueCount = static_cast<uint32>(ClearValues.size());
    RenderPassBeginInfo.pClearValues = ClearValues.data();

    // Create framebuffer
    {
        std::vector<VkImageView> ImageViews;

        for (int32 k = 0; k < RenderPassInfo.Attachments.size(); ++k)
        {
            check(RenderPassInfo.Attachments[k].IsValid());

            const auto* RT = RenderPassInfo.Attachments[k].RenderTargetPtr.lock().get();
            check(RT);

            const jTexture_Vulkan* texture_vk = (const jTexture_Vulkan*)RT->GetTexture();
            check(texture_vk);

            ImageViews.push_back(texture_vk->View);
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = RenderPass;

        // RenderPass와 같은 수와 같은 타입의 attachment 를 사용
        framebufferInfo.attachmentCount = static_cast<uint32>(ImageViews.size());
        framebufferInfo.pAttachments = ImageViews.data();

        framebufferInfo.width = RenderExtent.x;
        framebufferInfo.height = RenderExtent.y;
        framebufferInfo.layers = LayerCount;			// 이미지 배열의 레이어수(3D framebuffer에 사용할 듯)

        if (!ensure(vkCreateFramebuffer(g_rhi_vk->Device, &framebufferInfo, nullptr, &FrameBuffer) == VK_SUCCESS))
            return false;
    }

    return true;
}
