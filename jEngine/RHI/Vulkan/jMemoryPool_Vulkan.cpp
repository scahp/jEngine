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

    if (!!(EVulkanMemoryBits::HOST_VISIBLE & InProperties))
    {
        verify(VK_SUCCESS == vkMapMemory(g_rhi_vk->Device, DeviceMemory, 0, SubMemoryRange.DataSize, 0, &MappedPointer));
    }
}
