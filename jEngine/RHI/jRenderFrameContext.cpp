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
    if (CommandBuffer)
    {
        auto CommandBufferManager = (jCommandBufferManager_DX12*)g_rhi->GetCommandBufferManager2(CommandBuffer->Type);
        
        if (!CommandBuffer->IsEnd())
        {
            CommandBufferManager->ExecuteCommandList((jCommandBuffer_DX12*)CommandBuffer);
        }

        check(CommandBufferManager);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer);
        CommandBuffer = nullptr;
    }

    FrameIndex = -1;
}
