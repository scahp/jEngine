#include "pch.h"
#include "jResourceBarrierBatcher_Vulkan.h"
#include "jBuffer_Vulkan.h"

void jResourceBarrierBatcher_Vulkan::AddUAV(jBuffer* InBuffer)
{
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    jBarrier_Vulkan BarrierVulkan{};

    // todo : need to control pipeline stage
    BarrierVulkan.SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    BarrierVulkan.DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    BarrierVulkan.Barrier.MemroyBarrier = barrier;
    Barriers.emplace_back(BarrierVulkan);
}

void jResourceBarrierBatcher_Vulkan::AddUAV(jTexture* InTexture)
{
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    jBarrier_Vulkan BarrierVulkan{};

    // todo : need to control pipeline stage
    BarrierVulkan.SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    BarrierVulkan.DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    BarrierVulkan.Barrier.MemroyBarrier = barrier;
    Barriers.emplace_back(BarrierVulkan);
}

void jResourceBarrierBatcher_Vulkan::AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout)
{
    auto buffer_vk = (jBuffer_Vulkan*)InBuffer;
    VkImageLayout SrcLayout = GetVulkanImageLayout(InBuffer->GetLayout());
    VkImageLayout DstLayout = GetVulkanImageLayout(InNewLayout);

    if (SrcLayout == DstLayout)
        return;

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
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;		// todo : need to control this when the pipeline stage control is not All stage bits
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

    jBarrier_Vulkan BarrierVulkan{};
    
    // todo : need to control pipeline stage
    BarrierVulkan.SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    BarrierVulkan.DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    BarrierVulkan.Barrier.BufferBarrier = barrier;
    Barriers.emplace_back(BarrierVulkan);
}

void jResourceBarrierBatcher_Vulkan::AddTransition(jTexture* InTexture, EResourceLayout InNewLayout)
{
    auto texture_vk = (jTexture_Vulkan*)InTexture;
    VkImageLayout SrcLayout = GetVulkanImageLayout(InTexture->GetLayout());
    VkImageLayout DstLayout = GetVulkanImageLayout(InNewLayout);

    if (SrcLayout == DstLayout)
        return;

    // Layout Transition 에는 image memory barrier 사용
    // Pipeline barrier는 리소스들 간의 synchronize 를 맞추기 위해 사용 (버퍼를 읽기전에 쓰기가 완료되는 것을 보장받기 위해)
    // Pipeline barrier는 image layout 간의 전환과 VK_SHARING_MODE_EXCLUSIVE를 사용한 queue family ownership을 전달하는데에도 사용됨

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = SrcLayout;		// 현재 image 내용이 존재하던말던 상관없다면 VK_IMAGE_LAYOUT_UNDEFINED 로 하면 됨
    barrier.newLayout = DstLayout;

    // 아래 두필드는 Barrier를 사용해 Queue family ownership을 전달하는 경우에 사용됨.
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;		// todo : need to control this when the pipeline stage control is not All stage bits
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
    //if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    //{
    //    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    //    if (jVulkanBufferUtil::HasStencilComponent(format))
    //        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    //}
    //else
    //{
    //    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    //}
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = texture_vk->MipLevel;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture_vk->LayerCount;
    barrier.srcAccessMask = 0;	// TODO
    barrier.dstAccessMask = 0;	// TODO


    //  // Barrier 는 동기화를 목적으로 사용하므로, 이 리소스와 연관되는 어떤 종류의 명령이 이전에 일어나야 하는지와
    //  // 어떤 종류의 명령이 Barrier를 기다려야 하는지를 명시해야만 한다. vkQueueWaitIdle 을 사용하지만 그래도 수동으로 해줘야 함.

    //  // Undefined -> transfer destination : 이 경우 기다릴 필요없이 바로 쓰면됨. Undefined 라 다른 곳에서 딱히 쓰거나 하는것이 없음.
    //  // Transfer destination -> frag shader reading : frag 쉐이더에서 읽기 전에 transfer destination 에서 쓰기가 완료 됨이 보장되어야 함. 그래서 shader 에서 읽기 가능.
    //  VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    //  VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    //  if ((oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) && (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
    //  {
    //      // 이전 작업을 기다릴 필요가 없어서 srcAccessMask에 0, sourceStage 에 가능한 pipeline stage의 가장 빠른 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
    //      barrier.srcAccessMask = 0;
    //      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;	// VK_ACCESS_TRANSFER_WRITE_BIT 는 Graphics 나 Compute stage에 실제 stage가 아닌 pseudo-stage 임.

    //      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    //      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    //  }
    //  else if ((oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) && (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    //  {
    //      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    //      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    //      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    //      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    //  }
    //  else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    //  {
    //      barrier.srcAccessMask = 0;
    //      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    //      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    //      // 둘중 가장 빠른 stage 인 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 를 선택
    //      // VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 에서 depth read 가 일어날 수 있음.
    //      // VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT 에서 depth write 가 일어날 수 있음.
    //      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    //  }
    //  else
    //  {
          //if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
          //{
          //	barrier.srcAccessMask = 0;
          //}
          //else
          //{
          //	check(!"Unsupported layout transition!");
          //	return false;
          //}
    //  }

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

    jBarrier_Vulkan BarrierVulkan{};

    // todo : need to control pipeline stage
    BarrierVulkan.SrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    BarrierVulkan.DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    BarrierVulkan.Barrier.ImageBarrier = barrier;
    Barriers.emplace_back(BarrierVulkan);
}

void jResourceBarrierBatcher_Vulkan::Flush(jCommandBuffer* InCommandBuffer)
{

}
