﻿#include "pch.h"
#include "jShaderBindingsLayout_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"
#include "../jRHI_Vulkan.h"
#include "jDescriptorPool_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
jShaderBindingsLayout_Vulkan::~jShaderBindingsLayout_Vulkan()
{
    Release();
}

bool jShaderBindingsLayout_Vulkan::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    InShaderBindingArray.CloneWithoutResource(ShaderBindingArray);
    for (int32 i = 0; i < (int32)ShaderBindingArray.NumOfData; ++i)
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = ShaderBindingArray[i]->BindingPoint;
        binding.descriptorType = GetVulkanShaderBindingType(ShaderBindingArray[i]->BindingType);
        binding.descriptorCount = ShaderBindingArray[i]->NumOfDescriptors;
        binding.stageFlags = GetVulkanShaderAccessFlags(ShaderBindingArray[i]->AccessStageFlags);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (!ensure(vkCreateDescriptorSetLayout(g_rhi_vk->Device, &layoutInfo, nullptr, &DescriptorSetLayout) == VK_SUCCESS))
        return false;

    return true;
}

jShaderBindingInstance* jShaderBindingsLayout_Vulkan::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
    jDescriptorPool_Vulkan* DescriptorPool = nullptr;
    switch (InType)
    {
    case jShaderBindingInstanceType::SingleFrame:
        DescriptorPool = g_rhi_vk->GetDescriptorPoolForSingleFrame();
        break;
    case jShaderBindingInstanceType::MultiFrame:
        DescriptorPool = g_rhi_vk->GetDescriptorPoolMultiFrame();
        break;
    case jShaderBindingInstanceType::Max:
    default:
        check(0);
        break;
    }

    jShaderBindingInstance_Vulkan* DescriptorSet = DescriptorPool->AllocateDescriptorSet(DescriptorSetLayout);
    if (!ensure(DescriptorSet))
    {
        return nullptr;
    }

    DescriptorSet->ShaderBindingsLayouts = this;
    DescriptorSet->Initialize(InShaderBindingArray);
    DescriptorSet->SetType(InType);
    return DescriptorSet;
}

size_t jShaderBindingsLayout_Vulkan::GetHash() const
{
    if (Hash)
        return Hash;

    Hash = ShaderBindingArray.GetHash();
    return Hash;
}

void jShaderBindingsLayout_Vulkan::Release()
{
    if (DescriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(g_rhi_vk->Device, DescriptorSetLayout, nullptr);
        DescriptorSetLayout = nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
jShaderBindingInstance_Vulkan::~jShaderBindingInstance_Vulkan()
{
}

void jShaderBindingInstance_Vulkan::CreateWriteDescriptorSet(
    jWriteDescriptorSet& OutDescriptorWrites, const VkDescriptorSet InDescriptorSet, const jShaderBindingArray& InShaderBindingArray)
{
    if (!ensure(InShaderBindingArray.NumOfData))
        return;

    OutDescriptorWrites.Reset();

    std::vector<jWriteDescriptorInfo>& descriptors = OutDescriptorWrites.WriteDescriptorInfos;
    std::vector<VkWriteDescriptorSet>& descriptorWrites = OutDescriptorWrites.DescriptorWrites;
    descriptors.resize(InShaderBindingArray.NumOfData);
    descriptorWrites.resize(InShaderBindingArray.NumOfData);
    OutDescriptorWrites.DynamicOffsets.clear();
    OutDescriptorWrites.DynamicOffsets.reserve(InShaderBindingArray.NumOfData);

    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        OutDescriptorWrites.SetWriteDescriptorInfo(i, InShaderBindingArray[i]);

        VkWriteDescriptorSet& CurDescriptorWrite = descriptorWrites[i];
        CurDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        CurDescriptorWrite.dstSet = InDescriptorSet;
        CurDescriptorWrite.dstBinding = i;
        CurDescriptorWrite.dstArrayElement = 0;
        CurDescriptorWrite.descriptorType = GetVulkanShaderBindingType(InShaderBindingArray[i]->BindingType);
        CurDescriptorWrite.descriptorCount = 1;
        if (descriptors[i].BufferInfo.buffer)
        {
            CurDescriptorWrite.pBufferInfo = &descriptors[i].BufferInfo;		            // 현재는 Buffer 기반 Desriptor 이므로 이것을 사용
        }
        else if (descriptors[i].ImageInfo.imageView || descriptors[i].ImageInfo.sampler)
        {
            CurDescriptorWrite.pImageInfo = &descriptors[i].ImageInfo;	        			// Optional (Buffer View 기반에 사용)
        }
        else
        {
            check(0);
        }
    }

    OutDescriptorWrites.IsInitialized = true;
}

void jShaderBindingInstance_Vulkan::UpdateWriteDescriptorSet(
    jWriteDescriptorSet& OutDescriptorWrites, const jShaderBindingArray& InShaderBindingArray)
{
    check(InShaderBindingArray.NumOfData == OutDescriptorWrites.DescriptorWrites.size());
    
    OutDescriptorWrites.DynamicOffsets.clear();
    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        OutDescriptorWrites.SetWriteDescriptorInfo(i, InShaderBindingArray[i]);
    }
}

void jShaderBindingInstance_Vulkan::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    if (!WriteDescriptorSet.IsInitialized)
    {
        CreateWriteDescriptorSet(WriteDescriptorSet, DescriptorSet, InShaderBindingArray);
    }
    else
    {
        UpdateWriteDescriptorSet(WriteDescriptorSet, InShaderBindingArray);
    }

    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(WriteDescriptorSet.DescriptorWrites.size())
        , WriteDescriptorSet.DescriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray)
{
    check(ShaderBindingsLayouts->GetShaderBindingsLayout().NumOfData == InShaderBindingArray.NumOfData);
    check(InShaderBindingArray.NumOfData);

    if (!WriteDescriptorSet.IsInitialized)
    {
        CreateWriteDescriptorSet(WriteDescriptorSet, DescriptorSet, InShaderBindingArray);
    }
    else
    {
        UpdateWriteDescriptorSet(WriteDescriptorSet, InShaderBindingArray);
    }
    
    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(WriteDescriptorSet.DescriptorWrites.size())
        , WriteDescriptorSet.DescriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot /*= 0*/) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
    check(pipelineLayout);
    vkCmdBindDescriptorSets((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipelineLayout)(pipelineLayout), InSlot
        , 1, &DescriptorSet, (uint32)WriteDescriptorSet.DynamicOffsets.size(), WriteDescriptorSet.DynamicOffsets.data());
}

void jShaderBindingInstance_Vulkan::BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
    check(pipelineLayout);
    vkCmdBindDescriptorSets((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, (VkPipelineLayout)(pipelineLayout), InSlot
        , 1, &DescriptorSet, (uint32)WriteDescriptorSet.DynamicOffsets.size(), WriteDescriptorSet.DynamicOffsets.data());
}

void jShaderBindingInstance_Vulkan::Free()
{
    g_rhi_vk->GetDescriptorPoolMultiFrame()->Free(this);
}

void jWriteDescriptorSet::SetWriteDescriptorInfo(int32 InIndex, const jShaderBinding* InShaderBinding)
{
    switch (InShaderBinding->BindingType)
    {
    case EShaderBindingType::UNIFORMBUFFER:
    {
        const jUniformBufferResource* ubor = reinterpret_cast<const jUniformBufferResource*>(InShaderBinding->Resource);
        if (ensure(ubor && ubor->UniformBuffer))
        {
            VkDescriptorBufferInfo& bufferInfo = WriteDescriptorInfos[InIndex].BufferInfo;
            bufferInfo.buffer = (VkBuffer)ubor->UniformBuffer->GetBuffer();
            bufferInfo.offset = ubor->UniformBuffer->GetBufferOffset();
            bufferInfo.range = ubor->UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
            check(bufferInfo.buffer);
        }
        break;
    }
    case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
    {
        const jUniformBufferResource* ubor = reinterpret_cast<const jUniformBufferResource*>(InShaderBinding->Resource);
        if (ensure(ubor && ubor->UniformBuffer))
        {
            VkDescriptorBufferInfo& bufferInfo = WriteDescriptorInfos[InIndex].BufferInfo;
            bufferInfo.buffer = (VkBuffer)ubor->UniformBuffer->GetBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = ubor->UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
            check(bufferInfo.buffer);

            DynamicOffsets.push_back((uint32)ubor->UniformBuffer->GetBufferOffset());
        }
        break;
    }
    case EShaderBindingType::TEXTURE_SAMPLER_SRV:
    case EShaderBindingType::TEXTURE_SRV:
    {
        const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(InShaderBinding->Resource);
        if (ensure(tbor && tbor->Texture))
        {
            VkDescriptorImageInfo& imageInfo = WriteDescriptorInfos[InIndex].ImageInfo;
            imageInfo.imageLayout = tbor->Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = (VkImageView)tbor->Texture->GetViewHandle();
            imageInfo.sampler = tbor->SamplerState ? (VkSampler)tbor->SamplerState->GetHandle() : nullptr;
            if (!imageInfo.sampler)
                imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
            check(imageInfo.imageView);
        }
        break;
    }
    case EShaderBindingType::TEXTURE_ARRAY_SRV:
    {
        const jTextureArrayResource* tbor = reinterpret_cast<const jTextureArrayResource*>(InShaderBinding->Resource);
        if (ensure(tbor && tbor->TextureArray))
        {
        }
        break;
    }
    case EShaderBindingType::SUBPASS_INPUT_ATTACHMENT:
    {
        const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(InShaderBinding->Resource);
        if (ensure(tbor && tbor->Texture))
        {
            VkDescriptorImageInfo& imageInfo = WriteDescriptorInfos[InIndex].ImageInfo;
            imageInfo.imageLayout = (tbor->Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            imageInfo.imageView = (VkImageView)tbor->Texture->GetViewHandle();
            check(imageInfo.imageView);
        }
        break;
    }
    case EShaderBindingType::TEXTURE_UAV:
    {
        const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(InShaderBinding->Resource);
        if (ensure(tbor && tbor->Texture))
        {
            VkDescriptorImageInfo& imageInfo = WriteDescriptorInfos[InIndex].ImageInfo;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfo.imageView = (VkImageView)tbor->Texture->GetViewHandle();
            if (tbor->SamplerState)
                imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
            if (!imageInfo.sampler)
                imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
            check(imageInfo.imageView);
        }
        break;
    }
    case EShaderBindingType::SAMPLER:
    {
        const jSamplerResource* sr = reinterpret_cast<const jSamplerResource*>(InShaderBinding->Resource);
        if (ensure(sr && sr->SamplerState))
        {
            VkDescriptorImageInfo& imageInfo = WriteDescriptorInfos[InIndex].ImageInfo;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.sampler = (VkSampler)sr->SamplerState->GetHandle();
            check(imageInfo.sampler);
        }
        break;
    }
    case EShaderBindingType::BUFFER_TEXEL_SRV:
    case EShaderBindingType::BUFFER_TEXEL_UAV:
    case EShaderBindingType::BUFFER_SRV:
    case EShaderBindingType::BUFFER_UAV:
    case EShaderBindingType::BUFFER_UAV_DYNAMIC:
    {
        const jBufferResource* br = reinterpret_cast<const jBufferResource*>(InShaderBinding->Resource);
        if (ensure(br && br->Buffer))
        {
            VkDescriptorBufferInfo& bufferInfo = WriteDescriptorInfos[InIndex].BufferInfo;
            bufferInfo.buffer = (VkBuffer)br->Buffer->GetHandle();
            bufferInfo.offset = 0;
            bufferInfo.range = br->Buffer->GetAllocatedSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
            check(bufferInfo.buffer);
        }
        break;
    }
    default:
        check(0);
        break;
    }
}
