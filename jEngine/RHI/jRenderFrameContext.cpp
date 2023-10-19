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

void jRenderFrameContext::Destroy()
{
    if (SceneRenderTargetPtr)
    {
        SceneRenderTargetPtr->Return();
        SceneRenderTargetPtr.reset();
    }

    if (CommandBuffer)
    {
        check(g_rhi->GetCommandBufferManager());
        g_rhi->GetCommandBufferManager()->ReturnCommandBuffer(CommandBuffer);
        CommandBuffer = nullptr;
    }

    FrameIndex = -1;
}
