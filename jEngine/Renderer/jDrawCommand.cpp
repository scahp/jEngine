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
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const std::shared_ptr<jPushConstant>& pushConstantPtr, jQuery* occlusionQuery)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(view), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed)
    , PushConstantPtr(pushConstantPtr), OcclusionQuery(occlusionQuery), ShaderBindingInstanceArray(InShaderBindingInstanceArray)
{
}

void jDrawCommand::PrepareToDraw(bool bPositionOnly)
{
    // GetShaderBindings
    View->GetShaderBindingInstance(ShaderBindingInstanceArray);

    // GetShaderBindings
    jShaderBindingInstance* OneRenderObjectUniformBuffer = RenderObject->CreateRenderObjectUniformBuffer(View);
    ShaderBindingInstanceArray.Add(OneRenderObjectUniformBuffer);

    // Bind ShaderBindings
    jShaderBindingsLayoutArray ShaderBindingLayoutArray;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
        ShaderBindingLayoutArray.Add(ShaderBindingInstanceArray[i]->ShaderBindingsLayouts);

    jVertexBufferArray VertexBufferArray;
    VertexBufferArray.Add(bPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer);
    if (RenderObject->VertexBuffer_InstanceData)
    {
        VertexBufferArray.Add(RenderObject->VertexBuffer_InstanceData);
    }

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , VertexBufferArray, RenderPass, ShaderBindingLayoutArray, PushConstantPtr.get());
}

void jDrawCommand::Draw() const
{
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingInstanceArray[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)CurrentPipelineStateInfo->GetPipelineLayoutHandle(), i);
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
    RenderObject->Draw(RenderFrameContextPtr, nullptr, Shader, nullptr, 0, -1, InstanceCount);

    if (OcclusionQuery)
        OcclusionQuery->EndQuery(RenderFrameContextPtr->CommandBuffer);
}
