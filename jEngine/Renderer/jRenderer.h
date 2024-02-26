#pragma once

#include "jDrawCommand.h"

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

class IRenderer
{
public:
    IRenderer(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView& InView)
		: RenderFrameContextPtr(InRenderFrameContextPtr), View(InView)
	{}

    virtual ~IRenderer() {}

    virtual void Setup() {}
    virtual void Render() {}
    virtual void DebugPasses();
    virtual void UIPass();

	std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
	jView View;
	std::vector<std::shared_ptr<jTexture>> DebugRTs;
};

class jRenderer : public IRenderer
{
public:
    jRenderer() = default;
    using IRenderer::IRenderer;

    virtual ~jRenderer() {}

	virtual void Setup() override;
	virtual void Render() override;

    virtual void ShadowPass();
    virtual void BasePass();
    void DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass);

    virtual void AtmosphericShadow();
    
    virtual void AOPass();
    virtual std::shared_ptr<jTexture> SSAO();
    virtual std::shared_ptr<jTexture> RTAO();

    virtual void PostProcess();

    void SetupShadowPass();
    void SetupBasePass();

    std::future<void> ShadowPassSetupCompleteEvent;
    std::future<void> BasePassSetupCompleteEvent;

    std::vector<jShadowDrawInfo> ShadowDrawInfo;
    std::vector<jDrawCommand> BasePasses;

    jRenderPass* BaseRenderPass = nullptr;

    //jQueryOcclusion_Vulkan ShadowpassOcclusionTest;
    // jQueryOcclusion_Vulkan BasepassOcclusionTest;

    // Current FrameIndex
    int32 FrameIndex = 0;

    // Thread per task for PassSetup
    const int32 MaxPassSetupTaskPerThreadCount = 100;
};
