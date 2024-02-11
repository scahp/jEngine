#pragma once
#include "../jRenderFrameContext.h"

class jCommandBuffer;
struct jSceneRenderTarget;

struct jRenderFrameContext_DX12 : public jRenderFrameContext
{
    jRenderFrameContext_DX12() = default;
    jRenderFrameContext_DX12(jCommandBuffer* InCommandBuffer)
        : jRenderFrameContext(InCommandBuffer)
    {}
    virtual ~jRenderFrameContext_DX12() {}

    virtual std::shared_ptr<jRenderFrameContext> CreateRenderFrameContextAsync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync = nullptr) const override;
    virtual std::shared_ptr<jSyncAcrossCommandQueue_DX12> QueueSubmitCurrentActiveCommandBuffer();
    virtual std::shared_ptr<jSyncAcrossCommandQueue> SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete = true) override;
};