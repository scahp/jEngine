#include "pch.h"
#include "jBufferUtil_Vulkan.h"

namespace jBufferUtil_Vulkan
{

VkFormat FindSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        // props.linearTilingFeatures : Linear tiling 지원여부
        // props.optimalTilingFeatures : Optimal tiling 지원여부
        // props.bufferFeatures : 버퍼를 지원하는 경우
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            return format;
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            return format;
    }
    check(0);
    return VK_FORMAT_UNDEFINED;
}

VkFormat FindDepthFormat(VkPhysicalDevice physicalDevice)
{
    // VK_FORMAT_D32_SFLOAT : 32bit signed float for depth
    // VK_FORMAT_D32_SFLOAT_S8_UINT : 32bit signed float depth, 8bit stencil
    // VK_FORMAT_D24_UNORM_S8_UINT : 24bit float for depth, 8bit stencil

    return FindSupportedFormat(physicalDevice, { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }
    , VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

uint32 FindMemoryType(VkPhysicalDevice physicalDevice, uint32 typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32 i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    check(0);	// failed to find sutable memory type!
    return 0;
}

bool HasStencilComponent(VkFormat format)
{
    return ((format == VK_FORMAT_D32_SFLOAT_S8_UINT) || (format == VK_FORMAT_D24_UNORM_S8_UINT));
}

VkImageView CreateTextureView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo = {};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;

    // SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
    // 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
    // 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageView imageView;
    ensure(vkCreateImageView(g_rhi_vk->Device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

    return imageView;
}

VkImageView CreateTexture2DArrayView(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo = {};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;

    // SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    // RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
    // 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
    // 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageView imageView;
    ensure(vkCreateImageView(g_rhi_vk->Device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

    return imageView;
}

VkImageView CreateTextureCubeView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels)
{
    VkImageViewCreateInfo viewInfo = {};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;

    // SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    // RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
    // 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
    // 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageView imageView;
    ensure(vkCreateImageView(g_rhi_vk->Device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

    return imageView;
}

VkImageView CreateTextureViewForSpecificMipMap(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex)
{
    VkImageViewCreateInfo viewInfo = {};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;

    // SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = mipLevelIndex;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
    // 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
    // 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageView imageView;
    ensure(vkCreateImageView(g_rhi_vk->Device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

    return imageView;
}

VkImageView CreateTexture2DArrayViewForSpecificMipMap(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex)
{
    VkImageViewCreateInfo viewInfo = {};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;

    // SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = mipLevelIndex;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    // RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
    // 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
    // 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageView imageView;
    ensure(vkCreateImageView(g_rhi_vk->Device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

    return imageView;
}

VkImageView CreateTextureCubeViewForSpecificMipMap(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex)
{
    VkImageViewCreateInfo viewInfo = {};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;

    // SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = mipLevelIndex;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    // RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
    // 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
    // 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageView imageView;
    ensure(vkCreateImageView(g_rhi_vk->Device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

    return imageView;
}

bool CreateTexture2DArray_LowLevel(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImageCreateFlagBits imageCreateFlagBits, VkImage& image, VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;

    // VK_IMAGE_TILING_LINEAR : 텍셀이 Row-major 순서로 저장. pixels array 처럼.
    // VK_IMAGE_TILING_OPTIMAL : 텍셀이 최적의 접근을 위한 순서로 저장
    // image의 memory 안에 있는 texel에 직접 접근해야한다면, VK_IMAGE_TILING_LINEAR 를 써야함.
    // 그러나 지금 staging image가 아닌 staging buffer를 사용하기 때문에 VK_IMAGE_TILING_OPTIMAL 를 쓰면됨.
    imageInfo.tiling = tiling;

    // VK_IMAGE_LAYOUT_UNDEFINED : GPU에 의해 사용될 수 없으며, 첫번째 transition 에서 픽셀들을 버릴 것임.
    // VK_IMAGE_LAYOUT_PREINITIALIZED : GPU에 의해 사용될 수 없으며, 첫번째 transition 에서 픽셀들이 보존 될것임.
    // VK_IMAGE_LAYOUT_GENERAL : 성능은 좋지 않지만 image를 input / output 둘다 의 용도로 사용하는 경우 씀.
    // 첫번째 전환에서 텍셀이 보존되어야 하는 경우는 거의 없음.
    //	-> 이런 경우는 image를 staging image로 하고 VK_IMAGE_TILING_LINEAR를 쓴 경우이며, 이때는 Texel 데이터를
    //		image에 업로드하고, image를 transfer source로 transition 하는 경우가 됨.
    // 하지만 지금의 경우는 첫번째 transition에서 image는 transfer destination 이 된다. 그래서 기존 데이터를 유지
    // 할 필요가 없다 그래서 VK_IMAGE_LAYOUT_UNDEFINED 로 설정한다.
    imageInfo.initialLayout = imageLayout;
    imageInfo.usage = usage;

    imageInfo.samples = numSamples;
    imageInfo.flags = imageCreateFlagBits;		// Optional
                                                // Sparse image 에 대한 정보를 설정가능
                                                // Sparse image는 특정 영역의 정보를 메모리에 담아두는 것임. 예를들어 3D image의 경우
                                                // 복셀영역의 air 부분의 표현을 위해 많은 메모리를 할당하는것을 피하게 해줌.

    if (!ensure(vkCreateImage(g_rhi_vk->Device, &imageInfo, nullptr, &image) == VK_SUCCESS))
        return false;

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_rhi_vk->Device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = jBufferUtil_Vulkan::FindMemoryType(g_rhi_vk->PhysicalDevice, memRequirements.memoryTypeBits, properties);
    if (!ensure(vkAllocateMemory(g_rhi_vk->Device, &allocInfo, nullptr, &imageMemory) == VK_SUCCESS))
        return false;

    vkBindImageMemory(g_rhi_vk->Device, image, imageMemory, 0);

    return true;
}

size_t CreateBuffer_LowLevel(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize, VkBuffer& OutBuffer, VkDeviceMemory& OutBufferMemory, uint64& OutAllocatedSize)
{
    check(InSize);
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = InSize;

    // OR 비트연산자를 사용해 다양한 버퍼용도로 사용할 수 있음.
    bufferInfo.usage = GetVulkanBufferBits(InUsage);

    // swapchain과 마찬가지로 버퍼또한 특정 queue family가 소유하거나 혹은 여러 Queue에서 공유됨
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!ensure(vkCreateBuffer(g_rhi_vk->Device, &bufferInfo, nullptr, &OutBuffer) == VK_SUCCESS))
        return 0;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_rhi_vk->Device, OutBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = jBufferUtil_Vulkan::FindMemoryType(g_rhi_vk->PhysicalDevice, memRequirements.memoryTypeBits
        , GetVulkanMemoryPropertyFlagBits(InProperties));

    VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
    if (!!(InUsage & EVulkanBufferBits::SHADER_DEVICE_ADDRESS))
    {
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
        allocInfo.pNext = &allocFlagsInfo;
    }

    if (!ensure(vkAllocateMemory(g_rhi_vk->Device, &allocInfo, nullptr, &OutBufferMemory) == VK_SUCCESS))
        return 0;

    OutAllocatedSize = memRequirements.size;

    // 마지막 파라메터 0은 메모리 영역의 offset 임.
    // 이 값이 0이 아니면 memRequirements.alignment 로 나눠야 함. (align 되어있다는 의미)
    verify(VK_SUCCESS == vkBindBufferMemory(g_rhi_vk->Device, OutBuffer, OutBufferMemory, 0));

    return memRequirements.size;
}

std::shared_ptr<jBuffer_Vulkan> CreateBuffer(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize, EResourceLayout InResourceLayout)
{
    auto BufferPtr = std::make_shared<jBuffer_Vulkan>();
    BufferPtr->RealBufferSize = InSize;
#if USE_VK_MEMORY_POOL
    check(g_rhi->GetMemoryPool());
    const jMemory& Memory = g_rhi->GetMemoryPool()->Alloc(InUsage, InProperties, InSize);
    BufferPtr->InitializeWithMemory(Memory);
#else
    CreateBuffer_LowLevel(InUsage, InProperties, InSize, BufferPtr->Buffer, BufferPtr->BufferMemory, BufferPtr->AllocatedSize);
#endif

    if (BufferPtr->Layout != InResourceLayout)
    {
        g_rhi->TransitionLayout(BufferPtr.get(), InResourceLayout);
    }

    return BufferPtr;
}

void CopyBufferToTexture(jCommandBuffer_Vulkan* commandBuffer_vk, VkBuffer buffer, uint64 bufferOffset, VkImage image, uint32 width, uint32 height, int32 miplevel, int32 layerIndex)
{
    check(commandBuffer_vk);
#if USE_RESOURCE_BARRIER_BATCHER
    g_rhi->GetGlobalBarrierBatcher()->Flush(commandBuffer_vk);
    commandBuffer_vk->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    VkBufferImageCopy region = {};
    region.bufferOffset = bufferOffset;

    // 아래 2가지는 얼마나 많은 pixel이 들어있는지 설명, 둘다 0, 0이면 전체
    region.bufferRowLength = width;
    region.bufferImageHeight = height;

    // 아래 부분은 이미지의 어떤 부분의 픽셀을 복사할지 명세
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = miplevel;
    region.imageSubresource.baseArrayLayer = layerIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer_vk->GetRef(), buffer, image
        , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL		// image가 현재 어떤 레이아웃으로 사용되는지 명세
        , 1, &region);
}

void GenerateMipmaps(jCommandBuffer_Vulkan* commandBuffer_vk, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels, uint32 layerCount
    , VkImageLayout oldLayout, VkImageLayout newLayout)
{
    check(commandBuffer_vk);
#if USE_RESOURCE_BARRIER_BATCHER
    commandBuffer_vk->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER


    for (uint32 k = 0; k < layerCount; ++k)
    {
        VkImageMemoryBarrier barrier = { };
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = k;
        barrier.subresourceRange.layerCount = 1;

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;
        for (uint32 i = 1; i < mipLevels; ++i)
        {
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer_vk->GetRef(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0
                , 0, nullptr
                , 0, nullptr
                , 1, &barrier);

            VkImageBlit blit = {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = k;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = k;
            blit.dstSubresource.layerCount = 1;

            // 멀티샘플된 source or dest image는 사용할 수 없으며, 그러려면 vkCmdResolveImage를 사용해야함.
            // 만약 VK_FILTER_LINEAR를 사용한다면, vkCmdBlitImage를 사용하기 위해서 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT 를 srcImage가 포함해야함.
            vkCmdBlitImage(commandBuffer_vk->GetRef(), image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                , image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = newLayout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer_vk->GetRef(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0
                , 0, nullptr
                , 0, nullptr
                , 1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // 마지막 mipmap 은 source 로 사용되지 않아서 아래와 같이 루프 바깥에서 Barrier 를 추가로 해줌.
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer_vk->GetRef(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0
            , 0, nullptr
            , 0, nullptr
            , 1, &barrier);
    }
}

void CopyBuffer(jCommandBuffer_Vulkan* commandBuffer_vk, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
    check(commandBuffer_vk);
#if USE_RESOURCE_BARRIER_BATCHER
    commandBuffer_vk->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 커맨드버퍼를 1번만 쓰고, 복사가 다 될때까지 기다리기 위해서 사용

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = srcOffset;		// Optional
    copyRegion.dstOffset = dstOffset;		// Optional
    copyRegion.size = size;			// 여기서는 VK_WHOLE_SIZE 사용 불가
    vkCmdCopyBuffer(commandBuffer_vk->GetRef(), srcBuffer, dstBuffer, 1, &copyRegion);
}

void CopyBuffer(jCommandBuffer_Vulkan* commandBuffer_vk, const jBuffer_Vulkan& srcBuffer, const jBuffer_Vulkan& dstBuffer, VkDeviceSize size)
{
    check(srcBuffer.AllocatedSize >= size && dstBuffer.AllocatedSize >= size);
    CopyBuffer(commandBuffer_vk, srcBuffer.Buffer, dstBuffer.Buffer, size, srcBuffer.Offset, dstBuffer.Offset);
}

void CopyBufferToTexture(VkBuffer buffer, uint64 bufferOffset, VkImage image, uint32 width, uint32 height, int32 miplevel, int32 layerIndex)
{
    auto commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
    CopyBufferToTexture(commandBuffer, buffer, bufferOffset, image, width, height, miplevel, layerIndex);
    g_rhi_vk->EndSingleTimeCommands(commandBuffer);
}

void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels, uint32 layerCount
    , VkImageLayout oldLayout, VkImageLayout newLayout)
{
    //VkFormatProperties formatProperties;
    //vkGetPhysicalDeviceFormatProperties(g_rhi_vk->PhysicalDevice, imageFormat, &formatProperties);
    //if (!ensure(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    //    return false;

    auto commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
    GenerateMipmaps(commandBuffer, image, imageFormat, texWidth, texHeight, mipLevels, layerCount, oldLayout, newLayout);
    g_rhi_vk->EndSingleTimeCommands(commandBuffer);
}

void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
     // 임시 커맨드 버퍼를 통해서 메모리를 전송함.
    auto commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
    CopyBuffer(commandBuffer, srcBuffer, dstBuffer, size, srcOffset, dstOffset);
    g_rhi_vk->EndSingleTimeCommands(commandBuffer);
}

void CopyBuffer(const jBuffer_Vulkan& srcBuffer, const jBuffer_Vulkan& dstBuffer, VkDeviceSize size)
{
    check(srcBuffer.AllocatedSize >= size && dstBuffer.AllocatedSize >= size);
    auto commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
    CopyBuffer(commandBuffer, srcBuffer.Buffer, dstBuffer.Buffer, size, srcBuffer.Offset, dstBuffer.Offset);
    g_rhi_vk->EndSingleTimeCommands(commandBuffer);
}

}
