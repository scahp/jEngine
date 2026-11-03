#include "pch.h"
#include "jMemoryPool_Vulkan.h"

void jSubMemoryAllocator_Vulkan::Initialize(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize)
{
    check(SubMemoryRange.Offset == 0 && SubMemoryRange.DataSize == 0);
    check(0 == FreeLists.size());

    SubMemoryRange.Offset = 0;
    jBufferUtil_Vulkan::CreateBuffer_LowLevel(InUsage, InProperties, InSize, Buffer, DeviceMemory, SubMemoryRange.DataSize);
    Usages = InUsage;
    Properties = InProperties;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_rhi_vk->Device, Buffer, &memRequirements);
    Alignment = memRequirements.alignment;
    // Some drivers require alignment larger than 4 KiB for AS-capable buffers.
    // Ensure the allocator can reserve one aligned allocation of the requested size.
    const uint64 alignedSize = Align(InSize, Alignment);
    if (SubMemoryRange.DataSize < alignedSize) {
        vkDestroyBuffer(g_rhi_vk->Device, Buffer, nullptr);
        vkFreeMemory(g_rhi_vk->Device, DeviceMemory, nullptr);
        jBufferUtil_Vulkan::CreateBuffer_LowLevel(InUsage, InProperties, alignedSize,
            Buffer, DeviceMemory, SubMemoryRange.DataSize);
    }

    if (!!(EVulkanMemoryBits::HOST_VISIBLE & InProperties))
    {
        verify(VK_SUCCESS == vkMapMemory(g_rhi_vk->Device, DeviceMemory, 0, SubMemoryRange.DataSize, 0, &MappedPointer));
    }
}

jSubMemoryAllocator_Vulkan::~jSubMemoryAllocator_Vulkan()
{
    if (MappedPointer) vkUnmapMemory(g_rhi_vk->Device, DeviceMemory);
    if (Buffer) vkDestroyBuffer(g_rhi_vk->Device, Buffer, nullptr);
    if (DeviceMemory) vkFreeMemory(g_rhi_vk->Device, DeviceMemory, nullptr);
}
jMemoryPool_Vulkan::~jMemoryPool_Vulkan()
{
    for (auto& pool : MemoryPools)
        for (auto* allocator : pool) delete allocator;
}
