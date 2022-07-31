#pragma once
#include "../jRHIType.h"

struct jImage_Vulkan : public jImage
{
    jImage_Vulkan() = default;
    jImage_Vulkan(VkImage image, VkImageView imageView, VkDeviceMemory deviceMemory)
        : Image(image), ImageView(imageView), ImageMemory(deviceMemory)
    {}
    
    virtual void Destroy() override;

    virtual void* GetHandle() const override { return Image; }
    virtual void* GetViewHandle() const override { return ImageView; }
    virtual void* GetMemoryHandle() const override { return ImageMemory; }

    VkImage Image = nullptr;
    VkImageView ImageView = nullptr;
    VkDeviceMemory ImageMemory = nullptr;
};
