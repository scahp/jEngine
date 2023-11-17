#pragma once

struct jTexture_Vulkan : public jTexture
{
    jTexture_Vulkan() = default;
    jTexture_Vulkan(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight, int32 InLayerCount, EMSAASamples InSampleCount, int32 InMipLevel, bool InSRGB
        , VkImage InImage, VkImageView InImageView, VkDeviceMemory InImageMemory = nullptr, EImageLayout InImageLayout = EImageLayout::UNDEFINED, VkSampler InSamplerState = nullptr)
        : jTexture(InType, InFormat, InWidth, InHeight, InLayerCount, InSampleCount, InMipLevel, InSRGB), Image(InImage), View(InImageView), Memory(InImageMemory)
        , Layout(InImageLayout), SamplerState(InSamplerState)
    {}
    virtual ~jTexture_Vulkan()
    {
        ReleaseInternal();
    }
    VkImage Image = nullptr;
    VkImageView View = nullptr;
    VkDeviceMemory Memory = nullptr;
    EImageLayout Layout = EImageLayout::UNDEFINED;
    VkSampler SamplerState = nullptr;

    void ReleaseInternal();
    virtual void* GetHandle() const override { return Image; }
    virtual void* GetSamplerStateHandle() const override { return SamplerState; }
    virtual void Release() override;
    virtual EImageLayout GetLayout() const override { return Layout; }

    static VkSampler CreateDefaultSamplerState();
    static void DestroyDefaultSamplerState();
};
