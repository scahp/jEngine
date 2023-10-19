#pragma once

class jRenderObject;
class jView;
class jViewLight;
class jRenderPass;
struct jShader;
struct jGraphicsPipelineShader;
struct jPipelineStateFixedInfo;
struct jShaderBindingInstance;
class jMaterial;

class jDrawCommand
{
public:
    jDrawCommand() = default;
    jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView* InView, jRenderObject* InRenderObject
        , jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData = nullptr
        , int32 InSubpassIndex = 0);
    jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jViewLight* InViewLight, jRenderObject* InRenderObject
        , jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData = nullptr
        , int32 InSubpassIndex = 0);
    jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jRenderObject* InRenderObject
        , jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
        , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData = nullptr
        , int32 InSubpassIndex = 0);

    void PrepareToDraw(bool bPositionOnly);
    void Draw() const;

    jShaderBindingInstanceArray ShaderBindingInstanceArray;
    jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;

    bool IsViewLight = false;
    union
    {
        const jView* View;
        const jViewLight* ViewLight;
    };
    jRenderPass* RenderPass = nullptr;
    jGraphicsPipelineShader Shader;
    jRenderObject* RenderObject = nullptr;
    jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
    jPipelineStateInfo* CurrentPipelineStateInfo = nullptr;
    jMaterial* Material = nullptr;
    const jPushConstant* PushConstant = nullptr;
    const jVertexBuffer* OverrideInstanceData = nullptr;
    std::shared_ptr<jRenderFrameContext> RenderFrameContextPtr;
    bool IsPositionOnly = false;
    int32 SubpassIndex = 0;
    bool Test = false;

    std::shared_ptr<jShaderBindingInstance> OneRenderObjectUniformBuffer;
};

// jDrawCommand generator
class jDrawCommandGenerator
{
public:
    virtual ~jDrawCommandGenerator() {}
    virtual void Initialize(int32 InRTWidth, int32 InRTHeight) = 0;
    virtual void GenerateDrawCommand(jDrawCommand* OutDestDrawCommand, const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
        , const jView* InView, const jViewLight& InLightView, jRenderPass* InRenderPass, int32 InSubpassIndex) = 0;
};

