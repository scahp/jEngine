#include "pch.h"
#include "jShader_Vulkan.h"
#include "../jRHI_Vulkan.h"

jShader_Vulkan::jShader_Vulkan(const jShaderInfo& shaderInfo)
{
    ShaderInfo = shaderInfo;
}

jShader_Vulkan::~jShader_Vulkan()
{
    Release();
}

void jShader_Vulkan::Initialize()
{
    verify(g_rhi_vk->CreateShaderInternal(this, ShaderInfo));
}

void jShader_Vulkan::Release()
{
    for (auto& Stage : ShaderStages)
    {
        vkDestroyShaderModule(g_rhi_vk->Device, Stage.module, nullptr);
    }
    ShaderStages.clear();
}
