#pragma once
#include "jBuffer_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"

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
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImage& image, VkDeviceMemory& imageMemory);

FORCEINLINE bool CreateImage2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, jTexture_Vulkan& OutTexture)
{
    if (CreateImage2DArray(width, height, arrayLayers, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, OutTexture.Image, OutTexture.Memory))
    {
        OutTexture.Type = ETextureType::TEXTURE_2D_ARRAY;
        OutTexture.Width = width;
        OutTexture.Height = height;
        OutTexture.LayerCount = arrayLayers;
        OutTexture.MipLevel = mipLevels;
        OutTexture.SampleCount = (EMSAASamples)numSamples;
        OutTexture.Format = GetVulkanTextureFormat(format);
        OutTexture.Layout = GetVulkanImageLayout(imageLayout);
        return true;
    }
    return false;
}

FORCEINLINE bool CreateImage(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImage& image, VkDeviceMemory& imageMemory)
{
    return CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, image, imageMemory);
}

FORCEINLINE bool CreateImage(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, jTexture_Vulkan& OutTexture)
{
    if (CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, OutTexture.Image, OutTexture.Memory))
    {
        OutTexture.Type = ETextureType::TEXTURE_2D;
        OutTexture.Width = width;
        OutTexture.Height = height;
        OutTexture.LayerCount = 1;
        OutTexture.MipLevel = mipLevels;
        OutTexture.SampleCount = (EMSAASamples)numSamples;
        OutTexture.Format = GetVulkanTextureFormat(format);
        OutTexture.Layout = GetVulkanImageLayout(imageLayout);
        return true;
    }
    return false;
}

FORCEINLINE bool CreateImageCube(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImage& image, VkDeviceMemory& imageMemory)
{
    return CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, image, imageMemory);
}

FORCEINLINE bool CreateImageCube(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, jTexture_Vulkan& OutTexture)
{
    if (CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, OutTexture.Image, OutTexture.Memory))
    {
        OutTexture.Type = ETextureType::TEXTURE_CUBE;
        OutTexture.Width = width;
        OutTexture.Height = height;
        OutTexture.LayerCount = 6;
        OutTexture.MipLevel = mipLevels;
        OutTexture.SampleCount = (EMSAASamples)numSamples;
        OutTexture.Format = GetVulkanTextureFormat(format);
        OutTexture.Layout = GetVulkanImageLayout(imageLayout);
        return true;
    }
    return false;
}

size_t CreateBuffer(VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties, VkDeviceSize InSize, VkBuffer& OutBuffer, VkDeviceMemory& OutBufferMemory, uint64& OutAllocatedSize);

FORCEINLINE size_t CreateBuffer(VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties, VkDeviceSize InSize, jBuffer_Vulkan& OutBuffer)
{
    return CreateBuffer(InUsage, InProperties, InSize, OutBuffer.Buffer, OutBuffer.BufferMemory, OutBuffer.AllocatedSize);
}

void CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32 width, uint32 height);
void GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels
    , VkImageLayout oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void CopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height);
void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels
    , VkImageLayout oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
}