#pragma once
#include "jRHIType.h"
#include "Math/Vector.h"

class jSwapchainImage
{
public:
    virtual void Destroy() = 0;

    virtual void* GetHandle() const = 0;
    virtual void* GetViewHandle() const = 0;
    virtual void* GetMemoryHandle() const = 0;

    jImage* Image = nullptr;
};

class jSwapchain
{
public:
    virtual ~jSwapchain() {}

    virtual bool Create() = 0;
    virtual void Destroy() = 0;
    virtual void* GetHandle() const = 0;
    virtual ETextureFormat GetFormat() const = 0;
    virtual const Vector2i& GetExtent() const = 0;
    virtual jSwapchainImage* GetSwapchainImage(int32 index) const = 0;
    virtual int32 NumOfSwapchain() const = 0;
};
