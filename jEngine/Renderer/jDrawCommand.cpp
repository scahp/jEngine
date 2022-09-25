#include "pch.h"
#include "jDrawCommand.h"
#include "RHI/jShaderBindingsLayout.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderPass.h"
#include "Shader/jShader.h"
#include "RHI/jPipelineStateInfo.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "RHI/Vulkan/jVulkanBufferUtil.h"
#include "jOptions.h"

jDrawCommand::jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jView* view
    , jRenderObject* renderObject, jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed
    , const std::vector<jShaderBindingInstance*>& shaderBindingInstances, const std::shared_ptr<jPushConstant>& pushConstantPtr, jQuery* occlusionQuery)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(view), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed)
    , PushConstantPtr(pushConstantPtr), OcclusionQuery(occlusionQuery)
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
        , vertexBuffers, RenderPass, shaderBindings, PushConstantPtr.get());
}

void jDrawCommand::Draw() const
{
    for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
    {
        ShaderBindingInstances[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)CurrentPipelineStateInfo->GetPipelineLayoutHandle(), i);
    }

    // Bind the image that contains the shading rate patterns
    if (gOptions.UseVRS)
    {
        g_rhi_vk->BindShadingRateImage(RenderFrameContextPtr->CommandBuffer, g_rhi_vk->GetSampleVRSTexture());
    }

    // Bind Pipeline
    CurrentPipelineStateInfo->Bind(RenderFrameContextPtr);

    if (PushConstantPtr)
    {
        const std::vector<jPushConstantRange>* pushConstantRanges = PushConstantPtr->GetPushConstantRanges();
        if (ensure(pushConstantRanges))
        {
            for (const auto& iter : *pushConstantRanges)
            {
                vkCmdPushConstants((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle(), CurrentPipelineStateInfo->vkPipelineLayout
                    , GetVulkanShaderAccessFlags(iter.AccessStageFlag), iter.Offset, iter.Size, PushConstantPtr->GetConstantData());
            }
        }
    }

    const int32 InstanceCount = RenderObject->VertexBuffer_InstanceData ? RenderObject->VertexBuffer_InstanceData->GetElementCount() : 1;

    if (OcclusionQuery)
        OcclusionQuery->BeginQuery(RenderFrameContextPtr->CommandBuffer);

    // Draw
    RenderObject->Draw(RenderFrameContextPtr, nullptr, Shader, {}, 0, -1, InstanceCount);

    if (OcclusionQuery)
        OcclusionQuery->EndQuery(RenderFrameContextPtr->CommandBuffer);
}
