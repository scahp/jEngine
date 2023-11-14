#pragma once

#include "RHI/jRHIType.h"
#include "RHI/jMemoryPool.h"
#include "jBufferUtil_Vulkan.h"

class jSubMemoryAllocator_Vulkan : public jSubMemoryAllocator
{
    virtual ~jSubMemoryAllocator_Vulkan() {}

    virtual void Initialize(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize) override;
    virtual void* GetBuffer() const { return Buffer; }
    virtual void* GetMemory() const { return DeviceMemory; }

    VkBuffer Buffer = nullptr;
    VkDeviceMemory DeviceMemory = nullptr;
};

class jMemoryPool_Vulkan : public jMemoryPool
{
public:
    virtual ~jMemoryPool_Vulkan() {}

    virtual jSubMemoryAllocator* CreateSubMemoryAllocator() const override
    {
        return new jSubMemoryAllocator_Vulkan();
    }
};