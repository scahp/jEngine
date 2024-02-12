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
    virtual ~jRenderFrameContext_Vulkan() { Destroy(); }
    virtual void Destroy() override;

    virtual std::shared_ptr<jRenderFrameContext> CreateRenderFrameContextAsync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync = nullptr) const override;
    virtual std::shared_ptr<jSyncAcrossCommandQueue> SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool bWaitUntilExecuteComplete = true) override;


public:
    jSemaphore* CurrentWaitSemaphore = nullptr;

private:
    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore, bool bWaitUntilExecuteComplete = true);
};
