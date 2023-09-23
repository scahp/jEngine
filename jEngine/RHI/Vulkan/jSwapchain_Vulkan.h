#pragma once
#include "../jSwapchain.h"

class jSemaphore;

class jSwapchainImage_Vulkan : public jSwapchainImage
{
public:
    virtual ~jSwapchainImage_Vulkan()
    {
        ReleaseInternal();
    }

    void ReleaseInternal();
    virtual void Release() override;

    virtual void* GetHandle() const override
    {
        return TexturePtr ? TexturePtr->GetHandle() : nullptr;
    }
    virtual void* GetViewHandle() const override
    {
        return TexturePtr ? TexturePtr->GetViewHandle() : nullptr;
    }
    virtual void* GetMemoryHandle() const override
    {
        return TexturePtr ? TexturePtr->GetMemoryHandle() : nullptr;
    }

    VkFence CommandBufferFence = nullptr;   // 이 스왑체인에 렌더링하고 있는 이미지가 완료됨을 알 수 있는 CommandBuffer의 Fence.

    // Semaphore 는 GPU - GPU 간의 동기화를 맞춰줌.여러개의 프레임이 동시에 만들어질 수 있게 함.
    // Semaphores 는 커맨드 Queue 내부 혹은 Queue 들 사이에 명령어 동기화를 위해서 설계됨
    jSemaphore* Available = nullptr;		// 이미지를 획득해서 렌더링 준비가 완료된 경우 Signal(Lock 이 풀리는) 되는 것
    jSemaphore* RenderFinished = nullptr;	// 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것
    jSemaphore* RenderFinishedAfterShadow = nullptr;	// 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것
    jSemaphore* RenderFinishedAfterBasePass = nullptr;	// 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것
};

// Swapchain
class jSwapchain_Vulkan : public jSwapchain
{
public:
    virtual ~jSwapchain_Vulkan()
    {
        ReleaseInternal();
    }
    void ReleaseInternal();

    virtual bool Create() override;
    virtual void Release() override;
    virtual void* GetHandle() const override { return Swapchain; }
    virtual ETextureFormat GetFormat() const override { return Format; }
    virtual const Vector2i& GetExtent() const override { return Extent; }
    virtual jSwapchainImage* GetSwapchainImage(int32 index) const override { check(Images.size() > index); return Images[index]; }
    virtual int32 GetNumOfSwapchain() const override { return (int32)Images.size(); }

    VkSwapchainKHR Swapchain = nullptr;
    ETextureFormat Format = ETextureFormat::RGB8;
    Vector2i Extent;
    std::vector<jSwapchainImage_Vulkan*> Images;
};
