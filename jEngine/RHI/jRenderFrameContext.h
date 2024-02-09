#pragma once

class jCommandBuffer;
struct jSceneRenderTarget;
class jRaytracingScene;

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

    virtual void SubmitCurrentActiveCommandBuffer(ECurrentRenderPass InCurrentRenderPass, bool IsWaitUntilFinish = true) {}

    virtual std::shared_ptr<jRenderFrameContext> CreateRenderFrameContextAsync(const std::vector<jFence*>& InPrerequisites = std::vector<jFence*>()) const { return nullptr; }
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