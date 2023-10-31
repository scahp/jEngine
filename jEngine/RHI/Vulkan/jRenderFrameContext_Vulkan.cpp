#include "pch.h"
#include "jRenderFrameContext_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "../jCommandBufferManager.h"

void jRenderFrameContext_Vulkan::SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass)
{
    jSwapchainImage_Vulkan* SwapchainImage_Vulkan = (jSwapchainImage_Vulkan*)g_rhi->GetSwapchainImage(FrameIndex);

    switch(InCurrentRenderPass)
    {
    case jRenderFrameContext::ShadowPass:
        QueueSubmitCurrentActiveCommandBuffer(SwapchainImage_Vulkan->RenderFinishedAfterShadow);
        break;
    case jRenderFrameContext::BasePass:
        QueueSubmitCurrentActiveCommandBuffer(SwapchainImage_Vulkan->RenderFinishedAfterBasePass);
        break;
    default:
        break;
    }
}

void jRenderFrameContext_Vulkan::QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore)
{
    if (CommandBuffer)
    {
        CommandBuffer->End();

        g_rhi->QueueSubmit(shared_from_this(), InSignalSemaphore);
        g_rhi->GetCommandBufferManager()->ReturnCommandBuffer(CommandBuffer);

        // get new commandbuffer
        CommandBuffer = g_rhi_vk->CommandBufferManager->GetOrCreateCommandBuffer();
        g_rhi_vk->Swapchain->Images[FrameIndex]->CommandBufferFence = (VkFence)CommandBuffer->GetFenceHandle();
    }
}
