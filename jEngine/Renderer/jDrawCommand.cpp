#include "pch.h"
#include "jDrawCommand.h"
#include "RHI/jShaderBindingsLayout.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderPass.h"
#include "Shader/jShader.h"
#include "RHI/jPipelineStateInfo.h"

jDrawCommand::jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* view
    , jRenderObject* renderObject, jRenderPass* renderPass, jShader* shader
    , jPipelineStateFixedInfo* pipelineStateFixed, std::vector<jShaderBindingInstance*> shaderBindingInstances)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(view), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed)
{
    ShaderBindingInstances.reserve(shaderBindingInstances.size() + 3);
    ShaderBindingInstances.insert(ShaderBindingInstances.begin(), shaderBindingInstances.begin(), shaderBindingInstances.end());    
}

void jDrawCommand::PrepareToDraw(bool bPositionOnly)
{
    // GetShaderBindings
    View->GetShaderBindingInstance(ShaderBindingInstances);

    // GetShaderBindings
    ShaderBindingInstances.push_back(RenderObject->CreateRenderObjectUniformBuffer(View));

    // Bind ShaderBindings
    std::vector<const jShaderBindingsLayout*> shaderBindings;
    shaderBindings.reserve(ShaderBindingInstances.size());
    for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
        shaderBindings.push_back(ShaderBindingInstances[i]->ShaderBindingsLayouts);

    std::vector<const jVertexBuffer*> vertexBuffers;
    if (RenderObject->VertexBuffer_InstanceData)
        vertexBuffers = { (bPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer), RenderObject->VertexBuffer_InstanceData };
    else
        vertexBuffers = { (bPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer) };

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , vertexBuffers, RenderPass, shaderBindings);
}

void jDrawCommand::Draw()
{
    for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
    {
        ShaderBindingInstances[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)CurrentPipelineStateInfo->GetPipelineLayoutHandle(), i);
    }

    // Bind Pipeline
    CurrentPipelineStateInfo->Bind(RenderFrameContextPtr);

    const int32 InstanceCount = RenderObject->VertexBuffer_InstanceData ? RenderObject->VertexBuffer_InstanceData->GetElementCount() : 1;

    // Draw
    RenderObject->Draw(RenderFrameContextPtr, nullptr, Shader, {}, 0, -1, InstanceCount);
}
