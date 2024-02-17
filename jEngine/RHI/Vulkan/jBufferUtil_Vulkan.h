#pragma once
#include "jBuffer_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"

namespace jBufferUtil_Vulkan
{
VkFormat FindSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
VkFormat FindDepthFormat(VkPhysicalDevice physicalDevice);
uint32 FindMemoryType(VkPhysicalDevice physicalDevice, uint32 typeFilter, VkMemoryPropertyFlags properties);
bool HasStencilComponent(VkFormat format);

VkImageView CreateTextureView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
VkImageView CreateTexture2DArrayView(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
VkImageView CreateTextureCubeView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);

VkImageView CreateTextureViewForSpecificMipMap(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex);
VkImageView CreateTexture2DArrayViewForSpecificMipMap(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex);
VkImageView CreateTextureCubeViewForSpecificMipMap(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex);

bool CreateTexture2DArray_LowLevel(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImageCreateFlagBits imageCreateFlagBits, VkImage& image, VkDeviceMemory& imageMemory);

FORCEINLINE std::shared_ptr<jTexture_Vulkan> CreateTexture2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageCreateFlagBits imageCreateFlags, VkImageLayout imageLayout)
{
    auto TexturePtr = std::make_shared<jTexture_Vulkan>();
    if (CreateTexture2DArray_LowLevel(width, height, arrayLayers, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, imageCreateFlags, TexturePtr->Image, TexturePtr->Memory))
    {
        TexturePtr->Type = ETextureType::TEXTURE_2D_ARRAY;
        TexturePtr->Width = width;
        TexturePtr->Height = height;
        TexturePtr->LayerCount = arrayLayers;
        TexturePtr->MipLevel = mipLevels;
        TexturePtr->SampleCount = (EMSAASamples)numSamples;
        TexturePtr->Format = GetVulkanTextureFormat(format);
        TexturePtr->Layout = GetVulkanImageLayout(imageLayout);

        const bool hasDepthAttachment = IsDepthFormat(TexturePtr->Format);
        const VkImageAspectFlagBits ImageAspectFlagBit = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        TexturePtr->View = jBufferUtil_Vulkan::CreateTextureView(TexturePtr->Image, format, ImageAspectFlagBit, mipLevels);
    }
    return TexturePtr;
}

FORCEINLINE bool Create2DTexture_LowLevel(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImageCreateFlagBits imageCreateFlags, VkImage& image, VkDeviceMemory& imageMemory)
{
    return CreateTexture2DArray_LowLevel(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, imageCreateFlags, image, imageMemory);
}

FORCEINLINE std::shared_ptr<jTexture_Vulkan> Create2DTexture(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageCreateFlagBits imageCreateFlags, VkImageLayout imageLayout)
{
    auto TexturePtr = std::make_shared<jTexture_Vulkan>();
    if (Create2DTexture_LowLevel(width, height, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, imageCreateFlags, TexturePtr->Image, TexturePtr->Memory))
    {
        TexturePtr->Type = ETextureType::TEXTURE_2D;
        TexturePtr->Width = width;
        TexturePtr->Height = height;
        TexturePtr->LayerCount = 1;
        TexturePtr->MipLevel = mipLevels;
        TexturePtr->SampleCount = (EMSAASamples)numSamples;
        TexturePtr->Format = GetVulkanTextureFormat(format);
        TexturePtr->Layout = GetVulkanImageLayout(imageLayout);

        const bool hasDepthAttachment = IsDepthFormat(TexturePtr->Format);
        const VkImageAspectFlagBits ImageAspectFlagBit = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        TexturePtr->View = jBufferUtil_Vulkan::CreateTextureView(TexturePtr->Image, format, ImageAspectFlagBit, mipLevels);
    }
    return TexturePtr;
}

FORCEINLINE bool CreateCubeTexture_LowLevel(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageCreateFlagBits imageCreateFlags, VkImageLayout imageLayout, VkImage& image, VkDeviceMemory& imageMemory)
{
    return CreateTexture2DArray_LowLevel(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, (VkImageCreateFlagBits)(imageCreateFlags | VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT), image, imageMemory);
}

FORCEINLINE std::shared_ptr<jTexture_Vulkan> CreateCubeTexture(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageCreateFlagBits imageCreateFlags, VkImageLayout imageLayout)
{
    auto TexturePtr = std::make_shared<jTexture_Vulkan>();
    if (CreateCubeTexture_LowLevel(width, height, mipLevels, numSamples, format, tiling, usage, properties, imageCreateFlags, imageLayout, TexturePtr->Image, TexturePtr->Memory))
    {
        TexturePtr->Type = ETextureType::TEXTURE_CUBE;
        TexturePtr->Width = width;
        TexturePtr->Height = height;
        TexturePtr->LayerCount = 6;
        TexturePtr->MipLevel = mipLevels;
        TexturePtr->SampleCount = (EMSAASamples)numSamples;
        TexturePtr->Format = GetVulkanTextureFormat(format);
        TexturePtr->Layout = GetVulkanImageLayout(imageLayout);

        const bool hasDepthAttachment = IsDepthFormat(TexturePtr->Format);
        const VkImageAspectFlagBits ImageAspectFlagBit = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        TexturePtr->View = jBufferUtil_Vulkan::CreateTextureCubeView(TexturePtr->Image, format, ImageAspectFlagBit, mipLevels);
    }
    return TexturePtr;
}
size_t CreateBuffer_LowLevel(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize, VkBuffer& OutBuffer, VkDeviceMemory& OutBufferMemory, uint64& OutAllocatedSize);
std::shared_ptr<jBuffer_Vulkan> CreateBuffer(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize, EResourceLayout InResourceLayout);

void CopyBufferToTexture(jCommandBuffer_Vulkan* commandBuffer_vk, VkBuffer buffer, uint64 bufferOffset, VkImage image, uint32 width, uint32 height, VkFormat InFormat, int32 miplevel = 0, int32 layerIndex = 0);
void GenerateMipmaps(jCommandBuffer_Vulkan* commandBuffer_vk, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels, uint32 layerCount
    , VkImageLayout oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void CopyBuffer(jCommandBuffer_Vulkan* commandBuffer_vk, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset);
void CopyBuffer(jCommandBuffer_Vulkan* commandBuffer_vk, const jBuffer_Vulkan& srcBuffer, const jBuffer_Vulkan& dstBuffer, VkDeviceSize size);

void CopyBufferToTexture(VkBuffer buffer, uint64 bufferOffset, VkImage image, uint32 width, uint32 height, VkFormat InFormat, int32 miplevel = 0, int32 layerIndex = 0);
void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels, uint32 layerCount
    , VkImageLayout oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset);
void CopyBuffer(const jBuffer_Vulkan& srcBuffer, const jBuffer_Vulkan& dstBuffer, VkDeviceSize size);
}