#include "pch.h"
#include "jShaderBindingLayout_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"
#include "jRHI_Vulkan.h"
#include "jDescriptorPool_Vulkan.h"

//////////////////////////////////////////////////////////////////////////
// jShaderBinding_Vulkan
//////////////////////////////////////////////////////////////////////////
jMutexRWLock jShaderBindingLayout_Vulkan::DescriptorLayoutPoolLock;
robin_hood::unordered_map<size_t, VkDescriptorSetLayout> jShaderBindingLayout_Vulkan::DescriptorLayoutPool;

jMutexRWLock jShaderBindingLayout_Vulkan::PipelineLayoutPoolLock;
robin_hood::unordered_map<size_t, VkPipelineLayout> jShaderBindingLayout_Vulkan::PipelineLayoutPool;

jShaderBindingLayout_Vulkan::~jShaderBindingLayout_Vulkan()
{
    Release();
}

bool jShaderBindingLayout_Vulkan::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    InShaderBindingArray.CloneWithoutResource(ShaderBindingArray);
    DescriptorSetLayout = CreateDescriptorSetLayout(ShaderBindingArray);

    return !!DescriptorSetLayout;
}

std::shared_ptr<jShaderBindingInstance> jShaderBindingLayout_Vulkan::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
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

    std::shared_ptr<jShaderBindingInstance> DescriptorSet = DescriptorPool->AllocateDescriptorSet(DescriptorSetLayout);
    if (!ensure(DescriptorSet))
    {
        DescriptorSet = DescriptorPool->AllocateDescriptorSet(DescriptorSetLayout);
        return nullptr;
    }

    DescriptorSet->ShaderBindingsLayouts = this;
    DescriptorSet->Initialize(InShaderBindingArray);
    DescriptorSet->SetType(InType);
    return DescriptorSet;
}

size_t jShaderBindingLayout_Vulkan::GetHash() const
{
    if (Hash)
        return Hash;

    Hash = ShaderBindingArray.GetHash();
    return Hash;
}

void jShaderBindingLayout_Vulkan::Release()
{
    if (DescriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(g_rhi_vk->Device, DescriptorSetLayout, nullptr);
        DescriptorSetLayout = nullptr;
    }
}

VkDescriptorSetLayout jShaderBindingLayout_Vulkan::CreateDescriptorSetLayout(const jShaderBindingArray& InShaderBindingArray)
{
    VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    size_t hash = InShaderBindingArray.GetHash();
    check(hash);
    {
        jScopeReadLock sr(&DescriptorLayoutPoolLock);
        auto it_find = DescriptorLayoutPool.find(hash);
        if (DescriptorLayoutPool.end() != it_find)
        {
            DescriptorSetLayout = it_find->second;
            return DescriptorSetLayout;
        }
    }

    {
        jScopeWriteLock sw(&DescriptorLayoutPoolLock);

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(InShaderBindingArray.NumOfData);

        std::vector<VkDescriptorBindingFlagsEXT> bindingFlags;
        bindingFlags.reserve(InShaderBindingArray.NumOfData);

        int32 LastBindingIndex = 0;
        for (int32 i = 0; i < (int32)InShaderBindingArray.NumOfData; ++i)
        {
            VkDescriptorSetLayoutBinding binding = {};
            if (InShaderBindingArray[i]->BindingPoint != jShaderBinding::APPEND_LAST)
            {
                binding.binding = InShaderBindingArray[i]->BindingPoint;
                LastBindingIndex = InShaderBindingArray[i]->BindingPoint + 1;
            }
            else
            {
                binding.binding = LastBindingIndex++;
            }
            binding.descriptorType = GetVulkanShaderBindingType(InShaderBindingArray[i]->BindingType);
            binding.descriptorCount = InShaderBindingArray[i]->NumOfDescriptors;
            binding.stageFlags = GetVulkanShaderAccessFlags(InShaderBindingArray[i]->AccessStageFlags);
            binding.pImmutableSamplers = nullptr;
            bindings.push_back(binding);
            bindingFlags.push_back(InShaderBindingArray[i]->Resource->IsBindless() ? (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) : 0);
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;      // for bindless resources

        // for bindless resources
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT setLayoutBindingFlags{};
        if (bindingFlags.size() > 0)
        {
            setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
            setLayoutBindingFlags.bindingCount = (uint32)bindingFlags.size();
            setLayoutBindingFlags.pBindingFlags = bindingFlags.data();
            layoutInfo.pNext = &setLayoutBindingFlags;
        }

        if (!ensure(vkCreateDescriptorSetLayout(g_rhi_vk->Device, &layoutInfo, nullptr, &DescriptorSetLayout) == VK_SUCCESS))
            return nullptr;

        DescriptorLayoutPool[hash] = DescriptorSetLayout;
    }

    return DescriptorSetLayout;
}

VkPipelineLayout jShaderBindingLayout_Vulkan::CreatePipelineLayout(const jShaderBindingLayoutArray& InShaderBindingLayoutArray, const jPushConstant* InPushConstant, EShaderAccessStageFlag InShaderAccessStageFlag)
{
    if (InShaderBindingLayoutArray.NumOfData <= 0)
        return 0;

    VkPipelineLayout vkPipelineLayout = nullptr;
    size_t hash = InShaderBindingLayoutArray.GetHash();

    if (InPushConstant)
        hash = XXH64(InPushConstant->GetHash(), hash);
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
            const jShaderBindingLayout_Vulkan* binding_vulkan = (const jShaderBindingLayout_Vulkan*)InShaderBindingLayoutArray[i];
            
#if _DEBUG
            const auto& ShaderBindingArray = binding_vulkan->GetShaderBindingsLayout();
            for(int32 k = 0;k< ShaderBindingArray.NumOfData;++k)
            {
                check(!!(ShaderBindingArray[k]->AccessStageFlags & InShaderAccessStageFlag));
            }
#endif // _DEBUG
            
            DescriptorSetLayouts.push_back(binding_vulkan->DescriptorSetLayout);
        }

        std::vector<VkPushConstantRange> PushConstantRanges;
        if (InPushConstant)
        {
            const jResourceContainer<jPushConstantRange>* pushConstantRanges = InPushConstant->GetPushConstantRanges();
            check(pushConstantRanges);
            if (pushConstantRanges)
            {
                PushConstantRanges.reserve(pushConstantRanges->NumOfData);
                for (int32 i = 0; i < pushConstantRanges->NumOfData; ++i)
                {
                    const jPushConstantRange& range = (*pushConstantRanges)[i];

                    VkPushConstantRange pushConstantRange{};
                    check(!!(InShaderAccessStageFlag & range.AccessStageFlag));
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

void jShaderBindingLayout_Vulkan::ClearPipelineLayout()
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
    std::vector<uint32>& dynamicOffsets = OutDescriptorWrites.DynamicOffsets;
    
    int32 LastIndex = 0;
    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
    {
        OutDescriptorWrites.SetWriteDescriptorInfo(InShaderBindingArray[i]);
        descriptorWrites.resize(descriptors.size());

        for (int32 k = LastIndex; k < descriptorWrites.size(); ++k)
        {
            VkWriteDescriptorSet& CurDescriptorWrite = descriptorWrites[k];
            CurDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            CurDescriptorWrite.dstSet = InDescriptorSet;
            CurDescriptorWrite.dstBinding = i;
            CurDescriptorWrite.dstArrayElement = k - LastIndex;
            CurDescriptorWrite.descriptorType = GetVulkanShaderBindingType(InShaderBindingArray[i]->BindingType);
            CurDescriptorWrite.descriptorCount = 1;
        }
        LastIndex = (int32)descriptorWrites.size();
    }

    check(descriptors.size() == descriptorWrites.size());
    for(int32 i=0;i<(int32)descriptorWrites.size();++i)
    {
        const jWriteDescriptorInfo& WriteDescriptorInfo = descriptors[i];
        VkWriteDescriptorSet& CurDescriptorWrite = descriptorWrites[i];
        if (WriteDescriptorInfo.BufferInfo.buffer)
        {
            CurDescriptorWrite.pBufferInfo = &WriteDescriptorInfo.BufferInfo;		            // Buffer should be bound in pBufferInfo
        }
        else if (WriteDescriptorInfo.ImageInfo.imageView || WriteDescriptorInfo.ImageInfo.sampler)
        {
            CurDescriptorWrite.pImageInfo = &WriteDescriptorInfo.ImageInfo;	        			// Image should be bound in pImageInfo
        }
        else if (WriteDescriptorInfo.AccelerationStructureInfo.pAccelerationStructures)         // AS structure should be bound in pNext
        {
            CurDescriptorWrite.pNext = &WriteDescriptorInfo.AccelerationStructureInfo;
        }
        else
        {
            check(0);
        }
    }

    OutDescriptorWrites.IsInitialized = true;
}

//void jShaderBindingInstance_Vulkan::UpdateWriteDescriptorSet(
//    jWriteDescriptorSet& OutDescriptorWrites, const jShaderBindingArray& InShaderBindingArray)
//{
//    check(InShaderBindingArray.NumOfData == OutDescriptorWrites.DescriptorWrites.size());
//    
//    OutDescriptorWrites.DynamicOffsets.clear();
//    for (int32 i = 0; i < InShaderBindingArray.NumOfData; ++i)
//    {
//        OutDescriptorWrites.SetWriteDescriptorInfo(InShaderBindingArray[i]);
//    }
//}

void jShaderBindingInstance_Vulkan::Initialize(const jShaderBindingArray& InShaderBindingArray)
{
    //if (!WriteDescriptorSet.IsInitialized)
    {
        CreateWriteDescriptorSet(WriteDescriptorSet, DescriptorSet, InShaderBindingArray);
    }
    //else
    //{
    //    UpdateWriteDescriptorSet(WriteDescriptorSet, InShaderBindingArray);
    //}

    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(WriteDescriptorSet.DescriptorWrites.size())
        , WriteDescriptorSet.DescriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray)
{
    check(ShaderBindingsLayouts->GetShaderBindingsLayout().NumOfData == InShaderBindingArray.NumOfData);
    check(InShaderBindingArray.NumOfData);

    //if (!WriteDescriptorSet.IsInitialized)
    {
        CreateWriteDescriptorSet(WriteDescriptorSet, DescriptorSet, InShaderBindingArray);
    }
    //else
    //{
    //    UpdateWriteDescriptorSet(WriteDescriptorSet, InShaderBindingArray);
    //}
    
    vkUpdateDescriptorSets(g_rhi_vk->Device, static_cast<uint32>(WriteDescriptorSet.DescriptorWrites.size())
        , WriteDescriptorSet.DescriptorWrites.data(), 0, nullptr);
}

void jShaderBindingInstance_Vulkan::Free()
{
    if (GetType() == jShaderBindingInstanceType::MultiFrame)
        g_rhi_vk->GetDescriptorPoolMultiFrame()->Free(shared_from_this());
}

void jWriteDescriptorSet::SetWriteDescriptorInfo(const jShaderBinding* InShaderBinding)
{
    check(InShaderBinding);
    check(InShaderBinding->Resource);
    const bool IsBindless = InShaderBinding->Resource->IsBindless();

    switch (InShaderBinding->BindingType)
    {
    case EShaderBindingType::UNIFORMBUFFER:
    {
        if (IsBindless)
        {
            const jUniformBufferResourceBindless* ubor = reinterpret_cast<const jUniformBufferResourceBindless*>(InShaderBinding->Resource);
            if (ensure(ubor))
            {
                for(auto UniformBuffer : ubor->UniformBuffers)
                {
                    check(UniformBuffer);

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = (VkBuffer)UniformBuffer->GetLowLevelResource();
                    bufferInfo.offset = UniformBuffer->GetBufferOffset();
                    bufferInfo.range = UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                    check(bufferInfo.buffer);
                    WriteDescriptorInfos.push_back(jWriteDescriptorInfo(bufferInfo));
                }
            }
        }
        else
        {
            const jUniformBufferResource* ubor = reinterpret_cast<const jUniformBufferResource*>(InShaderBinding->Resource);
            if (ensure(ubor && ubor->UniformBuffer))
            {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = (VkBuffer)ubor->UniformBuffer->GetLowLevelResource();
                bufferInfo.offset = ubor->UniformBuffer->GetBufferOffset();
                bufferInfo.range = ubor->UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                check(bufferInfo.buffer);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(bufferInfo));
            }
        }
        break;
    }
    case EShaderBindingType::UNIFORMBUFFER_DYNAMIC:
    {
        if (IsBindless)
        {
            const jUniformBufferResourceBindless* ubor = reinterpret_cast<const jUniformBufferResourceBindless*>(InShaderBinding->Resource);
            if (ensure(ubor))
            {
                for (auto UniformBuffer : ubor->UniformBuffers)
                {
                    check(UniformBuffer);

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = (VkBuffer)UniformBuffer->GetLowLevelResource();
                    bufferInfo.offset = 0;      // Use DynamicOffset instead
                    bufferInfo.range = UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                    check(bufferInfo.buffer);
                    WriteDescriptorInfos.push_back(jWriteDescriptorInfo(bufferInfo));
                    DynamicOffsets.push_back((uint32)UniformBuffer->GetBufferOffset());
                }
            }
        }
        else
        {
            const jUniformBufferResource* ubor = reinterpret_cast<const jUniformBufferResource*>(InShaderBinding->Resource);
            if (ensure(ubor && ubor->UniformBuffer))
            {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = (VkBuffer)ubor->UniformBuffer->GetLowLevelResource();
                bufferInfo.offset = 0;      // Use DynamicOffset instead
                bufferInfo.range = ubor->UniformBuffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                check(bufferInfo.buffer);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(bufferInfo));
                DynamicOffsets.push_back((uint32)ubor->UniformBuffer->GetBufferOffset());
            }
        }
        break;
    }
    case EShaderBindingType::TEXTURE_SAMPLER_SRV:
    case EShaderBindingType::TEXTURE_SRV:
    {
        if (IsBindless)
        {
            const jTextureResourceBindless* tbor = reinterpret_cast<const jTextureResourceBindless*>(InShaderBinding->Resource);
            if (ensure(tbor))
            {
                for (auto TextureData : tbor->TextureBindDatas)
                {
                    check(TextureData.Texture);

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageLayout = TextureData.Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfo.imageView = ((const jTexture_Vulkan*)TextureData.Texture)->View;
                    imageInfo.sampler = TextureData.SamplerState ? (VkSampler)TextureData.SamplerState->GetHandle() : nullptr;
                    if (!imageInfo.sampler)
                        imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                    check(imageInfo.imageView);
                    WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
                }
            }
        }
        else
        {
            const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(InShaderBinding->Resource);
            if (ensure(tbor && tbor->Texture))
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = tbor->Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = ((const jTexture_Vulkan*)tbor->Texture)->View;
                imageInfo.sampler = tbor->SamplerState ? (VkSampler)tbor->SamplerState->GetHandle() : nullptr;
                if (!imageInfo.sampler)
                    imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                check(imageInfo.imageView);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
            }
        }
        break;
    }
    case EShaderBindingType::TEXTURE_ARRAY_SRV:
    {
        const jTextureArrayResource* tbor = reinterpret_cast<const jTextureArrayResource*>(InShaderBinding->Resource);
        if (ensure(tbor && tbor->TextureArray))
        {
            check(0);   // todo
        }
        break;
    }
    case EShaderBindingType::SUBPASS_INPUT_ATTACHMENT:
    {
        if (IsBindless)
        {
            const jTextureResourceBindless* tbor = reinterpret_cast<const jTextureResourceBindless*>(InShaderBinding->Resource);
            if (ensure(tbor))
            {
                for (auto TextureData : tbor->TextureBindDatas)
                {
                    check(TextureData.Texture);

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageLayout = TextureData.Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfo.imageView = ((const jTexture_Vulkan*)TextureData.Texture)->View;
                    imageInfo.sampler = TextureData.SamplerState ? (VkSampler)TextureData.SamplerState->GetHandle() : nullptr;
                    if (!imageInfo.sampler)
                        imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                    check(imageInfo.imageView);
                    WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
                }
            }
        }
        else
        {
            const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(InShaderBinding->Resource);
            if (ensure(tbor && tbor->Texture))
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = (tbor->Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                imageInfo.imageView = ((const jTexture_Vulkan*)tbor->Texture)->View;
                check(imageInfo.imageView);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
            }
        }
        break;
    }
    case EShaderBindingType::TEXTURE_UAV:
    {
        if (IsBindless)
        {
            const jTextureResourceBindless* tbor = reinterpret_cast<const jTextureResourceBindless*>(InShaderBinding->Resource);
            if (ensure(tbor))
            {
                for (auto TextureData : tbor->TextureBindDatas)
                {
                    check(TextureData.Texture);

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                    const jTexture_Vulkan* Texture = (const jTexture_Vulkan*)TextureData.Texture;
                    if (TextureData.MipLevel == 0)
                    {
                        imageInfo.imageView = Texture->ViewUAV ? Texture->ViewUAV : Texture->View;
                        check(imageInfo.imageView);
                    }
                    else
                    {
                        const auto& ViewForMipMap = (Texture->ViewUAVForMipMap.size() > 0) ? Texture->ViewUAVForMipMap : Texture->ViewForMipMap;
                        auto it_find = ViewForMipMap.find(TextureData.MipLevel);
                        if (it_find != ViewForMipMap.end() && it_find->second)
                        {
                            imageInfo.imageView = it_find->second;
                        }

                        if (!imageInfo.imageView)
                        {
                            imageInfo.imageView = Texture->ViewUAV;
                            check(imageInfo.imageView);
                        }
                    }

                    if (TextureData.SamplerState)
                        imageInfo.sampler = (VkSampler)TextureData.SamplerState->GetHandle();
                    if (!imageInfo.sampler)
                        imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                    check(imageInfo.imageView);
                    WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
                }
            }
        }
        else
        {
            const jTextureResource* tbor = reinterpret_cast<const jTextureResource*>(InShaderBinding->Resource);
            if (ensure(tbor && tbor->Texture))
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                const jTexture_Vulkan* Texture = (const jTexture_Vulkan*)tbor->Texture;
                if (tbor->MipLevel == 0)
                {
                    imageInfo.imageView = Texture->ViewUAV ? Texture->ViewUAV : Texture->View;
                    check(imageInfo.imageView);
                }
                else
                {
                    const auto& ViewForMipMap = (Texture->ViewUAVForMipMap.size() > 0) ? Texture->ViewUAVForMipMap : Texture->ViewForMipMap;
                    auto it_find = ViewForMipMap.find(tbor->MipLevel);
                    if (it_find != ViewForMipMap.end() && it_find->second)
                    {
                        imageInfo.imageView = it_find->second;
                    }

                    if (!imageInfo.imageView)
                    {
                        imageInfo.imageView = Texture->ViewUAV;
                        check(imageInfo.imageView);
                    }
                }

                if (tbor->SamplerState)
                    imageInfo.sampler = (VkSampler)tbor->SamplerState->GetHandle();
                if (!imageInfo.sampler)
                    imageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
                check(imageInfo.imageView);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
            }
        }
        break;
    }
    case EShaderBindingType::SAMPLER:
    {
        if (IsBindless)
        {
            const jSamplerResourceBindless* sr = reinterpret_cast<const jSamplerResourceBindless*>(InShaderBinding->Resource);
            if (ensure(sr))
            {
                for(auto SamplerState : sr->SamplerStates)
                {
                    check(SamplerState);

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfo.sampler = (VkSampler)SamplerState->GetHandle();
                    check(imageInfo.sampler);
                    WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
                }
            }
        }
        else
        {
            const jSamplerResource* sr = reinterpret_cast<const jSamplerResource*>(InShaderBinding->Resource);
            if (ensure(sr && sr->SamplerState))
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.sampler = (VkSampler)sr->SamplerState->GetHandle();
                check(imageInfo.sampler);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(imageInfo));
            }
        }
        break;
    }
    case EShaderBindingType::BUFFER_TEXEL_SRV:
    case EShaderBindingType::BUFFER_TEXEL_UAV:
    case EShaderBindingType::BUFFER_SRV:
    case EShaderBindingType::BUFFER_UAV:
    case EShaderBindingType::BUFFER_UAV_DYNAMIC:
    {
        if (IsBindless)
        {
            const jBufferResourceBindless* br = reinterpret_cast<const jBufferResourceBindless*>(InShaderBinding->Resource);
            for(auto Buffer : br->Buffers)
            {
                check(Buffer);

                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = (VkBuffer)Buffer->GetHandle();
                bufferInfo.offset = Buffer->GetOffset();
                bufferInfo.range = Buffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                check(bufferInfo.buffer);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(bufferInfo));
            }
        }
        else
        {
            const jBufferResource* br = reinterpret_cast<const jBufferResource*>(InShaderBinding->Resource);
            if (ensure(br && br->Buffer))
            {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = (VkBuffer)br->Buffer->GetHandle();
                bufferInfo.offset = br->Buffer->GetOffset();
                bufferInfo.range = br->Buffer->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
                check(bufferInfo.buffer);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(bufferInfo));
            }
        }
        break;
    }
    case EShaderBindingType::ACCELERATION_STRUCTURE_SRV:
    {
        if (IsBindless)
        {
            const jBufferResourceBindless* br = reinterpret_cast<const jBufferResourceBindless*>(InShaderBinding->Resource);
            for (auto Buffer : br->Buffers)
            {
                check(Buffer);

                VkWriteDescriptorSetAccelerationStructureKHR AccelerationStructureKHR{};
                AccelerationStructureKHR.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                AccelerationStructureKHR.accelerationStructureCount = 1;
                AccelerationStructureKHR.pAccelerationStructures = &((jBuffer_Vulkan*)Buffer)->AccelerationStructure;
                check(AccelerationStructureKHR.pAccelerationStructures);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(AccelerationStructureKHR));
            }
        }
        else
        {
            const jBufferResource* br = reinterpret_cast<const jBufferResource*>(InShaderBinding->Resource);
            if (ensure(br && br->Buffer))
            {
                VkWriteDescriptorSetAccelerationStructureKHR AccelerationStructureKHR{};
                AccelerationStructureKHR.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                AccelerationStructureKHR.accelerationStructureCount = 1;
                AccelerationStructureKHR.pAccelerationStructures = &((jBuffer_Vulkan*)br->Buffer)->AccelerationStructure;
                check(AccelerationStructureKHR.pAccelerationStructures);
                WriteDescriptorInfos.push_back(jWriteDescriptorInfo(AccelerationStructureKHR));
            }
        }
        break;
    }
    default:
        check(0);
        break;
    }
}

