#include "pch.h"
#include "jRenderFrameContext.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jCommandBufferManager.h"

jRenderFrameContext::~jRenderFrameContext()
{
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

void jRenderFrameContext::Destroy()
{
    if (CommandBuffer)
    {
        auto CommandBufferManager = g_rhi->GetCommandBufferManager(CommandBuffer->Type);

        check(CommandBufferManager);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer);
        CommandBuffer = nullptr;
    }

    FrameIndex = -1;
}
