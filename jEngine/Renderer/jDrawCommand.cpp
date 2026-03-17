#include "pch.h"
#include "jDrawCommand.h"
#include "RHI/jShaderBindingLayout.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHI.h"
#include "RHI/jRenderPass.h"
#include "Shader/jShader.h"
#include "RHI/jPipelineStateInfo.h"
#include "jOptions.h"
#include "Material/jMaterial.h"

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView* InView
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex, EDrawCommandBindingMode InBindingMode)
    : RenderFrameContextPtr(InRenderFrameContextPtr), View(InView), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed)
    , Material(InMaterial), PushConstant(InPushConstant), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex), BindingMode(InBindingMode)
{
    check(RenderObject);
    IsViewLight = false;
    ShaderBindingGroup.Add(InShaderBindingInstanceArray);
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jView* InView
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceGroup& InShaderBindingInstanceGroup, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex, EDrawCommandBindingMode InBindingMode)
    : ShaderBindingGroup(InShaderBindingInstanceGroup), RenderFrameContextPtr(InRenderFrameContextPtr), View(InView), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader)
    , PipelineStateFixed(InPipelineStateFixed), Material(InMaterial), PushConstant(InPushConstant), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex), BindingMode(InBindingMode)
{
    check(RenderObject);
    IsViewLight = false;
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jViewLight* InViewLight
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex, EDrawCommandBindingMode InBindingMode)
    : RenderFrameContextPtr(InRenderFrameContextPtr), ViewLight(InViewLight), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed)
    , Material(InMaterial), PushConstant(InPushConstant), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex), BindingMode(InBindingMode)
{
    check(RenderObject);
    IsViewLight = true;
    ShaderBindingGroup.Add(InShaderBindingInstanceArray);
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, const jViewLight* InViewLight
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceGroup& InShaderBindingInstanceGroup, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex, EDrawCommandBindingMode InBindingMode)
    : ShaderBindingGroup(InShaderBindingInstanceGroup), RenderFrameContextPtr(InRenderFrameContextPtr), ViewLight(InViewLight), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader)
    , PipelineStateFixed(InPipelineStateFixed), Material(InMaterial), PushConstant(InPushConstant), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex), BindingMode(InBindingMode)
{
    check(RenderObject);
    IsViewLight = true;
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceArray& InShaderBindingInstanceArray, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex, EDrawCommandBindingMode InBindingMode)
    : RenderFrameContextPtr(InRenderFrameContextPtr), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader), PipelineStateFixed(InPipelineStateFixed), Material(InMaterial)
    , PushConstant(InPushConstant), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex), BindingMode(InBindingMode)
{
    check(RenderObject);
    ShaderBindingGroup.Add(InShaderBindingInstanceArray);
}

jDrawCommand::jDrawCommand(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , jRenderObject* InRenderObject, jRenderPass* InRenderPass, jGraphicsPipelineShader InShader, jPipelineStateFixedInfo* InPipelineStateFixed, jMaterial* InMaterial
    , const jShaderBindingInstanceGroup& InShaderBindingInstanceGroup, const jPushConstant* InPushConstant, const jVertexBuffer* InOverrideInstanceData, int32 InSubpassIndex, EDrawCommandBindingMode InBindingMode)
    : ShaderBindingGroup(InShaderBindingInstanceGroup), RenderFrameContextPtr(InRenderFrameContextPtr), RenderObject(InRenderObject), RenderPass(InRenderPass), Shader(InShader)
    , PipelineStateFixed(InPipelineStateFixed), Material(InMaterial), PushConstant(InPushConstant), OverrideInstanceData(InOverrideInstanceData), SubpassIndex(InSubpassIndex), BindingMode(InBindingMode)
{
    check(RenderObject);
}

void jDrawCommand::AppendStandardBindings()
{
    if (IsViewLight)
    {
        ShaderBindingGroup.Add(ViewLight->ShaderBindingInstance);
    }
    else if (View)
    {
        jShaderBindingInstanceArray ShaderBindingInstanceArray;
        View->GetShaderBindingInstance(ShaderBindingInstanceArray, RenderFrameContextPtr->UseForwardRenderer);
        ShaderBindingGroup.Add(ShaderBindingInstanceArray);
    }

    OneRenderObjectUniformBuffer = RenderObject->CreateShaderBindingInstance();
    ShaderBindingGroup.Add(OneRenderObjectUniformBuffer);

    if (Material)
    {
        ShaderBindingGroup.Add(Material->CreateShaderBindingInstance());
    }
}

void jDrawCommand::PrepareToDraw(bool InIsPositionOnly)
{
    if (BindingMode == EDrawCommandBindingMode::Standard)
    {
        AppendStandardBindings();
    }

    const auto& RenderObjectGeoDataPtr = RenderObject->GeometryDataPtr;

    jVertexBufferArray VertexBufferArray;
    VertexBufferArray.Add(InIsPositionOnly ? RenderObjectGeoDataPtr->VertexBuffer_PositionOnlyPtr.get() : RenderObjectGeoDataPtr->VertexBufferPtr.get());
    if (OverrideInstanceData)
    {
        VertexBufferArray.Add(OverrideInstanceData);
    }
    else if (RenderObjectGeoDataPtr->VertexBuffer_InstanceDataPtr)
    {
        VertexBufferArray.Add(RenderObjectGeoDataPtr->VertexBuffer_InstanceDataPtr.get());
    }

    // Create Pipeline
    CurrentPipelineStateInfo = (jPipelineStateInfo*)g_rhi->CreatePipelineStateInfo(PipelineStateFixed, Shader
        , VertexBufferArray, RenderPass, ShaderBindingGroup.GetLayoutArray(), PushConstant, SubpassIndex);

    IsPositionOnly = InIsPositionOnly;
}

void jDrawCommand::Draw() const
{
    check(RenderFrameContextPtr);

    g_rhi->BindGraphicsShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), CurrentPipelineStateInfo, ShaderBindingGroup.GetCombiner(), 0);

    // Bind the image that contains the shading rate patterns
#if USE_VARIABLE_SHADING_RATE_TIER2
    if (gOptions.UseVRS)
    {
        g_rhi_vk->BindShadingRateImage(RenderFrameContextPtr->GetActiveCommandBuffer(), g_rhi_vk->GetSampleVRSTexture());
    }
#endif

    // Bind Pipeline
    CurrentPipelineStateInfo->Bind(RenderFrameContextPtr);

    if (IsUseVulkan())
    {
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
    }

    RenderObject->BindBuffers(RenderFrameContextPtr, IsPositionOnly, OverrideInstanceData);

    // Draw
    const auto& RenderObjectGeoDataPtr = RenderObject->GeometryDataPtr;
    const jVertexBuffer* InstanceData = OverrideInstanceData ? OverrideInstanceData : RenderObjectGeoDataPtr->VertexBuffer_InstanceDataPtr.get();
    const int32 InstanceCount = InstanceData ? InstanceData->GetElementCount() : 1;
    RenderObject->Draw(RenderFrameContextPtr, InstanceCount);
}
