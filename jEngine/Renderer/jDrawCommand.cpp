#include "pch.h"
#include "jDrawCommand.h"
#include "RHI/jShaderBindings.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderPass.h"
#include "Shader/jShader.h"
#include "RHI/jPipelineStateInfo.h"

jDrawCommand::jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* view
    , jRenderObject* renderObject, jRenderPass* renderPass, jShader* shader
    , jPipelineStateFixedInfo* pipelineStateFixed, std::vector<const jShaderBindingInstance*> shaderBindingInstances)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(view), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed)
{
    ShaderBindingInstances.reserve(shaderBindingInstances.size() + 3);
    ShaderBindingInstances.insert(ShaderBindingInstances.begin(), shaderBindingInstances.begin(), shaderBindingInstances.end());
}

void jDrawCommand::PrepareToDraw(bool bPositionOnly)
{
    RenderObject->UpdateRenderObjectUniformBuffer(View);
    RenderObject->PrepareShaderBindingInstance();

    // GetShaderBindings
    View->GetShaderBindingInstance(ShaderBindingInstances);

    // GetShaderBindings
    RenderObject->GetShaderBindingInstance(ShaderBindingInstances);

    // Bind ShaderBindings
    std::vector<const jShaderBindings*> shaderBindings;
    shaderBindings.reserve(ShaderBindingInstances.size());
    for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
        shaderBindings.push_back(ShaderBindingInstances[i]->ShaderBindings);

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo_Vulkan*)g_rhi_vk->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , bPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer, RenderPass, shaderBindings);
}

void jDrawCommand::Draw()
{
    for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
    {
        ShaderBindingInstances[i]->Bind(RenderFrameContextPtr, (VkPipelineLayout)CurrentPipelineStateInfo->GetPipelineLayoutHandle(), i);
    }

    // Bind Pipeline
    CurrentPipelineStateInfo->Bind(RenderFrameContextPtr);

    // Draw
    RenderObject->Draw(RenderFrameContextPtr, nullptr, Shader, {});
}
