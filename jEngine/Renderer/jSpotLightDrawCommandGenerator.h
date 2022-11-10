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
// Spot light jDrawCommand generator
class jSpotLightDrawCommandGenerator : public jDrawCommandGenerator
{
public:
    static jObject* SpotLightUIQuad;

    // Rect 정보는 Push Constant 로 전달하는 것으로 하자
    struct jSpotLightPushConstant
    {
        Vector2 Pos;
        Vector2 Size;
        Vector2 PixelSize;
        float Depth;
        float padding;
    };

    jSpotLightDrawCommandGenerator(const jShaderBindingInstanceArray& InShaderBindingInstances);

    virtual void Initialize(int32 InRTWidth, int32 InRTHeight) override;
    virtual void GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
        , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex) override;

    jRasterizationStateInfo* RasterizationState = nullptr;
    jMultisampleStateInfo* MultisampleState = nullptr;
    jDepthStencilStateInfo* DepthStencilState = nullptr;
    jBlendingStateInfo* BlendingState = nullptr;
    jPipelineStateFixedInfo PipelineStateFixedInfo;

    jShader* Shader = nullptr;
    Vector2 ScreenSize;
    const jShaderBindingInstanceArray& ShaderBindingInstances;
};
