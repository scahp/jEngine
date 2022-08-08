#pragma once

class jView;
struct jSceneRenderTarget;

class jRenderer
{
public:
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

    jSceneRenderTarget* SceneRenderTarget = nullptr;
    jView View;
};
