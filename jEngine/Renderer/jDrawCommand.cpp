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

    // POI: Bind the image that contains the shading rate patterns
    static PFN_vkCmdBindShadingRateImageNV vkCmdBindShadingRateImageNV = nullptr;
    static jTexture_Vulkan* vrs_texture = nullptr;
    if (!vkCmdBindShadingRateImageNV)
    {
        vkCmdBindShadingRateImageNV = reinterpret_cast<PFN_vkCmdBindShadingRateImageNV>(vkGetDeviceProcAddr(g_rhi_vk->Device, "vkCmdBindShadingRateImageNV"));

        VkPhysicalDeviceShadingRateImagePropertiesNV physicalDeviceShadingRateImagePropertiesNV{};
        physicalDeviceShadingRateImagePropertiesNV.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV;
        VkPhysicalDeviceProperties2 deviceProperties2{};
        deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties2.pNext = &physicalDeviceShadingRateImagePropertiesNV;
        vkGetPhysicalDeviceProperties2(g_rhi_vk->PhysicalDevice, &deviceProperties2);

        VkExtent3D imageExtent{};
        imageExtent.width = static_cast<uint32_t>(ceil(SCR_WIDTH / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.width));
        imageExtent.height = static_cast<uint32_t>(ceil(SCR_HEIGHT / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.height));
        imageExtent.depth = 1;

        vrs_texture = new jTexture_Vulkan();

        jVulkanBufferUtil::CreateImage(imageExtent.width, imageExtent.height, 1, (VkSampleCountFlagBits)1, GetVulkanTextureFormat(ETextureFormat::R8UI), VK_IMAGE_TILING_OPTIMAL
            , VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, *vrs_texture);

        VkDeviceSize imageSize = imageExtent.width * imageExtent.height * GetVulkanTextureComponentCount(ETextureFormat::R8UI);
        jBuffer_Vulkan stagingBuffer;

        jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            , imageSize, stagingBuffer);

        VkCommandBuffer commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
        ensure(g_rhi_vk->TransitionImageLayout(commandBuffer, (VkImage)vrs_texture->GetHandle(), GetVulkanTextureFormat(ETextureFormat::R8UI), 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

        jVulkanBufferUtil::CopyBufferToImage(commandBuffer, stagingBuffer.Buffer, (VkImage)vrs_texture->GetHandle()
            , static_cast<uint32>(imageExtent.width), static_cast<uint32>(imageExtent.height));

        // Create a circular pattern with decreasing sampling rates outwards (max. range, pattern)
        std::map<float, VkShadingRatePaletteEntryNV> patternLookup = {
            { 1.5f * 8.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV },
            { 1.5f * 12.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV },
            { 1.5f * 16.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV },
            { 1.5f * 18.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV },
            { 1.5f * 20.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV },
            { 1.5f * 24.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV }
        };

        VkDeviceSize bufferSize = imageExtent.width * imageExtent.height * sizeof(uint8);
        uint8_t val = VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV;
        uint8_t* shadingRatePatternData = new uint8_t[bufferSize];
        memset(shadingRatePatternData, val, bufferSize);
        uint8_t* ptrData = shadingRatePatternData;
        for (uint32_t y = 0; y < imageExtent.height; y++) {
            for (uint32_t x = 0; x < imageExtent.width; x++) {
                const float deltaX = (float)imageExtent.width / 2.0f - (float)x;
                const float deltaY = ((float)imageExtent.height / 2.0f - (float)y) * ((float)SCR_WIDTH / (float)SCR_HEIGHT);
                const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
                for (auto pattern : patternLookup) {
                    if (dist < pattern.first) {
                        *ptrData = pattern.second;
                        break;
                    }
                }
                ptrData++;
            }
        }

        check(imageSize == bufferSize);

        stagingBuffer.UpdateBuffer(shadingRatePatternData, bufferSize);

        g_rhi_vk->EndSingleTimeCommands(commandBuffer);

        stagingBuffer.Release();
        delete[]shadingRatePatternData;

        vrs_texture->View = jVulkanBufferUtil::CreateImageView((VkImage)vrs_texture->GetHandle(), GetVulkanTextureFormat(vrs_texture->Format)
            , VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
    vkCmdBindShadingRateImageNV((VkCommandBuffer)RenderFrameContextPtr->CommandBuffer->GetHandle()
        , (VkImageView)vrs_texture->GetViewHandle(), VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);

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
