#pragma once
#include "../jRHIType.h"
#include "../jMemoryPool.h"

struct jBuffer_Vulkan : public jBuffer
{
    jBuffer_Vulkan() = default;
    jBuffer_Vulkan(VkBuffer InBuffer, VkDeviceMemory InBufferMemory)
        : Buffer(InBuffer), BufferMemory(InBufferMemory)
    {}
    jBuffer_Vulkan(const jMemory& InMemory)
    {
        InitializeWithMemory(InMemory);
    }
    virtual ~jBuffer_Vulkan()
    {
        ReleaseInternal();
    }

    void InitializeWithMemory(const jMemory& InMemory)
    {
        check(InMemory.IsValid());
        HasBufferOwnership = false;
        Buffer = (VkBuffer)InMemory.GetBuffer();
        MappedPointer = InMemory.GetMappedPointer();
        BufferMemory = (VkDeviceMemory)InMemory.GetMemory();
        Offset = InMemory.Range.Offset;
        AllocatedSize = InMemory.Range.DataSize;
        Memory = InMemory;
    }

    virtual void Release() override;
    void ReleaseInternal();

    virtual void* GetMappedPointer() const override { return MappedPointer; }
    virtual void* Map(uint64 offset, uint64 size) override;
    virtual void* Map() override;
    virtual void Unmap() override;
    virtual void UpdateBuffer(const void* data, uint64 size) override;

    virtual void* GetHandle() const override { return Buffer; }
    virtual uint32 GetAllocatedSize() const override { return (uint32)AllocatedSize; }

    jMemory Memory;
    VkBuffer Buffer = nullptr;
    VkDeviceMemory BufferMemory = nullptr;
    
    // todo : need the description about this properties
    VkAccelerationStructureKHR AccelerationStructure = nullptr;
    uint64 DeviceAddress = 0;
    
    size_t Offset = 0;
    size_t AllocatedSize = 0;
    void* MappedPointer = nullptr;

    bool HasBufferOwnership = true;     // 버퍼를 소멸시킬 권한이 있는 여부. 링버퍼를 사용하는 유니폼 버퍼의 경우 이 값이 false 임.
};
