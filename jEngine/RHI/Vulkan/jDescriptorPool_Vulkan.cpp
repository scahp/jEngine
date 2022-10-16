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

jShaderBindingInstance_Vulkan* jDescriptorPool_Vulkan::AllocateDescriptorSet(VkDescriptorSetLayout InLayout)
{
    jScopedLock s(&DescriptorPoolLock);
#if !USE_RESET_DESCRIPTOR_POOL
    const auto it_find = PendingDescriptorSets.find(InLayout);
    if (it_find != PendingDescriptorSets.end())
    {
        std::vector<jShaderBindingInstance_Vulkan*>& pendingPools = it_find->second;
        if (!pendingPools.empty())
        {
            jShaderBindingInstance_Vulkan* descriptorSet = pendingPools.back();
            pendingPools.pop_back();
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

    jShaderBindingInstance_Vulkan* NewCachedDescriptorSetPtr = new jShaderBindingInstance_Vulkan();
    NewCachedDescriptorSetPtr->DescriptorSet = NewDescriptorSet;

#if !USE_RESET_DESCRIPTOR_POOL
    AllocatedDescriptorSets[InLayout].push_back(NewCachedDescriptorSetPtr);
#endif

    return NewCachedDescriptorSetPtr;
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

        for (auto& iter : AllocatedDescriptorSets)
        {
            std::vector<jShaderBindingInstance_Vulkan*>& instances = iter.second;
            for (auto& inst : instances)
                delete inst;
        }
        AllocatedDescriptorSets.clear();
    }
}

