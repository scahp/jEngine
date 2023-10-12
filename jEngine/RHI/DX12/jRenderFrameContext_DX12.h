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

    virtual void QueueSubmitCurrentActiveCommandBuffer();
    virtual void SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass) override;
};