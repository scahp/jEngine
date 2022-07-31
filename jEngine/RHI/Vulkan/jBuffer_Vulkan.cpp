#include "pch.h"
#include "jBuffer_Vulkan.h"
#include "../jRHI_Vulkan.h"

void jBuffer_Vulkan::Destroy()
{
    check(g_rhi_vk->Device);

    if (Buffer)
        vkDestroyBuffer(g_rhi_vk->Device, Buffer, nullptr);

    if (BufferMemory)
        vkFreeMemory(g_rhi_vk->Device, BufferMemory, nullptr);

    AllocatedSize = 0;
    MappedPointer = nullptr;
}

void* jBuffer_Vulkan::Map(uint64 offset, uint64 size)
{
    check(size);
    check(offset + size <= AllocatedSize);
    check(!MappedPointer);
    vkMapMemory(g_rhi_vk->Device, BufferMemory, offset, size, 0, &MappedPointer);
    return MappedPointer;
}

void* jBuffer_Vulkan::Map()
{
    check(AllocatedSize);
    check(!MappedPointer);
    vkMapMemory(g_rhi_vk->Device, BufferMemory, 0, VK_WHOLE_SIZE, 0, &MappedPointer);
    return MappedPointer;
}

void jBuffer_Vulkan::Unmap()
{
    check(MappedPointer);
    vkUnmapMemory(g_rhi_vk->Device, BufferMemory);
    MappedPointer = nullptr;
}

void jBuffer_Vulkan::UpdateBuffer(const void* data, uint64 size)
{
    check(size <= AllocatedSize);

    if (ensure(Map(0, size)))
    {
        memcpy(MappedPointer, data, size);
        Unmap();
    }
}
