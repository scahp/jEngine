#include "pch.h"
#include "jUniformBufferBlock_Vulkan.h"
#include "jVulkanBufferUtil.h"
#include "../jRHI_Vulkan.h"
#include "jRingBuffer_Vulkan.h"

#define USE_RINGBUFFER_FOR_UNIFORMBUFFER 1

void jUniformBufferBlock_Vulkan::Destroy()
{
}

void jUniformBufferBlock_Vulkan::Init(size_t size)
{
    check(size);

    BufferPtr = std::make_shared<jBuffer_Vulkan>();

#if USE_RINGBUFFER_FOR_UNIFORMBUFFER
    BufferPtr->HasBufferOwnership = false;
    BufferPtr->AllocatedSize = size;
#else
    jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VkDeviceSize(size), *BufferPtr.get());
#endif
}

void jUniformBufferBlock_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(BufferPtr->AllocatedSize >= InSize);

#if USE_RINGBUFFER_FOR_UNIFORMBUFFER
    jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetUniformRingBuffer();
    BufferPtr->Offset = ringBuffer->Alloc(InSize);
    BufferPtr->AllocatedSize = InSize;
    BufferPtr->Buffer = ringBuffer->Buffer;
    BufferPtr->BufferMemory = ringBuffer->BufferMemory;

    if (ensure(ringBuffer->MappedPointer))
    {
        char* startAddr = ((char*)ringBuffer->MappedPointer) + BufferPtr->Offset;
        if (InData)
            memcpy(startAddr, InData, InSize);
        else
            memset(startAddr, 0, InSize);
    }
#else
    if (BufferPtr->Buffer && BufferPtr->BufferMemory)
    {
        void* data = nullptr;
        vkMapMemory(g_rhi_vk->Device, BufferPtr->BufferMemory, BufferPtr->Offset, BufferPtr->AllocatedSize, 0, &data);
        if (InData)
            memcpy(data, InData, InSize);
        else
            memset(data, 0, InSize);
        vkUnmapMemory(g_rhi_vk->Device, BufferPtr->BufferMemory);
    }
#endif
}

void jUniformBufferBlock_Vulkan::ClearBuffer(int32 clearValue)
{
#if USE_RINGBUFFER_FOR_UNIFORMBUFFER
    jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetUniformRingBuffer();
    BufferPtr->Offset = ringBuffer->Alloc(BufferPtr->AllocatedSize);
    BufferPtr->AllocatedSize = BufferPtr->AllocatedSize;
    BufferPtr->Buffer = ringBuffer->Buffer;
    BufferPtr->BufferMemory = ringBuffer->BufferMemory;

    if (ensure(ringBuffer->MappedPointer))
    {
        char* startAddr = ((char*)ringBuffer->MappedPointer) + BufferPtr->Offset;
        memset(startAddr, 0, BufferPtr->AllocatedSize);
    }
#else
    if (BufferPtr->Buffer && BufferPtr->BufferMemory)
    {
        void* data = nullptr;
        vkMapMemory(g_rhi_vk->Device, BufferPtr->BufferMemory, BufferPtr->Offset, BufferPtr->AllocatedSize, 0, &data);
        memset(data, clearValue, BufferPtr->AllocatedSize);
        vkUnmapMemory(g_rhi_vk->Device, BufferPtr->BufferMemory);
    }
#endif
}
