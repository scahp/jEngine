#include "pch.h"
#include "jMemoryPool_Vulkan.h"

void jSubMemoryAllocator_Vulkan::Initialize(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize)
{
    check(SubMemoryRange.Offset == 0 && SubMemoryRange.DataSize == 0);
    check(0 == FreeLists.size());

    SubMemoryRange.Offset = 0;
    jVulkanBufferUtil::CreateBuffer(InUsage, InProperties, InSize, Buffer, DeviceMemory, SubMemoryRange.DataSize);
    Usages = InUsage;
    Properties = InProperties;

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_rhi_vk->Device, Buffer, &memRequirements);
    Alignment = memRequirements.alignment;

    if (!(EVulkanBufferBits::TRANSFER_DST & InUsage))
    {
        verify(VK_SUCCESS == vkMapMemory(g_rhi_vk->Device, DeviceMemory, 0, SubMemoryRange.DataSize, 0, &MappedPointer));
    }
}
