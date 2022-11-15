#pragma once

#include "jDrawCommand.h"
#include "RHI/Vulkan/jQueryPoolOcclusion_Vulkan.h"

class jView;
struct jSceneRenderTarget;
struct jRenderFrameContext;

// Light 별로 렌더링할 정보들을 갖고 있는 객체
struct jShadowDrawInfo
{
    jShadowDrawInfo() = default;
    jShadowDrawInfo(const jViewLight& InViewLight)
    {
        ViewLight = InViewLight;
    }

    const jViewLight& GetViewLight() const
    {
        return ViewLight;
    }

    const std::shared_ptr<jRenderTarget>& GetShadowMapPtr() const
    {
        return ViewLight.ShadowMapPtr;
    }

    jViewLight ViewLight;
    jRenderPass* ShadowMapRenderPass = nullptr;
    std::vector<jDrawCommand> DrawCommands;
};

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
    virtual void BasePass();

    void DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass);

    virtual void PostProcess();

    void SetupShadowPass();
    void SetupBasePass();

    virtual void Render();

    bool UseForwardRenderer = false;

    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
    jView View;

    std::future<void> ShadowPassSetupFinishEvent;
    std::future<void> BasePassSetupFinishEvent;

    std::vector<jShadowDrawInfo> ShadowDrawInfo;
    std::vector<jDrawCommand> BasePasses;

    jRenderPass* BaseRenderPass = nullptr;

    //jQueryOcclusion_Vulkan ShadowpassOcclusionTest;
    jQueryOcclusion_Vulkan BasepassOcclusionTest;

    // Current FrameIndex
    int32 FrameIndex = 0;

    // Thread per task for PassSetup
    const int32 MaxPassSetupTaskPerThreadCount = 100;
};
