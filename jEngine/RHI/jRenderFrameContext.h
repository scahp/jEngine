#pragma once

class jCommandBuffer;
struct jSceneRenderTarget;

struct jRenderFrameContext : public std::enable_shared_from_this<jRenderFrameContext>
{
    virtual ~jRenderFrameContext();
    jCommandBuffer* CommandBuffer = nullptr;
    jSceneRenderTarget* SceneRenderTarget = nullptr;
    uint32 FrameIndex = -1;
    bool UseForwardRenderer = true;

    void Destroy();
};