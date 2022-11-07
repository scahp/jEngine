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
    IsViewLight = false;
}

jDrawCommand::jDrawCommand(std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jViewLight* viewLight
    , jRenderObject* renderObject, jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const std::shared_ptr<jPushConstant>& pushConstantPtr, jQuery* occlusionQuery)
    : RenderFrameContextPtr(InRenderFrameContextPtr), ViewLight(viewLight), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed)
    , PushConstantPtr(pushConstantPtr), OcclusionQuery(occlusionQuery), ShaderBindingInstanceArray(InShaderBindingInstanceArray)
{
    IsViewLight = true;
}

void jDrawCommand::PrepareToDraw(bool InIsPositionOnly)
{
    // GetShaderBindings
    if (IsViewLight)
    {
        ShaderBindingInstanceArray.Add(ViewLight->ShaderBindingInstance);
    }
    else
    {        
        View->GetShaderBindingInstance(ShaderBindingInstanceArray, RenderFrameContextPtr->UseForwardRenderer);
    }
    
    // GetShaderBindings
    jShaderBindingInstance* OneRenderObjectUniformBuffer = RenderObject->CreateShaderBindingInstance();
    ShaderBindingInstanceArray.Add(OneRenderObjectUniformBuffer);

    // Bind ShaderBindings
    jShaderBindingsLayoutArray ShaderBindingLayoutArray;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
        ShaderBindingLayoutArray.Add(ShaderBindingInstanceArray[i]->ShaderBindingsLayouts);

    jVertexBufferArray VertexBufferArray;
    VertexBufferArray.Add(InIsPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer);
    if (RenderObject->VertexBuffer_InstanceData)
    {
        VertexBufferArray.Add(RenderObject->VertexBuffer_InstanceData);
    }

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , VertexBufferArray, RenderPass, ShaderBindingLayoutArray, PushConstantPtr.get());

    IsPositionOnly = InIsPositionOnly;
}

void jDrawCommand::Draw() const
{
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingInstanceArray[i]->BindGraphics(RenderFrameContextPtr, (VkPipelineLayout)CurrentPipelineStateInfo->GetPipelineLayoutHandle(), i);
    }

    // Bind the image that contains the shading rate patterns
#if USE_VARIABLE_SHADING_RATE_TIER2
    if (gOptions.UseVRS)
    {
        g_rhi_vk->BindShadingRateImage(RenderFrameContextPtr->CommandBuffer, g_rhi_vk->GetSampleVRSTexture());
    }
#endif

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

    RenderObject->BindBuffers(RenderFrameContextPtr, IsPositionOnly);

    // Todo : OcclusionQuery will be moved to another visibility passes
    if (OcclusionQuery)
        OcclusionQuery->BeginQuery(RenderFrameContextPtr->CommandBuffer);

    // Draw
    const int32 InstanceCount = RenderObject->VertexBuffer_InstanceData ? RenderObject->VertexBuffer_InstanceData->GetElementCount() : 1;
    RenderObject->Draw(RenderFrameContextPtr, 0, -1, InstanceCount);

    if (OcclusionQuery)
        OcclusionQuery->EndQuery(RenderFrameContextPtr->CommandBuffer);
}
