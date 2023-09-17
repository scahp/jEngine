#pragma once
#include "jDrawCommand.h"
#include "RHI/jShaderBindingsLayout.h"
#include "RHI/jRHI.h"
#include "RHI/jPipelineStateInfo.h"
#include "Shader/jShader.h"

class jObject;
struct jRenderFrameContext;
class jView;
class jRenderPass;
struct jShader;

//////////////////////////////////////////////////////////////////////////
 // Directional light jDrawCommand generator
class jDirectionalLightDrawCommandGenerator : public jDrawCommandGenerator
{
public:
    static jObject* GlobalFullscreenPrimitive;

    jDirectionalLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances);

    virtual void Initialize(int32 InRTWidth, int32 InRTHeight) override;
    virtual void GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
        , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex) override;

    jRasterizationStateInfo* RasterizationState = nullptr;
    jDepthStencilStateInfo* DepthStencilState = nullptr;
    jBlendingStateInfo* BlendingState = nullptr;
    jPipelineStateFixedInfo PipelineStateFixedInfo;

    jGraphicsPipelineShader Shader;
    const jShaderBindingInstanceArray& ShaderBindingInstances;
};
