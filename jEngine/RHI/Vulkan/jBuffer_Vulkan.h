#pragma once
#include "../jRHIType.h"

struct jBuffer_Vulkan : public jBuffer
{
    jBuffer_Vulkan() = default;
    jBuffer_Vulkan(VkBuffer buffer, VkDeviceMemory bufferMemory)
        : Buffer(buffer), BufferMemory(bufferMemory)
    {}

    virtual void Destroy() override;
    
    virtual void* GetMappedPointer() const override { return MappedPointer; }
    virtual void* Map(uint64 offset, uint64 size) override;
    virtual void* Map() override;
    virtual void Unmap() override;
    virtual void UpdateBuffer(const void* data, uint64 size) override;

    virtual void* GetHandle() const override { return Buffer; }
    virtual void* GetMemoryHandle() const override { return BufferMemory; }

    VkBuffer Buffer = nullptr;
    VkDeviceMemory BufferMemory = nullptr;
    uint64 AllocatedSize = 0;
    void* MappedPointer = nullptr;
};

