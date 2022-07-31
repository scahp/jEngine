#pragma once
#include "Shader/jShader.h"

struct jShader_Vulkan : public jShader
{
    virtual ~jShader_Vulkan()
    {
        for (auto& Stage : ShaderStages)
        {
            vkDestroyShaderModule(g_rhi_vk->Device, Stage.module, nullptr);
        }
    }

    std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
};
