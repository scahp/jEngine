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
#include "Material/jMaterial.h"

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView* InView
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, int32 InSubpassIndex)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(InView), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed)
    , PushConstant(InPushConstant), ShaderBindingInstanceArray(InShaderBindingInstanceArray), SubpassIndex(InSubpassIndex)
{
    check(RenderObject);
    IsViewLight = false;
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jViewLight* InViewLight
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, int32 InSubpassIndex)
    : RenderFrameContextPtr(InRenderFrameContextPtr), ViewLight(InViewLight), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed)
    , PushConstant(InPushConstant), ShaderBindingInstanceArray(InShaderBindingInstanceArray), SubpassIndex(InSubpassIndex)
{
    check(RenderObject);
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

    if (RenderObject->MaterialPtr)
    {
        jShaderBindingInstance* MaterialShaderBindingInstance = RenderObject->MaterialPtr->CreateShaderBindingInstance();
        if (MaterialShaderBindingInstance)
            ShaderBindingInstanceArray.Add(MaterialShaderBindingInstance);
    }
    else
    {
        jShaderBindingInstance* MaterialShaderBindingInstance = GDefaultMaterial->CreateShaderBindingInstance();
        if (MaterialShaderBindingInstance)
            ShaderBindingInstanceArray.Add(MaterialShaderBindingInstance);
    }

    // Bind ShaderBindings
    jShaderBindingsLayoutArray ShaderBindingLayoutArray;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingLayoutArray.Add(ShaderBindingInstanceArray[i]->ShaderBindingsLayouts);
        ShaderBindingInstanceCombiner.Add(ShaderBindingInstanceArray[i]->GetHandle());
    }

    jVertexBufferArray VertexBufferArray;
    VertexBufferArray.Add(InIsPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer);
    if (RenderObject->VertexBuffer_InstanceData)
    {
        VertexBufferArray.Add(RenderObject->VertexBuffer_InstanceData);
    }

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo_Vulkan*)g_rhi->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , VertexBufferArray, RenderPass, ShaderBindingLayoutArray, PushConstant, SubpassIndex);

    IsPositionOnly = InIsPositionOnly;
}

void jDrawCommand::Draw() const
{
    g_rhi->BindGraphicsShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), CurrentPipelineStateInfo, ShaderBindingInstanceCombiner, 0);

    // Bind the image that contains the shading rate patterns
#if USE_VARIABLE_SHADING_RATE_TIER2
    if (gOptions.UseVRS)
    {
        g_rhi_vk->BindShadingRateImage(RenderFrameContextPtr->GetActiveCommandBuffer(), g_rhi_vk->GetSampleVRSTexture());
    }
#endif

    // Bind Pipeline
    CurrentPipelineStateInfo->Bind(RenderFrameContextPtr);

    if (PushConstant && PushConstant->IsValid())
    {
        const jResourceContainer<jPushConstantRange>* pushConstantRanges = PushConstant->GetPushConstantRanges();
        if (ensure(pushConstantRanges))
        {
            for (int32 i = 0; i < pushConstantRanges->NumOfData; ++i)
            {
                const jPushConstantRange& range = (*pushConstantRanges)[i];
                vkCmdPushConstants((VkCommandBuffer)RenderFrameContextPtr->GetActiveCommandBuffer()->GetHandle(), CurrentPipelineStateInfo->vkPipelineLayout
                    , GetVulkanShaderAccessFlags(range.AccessStageFlag), range.Offset, range.Size, PushConstant->GetConstantData());
            }
        }
    }

    RenderObject->BindBuffers(RenderFrameContextPtr, IsPositionOnly);

    // Draw
    const int32 InstanceCount = RenderObject->VertexBuffer_InstanceData ? RenderObject->VertexBuffer_InstanceData->GetElementCount() : 1;
    RenderObject->Draw(RenderFrameContextPtr, InstanceCount);
}
