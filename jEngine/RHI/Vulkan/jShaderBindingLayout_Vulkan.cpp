#include "pch.h"
#include "jShaderBindingLayout_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"
#include "../jRHI_Vulkan.h"
#include "jDescriptorPool_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
jShaderBindingLayout_Vulkan::~jShaderBindingLayout_Vulkan()
{
    Release();
}

bool jShaderBindingLayout_Vulkan::Initialize(const std::vector<jShaderBinding>& shaderBindings)
{
    ShaderBindings.resize(shaderBindings.size());

    std::vector<VkDescriptorSetLayoutBinding> bindings;

    for (int32 i = 0; i < (int32)ShaderBindings.size(); ++i)
    {
        shaderBindings[i].CloneWithoutResource(ShaderBindings[i]);

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = ShaderBindings[i].BindingPoint;
        binding.descriptorType = GetVulkanShaderBindingType(ShaderBindings[i].BindingType);
        binding.descriptorCount = 1;
        binding.stageFlags = GetVulkanShaderAccessFlags(ShaderBindings[i].AccessStageFlags);
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

jShaderBindingInstance* jShaderBindingLayout_Vulkan::CreateShaderBindingInstance(const std::vector<jShaderBinding>& InShaderBindings) const
{
    jShaderBindingInstance_Vulkan* DescriptorSet = g_rhi_vk->GetDescriptorPools()->AllocateDescriptorSet(DescriptorSetLayout);
    if (!ensure(DescriptorSet))
    {
        return nullptr;
    }

    DescriptorSet->ShaderBindingsLayouts = this;
    DescriptorSet->Initialize(InShaderBindings);
    return DescriptorSet;
}

size_t jShaderBindingLayout_Vulkan::GetHash() const
{
    return CityHash64((const char*)ShaderBindings.data(), sizeof(jShaderBinding) * ShaderBindings.size());
}

void jShaderBindingLayout_Vulkan::Release()
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
    jWriteDescriptorSet& OutDescriptorWrites, const VkDescriptorSet InDescriptorSet, const std::vector<jShaderBinding>& InShaderBindings)
{
    if (!ensure(!InShaderBindings.empty()))
        return;

    OutDescriptorWrites.Reset();

    std::vector<jWriteDescriptorInfo>& descriptors = OutDescriptorWrites.WriteDescriptorInfos;
    std::vector<VkWriteDescriptorSet>& descriptorWrites = OutDescriptorWrites.DescriptorWrites;
    descriptors.resize(InShaderBindings.size());
    descriptorWrites.resize(InShaderBindings.size());

    for (int32 i = 0; i < (int32)InShaderBindings.size(); ++i)
    {
        OutDescriptorWrites.SetWriteDescriptorInfo(i, InShaderBindings[i]);

        VkWriteDescriptorSet& CurDescriptorWrite = descriptorWrites[i];
        CurDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        CurDescriptorWrite.dstSet = InDescriptorSet;
        CurDescriptorWrite.dstBinding = i;
        CurDescriptorWrite.dstArrayElement = 0;
        CurDescriptorWrite.descriptorType = GetVulkanShaderBindingType(InShaderBindings[i].BindingType);
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
    jWriteDescriptorSet& OutDescriptorWrites, const std::vector<jShaderBinding>& InShaderBindings)
{
    check(InShaderBindings.size() == OutDescriptorWrites.DescriptorWrites.size());

    for (int32 i = 0; i < (int32)InShaderBindings.size(); ++i)
    {
        OutDescriptorWrites.SetWriteDescriptorInfo(i, InShaderBindings[i]);
    }
}

void jShaderBindingInstance_Vulkan::Initialize(const std::vector<jShaderBinding>& InShaderBindings)
{
    if (!WriteDescriptorSet.IsInitialized)
    {
        CreateWriteDescriptorSet(WriteDescriptorSet, DescriptorSet, InShaderBindings);
    }
    else
    {
        UpdateWriteDescriptorSet(WriteDescriptorSet, InShaderBindings);
    }

    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(WriteDescriptorSet.DescriptorWrites.size())
        , WriteDescriptorSet.DescriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::UpdateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings)
{
    check(ShaderBindingsLayouts->GetShaderBindingsLayout().size() == InShaderBindings.size());
    check(!InShaderBindings.empty());

    if (!WriteDescriptorSet.IsInitialized)
    {
        CreateWriteDescriptorSet(WriteDescriptorSet, DescriptorSet, InShaderBindings);
    }
    else
    {
        UpdateWriteDescriptorSet(WriteDescriptorSet, InShaderBindings);
    }
    
    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(WriteDescriptorSet.DescriptorWrites.size())
        , WriteDescriptorSet.DescriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot /*= 0*/) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
    check(pipelineLayout);
    vkCmdBindDescriptorSets((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipelineLayout)(pipelineLayout), InSlot, 1, &DescriptorSet, 0, nullptr);
}

void jShaderBindingInstance_Vulkan::BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
    check(pipelineLayout);
    vkCmdBindDescriptorSets((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, (VkPipelineLayout)(pipelineLayout), InSlot, 1, &DescriptorSet, 0, nullptr);
}

void jWriteDescriptorSet::SetWriteDescriptorInfo(int32 InIndex, const jShaderBinding& InShaderBinding)
{
    switch (InShaderBinding.BindingType)
    {
    case EShaderBindingType::UNIFORMBUFFER:
    {
        jUniformBufferResource* ubor = reinterpret_cast<jUniformBufferResource*>(InShaderBinding.ResourcePtr.get());
        if (ubor && ubor->UniformBuffer)
        {
            VkDescriptorBufferInfo& bufferInfo = WriteDescriptorInfos[InIndex].BufferInfo;
            bufferInfo.buffer = (VkBuffer)ubor->UniformBuffer->GetBuffer();
            bufferInfo.offset = ubor->UniformBuffer->GetBufferOffset();
            bufferInfo.range = ubor->UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
            check(bufferInfo.buffer);
        }
        break;
    }
    case EShaderBindingType::TEXTURE_SAMPLER_SRV:
    case EShaderBindingType::TEXTURE_SRV:
    {
        jTextureResource* tbor = reinterpret_cast<jTextureResource*>(InShaderBinding.ResourcePtr.get());
        if (tbor && tbor->Texture)
        {
            VkDescriptorImageInfo& imageInfo = WriteDescriptorInfos[InIndex].ImageInfo;
            imageInfo.imageLayout = tbor->Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = (VkImageView)tbor->Texture->GetViewHandle();
            if (tbor->SamplerState)
                imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
            if (!imageInfo.sampler)
                imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
            check(imageInfo.imageView);
        }
        break;
    }
    case EShaderBindingType::TEXTURE_UAV:
    {
        jTextureResource* tbor = reinterpret_cast<jTextureResource*>(InShaderBinding.ResourcePtr.get());
        if (tbor && tbor->Texture)
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
        jTextureResource* tbor = reinterpret_cast<jTextureResource*>(InShaderBinding.ResourcePtr.get());
        if (tbor && tbor->SamplerState)
        {
            VkDescriptorImageInfo& imageInfo = WriteDescriptorInfos[InIndex].ImageInfo;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
            check(imageInfo.sampler);
        }
        break;
    }
    case EShaderBindingType::BUFFER_UAV:
    {
        check(0); // todo SSBO
        break;
    }
    default:
        check(0);
        break;
    }
}

