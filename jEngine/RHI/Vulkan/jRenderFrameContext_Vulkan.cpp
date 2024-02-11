#include "pch.h"
#include "jRenderFrameContext_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "../jCommandBufferManager.h"

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
    std::shared_ptr<jSyncAcrossCommandQueue_DX12> CommandQueueSyncObjectPtr;

    jSwapchainImage_Vulkan* SwapchainImage_Vulkan = (jSwapchainImage_Vulkan*)g_rhi->GetSwapchainImage(FrameIndex);
    if (CommandBuffer->Type == ECommandBufferType::GRAPHICS)
    {
        QueueSubmitCurrentActiveCommandBuffer(SwapchainImage_Vulkan->GraphicQueueSubmitSemaphore, bWaitUntilExecuteComplete);
    }
    else
    {
        QueueSubmitCurrentActiveCommandBuffer(SwapchainImage_Vulkan->ComputeQueueSubmitSemaphore, bWaitUntilExecuteComplete);
    }
    return CommandQueueSyncObjectPtr;
}

void jRenderFrameContext_Vulkan::QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore, bool bWaitUntilExecuteComplete)
{
    if (CommandBuffer)
    {
        CommandBuffer->End();

        jCommandBufferManager_Vulkan* CommandBufferManager = (jCommandBufferManager_Vulkan*)g_rhi->GetCommandBufferManager2(CommandBuffer->Type);

        g_rhi->QueueSubmit(shared_from_this(), InSignalSemaphore);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer);

        // get new commandbuffer
        CommandBuffer = CommandBufferManager->GetOrCreateCommandBuffer();
        g_rhi_vk->Swapchain->Images[FrameIndex]->CommandBufferFence = (VkFence)CommandBuffer->GetFenceHandle();
    }
}
