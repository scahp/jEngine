#pragma once

#include "jRenderer.h"
#include "jDrawCommand.h"

class jRenderPass;

class jForwardRenderer : public jRenderer
{
public:
    virtual void Setup() override;
    virtual void ShadowPass() override;
    virtual void OpaquePass() override;
    virtual void TranslucentPass() override;
    
    // Pass 별 Context 를 만들어서 넣도록 할 예정
    jView ShadowView;

    jCommandBuffer* CommandBuffer = nullptr;
    jRenderPass* ShadowMapRenderPass = nullptr;
    std::vector<jDrawCommand> ShadowPasses;

    jRenderPass* OpaqueRenderPass = nullptr;
};

