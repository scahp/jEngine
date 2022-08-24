#pragma once
#include "../jRHIType.h"

struct jBuffer_Vulkan : public jBuffer
{
    jBuffer_Vulkan() = default;
    jBuffer_Vulkan(VkBuffer buffer, VkDeviceMemory bufferMemory)
        : Buffer(buffer), BufferMemory(bufferMemory)
    {}
    virtual ~jBuffer_Vulkan()
    {
        if (HasBufferOwnership)
            jBuffer_Vulkan::Release();
    }

    virtual void Release() override;
    
    virtual void* GetMappedPointer() const override { return MappedPointer; }
    virtual void* Map(uint64 offset, uint64 size) override;
    virtual void* Map() override;
    virtual void Unmap() override;
    virtual void UpdateBuffer(const void* data, uint64 size) override;

    virtual void* GetHandle() const override { return Buffer; }
    virtual void* GetMemoryHandle() const override { return BufferMemory; }
    virtual size_t GetAllocatedSize() const override { return AllocatedSize; }

    VkBuffer Buffer = nullptr;
    VkDeviceMemory BufferMemory = nullptr;
    size_t Offset = 0;
    size_t AllocatedSize = 0;
    void* MappedPointer = nullptr;

    bool HasBufferOwnership = true;     // 버퍼를 소멸시킬 권한이 있는 여부. 링버퍼를 사용하는 유니폼 버퍼의 경우 이 값이 false 임.
};
