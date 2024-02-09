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
        jCommandBufferManager* CommandBufferManager = nullptr;
        switch (CommandBuffer->Type)
        {
        case ECommandBufferType::GRAPHICS:
            CommandBufferManager = g_rhi_dx12->GetCommandBufferManager();
            break;
        case ECommandBufferType::COMPUTE:
            CommandBufferManager = g_rhi_dx12->GetComputeCommandBufferManager();
            break;
        case ECommandBufferType::COPY:
            CommandBufferManager = g_rhi_dx12->GetCopyCommandBufferManager();
            break;
        default:
            check(0);
            break;
        }

        check(CommandBufferManager);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer);
        CommandBuffer = nullptr;
    }

    FrameIndex = -1;
}
