#include "pch.h"
#include "jCommandBufferManager_Vulkan.h"
#include "jRHI_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jSyncAcrossCommandQueue_Vulkan
//////////////////////////////////////////////////////////////////////////
jSyncAcrossCommandQueue_Vulkan::jSyncAcrossCommandQueue_Vulkan(ECommandBufferType InType, jSemaphore_Vulkan* InWaitSemaphore, uint64 InSemaphoreValue)
    : Type(InType), WaitSemaphore(InWaitSemaphore), SemaphoreValue(InSemaphoreValue)
{
    check(WaitSemaphore);
    if (SemaphoreValue == -1)
    {
        SemaphoreValue = WaitSemaphore->GetValue();
    }
}

void jCommandBufferManager_Vulkan::WaitCommandQueueAcrossSync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync)
{
    check(CommandPool);
    check(InSync);

    WaitSemaphoreAcrossQueues.push_back(InSync);
}

void jCommandBufferManager_Vulkan::GetWaitSemaphoreAndValueThenClear(std::vector<VkSemaphore>& InOutSemaphore, std::vector<uint64>& InOutSemaphoreValue)
{
    if (WaitSemaphoreAcrossQueues.empty())
        return;

    check(InOutSemaphore.size() == InOutSemaphoreValue.size());

    InOutSemaphore.reserve(InOutSemaphore.size() + WaitSemaphoreAcrossQueues.size());
    InOutSemaphoreValue.reserve(InOutSemaphoreValue.size() + WaitSemaphoreAcrossQueues.size());

    for(int32 i=0;i< WaitSemaphoreAcrossQueues.size();++i)
    {
        auto Sync_Vulkan = (jSyncAcrossCommandQueue_Vulkan*)WaitSemaphoreAcrossQueues[i].get();
        check(Sync_Vulkan);

        InOutSemaphore.push_back(Sync_Vulkan->WaitSemaphore->Semaphore);
        InOutSemaphoreValue.push_back(Sync_Vulkan->SemaphoreValue);
    }
    WaitSemaphoreAcrossQueues.clear();
}

std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> jCommandBufferManager_Vulkan::QueueSubmit(jCommandBuffer_Vulkan* InCommandBuffer, jSemaphore* InWaitSemaphore, jSemaphore* InSignalSemaphore)
{
    check(InCommandBuffer);

    InCommandBuffer->End();

    auto vkInCommandBuffer = (VkCommandBuffer)InCommandBuffer->GetHandle();
    VkFence vkFence = (VkFence)InCommandBuffer->GetFenceHandle();

    // Submitting the command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    auto CurrentInCommandBufferManager = (jCommandBufferManager_Vulkan*)g_rhi_vk->GetCommandBufferManager(InCommandBuffer->Type);
    check(CurrentInCommandBufferManager);

    std::vector<VkSemaphore> WaitSemaphores;
    std::vector<uint64> WaitSemaphoreValues;
    CurrentInCommandBufferManager->GetWaitSemaphoreAndValueThenClear(WaitSemaphores, WaitSemaphoreValues);

    if (InWaitSemaphore)
    {
        WaitSemaphores.push_back((VkSemaphore)InWaitSemaphore->GetHandle());
        WaitSemaphoreValues.push_back(InWaitSemaphore->GetValue());
    }

    VkPipelineStageFlags WaitStage;
    switch (InCommandBuffer->Type)
    {
    case ECommandBufferType::GRAPHICS:	WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
    case ECommandBufferType::COMPUTE:	WaitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; break;
    case ECommandBufferType::COPY:		WaitStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
    default:
        check(0);
        break;
    }

    uint64 waitValue = 0;
    uint64 signalValue = 0;

    const bool UseWaitTimeline = InWaitSemaphore && InWaitSemaphore->GetType() == ESemaphoreType::TIMELINE;
    const bool UseSignalTimeline = InSignalSemaphore && InSignalSemaphore->GetType() == ESemaphoreType::TIMELINE;

    if (UseWaitTimeline || UseSignalTimeline)
    {
        VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo = {};
        timelineSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        if (UseWaitTimeline)
        {
            waitValue = InWaitSemaphore->GetValue();

            timelineSemaphoreSubmitInfo.waitSemaphoreValueCount = (uint32)WaitSemaphoreValues.size();
            timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = WaitSemaphoreValues.data();
        }
        if (UseSignalTimeline)
        {
            signalValue = InSignalSemaphore->IncrementValue();

            timelineSemaphoreSubmitInfo.signalSemaphoreValueCount = 1;
            timelineSemaphoreSubmitInfo.pSignalSemaphoreValues = &signalValue;
        }
        submitInfo.pNext = &timelineSemaphoreSubmitInfo;
    }

    std::vector<VkPipelineStageFlags> WaitStages(WaitSemaphores.size(), WaitStage);
    submitInfo.pWaitSemaphores = WaitSemaphores.data();
    submitInfo.waitSemaphoreCount = (uint32)WaitSemaphores.size();
    submitInfo.pWaitDstStageMask = WaitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkInCommandBuffer;

    VkSemaphore signalSemaphores[] = { (InSignalSemaphore ? (VkSemaphore)InSignalSemaphore->GetHandle() : nullptr) };
    submitInfo.signalSemaphoreCount = InSignalSemaphore ? 1 : 0;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // SubmitInfo를 동시에 할수도 있음.
    vkResetFences(g_rhi_vk->Device, 1, &vkFence);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

    const jRHI_Vulkan::jQueue_Vulkan& CurrentQueue = g_rhi_vk->GetQueue(InCommandBuffer->Type);

    // 마지막에 Fences 파라메터는 커맨드 버퍼가 모두 실행되고나면 Signaled 될 Fences.
    auto queueSubmitResult = vkQueueSubmit(CurrentQueue.Queue, 1, &submitInfo, vkFence);
    if ((queueSubmitResult == VK_ERROR_OUT_OF_DATE_KHR) || (queueSubmitResult == VK_SUBOPTIMAL_KHR))
    {
        g_rhi_vk->RecreateSwapChain();
    }
    else if (queueSubmitResult != VK_SUCCESS)
    {
        check(0);
    }

    return std::make_shared<jSyncAcrossCommandQueue_Vulkan>(InCommandBuffer->Type, (jSemaphore_Vulkan*)InSignalSemaphore);
}

void jSyncAcrossCommandQueue_Vulkan::WaitSyncAcrossCommandQueue(ECommandBufferType InWaitCommandQueueType)
{
    if (!ensure(InWaitCommandQueueType != Type))
        return;

    auto CommandBufferManager = g_rhi->GetCommandBufferManager(InWaitCommandQueueType);
    check(CommandBufferManager);

    CommandBufferManager->WaitCommandQueueAcrossSync(shared_from_this());
}

//////////////////////////////////////////////////////////////////////////
// jCommandBuffer_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jCommandBuffer_Vulkan::Begin() const
{
    if (!IsClosed)
        return true;

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

    IsClosed = false;

    return true;
}

bool jCommandBuffer_Vulkan::End() const
{
    if (IsClosed)
        return true;

#if USE_RESOURCE_BARRIER_BATCHER
    FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    if (!ensure(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS))
        return false;

    IsClosed = true;

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

    QueueSubmitSemaphore = (jSemaphore_Vulkan*)g_rhi->GetSemaphoreManager()->GetOrCreateSemaphore(ESemaphoreType::TIMELINE);

    return true;
}

void jCommandBufferManager_Vulkan::Release()
{
    ReleaseInternal();
}

void jCommandBufferManager_Vulkan::ReleaseInternal()
{
    jScopedLock s(&CommandListLock);

    //// Command buffer pool 을 다시 만들기 보다 있는 커맨드 버퍼 풀을 cleanup 하고 재사용 함.
    //vkFreeCommandBuffers(device, CommandBufferManager.GetPool(), static_cast<uint32>(commandBuffers.size()), commandBuffers.data());

    for (auto& iter : UsingCommandBuffers)
    {
        iter->GetFence()->WaitForFence();
        g_rhi->GetFenceManager()->ReturnFence(iter->GetFence());
        delete iter;
    }
    UsingCommandBuffers.clear();

    for (auto& iter : AvailableCommandBuffers)
    {
        iter->GetFence()->WaitForFence();
        g_rhi->GetFenceManager()->ReturnFence(iter->GetFence());
        delete iter;
    }
    AvailableCommandBuffers.clear();

    if (CommandPool)
    {
        vkDestroyCommandPool(g_rhi_vk->Device, CommandPool, nullptr);
        CommandPool = nullptr;
    }

    if (QueueSubmitSemaphore)
    {
        g_rhi->GetSemaphoreManager()->ReturnSemaphore(QueueSubmitSemaphore);
        QueueSubmitSemaphore = nullptr;
    }
}

jCommandBuffer* jCommandBufferManager_Vulkan::GetOrCreateCommandBuffer()
{
    jScopedLock s(&CommandListLock);

    jCommandBuffer* SelectedCommandBuffer = nullptr;
    if (AvailableCommandBuffers.size() > 0)
    {
        for (int32 i = 0; i < (int32)AvailableCommandBuffers.size(); ++i)
        {
            const VkResult Result = vkGetFenceStatus(g_rhi_vk->Device, (VkFence)AvailableCommandBuffers[i]->GetFenceHandle());
            if (Result == VK_SUCCESS)		// VK_SUCCESS 면 Signaled
            {
                jCommandBuffer_Vulkan* commandBuffer = AvailableCommandBuffers[i];
                AvailableCommandBuffers.erase(AvailableCommandBuffers.begin() + i);

                UsingCommandBuffers.push_back(commandBuffer);
                commandBuffer->Reset();
                SelectedCommandBuffer = commandBuffer;
                break;
            }
        }
    }

    if (!SelectedCommandBuffer)
    {
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

        auto newCommandBuffer = new jCommandBuffer_Vulkan(CommandBufferType);
        newCommandBuffer->GetRef() = vkCommandBuffer;
        newCommandBuffer->SetFence(g_rhi_vk->FenceManager.GetOrCreateFence());

        UsingCommandBuffers.push_back(newCommandBuffer);

        SelectedCommandBuffer = newCommandBuffer;
    }
    SelectedCommandBuffer->Begin();
    return SelectedCommandBuffer;
}

void jCommandBufferManager_Vulkan::ReturnCommandBuffer(jCommandBuffer* commandBuffer)
{
    jScopedLock s(&CommandListLock);

    check(commandBuffer);
    for (int32 i = 0; i < UsingCommandBuffers.size(); ++i)
    {
        if (UsingCommandBuffers[i]->GetHandle() == commandBuffer->GetHandle())
        {
            UsingCommandBuffers.erase(UsingCommandBuffers.begin() + i);
            AvailableCommandBuffers.push_back((jCommandBuffer_Vulkan*)commandBuffer);
            return;
        }
    }
}
