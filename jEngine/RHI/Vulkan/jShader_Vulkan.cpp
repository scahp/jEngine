#include "pch.h"
#include "jShader_Vulkan.h"
#include "jRHI_Vulkan.h"

jCompiledShader_Vulkan::~jCompiledShader_Vulkan()
{
    if (ShaderStage.module)
        vkDestroyShaderModule(g_rhi_vk->Device, ShaderStage.module, nullptr);
    ShaderStage = {};
}
