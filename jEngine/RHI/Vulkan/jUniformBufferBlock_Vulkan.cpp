#include "pch.h"
#include "jUniformBufferBlock_Vulkan.h"
#include "jVulkanBufferUtil.h"
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
        jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VkDeviceSize(size), Buffer);
    }
}

void jUniformBufferBlock_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(Buffer.AllocatedSize >= InSize);

    if (IsUseRingBuffer())
    {
        jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetUniformRingBuffer();
        Buffer.Offset = ringBuffer->Alloc(InSize);
        Buffer.AllocatedSize = InSize;
        Buffer.Buffer = ringBuffer->Buffer;
        Buffer.BufferMemory = ringBuffer->BufferMemory;

        if (ensure(ringBuffer->MappedPointer))
        {
            char* startAddr = ((char*)ringBuffer->MappedPointer) + Buffer.Offset;
            if (InData)
                memcpy(startAddr, InData, InSize);
            else
                memset(startAddr, 0, InSize);
        }
    }
    else
    {
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
    }
}

void jUniformBufferBlock_Vulkan::ClearBuffer(int32 clearValue)
{
    if (IsUseRingBuffer())
    {
        jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetUniformRingBuffer();
        Buffer.Offset = ringBuffer->Alloc(Buffer.AllocatedSize);
        Buffer.AllocatedSize = Buffer.AllocatedSize;
        Buffer.Buffer = ringBuffer->Buffer;
        Buffer.BufferMemory = ringBuffer->BufferMemory;

        if (ensure(ringBuffer->MappedPointer))
        {
            char* startAddr = ((char*)ringBuffer->MappedPointer) + Buffer.Offset;
            memset(startAddr, 0, Buffer.AllocatedSize);
        }
    }
    else
    {
        if (Buffer.Buffer && Buffer.BufferMemory)
        {
            void* data = nullptr;
            vkMapMemory(g_rhi_vk->Device, Buffer.BufferMemory, Buffer.Offset, Buffer.AllocatedSize, 0, &data);
            memset(data, clearValue, Buffer.AllocatedSize);
            vkUnmapMemory(g_rhi_vk->Device, Buffer.BufferMemory);
        }
    }
}
