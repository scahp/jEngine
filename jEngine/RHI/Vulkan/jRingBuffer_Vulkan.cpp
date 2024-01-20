#include "pch.h"
#include "jRingBuffer_Vulkan.h"
#include "jRHI_Vulkan.h"
#include "jBufferUtil_Vulkan.h"

jRingBuffer_Vulkan::~jRingBuffer_Vulkan()
{
    Release();
}

void jRingBuffer_Vulkan::Create(EVulkanBufferBits bufferBits, uint64 totalSize, uint32 alignment)
{
    jScopedLock s(&Lock);

    Release();

    jBufferUtil_Vulkan::CreateBuffer_LowLevel(bufferBits, EVulkanMemoryBits::HOST_VISIBLE
        | EVulkanMemoryBits::HOST_COHERENT, VkDeviceSize(totalSize), Buffer, BufferMemory, RingBufferSize);
    
    RingBufferOffset = 0;
    Alignment = alignment;

    // 수정할 수 있게 바로 Map 사용
    Map(0, RingBufferSize);
}

void jRingBuffer_Vulkan::Reset()
{
    jScopedLock s(&Lock);

    RingBufferOffset = 0;
}

uint64 jRingBuffer_Vulkan::Alloc(uint64 allocSize)
{
    jScopedLock s(&Lock);

    const uint64 allocOffset = Align<uint64>(RingBufferOffset, Alignment);
    if (allocOffset + allocSize <= RingBufferSize)
    {
        RingBufferOffset = allocOffset + allocSize;
        return allocOffset;
    }

    check(0);

    return 0;
}

void jRingBuffer_Vulkan::Release()
{
    check(g_rhi_vk->Device);

    if (Buffer)
    {
        vkDestroyBuffer(g_rhi_vk->Device, Buffer, nullptr);
        Buffer = nullptr;
    }

    if (BufferMemory)
    {
        vkFreeMemory(g_rhi_vk->Device, BufferMemory, nullptr);
        BufferMemory = nullptr;
    }

    RingBufferSize = 0;
    MappedPointer = nullptr;
}

void* jRingBuffer_Vulkan::Map(uint64 offset, uint64 size)
{
    check(size);
    check(offset + size <= RingBufferSize);
    check(!MappedPointer);
    vkMapMemory(g_rhi_vk->Device, BufferMemory, offset, size, 0, &MappedPointer);
    return MappedPointer;
}

void* jRingBuffer_Vulkan::Map()
{
    check(RingBufferSize);
    check(!MappedPointer);
    vkMapMemory(g_rhi_vk->Device, BufferMemory, 0, VK_WHOLE_SIZE, 0, &MappedPointer);
    return MappedPointer;
}

void jRingBuffer_Vulkan::Unmap()
{
    check(MappedPointer);
    vkUnmapMemory(g_rhi_vk->Device, BufferMemory);
    MappedPointer = nullptr;
}

void jRingBuffer_Vulkan::UpdateBuffer(const void* data, uint64 size)
{
    check(size <= RingBufferSize);

    if (ensure(Map(0, size)))
    {
        memcpy(MappedPointer, data, size);
        Unmap();
    }
}
