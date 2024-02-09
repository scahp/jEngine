#include "pch.h"
#include "jRenderFrameContext_DX12.h"
#include "Renderer/jSceneRenderTargets.h"
#include "../jCommandBufferManager.h"
#include "jSwapchain_DX12.h"

std::shared_ptr<jRenderFrameContext> jRenderFrameContext_DX12::CreateRenderFrameContextAsync(const std::vector<jFence*>& InPrerequisites) const
{
    auto NewRenderFrameContext = std::make_shared<jRenderFrameContext_DX12>();
    *NewRenderFrameContext = *this;

    auto ComputeCommandBufferManager = g_rhi->GetComputeCommandBufferManager();
    NewRenderFrameContext->CommandBuffer = ComputeCommandBufferManager->GetOrCreateCommandBuffer();

    return NewRenderFrameContext;
}

void jRenderFrameContext_DX12::QueueSubmitCurrentActiveCommandBuffer()
{
    if (CommandBuffer)
    {
        jCommandBuffer_DX12* CommandBuffer_DX12 = (jCommandBuffer_DX12*)CommandBuffer;

        auto CommandBufferManager = g_rhi_dx12->GetCommandBufferManager();
        CommandBufferManager->ExecuteCommandList(CommandBuffer_DX12);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer_DX12);

        // get new commandbuffer
        CommandBuffer = CommandBufferManager->GetOrCreateCommandBuffer();
        g_rhi_dx12->Swapchain->Images[FrameIndex]->FenceValue = CommandBuffer_DX12->Owner->Fence->SignalWithNextFenceValue(CommandBuffer_DX12->Owner->GetCommandQueue().Get());
    }
}

void jRenderFrameContext_DX12::SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete)
{
    if (CommandBuffer)
    {
        jCommandBuffer_DX12* CommandBuffer_DX12 = (jCommandBuffer_DX12*)CommandBuffer;
        jCommandBufferManager_DX12* CommandBufferManager = (jCommandBufferManager_DX12*)g_rhi->GetCommandBufferManager2(CommandBuffer->Type);

        CommandBufferManager->ExecuteCommandList(CommandBuffer_DX12);
        CommandBufferManager->ReturnCommandBuffer(CommandBuffer_DX12);

        // get new commandbuffer
        CommandBuffer = CommandBufferManager->GetOrCreateCommandBuffer();
        g_rhi_dx12->Swapchain->Images[FrameIndex]->FenceValue = CommandBuffer_DX12->Owner->Fence->SignalWithNextFenceValue(CommandBuffer_DX12->Owner->GetCommandQueue().Get(), bWaitUntilExecuteComplete);
    }
}
