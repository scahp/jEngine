#include "pch.h"
#include "jUniformBufferBlock_Vulkan.h"
#include "jBufferUtil_Vulkan.h"
#include "../jRHI_Vulkan.h"
#include "jRingBuffer_Vulkan.h"

void jUniformBufferBlock_Vulkan::Init(size_t size)
{
    check(size);
    size = Align<uint64>(size, g_rhi_vk->DeviceProperties.limits.minUniformBufferOffsetAlignment);

    if (IsUseRingBuffer())
    {
        Buffer.HasBufferOwnership = false;          // Prevent destroy the ring buffer
        Buffer.AllocatedSize = size;
    }
    else
    {
        Buffer.HasBufferOwnership = false;          // Prevent destroy the ring buffer
        Buffer.AllocatedSize = size;
    }
}

void jUniformBufferBlock_Vulkan::AllocBufferFromGlobalMemory(size_t size)
{
    // If we have allocated memory frame global memory, reallocate it again.
    Buffer.Release();

    Buffer.InitializeWithMemory(g_rhi->GetMemoryPool()->Alloc(EVulkanBufferBits::UNIFORM_BUFFER
        , EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT, VkDeviceSize(size)));

    check(Buffer.Memory.IsValid());
}

void jUniformBufferBlock_Vulkan::Release()
{
    Buffer.Release();
}

void jUniformBufferBlock_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(Buffer.AllocatedSize >= InSize);

    if (IsUseRingBuffer())
    {
        jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetOneFrameUniformRingBuffer();
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
        check(Buffer.AllocatedSize);
        AllocBufferFromGlobalMemory(Buffer.AllocatedSize);

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

void jUniformBufferBlock_Vulkan::ClearBuffer(int32 clearValue)
{
    if (IsUseRingBuffer())
    {
        jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetOneFrameUniformRingBuffer();
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
        check(Buffer.AllocatedSize);
        AllocBufferFromGlobalMemory(Buffer.AllocatedSize);

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
