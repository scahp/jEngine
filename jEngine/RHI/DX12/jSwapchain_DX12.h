#pragma once
#include "../jSwapchain.h"

class jFence_DX12;

class jSwapchainImage_DX12 : public jSwapchainImage
{
public:
    virtual ~jSwapchainImage_DX12()
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

    jFence_DX12* CommandBufferFence = nullptr;
};

// Swapchain
class jSwapchain_DX12 : public jSwapchain
{
public:
    virtual ~jSwapchain_DX12()
    {
        ReleaseInternal();
    }
    void ReleaseInternal();

    virtual bool Create() override;
    virtual void Release() override;
    virtual void* GetHandle() const override { return SwapChain.Get(); }
    virtual ETextureFormat GetFormat() const override { return Format; }
    virtual const Vector2i& GetExtent() const override { return Extent; }
    virtual jSwapchainImage* GetSwapchainImage(int32 index) const override { check(Images.size() > index); return Images[index]; }
    virtual int32 GetNumOfSwapchain() const override { return (int32)Images.size(); }
    uint32 GetCurrentBackBufferIndex() const { return SwapChain->GetCurrentBackBufferIndex(); }
    jSwapchainImage* GetCurrentSwapchainImage() const { return Images[GetCurrentBackBufferIndex()]; }

    ComPtr<IDXGISwapChain3> SwapChain;
    ETextureFormat Format = ETextureFormat::RGB8;
    Vector2i Extent;
    std::vector<jSwapchainImage_DX12*> Images;

    jFence_DX12* Fence = nullptr;
    //HANDLE m_fenceEvent = nullptr;
    //ComPtr<ID3D12Fence> m_fence;
};
