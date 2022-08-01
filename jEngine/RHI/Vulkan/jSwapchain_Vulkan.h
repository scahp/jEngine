#pragma once
#include "../jSwapchain.h"
#include "jImage_Vulkan.h"

class jSwapchainImage_Vulkan : public jSwapchainImage
{
public:
    virtual void Destroy() override
    {
        if (Image)
        {
            Image->Destroy();
            delete Image;
        }
    }

    virtual void* GetHandle() const override
    {
        return Image ? Image->GetHandle() : nullptr;
    }
    virtual void* GetViewHandle() const override
    {
        return Image ? Image->GetViewHandle() : nullptr;
    }
    virtual void* GetMemoryHandle() const override
    {
        return Image ? Image->GetMemoryHandle() : nullptr;
    }

    VkFence CommandBufferFence = nullptr;

    // Semaphore 는 GPU - GPU 간의 동기화를 맞춰줌.여러개의 프레임이 동시에 만들어질 수 있게 함.
    // Semaphores 는 커맨드 Queue 내부 혹은 Queue 들 사이에 명령어 동기화를 위해서 설계됨
    VkSemaphore Available = nullptr;		// 이미지를 획득해서 렌더링 준비가 완료된 경우 Signal(Lock 이 풀리는) 되는 것
    VkSemaphore RenderFinished = nullptr;	// 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것
};

// Swapchain
class jSwapchain_Vulkan : public jSwapchain
{
public:
    virtual bool Create() override;
    virtual void Destroy() override;
    virtual void* GetHandle() const override { return Swapchain; }
    virtual ETextureFormat GetFormat() const override { return Format; }
    virtual const Vector2i& GetExtent() const override { return Extent; }
    virtual jSwapchainImage* GetSwapchainImage(int32 index) const override { check(Images.size() > index); return Images[index]; }
    virtual int32 NumOfSwapchain() const override { return (int32)Images.size(); }

    VkSwapchainKHR Swapchain = nullptr;
    ETextureFormat Format = ETextureFormat::RGB8;
    Vector2i Extent;
    std::vector<jSwapchainImage_Vulkan*> Images;
};
