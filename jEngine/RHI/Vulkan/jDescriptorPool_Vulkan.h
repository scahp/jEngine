#pragma once
#include "../jRHIType.h"
#include "jDescriptorPoolSizing_Vulkan.h"

struct jDescriptorPool_Vulkan
{
    std::map<VkDescriptorSetLayout, jShaderBindingInstancePtrArray> PendingDescriptorSets;
    std::map<VkDescriptorSetLayout, jShaderBindingInstancePtrArray> AllocatedDescriptorSets;

    jDescriptorPool_Vulkan() = default;
    virtual ~jDescriptorPool_Vulkan();

    virtual void Create(uint32 InMaxDescriptorSets = 128);
    virtual void Reset();
    virtual std::shared_ptr<jShaderBindingInstance> AllocateDescriptorSet(VkDescriptorSetLayout InLayout,
        const std::vector<VkDescriptorPoolSize>& InRequirements);
    virtual void Free(std::shared_ptr<jShaderBindingInstance> InShaderBindingInstance);
    void Release();

    // This will be called from 'jDeallocatorMultiFrameShaderBindingInstance'
    void FreedFromPendingDelegate(std::shared_ptr<jShaderBindingInstance> InShaderBindingInstance)
    {
        jShaderBindingInstance_Vulkan* ShaderBindingInstance_Vulkan = (jShaderBindingInstance_Vulkan*)InShaderBindingInstance.get();
        check(ShaderBindingInstance_Vulkan);

        const VkDescriptorSetLayout DescriptorSetLayout = (VkDescriptorSetLayout)ShaderBindingInstance_Vulkan->ShaderBindingsLayouts->GetHandle();
        PendingDescriptorSets[DescriptorSetLayout].push_back(InShaderBindingInstance);
    }

    uint32 MaxDescriptorSets = 128;
    std::map<VkDescriptorType, uint32> RemainingDescriptors;
    uint32 AllocatedDescriptorSetCount = 0;
    std::vector<VkDescriptorPool> OverflowDescriptorPools;
    VkDescriptorPool DescriptorPool = nullptr;;
    mutable jMutexLock DescriptorPoolLock;

    jDeallocatorMultiFrameShaderBindingInstance DeallocateMultiframeShaderBindingInstance;
};
