#include "pch.h"
#include "jRenderFrameContext_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "../jCommandBufferManager.h"

void jRenderFrameContext_Vulkan::QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore)
{
    if (CommandBuffer)
    {
        g_rhi->QueueSubmit(shared_from_this(), InSignalSemaphore);
        g_rhi->GetCommandBufferManager()->ReturnCommandBuffer(CommandBuffer);

        // get new commandbuffer
        CommandBuffer = g_rhi_vk->CommandBufferManager->GetOrCreateCommandBuffer();
        g_rhi_vk->Swapchain->Images[FrameIndex]->CommandBufferFence = (VkFence)CommandBuffer->GetFenceHandle();
    }
}
