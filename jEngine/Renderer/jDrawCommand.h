#pragma once

class jRenderObject;
class jView;
class jViewLight;
class jRenderPass;
struct jShader;
struct jPipelineStateFixedInfo;
struct jShaderBindingInstance;

class jDrawCommand
{
public:
    jDrawCommand() = default;
    jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* InView, jRenderObject* InRenderObject
        , jRenderPass* InRenderPass, jShader* InShader, jPipelineStateFixedInfo* InPipelineStateFixed
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const std::shared_ptr<jPushConstant>& InPushConstantPtr
        , jQuery* InOcclusionQuery = nullptr, int32 InSubpassIndex = 0);
    jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jViewLight* InViewLight, jRenderObject* InRenderObject
        , jRenderPass* InRenderPass, jShader* InShader, jPipelineStateFixedInfo* InPipelineStateFixed
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const std::shared_ptr<jPushConstant>& InPushConstantPtr
        , jQuery* InOcclusionQuery = nullptr, int32 InSubpassIndex = 0);

    void PrepareToDraw(bool bPositionOnly);
    void Draw() const;

    jShaderBindingInstanceArray ShaderBindingInstanceArray;

    bool IsViewLight = false;
    union
    {
        jView* View;
        jViewLight* ViewLight;
    };
    jRenderPass* RenderPass = nullptr;
    jShader* Shader = nullptr;
    jRenderObject* RenderObject = nullptr;
    jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
    jPipelineStateInfo_Vulkan* CurrentPipelineStateInfo = nullptr;
    std::shared_ptr<jPushConstant> PushConstantPtr;
    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
    jQuery* OcclusionQuery = nullptr;
    bool IsPositionOnly = false;
    int32 SubpassIndex = 0;
};