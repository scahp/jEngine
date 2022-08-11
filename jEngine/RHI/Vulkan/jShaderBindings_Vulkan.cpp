#include "pch.h"
#include "jShaderBindings_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jShaderBindings_Vulkan::CreateDescriptorSetLayout()
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    for (int32 i = 0; i < UniformBuffers.size(); ++i)
    {
        auto& UBBindings = UniformBuffers[i];

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = UBBindings.BindingPoint;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = GetVulkanShaderAccessFlags(UBBindings.AccessStageFlags);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    for (int32 i = 0; i < Textures.size(); ++i)
    {
        auto& TexBindings = Textures[i];

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = TexBindings.BindingPoint;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = GetVulkanShaderAccessFlags(TexBindings.AccessStageFlags);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (!ensure(vkCreateDescriptorSetLayout(g_rhi_vk->Device, &layoutInfo, nullptr, &DescriptorSetLayout) == VK_SUCCESS))
        return false;

    return true;
}

jShaderBindingInstance* jShaderBindings_Vulkan::CreateShaderBindingInstance() const
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &DescriptorSetLayout;

    jShaderBindingInstance_Vulkan* Instance = new jShaderBindingInstance_Vulkan();
    Instance->ShaderBindings = this;
    if (!ensure(vkAllocateDescriptorSets(g_rhi_vk->Device, &allocInfo, &Instance->DescriptorSet) == VK_SUCCESS))
    {
        delete Instance;
        return nullptr;
    }
    CreatedBindingInstances.push_back(Instance);

    return Instance;
}

std::vector<jShaderBindingInstance*> jShaderBindings_Vulkan::CreateShaderBindingInstance(int32 count) const
{
    std::vector<VkDescriptorSetLayout> descSetLayout(count, DescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.descriptorSetCount = (uint32)descSetLayout.size();
    allocInfo.pSetLayouts = descSetLayout.data();

    std::vector<jShaderBindingInstance*> Instances;

    std::vector<VkDescriptorSet> DescriptorSets;
    DescriptorSets.resize(descSetLayout.size());
    if (!ensure(vkAllocateDescriptorSets(g_rhi_vk->Device, &allocInfo, DescriptorSets.data()) == VK_SUCCESS))
        return Instances;

    Instances.resize(DescriptorSets.size());
    for (int32 i = 0; i < DescriptorSets.size(); ++i)		// todo opt
    {
        auto NewBindingInstance = new jShaderBindingInstance_Vulkan();
        CreatedBindingInstances.push_back(NewBindingInstance);
        NewBindingInstance->DescriptorSet = DescriptorSets[i];
        NewBindingInstance->ShaderBindings = this;

        Instances[i] = NewBindingInstance;
    }

    return Instances;
}

size_t jShaderBindings_Vulkan::GetHash() const
{
    size_t result = 0;

    result ^= STATIC_NAME_CITY_HASH("UniformBuffer");
    result ^= CityHash64((const char*)UniformBuffers.data(), sizeof(jShaderBinding) * UniformBuffers.size());

    result ^= STATIC_NAME_CITY_HASH("Texture");
    result ^= CityHash64((const char*)Textures.data(), sizeof(jShaderBinding) * Textures.size());

    return result;
}

void jShaderBindings_Vulkan::CreatePool()
{
    DescriptorPool = g_rhi_vk->ShaderBindingsManager.CreatePool(*this);
}

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
void jShaderBindingInstance_Vulkan::UpdateShaderBindings()
{
    check(ShaderBindings->UniformBuffers.size() == UniformBuffers.size());
    check(ShaderBindings->Textures.size() == Textures.size());

    std::vector<VkDescriptorBufferInfo> descriptorBuffers;
    descriptorBuffers.resize(ShaderBindings->UniformBuffers.size());
    int32 bufferOffset = 0;
    for (int32 i = 0; i < ShaderBindings->UniformBuffers.size(); ++i)
    {
        descriptorBuffers[i].buffer = (VkBuffer)UniformBuffers[i]->GetBuffer();
        descriptorBuffers[i].offset = bufferOffset;
        descriptorBuffers[i].range = UniformBuffers[i]->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능

        bufferOffset += (int32)UniformBuffers[i]->GetBufferSize();
    }

    std::vector<VkDescriptorImageInfo> descriptorImages;
    descriptorImages.resize(ShaderBindings->Textures.size());
    for (int32 i = 0; i < ShaderBindings->Textures.size(); ++i)
    {
        descriptorImages[i].imageLayout = Textures[i].Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorImages[i].imageView = (VkImageView)Textures[i].Texture->GetViewHandle();
        if (Textures[i].SamplerState)
            descriptorImages[i].sampler = (VkSampler)Textures[i].SamplerState->GetHandle();

        if (!descriptorImages[i].sampler)
            descriptorImages[i].sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites;
    descriptorWrites.resize(descriptorBuffers.size() + descriptorImages.size());

    int32 writeDescIndex = 0;
    if (descriptorBuffers.size() > 0)
    {
        descriptorWrites[writeDescIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[writeDescIndex].dstSet = DescriptorSet;
        descriptorWrites[writeDescIndex].dstBinding = writeDescIndex;
        descriptorWrites[writeDescIndex].dstArrayElement = 0;
        descriptorWrites[writeDescIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[writeDescIndex].descriptorCount = (uint32)descriptorBuffers.size();
        descriptorWrites[writeDescIndex].pBufferInfo = &descriptorBuffers[0];		// 현재는 Buffer 기반 Desriptor 이므로 이것을 사용
        descriptorWrites[writeDescIndex].pImageInfo = nullptr;						// Optional	(Image Data 기반에 사용)
        descriptorWrites[writeDescIndex].pTexelBufferView = nullptr;				// Optional (Buffer View 기반에 사용)
        ++writeDescIndex;
    }
    if (descriptorImages.size() > 0)
    {
        descriptorWrites[writeDescIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[writeDescIndex].dstSet = DescriptorSet;
        descriptorWrites[writeDescIndex].dstBinding = writeDescIndex;
        descriptorWrites[writeDescIndex].dstArrayElement = 0;
        descriptorWrites[writeDescIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[writeDescIndex].descriptorCount = (uint32)descriptorImages.size();
        descriptorWrites[writeDescIndex].pImageInfo = &descriptorImages[0];
        ++writeDescIndex;
    }

    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(descriptorWrites.size())
        , descriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
    check(pipelineLayout);
    vkCmdBindDescriptorSets((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipelineLayout)(pipelineLayout), InSlot, 1, &DescriptorSet, 0, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// jShaderBindingsManager_Vulkan
//////////////////////////////////////////////////////////////////////////
VkDescriptorPool jShaderBindingsManager_Vulkan::CreatePool(const jShaderBindings_Vulkan& bindings, uint32 maxAllocations) const
{
    auto DescriptorPoolSizes = bindings.GetDescriptorPoolSizeArray(maxAllocations);

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32>(DescriptorPoolSizes.size());
    poolInfo.pPoolSizes = DescriptorPoolSizes.data();
    poolInfo.maxSets = maxAllocations;
    poolInfo.flags = 0;		// Descriptor Set을 만들고나서 더 이상 손대지 않을거라 그냥 기본값 0으로 설정

    VkDescriptorPool pool = nullptr;
    if (!ensure(vkCreateDescriptorPool(g_rhi_vk->Device, &poolInfo, nullptr, &pool) == VK_SUCCESS))
        return nullptr;

    return pool;
}

void jShaderBindingsManager_Vulkan::Release(VkDescriptorPool pool) const
{
    if (pool)
        vkDestroyDescriptorPool(g_rhi_vk->Device, pool, nullptr);
}
