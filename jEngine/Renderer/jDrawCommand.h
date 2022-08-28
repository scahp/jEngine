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
    jDrawCommand() = default;
    jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* view, jRenderObject* renderObject
        , jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed, std::vector<jShaderBindingInstance*> shaderBindingInstances);

    void PrepareToDraw(bool bPositionOnly);
    void Draw();

    std::vector<jShaderBindingInstance*> ShaderBindingInstances;

    jView* View = nullptr;
    jRenderPass* RenderPass = nullptr;
    jShader* Shader = nullptr;
    jRenderObject* RenderObject = nullptr;
    jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
    jPipelineStateInfo_Vulkan* CurrentPipelineStateInfo = nullptr;
    std::vector<const jVertexBuffer*> VertexBuffers;
    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
};