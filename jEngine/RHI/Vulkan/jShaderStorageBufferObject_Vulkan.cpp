#include "pch.h"
#include "jShaderStorageBufferObject_Vulkan.h"
#include "jBufferUtil_Vulkan.h"
#include "jRHI_Vulkan.h"
#include "jRingBuffer_Vulkan.h"

void jShaderStorageBufferObject_Vulkan::Init(size_t size)
{
    check(size);
    size = Align<uint64>(size, g_rhi_vk->DeviceProperties2.properties.limits.minStorageBufferOffsetAlignment);

    if (IsUseRingBuffer())
    {
        Buffer.HasBufferOwnership = false;
        Buffer.AllocatedSize = size;
    }
    else
    {
        jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::STORAGE_BUFFER, EVulkanMemoryBits::HOST_VISIBLE
            | EVulkanMemoryBits::HOST_COHERENT, VkDeviceSize(size), Buffer);
    }
}

void jShaderStorageBufferObject_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(Buffer.AllocatedSize >= InSize);

    if (IsUseRingBuffer())
    {
        jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetSSBORingBuffer();
        Buffer.Offset = ringBuffer->Alloc(InSize);
        Buffer.AllocatedSize = InSize;
        Buffer.Buffer = ringBuffer->Buffer;
        Buffer.BufferMemory = ringBuffer->BufferMemory;
        
        if (ensure(ringBuffer->MappedPointer))
        {
            uint8* startAddr = ((uint8*)ringBuffer->MappedPointer) + Buffer.Offset;
            if (InData)
                memcpy(startAddr, InData, InSize);
            else
                memset(startAddr, 0, InSize);
        }
    }
    else
    {
#if USE_VK_MEMORY_POOL
        if (ensure(Buffer.MappedPointer))
        {
            uint8* startAddr = ((uint8*)Buffer.MappedPointer) + Buffer.Offset;
            if (InData)
                memcpy(startAddr, InData, InSize);
            else
                memset(startAddr, 0, InSize);
        }
#else
        if (Buffer.Buffer && Buffer.BufferMemory)
        {
            void* data = nullptr;
            vkMapMemory(g_rhi_vk->Device, Buffer.BufferMemory, Buffer.Offset, Buffer.AllocatedSize, 0, &data);
            if (InData)
                memcpy(data, InData, InSize);
            else
                memset(data, 0, InSize);
            vkUnmapMemory(g_rhi_vk->Device, Buffer.BufferMemory);
        }
#endif
    }
}

void jShaderStorageBufferObject_Vulkan::ClearBuffer(int32 clearValue)
{
    if (IsUseRingBuffer())
    {
        jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetSSBORingBuffer();
        Buffer.Offset = ringBuffer->Alloc(Buffer.AllocatedSize);
        Buffer.AllocatedSize = Buffer.AllocatedSize;
        Buffer.Buffer = ringBuffer->Buffer;
        Buffer.BufferMemory = ringBuffer->BufferMemory;

        if (ensure(ringBuffer->MappedPointer))
        {
            memset(((uint8*)ringBuffer->MappedPointer) + Buffer.Offset, 0, Buffer.AllocatedSize);
        }
    }
    else
    {
#if USE_VK_MEMORY_POOL
        if (ensure(Buffer.MappedPointer))
        {
            memset(((uint8*)Buffer.MappedPointer) + Buffer.Offset, 0, Buffer.AllocatedSize);
        }
#else
        if (Buffer.Buffer && Buffer.BufferMemory)
        {
            void* data = nullptr;
            vkMapMemory(g_rhi_vk->Device, Buffer.BufferMemory, Buffer.Offset, Buffer.AllocatedSize, 0, &data);
            memset(data, clearValue, Buffer.AllocatedSize);
            vkUnmapMemory(g_rhi_vk->Device, Buffer.BufferMemory);
        }
#endif
    }
}
