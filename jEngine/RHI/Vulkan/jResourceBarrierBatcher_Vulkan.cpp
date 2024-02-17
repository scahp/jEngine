#include "pch.h"
#include "jResourceBarrierBatcher_Vulkan.h"
#include "jBuffer_Vulkan.h"

void jResourceBarrierBatcher_Vulkan::AddUAV(jBuffer* InBuffer)
{
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;       // Global memroy invalidation
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	// todo : need to control pipeline stage
	const auto SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	const auto DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	AddBarrier(SrcStage, DstStage, barrier);
}

void jResourceBarrierBatcher_Vulkan::AddUAV(jTexture* InTexture)
{
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;       // Global memroy invalidation
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	// todo : need to control pipeline stage
	const auto SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	const auto DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	AddBarrier(SrcStage, DstStage, barrier);
}

void jResourceBarrierBatcher_Vulkan::AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout)
{
    auto buffer_vk = (jBuffer_Vulkan*)InBuffer;
    VkImageLayout SrcLayout = GetVulkanImageLayout(InBuffer->GetLayout());
    VkImageLayout DstLayout = GetVulkanImageLayout(InNewLayout);

    if (SrcLayout == DstLayout)
        return;

    buffer_vk->Layout = InNewLayout;

    auto GetAccessFlagBits = [](VkImageLayout InLayout) -> VkAccessFlagBits
        {
            VkAccessFlagBits AccessFlagBits{};
            switch (InLayout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
            case VK_IMAGE_LAYOUT_GENERAL:
                AccessFlagBits = (VkAccessFlagBits)(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                AccessFlagBits = VK_ACCESS_TRANSFER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                AccessFlagBits = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                AccessFlagBits = VK_ACCESS_HOST_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
                AccessFlagBits = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            case VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR:
            case VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR:
            case VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR:
            case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
            case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
            case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
                check(0);	// this is only for image
                break;
            }
            return AccessFlagBits;
        };

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    
    // todo : Need to control this if I use VK_SHARING_MODE_EXCLUSIVE(VkSharingMode).
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.buffer = buffer_vk->Buffer;
    barrier.offset = buffer_vk->Offset;
    barrier.size = buffer_vk->RealBufferSize;
    barrier.srcAccessMask = GetAccessFlagBits(SrcLayout);
    barrier.dstAccessMask = GetAccessFlagBits(DstLayout);

    // Image is read by a shader
    // Make sure any shader reads from the image have been finished
    if (!!(barrier.dstAccessMask & VK_ACCESS_SHADER_READ_BIT))
    {
        if (barrier.srcAccessMask == 0)
        {
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        }
    }

	// todo : need to control pipeline stage
	const auto SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	const auto DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	AddBarrier(SrcStage, DstStage, barrier);
}

void jResourceBarrierBatcher_Vulkan::AddTransition(jTexture* InTexture, EResourceLayout InNewLayout)
{
    auto texture_vk = (jTexture_Vulkan*)InTexture;

    // VkImageView 가 VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL 를 지원하지 않기 때문에 추가
    if (texture_vk->IsDepthFormat() && (EResourceLayout::DEPTH_READ_ONLY == InNewLayout || EResourceLayout::SHADER_READ_ONLY == InNewLayout))
    {
        InNewLayout = EResourceLayout::DEPTH_STENCIL_READ_ONLY;
    }

    VkImageLayout SrcLayout = GetVulkanImageLayout(InTexture->GetLayout());
    VkImageLayout DstLayout = GetVulkanImageLayout(InNewLayout);

    if (SrcLayout == DstLayout)
        return;

    texture_vk->Layout = InNewLayout;

    // Layout Transition 에는 image memory barrier 사용
    // Pipeline barrier는 리소스들 간의 synchronize 를 맞추기 위해 사용 (버퍼를 읽기전에 쓰기가 완료되는 것을 보장받기 위해)
    // Pipeline barrier는 image layout 간의 전환과 VK_SHARING_MODE_EXCLUSIVE를 사용한 queue family ownership을 전달하는데에도 사용됨

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = SrcLayout;		// 현재 image 내용이 존재하던말던 상관없다면 VK_IMAGE_LAYOUT_UNDEFINED 로 하면 됨
    barrier.newLayout = DstLayout;

    // todo : Need to control this if I use VK_SHARING_MODE_EXCLUSIVE(VkSharingMode).
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = texture_vk->Image;

    // subresourcerange 는 image에서 영향을 받는 것과 부분을 명세함.
    // mimapping 이 없으므로 levelCount와 layercount 를 1로
    switch (DstLayout)
    {
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
        if (IsDepthOnlyFormat(texture_vk->Format))
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;		// VkImageView 가 VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL 를 지원하지 않기 때문에 추가
        else if (IsDepthFormat(texture_vk->Format))
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        else
            check(0);
        break;
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
        if (IsDepthOnlyFormat(texture_vk->Format))
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        else
            check(0);
        break;
    default:
        if (IsDepthOnlyFormat(texture_vk->Format))
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        else if (IsDepthFormat(texture_vk->Format))
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        else
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        break;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = texture_vk->MipLevel;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture_vk->LayerCount;
    barrier.srcAccessMask = 0;	// TODO
    barrier.dstAccessMask = 0;	// TODO

    // Source layouts (old)
    // Source access mask controls actions that have to be finished on the old layout
    // before it will be transitioned to the new layout
    switch (SrcLayout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        // Image layout is undefined (or does not matter)
        // Only valid as initial layout
        // No flags required, listed only for completeness
        barrier.srcAccessMask = 0;
        break;

    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        // Image is preinitialized
        // Only valid as initial layout for linear images, preserves memory contents
        // Make sure host writes have been finished
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image is a color attachment
        // Make sure any writes to the color buffer have been finished
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image is a depth/stencil attachment
        // Make sure any writes to the depth/stencil buffer have been finished
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image is a transfer source
        // Make sure any reads from the image have been finished
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image is a transfer destination
        // Make sure any writes to the image have been finished
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Image is read by a shader
        // Make sure any shader reads from the image have been finished
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        // Other source layouts aren't handled (yet)
        break;
    }

    // Target layouts (new)
    // Destination access mask controls the dependency for the new image layout
    switch (DstLayout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Image will be used as a transfer destination
        // Make sure any writes to the image have been finished
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Image will be used as a transfer source
        // Make sure any reads from the image have been finished
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Image will be used as a color attachment
        // Make sure any writes to the color buffer have been finished
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        // Image layout will be used as a depth/stencil attachment
        // Make sure any writes to depth/stencil buffer have been finished
        barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
        // Image will be read in a shader (sampler, input attachment)
        // Make sure any writes to the image have been finished
        if (barrier.srcAccessMask == 0)
        {
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        // Other source layouts aren't handled (yet)
        break;
    }

	// todo : need to control pipeline stage
	const auto SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	const auto DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	AddBarrier(SrcStage, DstStage, barrier);
}

void jResourceBarrierBatcher_Vulkan::Flush(const jCommandBuffer* InCommandBuffer)
{
	if (BarriersList.empty())
		return;

    check(InCommandBuffer);
    check(!InCommandBuffer->IsEnd());
    auto commandbuffer_vk = (const jCommandBuffer_Vulkan*)InCommandBuffer;

    for (const jBarrier_Vulkan& iter : BarriersList)
    {
		vkCmdPipelineBarrier(commandbuffer_vk->GetRef()
			, iter.SrcStage
			, iter.DstStage
			, 0
			, (uint32)iter.MemroyBarriers.size(), iter.MemroyBarriers.data()
			, (uint32)iter.BufferBarriers.size(), iter.BufferBarriers.data()
			, (uint32)iter.ImageBarriers.size(), iter.ImageBarriers.data()
		);
    }

    ClearBarriers();
}

void jResourceBarrierBatcher_Vulkan::ClearBarriers()
{
    CurrentBarriers = nullptr;
    BarriersList.clear();
}
