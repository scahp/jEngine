#include "pch.h"
#include "jSpirvHelper.h"

#if _DEBUG
#pragma comment(lib, "glslangd.lib")
#pragma comment(lib, "MachineIndependentd.lib")
#pragma comment(lib, "OSDependentd.lib")
#pragma comment(lib, "OGLCompilerd.lib")
#pragma comment(lib, "GenericCodeGend.lib")
#pragma comment(lib, "SPIRVd.lib")
#else
#pragma comment(lib, "glslang.lib")
#pragma comment(lib, "MachineIndependent.lib")
#pragma comment(lib, "OSDependent.lib")
#pragma comment(lib, "OGLCompiler.lib")
#pragma comment(lib, "GenericCodeGen.lib")
#pragma comment(lib, "SPIRV.lib")
#endif

#pragma comment(lib, "ShaderConductor.lib")

bool jSpirvHelper::IsInitialized = false;
TBuiltInResource jSpirvHelper::Resources = {};

void jSpirvHelper::InitResources(TBuiltInResource& Resources)
{
    Resources.maxLights = 32;
    Resources.maxClipPlanes = 6;
    Resources.maxTextureUnits = 32;
    Resources.maxTextureCoords = 32;
    Resources.maxVertexAttribs = 64;
    Resources.maxVertexUniformComponents = 4096;
    Resources.maxVaryingFloats = 64;
    Resources.maxVertexTextureImageUnits = 32;
    Resources.maxCombinedTextureImageUnits = 80;
    Resources.maxTextureImageUnits = 32;
    Resources.maxFragmentUniformComponents = 4096;
    Resources.maxDrawBuffers = 32;
    Resources.maxVertexUniformVectors = 128;
    Resources.maxVaryingVectors = 8;
    Resources.maxFragmentUniformVectors = 16;
    Resources.maxVertexOutputVectors = 16;
    Resources.maxFragmentInputVectors = 15;
    Resources.minProgramTexelOffset = -8;
    Resources.maxProgramTexelOffset = 7;
    Resources.maxClipDistances = 8;
    Resources.maxComputeWorkGroupCountX = 65535;
    Resources.maxComputeWorkGroupCountY = 65535;
    Resources.maxComputeWorkGroupCountZ = 65535;
    Resources.maxComputeWorkGroupSizeX = 1024;
    Resources.maxComputeWorkGroupSizeY = 1024;
    Resources.maxComputeWorkGroupSizeZ = 64;
    Resources.maxComputeUniformComponents = 1024;
    Resources.maxComputeTextureImageUnits = 16;
    Resources.maxComputeImageUniforms = 8;
    Resources.maxComputeAtomicCounters = 8;
    Resources.maxComputeAtomicCounterBuffers = 1;
    Resources.maxVaryingComponents = 60;
    Resources.maxVertexOutputComponents = 64;
    Resources.maxGeometryInputComponents = 64;
    Resources.maxGeometryOutputComponents = 128;
    Resources.maxFragmentInputComponents = 128;
    Resources.maxImageUnits = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    Resources.maxCombinedShaderOutputResources = 8;
    Resources.maxImageSamples = 0;
    Resources.maxVertexImageUniforms = 0;
    Resources.maxTessControlImageUniforms = 0;
    Resources.maxTessEvaluationImageUniforms = 0;
    Resources.maxGeometryImageUniforms = 0;
    Resources.maxFragmentImageUniforms = 8;
    Resources.maxCombinedImageUniforms = 8;
    Resources.maxGeometryTextureImageUnits = 16;
    Resources.maxGeometryOutputVertices = 256;
    Resources.maxGeometryTotalOutputComponents = 1024;
    Resources.maxGeometryUniformComponents = 1024;
    Resources.maxGeometryVaryingComponents = 64;
    Resources.maxTessControlInputComponents = 128;
    Resources.maxTessControlOutputComponents = 128;
    Resources.maxTessControlTextureImageUnits = 16;
    Resources.maxTessControlUniformComponents = 1024;
    Resources.maxTessControlTotalOutputComponents = 4096;
    Resources.maxTessEvaluationInputComponents = 128;
    Resources.maxTessEvaluationOutputComponents = 128;
    Resources.maxTessEvaluationTextureImageUnits = 16;
    Resources.maxTessEvaluationUniformComponents = 1024;
    Resources.maxTessPatchComponents = 120;
    Resources.maxPatchVertices = 32;
    Resources.maxTessGenLevel = 64;
    Resources.maxViewports = 16;
    Resources.maxVertexAtomicCounters = 0;
    Resources.maxTessControlAtomicCounters = 0;
    Resources.maxTessEvaluationAtomicCounters = 0;
    Resources.maxGeometryAtomicCounters = 0;
    Resources.maxFragmentAtomicCounters = 8;
    Resources.maxCombinedAtomicCounters = 8;
    Resources.maxAtomicCounterBindings = 1;
    Resources.maxVertexAtomicCounterBuffers = 0;
    Resources.maxTessControlAtomicCounterBuffers = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers = 0;
    Resources.maxGeometryAtomicCounterBuffers = 0;
    Resources.maxFragmentAtomicCounterBuffers = 1;
    Resources.maxCombinedAtomicCounterBuffers = 1;
    Resources.maxAtomicCounterBufferSize = 16384;
    Resources.maxTransformFeedbackBuffers = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances = 8;
    Resources.maxCombinedClipAndCullDistances = 8;
    Resources.maxSamples = 4;
    Resources.maxMeshOutputVerticesNV = 256;
    Resources.maxMeshOutputPrimitivesNV = 512;
    Resources.maxMeshWorkGroupSizeX_NV = 32;
    Resources.maxMeshWorkGroupSizeY_NV = 1;
    Resources.maxMeshWorkGroupSizeZ_NV = 1;
    Resources.maxTaskWorkGroupSizeX_NV = 32;
    Resources.maxTaskWorkGroupSizeY_NV = 1;
    Resources.maxTaskWorkGroupSizeZ_NV = 1;
    Resources.maxMeshViewCountNV = 4;
    Resources.limits.nonInductiveForLoops = 1;
    Resources.limits.whileLoops = 1;
    Resources.limits.doWhileLoops = 1;
    Resources.limits.generalUniformIndexing = 1;
    Resources.limits.generalAttributeMatrixVectorIndexing = 1;
    Resources.limits.generalVaryingIndexing = 1;
    Resources.limits.generalSamplerIndexing = 1;
    Resources.limits.generalVariableIndexing = 1;
    Resources.limits.generalConstantMatrixVectorIndexing = 1;
}

// https://github.com/DragonJoker/Ashes/blob/master/source/util/GlslToSpv.cpp
void jSpirvHelper::InitResources(TBuiltInResource& resources, const VkPhysicalDeviceProperties& props)
{
	auto& limits = props.limits;

	resources.limits.doWhileLoops = true;
	resources.limits.generalAttributeMatrixVectorIndexing = true;
	resources.limits.generalConstantMatrixVectorIndexing = true;
	resources.limits.generalSamplerIndexing = true;
	resources.limits.generalUniformIndexing = true;
	resources.limits.generalVariableIndexing = true;
	resources.limits.generalVaryingIndexing = true;
	resources.limits.nonInductiveForLoops = true;
	resources.limits.whileLoops = true;
	resources.maxAtomicCounterBindings = 1;
	resources.maxAtomicCounterBufferSize = 16384;
	resources.maxClipDistances = int(limits.maxClipDistances);
	resources.maxClipPlanes = 6;
	resources.maxCombinedAtomicCounterBuffers = 1;
	resources.maxCombinedAtomicCounters = 8;
	resources.maxCombinedClipAndCullDistances = int(limits.maxCombinedClipAndCullDistances);
	resources.maxCombinedImageUniforms = 8;
	resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
	resources.maxCombinedShaderOutputResources = 8;
	resources.maxCombinedTextureImageUnits = 80;
	resources.maxComputeAtomicCounterBuffers = 1;
	resources.maxComputeAtomicCounters = 8;
	resources.maxComputeImageUniforms = 8;
	resources.maxComputeTextureImageUnits = 16;
	resources.maxComputeUniformComponents = 1024;
	resources.maxComputeWorkGroupCountX = int(limits.maxComputeWorkGroupCount[0]);
	resources.maxComputeWorkGroupCountY = int(limits.maxComputeWorkGroupCount[1]);
	resources.maxComputeWorkGroupCountZ = int(limits.maxComputeWorkGroupCount[2]);
	resources.maxComputeWorkGroupSizeX = int(limits.maxComputeWorkGroupSize[0]);
	resources.maxComputeWorkGroupSizeY = int(limits.maxComputeWorkGroupSize[1]);
	resources.maxComputeWorkGroupSizeZ = int(limits.maxComputeWorkGroupSize[2]);
	resources.maxCullDistances = int(limits.maxCullDistances);
	resources.maxDrawBuffers = int(limits.maxColorAttachments);
	resources.maxFragmentAtomicCounterBuffers = 1;
	resources.maxFragmentAtomicCounters = 8;
	resources.maxFragmentImageUniforms = 8;
	resources.maxFragmentInputComponents = int(limits.maxFragmentInputComponents);
	resources.maxFragmentInputVectors = 15;
	resources.maxFragmentUniformComponents = 4096;
	resources.maxFragmentUniformVectors = 16;
	resources.maxGeometryAtomicCounterBuffers = 0;
	resources.maxGeometryAtomicCounters = 0;
	resources.maxGeometryImageUniforms = 0;
	resources.maxGeometryInputComponents = int(limits.maxGeometryInputComponents);
	resources.maxGeometryOutputComponents = int(limits.maxGeometryOutputComponents);
	resources.maxGeometryOutputVertices = 256;
	resources.maxGeometryTextureImageUnits = 16;
	resources.maxGeometryTotalOutputComponents = int(limits.maxGeometryTotalOutputComponents);
	resources.maxGeometryUniformComponents = 1024;
	resources.maxGeometryVaryingComponents = int(limits.maxGeometryOutputVertices);
	resources.maxImageSamples = 0;
	resources.maxImageUnits = 8;
	resources.maxLights = 32;
	resources.maxPatchVertices = int(limits.maxTessellationPatchSize);
	resources.maxProgramTexelOffset = int(limits.maxTexelOffset);
	resources.maxSamples = 4;
	resources.maxTessControlAtomicCounterBuffers = 0;
	resources.maxTessControlAtomicCounters = 0;
	resources.maxTessControlImageUniforms = 0;
	resources.maxTessControlInputComponents = int(limits.maxTessellationControlPerVertexInputComponents);
	resources.maxTessControlOutputComponents = 128;
	resources.maxTessControlTextureImageUnits = 16;
	resources.maxTessControlTotalOutputComponents = int(limits.maxTessellationControlTotalOutputComponents);
	resources.maxTessControlUniformComponents = 1024;
	resources.maxTessEvaluationAtomicCounterBuffers = 0;
	resources.maxTessEvaluationAtomicCounters = 0;
	resources.maxTessEvaluationImageUniforms = 0;
	resources.maxTessEvaluationInputComponents = int(limits.maxTessellationEvaluationInputComponents);
	resources.maxTessEvaluationOutputComponents = int(limits.maxTessellationEvaluationOutputComponents);
	resources.maxTessEvaluationTextureImageUnits = 16;
	resources.maxTessEvaluationUniformComponents = 1024;
	resources.maxTessGenLevel = int(limits.maxTessellationGenerationLevel);
	resources.maxTessPatchComponents = int(limits.maxTessellationControlPerPatchOutputComponents);
	resources.maxTextureCoords = 32;
	resources.maxTextureImageUnits = 32;
	resources.maxTextureUnits = 32;
	resources.maxTransformFeedbackBuffers = 4;
	resources.maxTransformFeedbackInterleavedComponents = 64;
	resources.maxVaryingComponents = 60;
	resources.maxVaryingFloats = 64;
	resources.maxVaryingVectors = 8;
	resources.maxVertexAtomicCounterBuffers = 0;
	resources.maxVertexAtomicCounters = 0;
	resources.maxVertexAttribs = 64;
	resources.maxVertexImageUniforms = 0;
	resources.maxVertexOutputComponents = 64;
	resources.maxVertexOutputVectors = 16;
	resources.maxVertexTextureImageUnits = 32;
	resources.maxVertexUniformComponents = 4096;
	resources.maxVertexUniformVectors = 128;
	resources.maxViewports = int(limits.maxViewports);
	resources.minProgramTexelOffset = -8;
}

bool jSpirvHelper::GLSLtoSpirv(std::vector<uint32>& OutSpirv, const EShLanguage stage, const char* pshader)
{
	check(IsInitialized);

	glslang::TShader shader(stage);
	glslang::TProgram program;
	const char* shaderStrings[1];	

	// Enable SPIR-V and Vulkan rules when parsing GLSL
	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

	shaderStrings[0] = pshader;
	shader.setStrings(shaderStrings, 1);

	if (!shader.parse(&Resources, 100, false, messages)) {
		puts(shader.getInfoLog());
		puts(shader.getInfoDebugLog());
		return false;  // something didn't work
	}

	program.addShader(&shader);

	//
	// Program-level processing...
	//

	if (!program.link(messages)) {
		puts(shader.getInfoLog());
		puts(shader.getInfoDebugLog());
		fflush(stdout);
		return false;
	}

	glslang::GlslangToSpv(*program.getIntermediate(stage), OutSpirv);
	return true;
}

bool jSpirvHelper::HLSLtoSpirv(std::vector<uint32>& OutSpirv, ShaderConductor::ShaderStage stage, const char* pshader)
{
	ShaderConductor::Compiler::SourceDesc sourceDesc = {};
	sourceDesc.entryPoint = "main";
	sourceDesc.stage = stage;
	sourceDesc.source = pshader;

	ShaderConductor::Compiler::Options options;
	options.packMatricesInRowMajor = false;
	ShaderConductor::Compiler::TargetDesc targetDesc = { ShaderConductor::ShadingLanguage::SpirV, "", true };
	ShaderConductor::Compiler::ResultDesc resultDesc;
	ShaderConductor::Compiler::Compile(sourceDesc, options, &targetDesc, 1, &resultDesc);
	if (resultDesc.hasError)
	{
		const char* errorStr = reinterpret_cast<const char*>(resultDesc.errorWarningMsg.Data());
		OutputDebugStringA(errorStr);
		check(0);
		return false;
	}
	
	const uint8* target_ptr = reinterpret_cast<const uint8*>(resultDesc.target.Data());
	const int32 totalElement = (resultDesc.target.Size() / 4) + (int32)((resultDesc.target.Size() % 4) > 0);
	if (!totalElement)
		return false;

	OutSpirv.resize(totalElement);
	memcpy(OutSpirv.data(), target_ptr, resultDesc.target.Size());
	return true;
}