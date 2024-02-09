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

    virtual std::shared_ptr<jRenderFrameContext> CreateRenderFrameContextAsync(const std::vector<jFence*>& InPrerequisites = std::vector<jFence*>()) const override;
    virtual void QueueSubmitCurrentActiveCommandBuffer();
    virtual void SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete = true) override;
};