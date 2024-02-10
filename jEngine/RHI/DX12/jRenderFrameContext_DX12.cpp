#include "pch.h"
#include "jRenderFrameContext_DX12.h"
#include "Renderer/jSceneRenderTargets.h"
#include "../jCommandBufferManager.h"
#include "jSwapchain_DX12.h"

std::shared_ptr<jRenderFrameContext> jRenderFrameContext_DX12::CreateRenderFrameContextAsync(const std::shared_ptr<jCommandQueueAcrossSyncObject>& InSync) const
{
    if (InSync)
        InSync->WaitCommandQueueAcrossSync(ECommandBufferType::COMPUTE);

    auto NewRenderFrameContext = std::make_shared<jRenderFrameContext_DX12>();
    *NewRenderFrameContext = *this;

    auto ComputeCommandBufferManager = g_rhi->GetComputeCommandBufferManager();
    NewRenderFrameContext->CommandBuffer = ComputeCommandBufferManager->GetOrCreateCommandBuffer();

    return NewRenderFrameContext;
}

std::shared_ptr<jCommandQueueAcrossSyncObject_DX12> jRenderFrameContext_DX12::QueueSubmitCurrentActiveCommandBuffer()
{
    std::shared_ptr<jCommandQueueAcrossSyncObject_DX12> CommandQueueSyncObjectPtr;
    if (CommandBuffer)
    {
        jCommandBuffer_DX12* CommandBuffer_DX12 = (jCommandBuffer_DX12*)CommandBuffer;

        auto CommandBufferManager = g_rhi_dx12->GetCommandBufferManager();
        CommandQueueSyncObjectPtr = CommandBufferManager->ExecuteCommandList(CommandBuffer_DX12);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer_DX12);

        // get new commandbuffer
        CommandBuffer = CommandBufferManager->GetOrCreateCommandBuffer();

        if (CommandBuffer->Type == ECommandBufferType::GRAPHICS)
            g_rhi_dx12->Swapchain->Images[FrameIndex]->FenceValue = CommandBuffer_DX12->FenceValue;
    }
    return CommandQueueSyncObjectPtr;
}

std::shared_ptr<jCommandQueueAcrossSyncObject> jRenderFrameContext_DX12::SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete)
{
    std::shared_ptr<jCommandQueueAcrossSyncObject_DX12> CommandQueueSyncObjectPtr;
    if (CommandBuffer)
    {
        jCommandBuffer_DX12* CommandBuffer_DX12 = (jCommandBuffer_DX12*)CommandBuffer;
        jCommandBufferManager_DX12* CommandBufferManager = (jCommandBufferManager_DX12*)g_rhi->GetCommandBufferManager2(CommandBuffer->Type);

        CommandQueueSyncObjectPtr = CommandBufferManager->ExecuteCommandList(CommandBuffer_DX12, bWaitUntilExecuteComplete);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer_DX12);

        // get new commandbuffer
        CommandBuffer = CommandBufferManager->GetOrCreateCommandBuffer();
        if (CommandBuffer->Type == ECommandBufferType::GRAPHICS)
            g_rhi_dx12->Swapchain->Images[FrameIndex]->FenceValue = CommandBuffer_DX12->FenceValue;
    }
    return CommandQueueSyncObjectPtr;
}
