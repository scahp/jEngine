#pragma once
#include "Shader/jShader.h"

struct jCompiledShader_Vulkan : public jCompiledShader
{
    virtual ~jCompiledShader_Vulkan();

    VkPipelineShaderStageCreateInfo ShaderStage{};
};
