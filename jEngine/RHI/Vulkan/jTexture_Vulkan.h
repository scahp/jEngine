#pragma once

struct jTexture_Vulkan : public jTexture
{
    jTexture_Vulkan() = default;
    jTexture_Vulkan(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight, bool InSRGB, int32 InLayerCount, int32 InSampleCount
        , VkImage InImage, VkImageView InImageView, VkDeviceMemory InImageMemory = nullptr, EImageLayout InImageLayout = EImageLayout::UNDEFINED, VkSampler InSamplerState = nullptr)
        : jTexture(InType, InFormat, InWidth, InHeight, InLayerCount, InSampleCount, InSRGB), Image(InImage), View(InImageView), Memory(InImageMemory)
        , Layout(InImageLayout), SamplerState(InSamplerState)
    {}
    VkImage Image = nullptr;
    VkImageView View = nullptr;
    VkDeviceMemory Memory = nullptr;
    EImageLayout Layout = EImageLayout::UNDEFINED;
    VkSampler SamplerState = nullptr;

    virtual void* GetHandle() const override { return Image; }
    virtual void* GetViewHandle() const override { return View; }
    virtual void* GetMemoryHandle() const override { return Memory; }
    virtual void* GetSamplerStateHandle() const override { return SamplerState; }
    virtual void Destroy() override;

    static VkSampler CreateDefaultSamplerState();
};
