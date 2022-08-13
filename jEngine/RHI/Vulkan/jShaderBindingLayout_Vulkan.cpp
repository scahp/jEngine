#include "pch.h"
#include "jShaderBindingLayout_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jShaderBindingLayout_Vulkan::CreateDescriptorSetLayout()
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    for (int32 i = 0; i < (int32)ShaderBindings.size(); ++i)
    {
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

jShaderBindingInstance* jShaderBindingLayout_Vulkan::CreateShaderBindingInstance() const
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

std::vector<jShaderBindingInstance*> jShaderBindingLayout_Vulkan::CreateShaderBindingInstance(int32 count) const
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

size_t jShaderBindingLayout_Vulkan::GetHash() const
{
    return CityHash64((const char*)ShaderBindings.data(), sizeof(jShaderBinding) * ShaderBindings.size());
}

void jShaderBindingLayout_Vulkan::CreatePool()
{
    DescriptorPool = g_rhi_vk->ShaderBindingsManager.CreatePool(*this);
}

//////////////////////////////////////////////////////////////////////////
// jShaderBindingInstance_Vulkan
//////////////////////////////////////////////////////////////////////////
void jShaderBindingInstance_Vulkan::UpdateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings)
{
    check(ShaderBindings->ShaderBindings.size() == InShaderBindings.size());

    if (!InShaderBindings.empty())
    {
        struct jWriteDescriptorInfo
        {
            EShaderBindingType BindingType = EShaderBindingType::UNIFORMBUFFER;
            std::vector<VkDescriptorBufferInfo> BufferInfo;
            std::vector<VkDescriptorImageInfo> ImageInfo;
        };
        std::vector<jWriteDescriptorInfo> descriptors;

        int32 DescriptorIndex = 0;
        EShaderBindingType PrevBindingType = InShaderBindings[0].BindingType;
        for (int32 i = 0; i < (int32)InShaderBindings.size(); ++i)
        {
            if (InShaderBindings[i].BindingType != PrevBindingType)
            {
                PrevBindingType = InShaderBindings[i].BindingType;
                ++DescriptorIndex;
            }
            if (descriptors.size() <= DescriptorIndex)
            {
                descriptors.resize(DescriptorIndex + 1);
                descriptors[DescriptorIndex].BindingType = InShaderBindings[i].BindingType;
            }

            switch (InShaderBindings[i].BindingType)
            {
                case EShaderBindingType::UNIFORMBUFFER:
                {
                    jUniformBufferResource* ubor = reinterpret_cast<jUniformBufferResource*>(InShaderBindings[i].ResourcePtr.get());
                    if (ubor && ubor->UniformBuffer)
                    {
                        VkDescriptorBufferInfo bufferInfo{};
                        bufferInfo.buffer = (VkBuffer)ubor->UniformBuffer->GetBuffer();
                        //bufferInfo.offset = bufferOffset;
                        bufferInfo.range = ubor->UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                        //bufferOffset += (int32)ubor->UniformBuffer->GetBufferSize();
                        descriptors[DescriptorIndex].BufferInfo.push_back(bufferInfo);
                    }
                    break;
                }
                case EShaderBindingType::TEXTURE_SAMPLER_SRV:
                case EShaderBindingType::TEXTURE_SRV:
                {
                    jTextureResource* tbor = reinterpret_cast<jTextureResource*>(InShaderBindings[i].ResourcePtr.get());
                    if (tbor && tbor->Texture)
                    {
                        VkDescriptorImageInfo imageInfo{};
                        imageInfo.imageLayout = tbor->Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imageInfo.imageView = (VkImageView)tbor->Texture->GetViewHandle();
                        if (tbor->SamplerState)
                            imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
                        if (!imageInfo.sampler)
                            imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                        descriptors[DescriptorIndex].ImageInfo.push_back(imageInfo);
                    }
                    break;
                }
                case EShaderBindingType::TEXTURE_UAV:
                {
                    jTextureResource* tbor = reinterpret_cast<jTextureResource*>(InShaderBindings[i].ResourcePtr.get());
                    if (tbor && tbor->Texture)
                    {
                        VkDescriptorImageInfo imageInfo{};
                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        imageInfo.imageView = (VkImageView)tbor->Texture->GetViewHandle();
                        if (tbor->SamplerState)
                            imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
                        if (!imageInfo.sampler)
                            imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                        descriptors[DescriptorIndex].ImageInfo.push_back(imageInfo);
                    }
                    break;
                }
                case EShaderBindingType::SAMPLER:
                {
                    jTextureResource* tbor = reinterpret_cast<jTextureResource*>(InShaderBindings[i].ResourcePtr.get());
                    if (tbor && tbor->SamplerState)
                    {
                        VkDescriptorImageInfo imageInfo{};
                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
                        check(imageInfo.sampler);
                        descriptors[DescriptorIndex].ImageInfo.push_back(imageInfo);
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

        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.resize(descriptors.size());
        for (int32 i = 0; i < (int32)descriptorWrites.size(); ++i)
        {
            check(descriptors[i].ImageInfo.size() == 0 || descriptors[i].BufferInfo.size() == 0);
            descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[i].dstSet = DescriptorSet;
            descriptorWrites[i].dstBinding = i;
            descriptorWrites[i].dstArrayElement = 0;
            descriptorWrites[i].descriptorType = GetVulkanShaderBindingType(descriptors[i].BindingType);
            if (descriptors[i].ImageInfo.size() > 0)
            {
                descriptorWrites[i].descriptorCount = (uint32)descriptors[i].ImageInfo.size();			// Optional	(Image Data 기반에 사용)
                descriptorWrites[i].pImageInfo = descriptors[i].ImageInfo.data();				// Optional (Buffer View 기반에 사용)
            }
            else if (descriptors[i].BufferInfo.size() > 0)
            {
                descriptorWrites[i].descriptorCount = (uint32)descriptors[i].BufferInfo.size();
                descriptorWrites[i].pBufferInfo = descriptors[i].BufferInfo.data();		            // 현재는 Buffer 기반 Desriptor 이므로 이것을 사용
            }
        }
        vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(descriptorWrites.size())
            , descriptorWrites.data(), 0, nullptr);
    }
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

//////////////////////////////////////////////////////////////////////////
// jShaderBindingsManager_Vulkan
//////////////////////////////////////////////////////////////////////////
VkDescriptorPool jShaderBindingsManager_Vulkan::CreatePool(const jShaderBindingLayout_Vulkan& bindings, uint32 maxAllocations) const
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
