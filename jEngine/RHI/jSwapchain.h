#pragma once
#include "jRHIType.h"
#include "Math/Vector.h"

class jSwapchainImage
{
public:
    virtual ~jSwapchainImage() {}
    virtual void Release() = 0;

    virtual void* GetHandle() const
    {
        return TexturePtr ? TexturePtr->GetHandle() : nullptr;
    }

    std::shared_ptr<jTexture> TexturePtr;
};

class jSwapchain
{
public:
    virtual ~jSwapchain() {}

    virtual bool Create() = 0;
    virtual void Release() = 0;
    virtual void* GetHandle() const = 0;
    virtual ETextureFormat GetFormat() const = 0;
    virtual const Vector2i& GetExtent() const = 0;
    virtual jSwapchainImage* GetSwapchainImage(int32 index) const = 0;
    virtual int32 GetNumOfSwapchain() const = 0;
};
