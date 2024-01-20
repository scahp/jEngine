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

VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
VkImageView CreateImage2DArrayView(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);
VkImageView CreateImageCubeView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels);

VkImageView CreateImageViewForSpecificMipMap(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex);
VkImageView CreateImage2DArrayViewForSpecificMipMap(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex);
VkImageView CreateImageCubeViewForSpecificMipMap(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevelIndex);

bool CreateImage2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, VkImageCreateFlagBits imageCreateFlagBits, VkImage& image, VkDeviceMemory& imageMemory);

FORCEINLINE bool CreateImage2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, jTexture_Vulkan& OutTexture)
{
    if (CreateImage2DArray(width, height, arrayLayers, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, VkImageCreateFlagBits(0), OutTexture.Image, OutTexture.Memory))
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
    return CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, VkImageCreateFlagBits(0), image, imageMemory);
}

FORCEINLINE bool CreateImage(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, jTexture_Vulkan& OutTexture)
{
    if (CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, VkImageCreateFlagBits(0), OutTexture.Image, OutTexture.Memory))
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
    return CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, image, imageMemory);
}

FORCEINLINE bool CreateImageCube(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
    , VkMemoryPropertyFlags properties, VkImageLayout imageLayout, jTexture_Vulkan& OutTexture)
{
    if (CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, imageLayout, VkImageCreateFlagBits(0), OutTexture.Image, OutTexture.Memory))
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
size_t CreateBuffer_LowLevel(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize, VkBuffer& OutBuffer, VkDeviceMemory& OutBufferMemory, uint64& OutAllocatedSize);
std::shared_ptr<jBuffer_Vulkan> CreateBuffer(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize);

void CopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64 bufferOffset, VkImage image, uint32 width, uint32 height, int32 miplevel = 0, int32 layerIndex = 0);
void GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels, uint32 layerCount
    , VkImageLayout oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void CopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset);
void CopyBuffer(VkCommandBuffer commandBuffer, const jBuffer_Vulkan& srcBuffer, const jBuffer_Vulkan& dstBuffer, VkDeviceSize size);

void CopyBufferToImage(VkBuffer buffer, uint64 bufferOffset, VkImage image, uint32 width, uint32 height, int32 miplevel = 0, int32 layerIndex = 0);
void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels, uint32 layerCount
    , VkImageLayout oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset);
void CopyBuffer(const jBuffer_Vulkan& srcBuffer, const jBuffer_Vulkan& dstBuffer, VkDeviceSize size);
}