#pragma once
#include "../jRenderFrameContext.h"

class jCommandBuffer;
struct jSceneRenderTarget;
class jSemaphore;

struct jRenderFrameContext_Vulkan : public jRenderFrameContext
{
    jRenderFrameContext_Vulkan() = default;
    jRenderFrameContext_Vulkan(jCommandBuffer* InCommandBuffer)
        : jRenderFrameContext(InCommandBuffer)
    {}
    virtual ~jRenderFrameContext_Vulkan() {}

    virtual void SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete = true) override;

    virtual void QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore, bool bWaitUntilExecuteComplete = true);

public:
    jSemaphore* CurrentWaitSemaphore = nullptr;
};