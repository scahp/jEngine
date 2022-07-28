#pragma once

#include "glslang/SPIRV/GlslangToSpv.h"

// https://lxjk.github.io/2020/03/10/Translate-GLSL-to-SPIRV-for-Vulkan-at-Runtime.html
struct jSpirvHelper
{
	static bool IsInitialized;
	static TBuiltInResource Resources;

	static void Init(VkPhysicalDeviceProperties deviceProperties)
	{
		IsInitialized = glslang::InitializeProcess();
		InitResources(Resources, deviceProperties);
	}

	static void Finalize()
	{
		if (IsInitialized)
			glslang::FinalizeProcess();
	}

	static void InitResources(TBuiltInResource& resources, const VkPhysicalDeviceProperties& props);
	static bool GLSLtoSPV(std::vector<uint32>& OutSpirv, const EShLanguage stage, const char* pshader);

	[[deprecated("Recommanded to use InitResources function that is based on VkPhysicalDeviceProperties")]]
	static void InitResources(TBuiltInResource& Resources);
};