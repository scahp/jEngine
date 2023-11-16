#include "pch.h"
#include "jBuffer_Vulkan.h"
#include "jRHI_Vulkan.h"

void jBuffer_Vulkan::Release()
{
    ReleaseInternal();
}

void jBuffer_Vulkan::ReleaseInternal()
{
    if (!HasBufferOwnership)
    {
        // Return an allocated memory to vulkan memory pool
        if (Memory.IsValid())
            Memory.Free();

        return;
    }

    check(g_rhi_vk->Device);

    if (Buffer)
    {
        vkDestroyBuffer(g_rhi_vk->Device, Buffer, nullptr);
    }
    Buffer = nullptr;

    if (BufferMemory)
    {
        vkFreeMemory(g_rhi_vk->Device, BufferMemory, nullptr);
    }
    BufferMemory = nullptr;

    AllocatedSize = 0;
    MappedPointer = nullptr;
}

void* jBuffer_Vulkan::Map(uint64 offset, uint64 size)
{
    // Ownership 이 있는 경우만 Map
    if (!HasBufferOwnership)
        return MappedPointer;

    check(size);
    check(offset + size <= AllocatedSize);
    check(!MappedPointer);
    vkMapMemory(g_rhi_vk->Device, BufferMemory, offset, size, 0, &MappedPointer);
    return MappedPointer;
}

void* jBuffer_Vulkan::Map()
{
    // Ownership 이 있는 경우만 Map
    if (!HasBufferOwnership)
        return MappedPointer;

    check(AllocatedSize);
    check(!MappedPointer);
    vkMapMemory(g_rhi_vk->Device, BufferMemory, Offset, VK_WHOLE_SIZE, 0, &MappedPointer);
    return MappedPointer;
}

void jBuffer_Vulkan::Unmap()
{
    // Ownership 이 있는 경우만 Unmap
    if (!HasBufferOwnership)
        return;
    
    check(MappedPointer);
    vkUnmapMemory(g_rhi_vk->Device, BufferMemory);
    MappedPointer = nullptr;
}

void jBuffer_Vulkan::UpdateBuffer(const void* data, uint64 size)
{
    check(size <= AllocatedSize);

    if (HasBufferOwnership)
    {
        if (ensure(Map(0, size)))
        {
            check(MappedPointer);
            memcpy(MappedPointer, data, size);
            Unmap();
        }
    }
    else
    {
        check(MappedPointer);
        memcpy(((uint8*)MappedPointer) + Offset, data, size);
    }
}
