#pragma once

class jCommandBuffer;
struct jSceneRenderTarget;
class jSemaphore;

struct jRenderFrameContext : public std::enable_shared_from_this<jRenderFrameContext>
{
    jRenderFrameContext() = default;
    jRenderFrameContext(jCommandBuffer* InCommandBuffer)
        : CommandBuffer(InCommandBuffer)
    {}
    virtual ~jRenderFrameContext();

    void Destroy();

    FORCEINLINE jCommandBuffer* GetActiveCommandBuffer() const { return CommandBuffer; }
    bool BeginActiveCommandBuffer();
    bool EndActiveCommandBuffer();
    void QueueSubmitCurrentActiveCommandBuffer(jSemaphore* InSignalSemaphore);

public:
    jSceneRenderTarget* SceneRenderTarget = nullptr;
    uint32 FrameIndex = -1;
    jSemaphore* CurrentWaitSemaphore = nullptr;
    bool UseForwardRenderer = true;
    bool IsBeginActiveCommandbuffer = false;

private:
    jCommandBuffer* CommandBuffer = nullptr;
};