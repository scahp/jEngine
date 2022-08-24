﻿#pragma once

#include "jRenderer.h"
#include "jDrawCommand.h"

class jRenderPass;

class jForwardRenderer : public jRenderer
{
public:
    using jRenderer::jRenderer;

    virtual void Setup() override;

    virtual void Render() override;
    virtual void ShadowPass() override;
    virtual void OpaquePass() override;
    virtual void TranslucentPass() override;

    void SetupShadowPass();
    void SetupBasePass();

    // Pass 별 Context 를 만들어서 넣도록 할 예정
    jView ShadowView;

    std::future<void> ShadowPassSetupFinishEvent;
    std::future<void> BasePassSetupFinishEvent;

    std::vector<jDrawCommand> ShadowPasses;
    std::vector<jDrawCommand> BasePasses;

    jRenderPass* ShadowMapRenderPass = nullptr;
    jRenderPass* OpaqueRenderPass = nullptr;
};

