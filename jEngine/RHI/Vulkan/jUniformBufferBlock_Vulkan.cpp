#include "pch.h"
#include "jUniformBufferBlock_Vulkan.h"
#include "jVulkanBufferUtil.h"
#include "../jRHI_Vulkan.h"
#include "jRingBuffer_Vulkan.h"

#define USE_RINGBUFFER_FOR_UNIFORMBUFFER 1

void jUniformBufferBlock_Vulkan::Destroy()
{
    Buffer.Destroy();
}

void jUniformBufferBlock_Vulkan::Init(size_t size)
{
    check(size);

#if USE_RINGBUFFER_FOR_UNIFORMBUFFER
    Buffer.AllocatedSize = size;
#else
    jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VkDeviceSize(size), Buffer);
#endif
}

void jUniformBufferBlock_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(Buffer.AllocatedSize == InSize);

#if USE_RINGBUFFER_FOR_UNIFORMBUFFER
    jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetRingBuffer();
    Buffer.Offset = ringBuffer->Alloc(InSize);
    Buffer.AllocatedSize = InSize;
    Buffer.Buffer = ringBuffer->Buffer;
    Buffer.BufferMemory = ringBuffer->BufferMemory;
#endif

    check(Buffer.AllocatedSize == InSize);
    // 다르면 여기서 추가 작업을 해야 함.

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

void jUniformBufferBlock_Vulkan::ClearBuffer(int32 clearValue)
{
#if USE_RINGBUFFER_FOR_UNIFORMBUFFER
    jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetRingBuffer();
    Buffer.Offset = ringBuffer->Alloc(Buffer.AllocatedSize);
    Buffer.AllocatedSize = Buffer.AllocatedSize;
    Buffer.Buffer = ringBuffer->Buffer;
    Buffer.BufferMemory = ringBuffer->BufferMemory;
#endif

    if (Buffer.Buffer && Buffer.BufferMemory)
    {
        void* data = nullptr;
        vkMapMemory(g_rhi_vk->Device, Buffer.BufferMemory, Buffer.Offset, Buffer.AllocatedSize, 0, &data);
        memset(data, clearValue, Buffer.AllocatedSize);
        vkUnmapMemory(g_rhi_vk->Device, Buffer.BufferMemory);
    }
}
