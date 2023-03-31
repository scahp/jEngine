#include "pch.h"
#include "jCommandBufferManager_Vulkan.h"
#include "../jRHI_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jCommandBuffer_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jCommandBuffer_Vulkan::Begin() const
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 커맨드가 한번 실행된다음에 다시 기록됨
    // VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : Single Render Pass 범위에 있는 Secondary Command Buffer.
    // VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 실행 대기중인 동안에 다시 서밋 될 수 있음.
    beginInfo.flags = 0;					// Optional

    // 이 플래그는 Secondary command buffer를 위해서만 사용하며, Primary command buffer 로 부터 상속받을 상태를 명시함.
    beginInfo.pInheritanceInfo = nullptr;	// Optional

    if (!ensure(vkBeginCommandBuffer(CommandBuffer, &beginInfo) == VK_SUCCESS))
        return false;

    return true;
}

bool jCommandBuffer_Vulkan::End() const
{
    if (!ensure(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS))
        return false;

    return true;
}

//////////////////////////////////////////////////////////////////////////
// jCommandBufferManager_Vulkan
//////////////////////////////////////////////////////////////////////////
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

void jCommandBufferManager_Vulkan::Release()
{
    ReleaseInternal();
}

void jCommandBufferManager_Vulkan::ReleaseInternal()
{
    //// Command buffer pool 을 다시 만들기 보다 있는 커맨드 버퍼 풀을 cleanup 하고 재사용 함.
    //vkFreeCommandBuffers(device, CommandBufferManager.GetPool(), static_cast<uint32>(commandBuffers.size()), commandBuffers.data());

    for (auto& iter : UsingCommandBuffers)
    {
        iter->GetFence()->WaitForFence();
        g_rhi->GetFenceManager()->ReturnFence(iter->GetFence());
        delete iter;
    }
    UsingCommandBuffers.clear();

    for (auto& iter : PendingCommandBuffers)
    {
        iter->GetFence()->WaitForFence();
        g_rhi->GetFenceManager()->ReturnFence(iter->GetFence());
        delete iter;
    }
    PendingCommandBuffers.clear();

    if (CommandPool)
    {
        vkDestroyCommandPool(g_rhi_vk->Device, CommandPool, nullptr);
        CommandPool = nullptr;
    }
}

jCommandBuffer* jCommandBufferManager_Vulkan::GetOrCreateCommandBuffer()
{
    if (PendingCommandBuffers.size() > 0)
    {
        for (int32 i = 0; i < (int32)PendingCommandBuffers.size(); ++i)
        {
            const VkResult Result = vkGetFenceStatus(g_rhi_vk->Device, (VkFence)PendingCommandBuffers[i]->GetFenceHandle());
            if (Result == VK_SUCCESS)		// VK_SUCCESS 면 Signaled
            {
                jCommandBuffer_Vulkan* commandBuffer = PendingCommandBuffers[i];
                PendingCommandBuffers.erase(PendingCommandBuffers.begin() + i);

                UsingCommandBuffers.push_back(commandBuffer);
                commandBuffer->Reset();

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

    VkCommandBuffer vkCommandBuffer = nullptr;
    if (!ensure(vkAllocateCommandBuffers(g_rhi_vk->Device, &allocInfo, &vkCommandBuffer) == VK_SUCCESS))
    {
        return nullptr;
    }

    auto newCommandBuffer = new jCommandBuffer_Vulkan();
    newCommandBuffer->GetRef() = vkCommandBuffer;
    newCommandBuffer->SetFence(g_rhi_vk->FenceManager.GetOrCreateFence());

    UsingCommandBuffers.push_back(newCommandBuffer);

    return newCommandBuffer;
}

void jCommandBufferManager_Vulkan::ReturnCommandBuffer(jCommandBuffer* commandBuffer)
{
    check(commandBuffer);
    for (int32 i = 0; i < UsingCommandBuffers.size(); ++i)
    {
        if (UsingCommandBuffers[i]->GetHandle() == commandBuffer->GetHandle())
        {
            UsingCommandBuffers.erase(UsingCommandBuffers.begin() + i);
            PendingCommandBuffers.push_back((jCommandBuffer_Vulkan*)commandBuffer);
            return;
        }
    }
}
