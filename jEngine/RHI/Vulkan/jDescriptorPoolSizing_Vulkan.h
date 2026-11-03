#pragma once
#include "../jShaderBindingLayout.h"
#include "jRHIType_Vulkan.h"
#include <limits>
#include <map>
#include <stdexcept>

namespace jVulkanDescriptorPoolSizing
{
    struct Budget { VkDescriptorType Type; float PerSet; };
    inline constexpr Budget DefaultPoolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2.0f},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2.0f},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2.0f},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f / 8},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.0f / 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.0f / 8},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f / 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1.0f / 8},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4.0f},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.0f / 8},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1.0f / 8},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1.0f / 8}
    };

    inline std::vector<VkDescriptorPoolSize> GetRequirements(const jShaderBindingArray& Bindings, uint32 Allocations)
    {
        std::map<VkDescriptorType, uint64> Counts;
        for (int32 i = 0; i < Bindings.NumOfData; ++i)
        {
            if (Bindings[i]->NumOfDescriptors < 0) throw std::invalid_argument("Negative descriptor count");
            auto& Count = Counts[GetVulkanShaderBindingType(Bindings[i]->BindingType)];
            Count += uint64(Bindings[i]->NumOfDescriptors) * Allocations;
            if (Count > std::numeric_limits<uint32>::max()) throw std::overflow_error("Descriptor pool size overflow");
        }
        std::vector<VkDescriptorPoolSize> Result;
        for (const auto& Count : Counts)
            if (Count.second) Result.push_back({Count.first, static_cast<uint32>(Count.second)});
        return Result;
    }
}
