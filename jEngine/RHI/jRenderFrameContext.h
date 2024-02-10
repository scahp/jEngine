#pragma once

class jCommandBuffer;
class jRaytracingScene;
struct jSceneRenderTarget;
struct jCommandQueueAcrossSyncObject;

struct jRenderFrameContext : public std::enable_shared_from_this<jRenderFrameContext>
{
    enum ECurrentRenderPass
    {
        None,
        ShadowPass,
        BasePass,
    };

    jRenderFrameContext() = default;
    jRenderFrameContext(jCommandBuffer* InCommandBuffer)
        : CommandBuffer(InCommandBuffer)
    {}
    virtual ~jRenderFrameContext();

    virtual void Destroy();

    FORCEINLINE virtual jCommandBuffer* GetActiveCommandBuffer() const { return CommandBuffer; }
    virtual bool BeginActiveCommandBuffer();
    virtual bool EndActiveCommandBuffer();

    virtual std::shared_ptr<jCommandQueueAcrossSyncObject> SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool IsWaitUntilFinish = true) { return std::shared_ptr<jCommandQueueAcrossSyncObject>(); }

    virtual std::shared_ptr<jRenderFrameContext> CreateRenderFrameContextAsync(const std::shared_ptr<jCommandQueueAcrossSyncObject>& InSync = nullptr) const { return nullptr; }
    ECommandBufferType GetCommandBufferType() const { check(CommandBuffer); return CommandBuffer->Type; }

public:
    jRaytracingScene* RaytracingScene = nullptr;
    std::shared_ptr<jSceneRenderTarget> SceneRenderTargetPtr = nullptr;
    uint32 FrameIndex = -1;
    bool UseForwardRenderer = true;
    bool IsBeginActiveCommandbuffer = false;

protected:
    jCommandBuffer* CommandBuffer = nullptr;
};