#pragma once
#include "jImage_Vulkan.h"

struct jTexture_Vulkan : public jTexture
{
    jTexture_Vulkan() = default;
    jTexture_Vulkan(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight, bool InSRGB, int32 InLayerCount, int32 InSampleCount
        , VkImage InImage, VkImageView InImageView, VkDeviceMemory InImageMemory = nullptr, VkSampler InSamplerState = nullptr)
        : jTexture(InType, InFormat, InWidth, InHeight, InLayerCount, InSampleCount, InSRGB), Image(InImage, InImageView, InImageMemory), SamplerState(InSamplerState)
    {}
    jImage_Vulkan Image;
    VkSampler SamplerState = nullptr;

    virtual const void* GetHandle() const override { return Image.Image; }
    virtual const void* GetViewHandle() const override { return Image.ImageView; }
    virtual const void* GetSamplerStateHandle() const override { return SamplerState; }

    static VkSampler CreateDefaultSamplerState();
};
