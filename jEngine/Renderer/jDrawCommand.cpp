#include "pch.h"
#include "jDrawCommand.h"
#include "RHI/jShaderBindingsLayout.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderPass.h"
#include "Shader/jShader.h"
#include "RHI/jPipelineStateInfo.h"
#include "jOptions.h"
#include "Material/jMaterial.h"

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView* InView
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(InView), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed)
    , Material(InMaterial), PushConstant(InPushConstant), ShaderBindingInstanceArray(InShaderBindingInstanceArray), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex)
{
    check(RenderObject);
    IsViewLight = false;
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jViewLight* InViewLight
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex)
    : RenderFrameContextPtr(InRenderFrameContextPtr), ViewLight(InViewLight), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed)
    , Material(InMaterial), PushConstant(InPushConstant), ShaderBindingInstanceArray(InShaderBindingInstanceArray), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex)
{
    check(RenderObject);
    IsViewLight = true;
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex)
    : RenderFrameContextPtr(InRenderFrameContextPtr), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed), Material(InMaterial)
    , PushConstant(InPushConstant), ShaderBindingInstanceArray(InShaderBindingInstanceArray), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex)
{
    check(RenderObject);
}

void jDrawCommand::PrepareToDraw(bool InIsPositionOnly)
{
    if (!Test)
    {
        // GetShaderBindings
        if (IsViewLight)
        {
            ShaderBindingInstanceArray.Add(ViewLight->ShaderBindingInstance.get());
        }
        else if (View)
        {
            View->GetShaderBindingInstance(ShaderBindingInstanceArray, RenderFrameContextPtr->UseForwardRenderer);
        }
    
        // GetShaderBindings
        OneRenderObjectUniformBuffer = RenderObject->CreateShaderBindingInstance();
        ShaderBindingInstanceArray.Add(OneRenderObjectUniformBuffer.get());

        if (Material)
        {
            ShaderBindingInstanceArray.Add(Material->CreateShaderBindingInstance().get());
        }
    }

    // Gather ShaderBindings
    jShaderBindingsLayoutArray ShaderBindingLayoutArray;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        // Add DescriptorSetLayout data
        ShaderBindingLayoutArray.Add(ShaderBindingInstanceArray[i]->ShaderBindingsLayouts);

        // Add ShaderBindingInstanceCombiner data : DescriptorSets, DynamicOffsets
        ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
        const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
        if (pDynamicOffsetTest && pDynamicOffsetTest->size())
        {
            ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
        }
    }
    ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

    const auto& RenderObjectGeoDataPtr = RenderObject->GeometryDataPtr;

    jVertexBufferArray VertexBufferArray;
    VertexBufferArray.Add(InIsPositionOnly ? RenderObjectGeoDataPtr->VertexBuffer_PositionOnly : RenderObjectGeoDataPtr->VertexBuffer);
    if (OverrideInstanceData)
    {
        VertexBufferArray.Add(OverrideInstanceData);
    }
    else if (RenderObjectGeoDataPtr->VertexBuffer_InstanceData)
    {
        VertexBufferArray.Add(RenderObjectGeoDataPtr->VertexBuffer_InstanceData);
    }

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo*)g_rhi->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , VertexBufferArray, RenderPass, ShaderBindingLayoutArray, PushConstant, SubpassIndex);

    IsPositionOnly = InIsPositionOnly;
}

void jDrawCommand::Draw() const
{
    check(RenderFrameContextPtr);

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

#if USE_VULKAN
    if (PushConstant && PushConstant->IsValid())
    {
        const jResourceContainer<jPushConstantRange>* pushConstantRanges = PushConstant->GetPushConstantRanges();
        if (ensure(pushConstantRanges))
        {
            for (int32 i = 0; i < pushConstantRanges->NumOfData; ++i)
            {
                const jPushConstantRange& range = (*pushConstantRanges)[i];
                vkCmdPushConstants((VkCommandBuffer)RenderFrameContextPtr->GetActiveCommandBuffer()->GetHandle(), ((jPipelineStateInfo_Vulkan*)CurrentPipelineStateInfo)->vkPipelineLayout
                    , GetVulkanShaderAccessFlags(range.AccessStageFlag), range.Offset, range.Size, PushConstant->GetConstantData());
            }
        }
    }
#endif

    RenderObject->BindBuffers(RenderFrameContextPtr, IsPositionOnly, OverrideInstanceData);

    // Draw
    const auto& RenderObjectGeoDataPtr = RenderObject->GeometryDataPtr;
    const jVertexBuffer* InstanceData = OverrideInstanceData ? OverrideInstanceData : RenderObjectGeoDataPtr->VertexBuffer_InstanceData;
    const int32 InstanceCount = InstanceData ? InstanceData->GetElementCount() : 1;
    RenderObject->Draw(RenderFrameContextPtr, InstanceCount);
}
