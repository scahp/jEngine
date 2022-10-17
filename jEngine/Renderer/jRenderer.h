#pragma once

#include "jDrawCommand.h"
#include "RHI/Vulkan/jQueryPoolOcclusion_Vulkan.h"

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

    virtual void Setup();
    virtual void ShadowPass();
    virtual void OpaquePass();
    virtual void TranslucentPass();
    virtual void PostProcess();

    void SetupShadowPass();
    void SetupBasePass();

    virtual void Render();

    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
    jView View;

    // Pass 별 Context 를 만들어서 넣도록 할 예정
    jView ShadowView;

    std::future<void> ShadowPassSetupFinishEvent;
    std::future<void> BasePassSetupFinishEvent;

    std::vector<jDrawCommand> ShadowPasses;
    std::vector<jDrawCommand> BasePasses;

    jRenderPass* ShadowMapRenderPass = nullptr;
    jRenderPass* OpaqueRenderPass = nullptr;

    jQueryOcclusion_Vulkan ShadowpassOcclusionTest;
    jQueryOcclusion_Vulkan BasepassOcclusionTest;

    int32 FrameIndex = 0;
};
