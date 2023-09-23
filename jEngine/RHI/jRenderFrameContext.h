#pragma once

class jCommandBuffer;
struct jSceneRenderTarget;

struct jRenderFrameContext : public std::enable_shared_from_this<jRenderFrameContext>
{
    jRenderFrameContext() = default;
    jRenderFrameContext(jCommandBuffer* InCommandBuffer)
        : CommandBuffer(InCommandBuffer)
    {}
    virtual ~jRenderFrameContext();

    virtual void Destroy();

    FORCEINLINE virtual jCommandBuffer* GetActiveCommandBuffer() const { return CommandBuffer; }
    virtual bool BeginActiveCommandBuffer();
    virtual bool EndActiveCommandBuffer();

public:
    jSceneRenderTarget* SceneRenderTarget = nullptr;
    uint32 FrameIndex = -1;
    bool UseForwardRenderer = true;
    bool IsBeginActiveCommandbuffer = false;

protected:
    jCommandBuffer* CommandBuffer = nullptr;
};