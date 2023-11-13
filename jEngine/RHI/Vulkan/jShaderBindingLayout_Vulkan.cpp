#include "pch.h"
#include "jShaderBindingLayout_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"
#include "../jRHI_Vulkan.h"
#include "jDescriptorPool_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBindings_Vulkan
//////////////////////////////////////////////////////////////////////////
jMutexRWLock jShaderBindingsLayout_Vulkan::PipelineLayoutPoolLock;
robin_hood::unordered_map<size_t, VkPipelineLayout> jShaderBindingsLayout_Vulkan::PipelineLayoutPool;

jShaderBindingsLayout_Vulkan::~jShaderBindingsLayout_Vulkan()
{
    Release();
}

bool jShaderBindingsLayout_Vulkan::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    InShaderBindingArray.CloneWithoutResource(ShaderBindingArray);
    DescriptorSetLayout = CreateDescriptorSetLayout(ShaderBindingArray);

    return !!DescriptorSetLayout;
}

std::shared_ptr<jShaderBindingInstance> jShaderBindingsLayout_Vulkan::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
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

    std::shared_ptr<jShaderBindingInstance_Vulkan> DescriptorSet = DescriptorPool->AllocateDescriptorSet(DescriptorSetLayout);
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

VkDescriptorSetLayout jShaderBindingsLayout_Vulkan::CreateDescriptorSetLayout(const jShaderBindingArray& InShaderBindingArray)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (int32 i = 0; i < (int32)InShaderBindingArray.NumOfData; ++i)
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = InShaderBindingArray[i]->BindingPoint;
        binding.descriptorType = GetVulkanShaderBindingType(InShaderBindingArray[i]->BindingType);
        binding.descriptorCount = InShaderBindingArray[i]->NumOfDescriptors;
        binding.stageFlags = GetVulkanShaderAccessFlags(InShaderBindingArray[i]->AccessStageFlags);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    if (!ensure(vkCreateDescriptorSetLayout(g_rhi_vk->Device, &layoutInfo, nullptr, &DescriptorSetLayout) == VK_SUCCESS))
        return nullptr;
    return DescriptorSetLayout;
}

VkPipelineLayout jShaderBindingsLayout_Vulkan::CreatePipelineLayout(const jShaderBindingsLayoutArray& InShaderBindingLayoutArray, const jPushConstant* pushConstant)
{
    if (InShaderBindingLayoutArray.NumOfData <= 0)
        return 0;

    VkPipelineLayout vkPipelineLayout = nullptr;
    size_t hash = InShaderBindingLayoutArray.GetHash();

    if (pushConstant)
        hash = CityHash64WithSeed(pushConstant->GetHash(), hash);
    check(hash);

    {
        jScopeReadLock sr(&PipelineLayoutPoolLock);
        auto it_find = PipelineLayoutPool.find(hash);
        if (PipelineLayoutPool.end() != it_find)
        {
            vkPipelineLayout = it_find->second;
            return vkPipelineLayout;
        }
    }

    {
        jScopeWriteLock sw(&PipelineLayoutPoolLock);

        // Try again, to avoid entering creation section simultaneously.
        auto it_find = PipelineLayoutPool.find(hash);
        if (PipelineLayoutPool.end() != it_find)
        {
            vkPipelineLayout = it_find->second;
            return vkPipelineLayout;
        }

        std::vector<VkDescriptorSetLayout> DescriptorSetLayouts;
        DescriptorSetLayouts.reserve(InShaderBindingLayoutArray.NumOfData);
        for (int32 i = 0; i < InShaderBindingLayoutArray.NumOfData; ++i)
        {
            const jShaderBindingsLayout_Vulkan* binding_vulkan = (const jShaderBindingsLayout_Vulkan*)InShaderBindingLayoutArray[i];
            DescriptorSetLayouts.push_back(binding_vulkan->DescriptorSetLayout);
        }

        std::vector<VkPushConstantRange> PushConstantRanges;
        if (pushConstant)
        {
            const jResourceContainer<jPushConstantRange>* pushConstantRanges = pushConstant->GetPushConstantRanges();
            check(pushConstantRanges);
            if (pushConstantRanges)
            {
                PushConstantRanges.reserve(pushConstantRanges->NumOfData);
                for (int32 i = 0; i < pushConstantRanges->NumOfData; ++i)
                {
                    const jPushConstantRange& range = (*pushConstantRanges)[i];

                    VkPushConstantRange pushConstantRange{};
                    pushConstantRange.stageFlags = GetVulkanShaderAccessFlags(range.AccessStageFlag);
                    pushConstantRange.offset = range.Offset;
                    pushConstantRange.size = range.Size;
                    PushConstantRanges.emplace_back(pushConstantRange);
                }
            }
        }

        // 쉐이더에 전달된 Uniform 들을 명세하기 위한 오브젝트
        // 이 오브젝트는 프로그램 실행동안 계속해서 참조되므로 cleanup 에서 제거해줌
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = (uint32)DescriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = &DescriptorSetLayouts[0];
        if (PushConstantRanges.size() > 0)
        {
            pipelineLayoutInfo.pushConstantRangeCount = (int32)PushConstantRanges.size();
            pipelineLayoutInfo.pPushConstantRanges = PushConstantRanges.data();
        }
        check(g_rhi_vk->Device);
        if (!ensure(vkCreatePipelineLayout(g_rhi_vk->Device, &pipelineLayoutInfo, nullptr, &vkPipelineLayout) == VK_SUCCESS))
            return nullptr;

        PipelineLayoutPool[hash] = vkPipelineLayout;
    }

    return vkPipelineLayout;
}

void jShaderBindingsLayout_Vulkan::ClearPipelineLayout()
{
    check(g_rhi_vk);
    {
        jScopeWriteLock s(&PipelineLayoutPoolLock);
        for (auto& iter : PipelineLayoutPool)
            vkDestroyPipelineLayout(g_rhi_vk->Device, iter.second, nullptr);
        PipelineLayoutPool.clear();
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

void jShaderBindingInstance_Vulkan::Free()
{
    if (GetType() == jShaderBindingInstanceType::MultiFrame)
        g_rhi_vk->GetDescriptorPoolMultiFrame()->Free(shared_from_this());
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

