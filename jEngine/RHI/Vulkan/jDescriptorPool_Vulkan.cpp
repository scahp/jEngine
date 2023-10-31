#include "pch.h"
#include "jDescriptorPool_Vulkan.h"
#include "Math/MathUtility.h"

#define USE_RESET_DESCRIPTOR_POOL 0

jDescriptorPool_Vulkan::~jDescriptorPool_Vulkan()
{
    if (DescriptorPool)
    {
        vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
        DescriptorPool = nullptr;
    }
}

void jDescriptorPool_Vulkan::Create(uint32 InMaxDescriptorSets)
{
    if (DescriptorPool)
    {
        vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
        DescriptorPool = nullptr;

#if !USE_RESET_DESCRIPTOR_POOL
        jScopedLock s(&DescriptorPoolLock);

        PendingDescriptorSets.clear();
        check(AllocatedDescriptorSets.size() <= 0);
#endif
    }

    check(!DescriptorPool);

    MaxDescriptorSets = InMaxDescriptorSets;

    constexpr int32 NumOfPoolSize = _countof(DefaultPoolSizes);
    VkDescriptorPoolSize Types[NumOfPoolSize];
    memset(Types, 0, sizeof(Types));

    for (uint32 i = 0; i < NumOfPoolSize; ++i)
    {
        VkDescriptorType DescriptorType = static_cast<VkDescriptorType>(VK_DESCRIPTOR_TYPE_SAMPLER + i);

        PoolSizes[i] = (uint32)Max(DefaultPoolSizes[i] * InMaxDescriptorSets, 4.0f);

        Types[i].type = DescriptorType;
        Types[i].descriptorCount = PoolSizes[i];
    }

    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.poolSizeCount = NumOfPoolSize;
    PoolInfo.pPoolSizes = Types;
    PoolInfo.maxSets = InMaxDescriptorSets;

    verify(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &PoolInfo, nullptr, &DescriptorPool));
}

void jDescriptorPool_Vulkan::Reset()
{
#if USE_RESET_DESCRIPTOR_POOL
    verify(VK_SUCCESS == vkResetDescriptorPool(g_rhi_vk->Device, DescriptorPool, 0));
#else
    jScopedLock s(&DescriptorPoolLock);
    PendingDescriptorSets = AllocatedDescriptorSets;
#endif
}

std::shared_ptr<jShaderBindingInstance_Vulkan> jDescriptorPool_Vulkan::AllocateDescriptorSet(VkDescriptorSetLayout InLayout)
{
    jScopedLock s(&DescriptorPoolLock);
#if !USE_RESET_DESCRIPTOR_POOL
    const auto it_find = PendingDescriptorSets.find(InLayout);
    if (it_find != PendingDescriptorSets.end())
    {
        jShaderBindingInstanceVulkanArray& pendingPools = it_find->second;
        if (pendingPools.size())
        {
            std::shared_ptr<jShaderBindingInstance_Vulkan> descriptorSet = *pendingPools.rbegin();
            //pendingPools.PopBack();
            pendingPools.resize(pendingPools.size() - 1);
            return descriptorSet;
        }
    }
#endif

    VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo{};
    DescriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    DescriptorSetAllocateInfo.descriptorPool = DescriptorPool;
    DescriptorSetAllocateInfo.descriptorSetCount = 1;
    DescriptorSetAllocateInfo.pSetLayouts = &InLayout;

    VkDescriptorSet NewDescriptorSet = nullptr;
    if (!ensure(VK_SUCCESS == vkAllocateDescriptorSets(g_rhi_vk->Device, &DescriptorSetAllocateInfo, &NewDescriptorSet)))
        return nullptr;

    std::shared_ptr<jShaderBindingInstance_Vulkan> NewCachedDescriptorSet = std::make_shared<jShaderBindingInstance_Vulkan>();
    NewCachedDescriptorSet->DescriptorSet = NewDescriptorSet;

#if !USE_RESET_DESCRIPTOR_POOL
    AllocatedDescriptorSets[InLayout].push_back(NewCachedDescriptorSet);
#endif

    return NewCachedDescriptorSet;
}

void jDescriptorPool_Vulkan::Free(std::shared_ptr<jShaderBindingInstance_Vulkan> InShaderBindingInstance)
{
    check(InShaderBindingInstance);
    jScopedLock s(&DescriptorPoolLock);
    
    const int32 CurrentFrameNumber = g_rhi->GetCurrentFrameNumber();
    const int32 OldestFrameToKeep = CurrentFrameNumber - NumOfFramesToWaitBeforeReleasing;

    // ProcessPendingDescriptorPoolFree
    {
        // Check it is too early
        if (CurrentFrameNumber >= CanReleasePendingFreeShaderBindingInstanceFrameNumber)
        {
            // Release pending memory
            int32 i = 0;
            for (; i < PendingFree.size(); ++i)
            {
                jPendingFreeShaderBindingInstance& PendingFreeShaderBindingInstance = PendingFree[i];
                if (PendingFreeShaderBindingInstance.FrameIndex < OldestFrameToKeep)
                {
                    // Return to pending descriptor set
                    check(PendingFreeShaderBindingInstance.ShaderBindingInstance);
                    const VkDescriptorSetLayout DescriptorSetLayout = (VkDescriptorSetLayout)PendingFreeShaderBindingInstance.ShaderBindingInstance->ShaderBindingsLayouts->GetHandle();
                    PendingDescriptorSets[DescriptorSetLayout].push_back(PendingFreeShaderBindingInstance.ShaderBindingInstance);
                }
                else
                {
                    CanReleasePendingFreeShaderBindingInstanceFrameNumber = PendingFreeShaderBindingInstance.FrameIndex + NumOfFramesToWaitBeforeReleasing + 1;
                    break;
                }
            }
            if (i > 0)
            {
                const size_t RemainingSize = (PendingFree.size() - i);
                if (RemainingSize > 0)
                {
                    for (int32 k = 0; k < RemainingSize; ++k)
                        PendingFree[k] = PendingFree[i + k];
                    //memcpy(&PendingFree[0], &PendingFree[i], sizeof(jPendingFreeShaderBindingInstance) * RemainingSize);
                }
                PendingFree.resize(RemainingSize);
            }
        }
    }

    PendingFree.emplace_back(jPendingFreeShaderBindingInstance(CurrentFrameNumber, InShaderBindingInstance));
}

void jDescriptorPool_Vulkan::Release()
{
    if (DescriptorPool)
    {
        vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
        DescriptorPool = nullptr;
    }

    {
        jScopedLock s(&DescriptorPoolLock);

        //for (auto& iter : AllocatedDescriptorSets)
        //{
        //    jShaderBindingInstanceVulkanArray& instances = iter.second;
        //    for (int32 i = 0; i < instances.size(); ++i)
        //    {
        //        delete instances[i];
        //    }
        //}
        AllocatedDescriptorSets.clear();
    }
}

