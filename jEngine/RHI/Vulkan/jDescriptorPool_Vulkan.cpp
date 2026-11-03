#include "pch.h"
#include "jDescriptorPool_Vulkan.h"
#include "Math/MathUtility.h"

#define USE_RESET_DESCRIPTOR_POOL 0

jDescriptorPool_Vulkan::~jDescriptorPool_Vulkan()
{
    Release();
    DeallocateMultiframeShaderBindingInstance.FreeDelegate = nullptr;
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
    for (auto Pool : OverflowDescriptorPools) vkDestroyDescriptorPool(g_rhi_vk->Device, Pool, nullptr);
    OverflowDescriptorPools.clear();
    RemainingDescriptors.clear();
    AllocatedDescriptorSetCount = 0;

    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    std::vector<VkDescriptorPoolSize> supportedTypes;
    for (const auto& Budget : jVulkanDescriptorPoolSizing::DefaultPoolSizes)
    {
        if (Budget.Type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR && !IsRaytracingSceneSupported()) continue;
        const uint32 Count = static_cast<uint32>(Max(Budget.PerSet * InMaxDescriptorSets, 4.0f));
        supportedTypes.push_back({Budget.Type, Count});
        RemainingDescriptors[Budget.Type] = Count;
    }
    PoolInfo.poolSizeCount = static_cast<uint32>(supportedTypes.size());
    PoolInfo.pPoolSizes = supportedTypes.data();
    PoolInfo.maxSets = InMaxDescriptorSets;
    PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;       // for bindless resources

    verify(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &PoolInfo, nullptr, &DescriptorPool));

    DeallocateMultiframeShaderBindingInstance.FreeDelegate = std::bind(&jDescriptorPool_Vulkan::FreedFromPendingDelegate, this, std::placeholders::_1);
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

std::shared_ptr<jShaderBindingInstance> jDescriptorPool_Vulkan::AllocateDescriptorSet(VkDescriptorSetLayout InLayout,
    const std::vector<VkDescriptorPoolSize>& InRequirements)
{
    jScopedLock s(&DescriptorPoolLock);
#if !USE_RESET_DESCRIPTOR_POOL
    const auto it_find = PendingDescriptorSets.find(InLayout);
    if (it_find != PendingDescriptorSets.end())
    {
        jShaderBindingInstancePtrArray& pendingPools = it_find->second;
        if (pendingPools.size())
        {
            std::shared_ptr<jShaderBindingInstance> descriptorSet = *pendingPools.rbegin();
            //pendingPools.PopBack();
            pendingPools.resize(pendingPools.size() - 1);
            return descriptorSet;
        }
    }
#endif

    bool UsePrimaryPool = AllocatedDescriptorSetCount < MaxDescriptorSets;
    for (const auto& Requirement : InRequirements)
    {
        const auto Capacity = RemainingDescriptors.find(Requirement.type);
        if (Capacity == RemainingDescriptors.end() || Capacity->second < Requirement.descriptorCount)
            UsePrimaryPool = false;
    }
    VkDescriptorPool AllocationPool = DescriptorPool;
    if (!UsePrimaryPool)
    {
        // A bindless set can exceed the default per-type budget. Size a cached
        // overflow allocation from the actual layout, without guessing a cap.
        VkDescriptorPoolCreateInfo PoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        PoolInfo.maxSets = 1;
        PoolInfo.poolSizeCount = static_cast<uint32>(InRequirements.size());
        PoolInfo.pPoolSizes = InRequirements.data();
        if (!ensure(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &PoolInfo, nullptr, &AllocationPool)))
            return nullptr;
    }

    VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo{};
    DescriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    DescriptorSetAllocateInfo.descriptorPool = AllocationPool;
    DescriptorSetAllocateInfo.descriptorSetCount = 1;
    DescriptorSetAllocateInfo.pSetLayouts = &InLayout;

    VkDescriptorSet NewDescriptorSet = nullptr;
    if (!ensure(VK_SUCCESS == vkAllocateDescriptorSets(g_rhi_vk->Device, &DescriptorSetAllocateInfo, &NewDescriptorSet)))
    {
        if (!UsePrimaryPool) vkDestroyDescriptorPool(g_rhi_vk->Device, AllocationPool, nullptr);
        return nullptr;
    }
    if (UsePrimaryPool)
    {
        ++AllocatedDescriptorSetCount;
        for (const auto& Requirement : InRequirements)
            RemainingDescriptors[Requirement.type] -= Requirement.descriptorCount;
    }
    else OverflowDescriptorPools.push_back(AllocationPool);

    auto NewShaderBindingInstance_Vulkan = std::make_shared<jShaderBindingInstance_Vulkan>();
    NewShaderBindingInstance_Vulkan->DescriptorSet = NewDescriptorSet;

#if !USE_RESET_DESCRIPTOR_POOL
    AllocatedDescriptorSets[InLayout].push_back(NewShaderBindingInstance_Vulkan);
#endif

    return NewShaderBindingInstance_Vulkan;
}

void jDescriptorPool_Vulkan::Free(std::shared_ptr<jShaderBindingInstance> InShaderBindingInstance)
{
    check(InShaderBindingInstance);
    jScopedLock s(&DescriptorPoolLock);

    DeallocateMultiframeShaderBindingInstance.Free(InShaderBindingInstance);
}

void jDescriptorPool_Vulkan::Release()
{
    if (DescriptorPool)
    {
        vkDestroyDescriptorPool(g_rhi_vk->Device, DescriptorPool, nullptr);
        DescriptorPool = nullptr;
    }
    for (auto Pool : OverflowDescriptorPools) vkDestroyDescriptorPool(g_rhi_vk->Device, Pool, nullptr);
    OverflowDescriptorPools.clear();
    RemainingDescriptors.clear();
    AllocatedDescriptorSetCount = 0;

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
        PendingDescriptorSets.clear();
    }
}
