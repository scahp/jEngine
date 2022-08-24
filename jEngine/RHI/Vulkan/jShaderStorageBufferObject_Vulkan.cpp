#include "pch.h"
#include "jShaderStorageBufferObject_Vulkan.h"
#include "jVulkanBufferUtil.h"
#include "../jRHI_Vulkan.h"
#include "jRingBuffer_Vulkan.h"

#define USE_RINGBUFFER_FOR_SSBO 1

void jShaderStorageBufferObject_Vulkan::Destroy()
{
}

void jShaderStorageBufferObject_Vulkan::Init(size_t size)
{
    check(size);

    BufferPtr = std::make_shared<jBuffer_Vulkan>();

#if USE_RINGBUFFER_FOR_SSBO
    BufferPtr->HasBufferOwnership = false;
    BufferPtr->AllocatedSize = size;
#else
    jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VkDeviceSize(size), *BufferPtr.get());
#endif
}

void jShaderStorageBufferObject_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(BufferPtr->AllocatedSize >= InSize);

#if USE_RINGBUFFER_FOR_SSBO
    jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetSSBORingBuffer();
    BufferPtr->Offset = ringBuffer->Alloc(InSize);
    BufferPtr->AllocatedSize = InSize;
    BufferPtr->Buffer = ringBuffer->Buffer;
    BufferPtr->BufferMemory = ringBuffer->BufferMemory;
#endif

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
}

void jShaderStorageBufferObject_Vulkan::ClearBuffer(int32 clearValue)
{
#if USE_RINGBUFFER_FOR_SSBO
    jRingBuffer_Vulkan* ringBuffer = g_rhi_vk->GetSSBORingBuffer();
    BufferPtr->Offset = ringBuffer->Alloc(BufferPtr->AllocatedSize);
    BufferPtr->AllocatedSize = BufferPtr->AllocatedSize;
    BufferPtr->Buffer = ringBuffer->Buffer;
    BufferPtr->BufferMemory = ringBuffer->BufferMemory;
#endif

    if (BufferPtr->Buffer && BufferPtr->BufferMemory)
    {
        void* data = nullptr;
        vkMapMemory(g_rhi_vk->Device, BufferPtr->BufferMemory, BufferPtr->Offset, BufferPtr->AllocatedSize, 0, &data);
        memset(data, clearValue, BufferPtr->AllocatedSize);
        vkUnmapMemory(g_rhi_vk->Device, BufferPtr->BufferMemory);
    }
}
