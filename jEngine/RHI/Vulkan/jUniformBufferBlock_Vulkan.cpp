#include "pch.h"
#include "jUniformBufferBlock_Vulkan.h"
#include "jVulkanBufferUtil.h"

void jUniformBufferBlock_Vulkan::Destroy()
{
    Buffer.Destroy();
}

void jUniformBufferBlock_Vulkan::Init()
{
    check(Size);
    VkDeviceSize bufferSize = Size;

    jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, Buffer);
}

void jUniformBufferBlock_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
    check(Size == InSize);
    // 다르면 여기서 추가 작업을 해야 함.

    Size = InSize;

    if (Buffer.Buffer, Buffer.BufferMemory)
    {
        void* data = nullptr;
        vkMapMemory(g_rhi_vk->Device, Buffer.BufferMemory, 0, Size, 0, &data);
        if (InData)
            memcpy(data, InData, InSize);
        else
            memset(data, 0, InSize);
        vkUnmapMemory(g_rhi_vk->Device, Buffer.BufferMemory);
    }
}

void jUniformBufferBlock_Vulkan::ClearBuffer(int32 clearValue)
{
    if (Buffer.Buffer, Buffer.BufferMemory)
    {
        void* data = nullptr;
        vkMapMemory(g_rhi_vk->Device, Buffer.BufferMemory, 0, Size, 0, &data);
        memset(data, clearValue, Size);
        vkUnmapMemory(g_rhi_vk->Device, Buffer.BufferMemory);
    }
}
