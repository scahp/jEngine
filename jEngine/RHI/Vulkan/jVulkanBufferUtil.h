#pragma once
#include "jBuffer_Vulkan.h"

namespace jVulkanBufferUtil
{
VkFormat FindSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
VkFormat FindDepthFormat(VkPhysicalDevice physicalDevice);
uint32 FindMemoryType(VkPhysicalDevice physicalDevice, uint32 typeFilter, VkMemoryPropertyFlags properties);
bool HasStencilComponent(VkFormat format);

VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
VkImageView CreateImage2DArrayView(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
VkImageView CreateImageCubeView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
bool CreateImage2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);

FORCEINLINE bool CreateImage2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, jImage_Vulkan& OutImage)
{
    return CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, OutImage.Image, OutImage.ImageMemory);
}

FORCEINLINE bool CreateImage(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory)
{
    return CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, image, imageMemory);
}

FORCEINLINE bool CreateImage(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, jImage_Vulkan& OutImage)
{
    return CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, OutImage.Image, OutImage.ImageMemory);
}

FORCEINLINE bool CreateImageCube(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory)
{
    return CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, image, imageMemory);
}

FORCEINLINE bool CreateImageCube(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, jImage_Vulkan& OutImage)
{
    return CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, OutImage.Image, OutImage.ImageMemory);
}

size_t CreateBuffer(VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties, VkDeviceSize InSize, VkBuffer& OutBuffer, VkDeviceMemory& OutBufferMemory, uint64& OutAllocatedSize);

FORCEINLINE size_t CreateBuffer(VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties, VkDeviceSize InSize, jBuffer_Vulkan& OutBuffer)
{
    return CreateBuffer(InUsage, InProperties, InSize, OutBuffer.Buffer, OutBuffer.BufferMemory, OutBuffer.AllocatedSize);
}

void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height);
bool GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels);
bool CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
}