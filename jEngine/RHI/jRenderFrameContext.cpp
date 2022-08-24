#include "pch.h"
#include "jRenderFrameContext.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jCommandBufferManager.h"

jRenderFrameContext::~jRenderFrameContext()
{
    Destroy();
}

void jRenderFrameContext::Destroy()
{
    if (SceneRenderTarget)
    {
        if (SceneRenderTarget->IsValid())
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
