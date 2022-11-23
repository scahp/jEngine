#include "pch.h"
#include "jRenderFrameContext.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jCommandBufferManager.h"

jRenderFrameContext::~jRenderFrameContext()
{
    Destroy();
}

bool jRenderFrameContext::BeginActiveCommandBuffer()
{
    check(!IsBeginActiveCommandbuffer);
    IsBeginActiveCommandbuffer = true;
    return CommandBuffer->Begin();
}

bool jRenderFrameContext::EndActiveCommandBuffer()
{
    check(IsBeginActiveCommandbuffer);
    IsBeginActiveCommandbuffer = false;
    return CommandBuffer->End();
}

void jRenderFrameContext::QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore)
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

void jRenderFrameContext::Destroy()
{
    if (SceneRenderTarget)
    {
        SceneRenderTarget->Return();
        delete SceneRenderTarget;
        SceneRenderTarget = nullptr;
    }

    if (CommandBuffer)
    {
        check(g_rhi->GetCommandBufferManager());
        g_rhi->GetCommandBufferManager()->ReturnCommandBuffer(CommandBuffer);
        CommandBuffer = nullptr;
    }

    FrameIndex = -1;
}
