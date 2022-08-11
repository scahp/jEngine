#pragma once
#include "Shader/jShader.h"

struct jShader_Vulkan : public jShader
{
    jShader_Vulkan(const jShaderInfo& shaderInfo);
    virtual ~jShader_Vulkan();

    virtual void Initialize() override;

    std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
};
