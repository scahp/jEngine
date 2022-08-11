#pragma once

class jRenderObject;
class jView;
class jRenderPass;
struct jShader;
struct jPipelineStateFixedInfo;
struct jShaderBindingInstance;

class jDrawCommand
{
public:
    jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* view, jRenderObject* renderObject
        , jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed, std::vector<const jShaderBindingInstance*> shaderBindingInstances);

    void PrepareToDraw(bool bPositionOnly);

    void Draw();

    std::vector<const jShaderBindingInstance*> ShaderBindingInstances;

    jView* View = nullptr;
    jRenderPass* RenderPass = nullptr;
    jShader* Shader = nullptr;
    jRenderObject* RenderObject = nullptr;
    jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
    jPipelineStateInfo_Vulkan* CurrentPipelineStateInfo = nullptr;
    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
};