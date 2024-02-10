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

    virtual std::shared_ptr<jRenderFrameContext> CreateRenderFrameContextAsync(const std::shared_ptr<jCommandQueueAcrossSyncObject>& InSync = nullptr) const override;
    virtual std::shared_ptr<jCommandQueueAcrossSyncObject> SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete = true) override;


public:
    jSemaphore* CurrentWaitSemaphore = nullptr;

private:
    virtual void QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore, bool bWaitUntilExecuteComplete = true);
};
