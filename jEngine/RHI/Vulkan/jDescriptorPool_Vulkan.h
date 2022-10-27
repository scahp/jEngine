#pragma once
#include "../jRHIType.h"

const float DefaultPoolSizes[] =
{
    2,		    // VK_DESCRIPTOR_TYPE_SAMPLER
    2,		    // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    2,		    // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    1 / 2.0,	// VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
    1 / 4.0,	// VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    4,		    // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    1 / 8.0,	// VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
    1 / 8.0	    // VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
};

using jShaderBindingInstanceVulkanArray = jResourceContainer<jShaderBindingInstance_Vulkan, 2000>;

struct jDescriptorPool_Vulkan
{
    robin_hood::unordered_map<VkDescriptorSetLayout, jShaderBindingInstanceVulkanArray> PendingDescriptorSets;
    robin_hood::unordered_map<VkDescriptorSetLayout, jShaderBindingInstanceVulkanArray> AllocatedDescriptorSets;

    jDescriptorPool_Vulkan() = default;
    virtual ~jDescriptorPool_Vulkan();

    virtual void Create(uint32 InMaxDescriptorSets = 128);
    virtual void Reset();
    virtual jShaderBindingInstance_Vulkan* AllocateDescriptorSet(VkDescriptorSetLayout InLayout);
    void Release();

    uint32 MaxDescriptorSets = 128;
    uint32 PoolSizes[_countof(DefaultPoolSizes)];
    VkDescriptorPool DescriptorPool = nullptr;;
    // mutable jMutexLock DescriptorPoolLock;
    mutable jMutexLock DescriptorPoolLock;
};

