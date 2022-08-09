#pragma once

class jView;
struct jSceneRenderTarget;
struct jRenderFrameContext;

class jRenderer
{
public:
    jRenderer() = default;
    jRenderer(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView& InView)
        : RenderFrameContextPtr(InRenderFrameContextPtr), View(InView)
    {}

    virtual ~jRenderer() {}

    virtual void Setup() = 0;
    virtual void ShadowPass() = 0;
    virtual void OpaquePass() = 0;
    virtual void TranslucentPass() = 0;

    virtual void Render()
    {
        ShadowPass();
        OpaquePass();
        TranslucentPass();
    }

    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
    jView View;
};
