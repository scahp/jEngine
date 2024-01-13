#pragma once
#include "../jRHIType.h"

struct jRingBuffer_Vulkan : public jBuffer
{
    jRingBuffer_Vulkan() = default;
    virtual ~jRingBuffer_Vulkan();

    virtual void Create(EVulkanBufferBits bufferBits, uint64 totalSize, uint32 alignment = 16);
    virtual void Reset();
    virtual uint64 Alloc(uint64 allocSize);

    virtual void Release() override;
    
    virtual void* GetMappedPointer() const override { return MappedPointer; }
    virtual void* Map(uint64 offset, uint64 size) override;
    virtual void* Map() override;
    virtual void Unmap() override;
    virtual void UpdateBuffer(const void* data, uint64 size) override;

    virtual void* GetHandle() const override { return Buffer; }
    virtual uint64 GetAllocatedSize() const override { return RingBufferSize; }
    virtual uint64 GetBufferSize() const override { return RingBufferSize; }
    virtual uint64 GetOffset() const override { return RingBufferOffset; }

    uint64 RingBufferOffset = 0;
    uint32 Alignment = 16;
    VkBuffer Buffer = nullptr;
    VkDeviceMemory BufferMemory = nullptr;
    uint64 RingBufferSize = 0;
    void* MappedPointer = nullptr;

    jMutexLock Lock;
};

