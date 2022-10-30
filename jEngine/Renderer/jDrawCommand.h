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
    jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* view, jRenderObject* renderObject
        , jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const std::shared_ptr<jPushConstant>& pushConstantPtr
        , jQuery* occlusionQuery = nullptr);
    jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jViewLight* viewLight, jRenderObject* renderObject
        , jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const std::shared_ptr<jPushConstant>& pushConstantPtr
        , jQuery* occlusionQuery = nullptr);

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
};