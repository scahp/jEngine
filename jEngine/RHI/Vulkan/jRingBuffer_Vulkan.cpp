#include "pch.h"
#include "jRingBuffer_Vulkan.h"
#include "../jRHI_Vulkan.h"
#include "jVulkanBufferUtil.h"

jRingBuffer_Vulkan::~jRingBuffer_Vulkan()
{
    Release();
}

void jRingBuffer_Vulkan::Create(EVulkanBufferBits bufferBits, uint64 totalSize, uint32 alignment)
{
    Release();

    jVulkanBufferUtil::CreateBuffer(GetVulkanBufferBits(bufferBits), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VkDeviceSize(totalSize), Buffer, BufferMemory, RingBufferSize);
    
    RingBufferOffset = 0;
    Alignment = alignment;
}

void jRingBuffer_Vulkan::Reset()
{
    RingBufferOffset = 0;
}

uint64 jRingBuffer_Vulkan::Alloc(uint64 allocSize)
{
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
