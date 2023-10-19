#pragma once
#include "jDrawCommand.h"
#include "RHI/jShaderBindingsLayout.h"
#include "RHI/jRHI.h"
#include "RHI/jPipelineStateInfo.h"

class jObject;
struct jRenderFrameContext;
class jView;
class jRenderPass;
struct jShader;

//////////////////////////////////////////////////////////////////////////
// // Point light jDrawCommand generator
class jPointLightDrawCommandGenerator : public jDrawCommandGenerator
{
public:
    static jObject* PointLightSphere;

    struct jPointLightPushConstant
    {
        jPointLightPushConstant() = default;
        jPointLightPushConstant(const Matrix& InMVP) : MVP(InMVP) {}
        Matrix MVP;
    };

    jPointLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances);

    virtual void Initialize(int32 InRTWidth, int32 InRTHeight) override;
    virtual void GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
        , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex) override;

    jRasterizationStateInfo* RasterizationState = nullptr;
    jDepthStencilStateInfo* DepthStencilState = nullptr;
    jBlendingStateInfo* BlendingState = nullptr;
    jPipelineStateFixedInfo PipelineStateFixedInfo;

    jGraphicsPipelineShader Shader;
    std::shared_ptr<IUniformBufferBlock> UniformBuffer;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance;
    const jShaderBindingInstanceArray& ShaderBindingInstances;
};
