#include "pch.h"
#include "jRenderFrameContext_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "../jCommandBufferManager.h"

void jRenderFrameContext_Vulkan::Destroy()
{
    if (CommandBuffer)
    {
        if (!CommandBuffer->IsEnd())
        {
            auto CommandBufferManager = (jCommandBufferManager_Vulkan*)g_rhi->GetCommandBufferManager(CommandBuffer->Type);
            CommandBufferManager->QueueSubmit((jCommandBuffer_Vulkan*)CommandBuffer, CurrentWaitSemaphore, CommandBufferManager->GetQueueSubmitSemaphore());
        }
    }

    __super::Destroy();
}

std::shared_ptr<jRenderFrameContext> jRenderFrameContext_Vulkan::CreateRenderFrameContextAsync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync) const
{
    auto NewRenderFrameContext = std::make_shared<jRenderFrameContext_Vulkan>();
    *NewRenderFrameContext = *this;

    auto ComputeCommandBufferManager = g_rhi->GetComputeCommandBufferManager();
    NewRenderFrameContext->CommandBuffer = ComputeCommandBufferManager->GetOrCreateCommandBuffer();

    return NewRenderFrameContext;
}

std::shared_ptr<jSyncAcrossCommandQueue> jRenderFrameContext_Vulkan::SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete)
{
    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> CommandQueueSyncObjectPtr;

    auto CommandBufferManager = (jCommandBufferManager_Vulkan*)g_rhi->GetCommandBufferManager(CommandBuffer->Type);
    check(CommandBufferManager);

    CommandQueueSyncObjectPtr = QueueSubmitCurrentActiveCommandBuffer(CommandBufferManager->GetQueueSubmitSemaphore(), bWaitUntilExecuteComplete);
    return CommandQueueSyncObjectPtr;
}

std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> jRenderFrameContext_Vulkan::QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore, bool bWaitUntilExecuteComplete)
{
    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> CommandQueueSyncObjectPtr;
    if (CommandBuffer)
    {
        CommandBuffer->End();

        jCommandBufferManager_Vulkan* CommandBufferManager = (jCommandBufferManager_Vulkan*)g_rhi->GetCommandBufferManager(CommandBuffer->Type);

        CommandQueueSyncObjectPtr = g_rhi_vk->QueueSubmit(shared_from_this(), InSignalSemaphore);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer);

        // get new commandbuffer
        CommandBuffer = CommandBufferManager->GetOrCreateCommandBuffer();
        
        if (CommandBuffer->Type == ECommandBufferType::GRAPHICS)
            g_rhi_vk->Swapchain->Images[FrameIndex]->CommandBufferFence = (VkFence)CommandBuffer->GetFenceHandle();
    }
    return CommandQueueSyncObjectPtr;
}
