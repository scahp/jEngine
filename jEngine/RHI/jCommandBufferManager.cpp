#include "pch.h"
#include "jCommandBufferManager.h"

bool jCommandBufferManager_Vulkan::CreatePool(uint32 QueueIndex)
{
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = QueueIndex;

    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 커맨드 버퍼가 새로운 커맨드를 자주 다시 기록한다고 힌트를 줌.
    //											(메모리 할당 동작을 변경할 것임)
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 커맨드 버퍼들이 개별적으로 다시 기록될 수 있다.
    //													이 플래그가 없으면 모든 커맨드 버퍼들이 동시에 리셋되야 함.
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;		// Optional

    if (!ensure(vkCreateCommandPool(g_rhi_vk->Device, &poolInfo, nullptr, &CommandPool) == VK_SUCCESS))
        return false;

    return true;
}

jCommandBuffer_Vulkan jCommandBufferManager_Vulkan::GetOrCreateCommandBuffer()
{
    if (PendingCommandBuffers.size() > 0)
    {
        for (int32 i = 0; i < (int32)PendingCommandBuffers.size(); ++i)
        {
            const VkResult Result = vkGetFenceStatus(g_rhi_vk->Device, (VkFence)PendingCommandBuffers[i].GetFenceHandle());
            if (Result == VK_SUCCESS)		// VK_SUCCESS 면 Signaled
            {
                jCommandBuffer_Vulkan commandBuffer = PendingCommandBuffers[i];
                PendingCommandBuffers.erase(PendingCommandBuffers.begin() + i);

                UsingCommandBuffers.push_back(commandBuffer);
                commandBuffer.Reset();

                return commandBuffer;
            }
        }
    }

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = GetPool();

    // VK_COMMAND_BUFFER_LEVEL_PRIMARY : 실행을 위해 Queue를 제출할 수 있으면 다른 커맨드버퍼로 부터 호출될 수 없다.
    // VK_COMMAND_BUFFER_LEVEL_SECONDARY : 직접 제출할 수 없으며, Primary command buffer 로 부터 호출될 수 있다.
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    jCommandBuffer_Vulkan commandBuffer;
    if (!ensure(vkAllocateCommandBuffers(g_rhi_vk->Device, &allocInfo, &commandBuffer.GetRef()) == VK_SUCCESS))
        return commandBuffer;

    commandBuffer.SetFence(g_rhi_vk->FenceManager.GetOrCreateFence());

    UsingCommandBuffers.push_back(commandBuffer);

    return commandBuffer;
}

void jCommandBufferManager_Vulkan::ReturnCommandBuffer(jCommandBuffer_Vulkan commandBuffer)
{
    for (int32 i = 0; i < UsingCommandBuffers.size(); ++i)
    {
        if (UsingCommandBuffers[i].GetHandle() == commandBuffer.GetHandle())
        {
            UsingCommandBuffers.erase(UsingCommandBuffers.begin() + i);
            break;
        }
    }
    PendingCommandBuffers.push_back(commandBuffer);
}