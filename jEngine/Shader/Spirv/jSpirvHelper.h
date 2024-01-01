#pragma once

#include "glslang/SPIRV/GlslangToSpv.h"
#include <ShaderConductor/ShaderConductor.hpp>

// https://lxjk.github.io/2020/03/10/Translate-GLSL-to-SPIRV-for-Vulkan-at-Runtime.html
struct jSpirvHelper
{
	static bool IsInitialized;
	static TBuiltInResource Resources;

	static void Init(VkPhysicalDeviceProperties2 deviceProperties)
    {
        IsInitialized = glslang::InitializeProcess();
		InitResources(Resources, deviceProperties);
	}

	static void Finalize()
	{
		if (IsInitialized)
			glslang::FinalizeProcess();
	}

	//////////////////////////////////////////////////////////////////////////
	// GLSL to SPV
	static void InitResources(TBuiltInResource& resources, const VkPhysicalDeviceProperties2& props);
	static bool GLSLtoSpirv(std::vector<uint32>& OutSpirv, const EShLanguage stage, const char* pshader);

	[[deprecated("Recommanded to use InitResources function that is based on VkPhysicalDeviceProperties")]]
	static void InitResources(TBuiltInResource& Resources);
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	// HLSL to SPV
	static bool HLSLtoSpirv(std::vector<uint32>& OutSpirv, ShaderConductor::ShaderStage stage, const char* pshader);
	//////////////////////////////////////////////////////////////////////////
};
