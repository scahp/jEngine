#pragma once

#include "Shader/jCommonShaderParameters.h"
#include "Renderer/Features/jHiZOcclusionMath.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jHiZDepthData)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, ViewProjection)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, World)
END_SHADER_UNIFORM_BUFFER_STRUCT()
static_assert(sizeof(jHiZDepthData) == 128);

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jHiZDispatchData)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Source)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Destination)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Viewport)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Control)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, Levels, 14)
END_SHADER_UNIFORM_BUFFER_STRUCT()
static_assert(sizeof(jHiZDispatchData) == 288);

// Structured indirect records have a strict 20-byte stride. The generic shader
// struct macro aligns to 16 bytes, so this plain standard-layout type uses an
// explicit declaration instead of silently producing 32-byte records.
using jHiZIndexedArguments = jHiZOcclusion::IndexedArguments;
static_assert(sizeof(jHiZIndexedArguments) == 20);
BEGIN_SHADER_PARAMETER_HLSL_TYPE(jHiZIndexedArguments)
    HLSL_LINE("struct jHiZIndexedArguments { uint IndexCount; uint InstanceCount; uint FirstIndex; int BaseVertex; uint FirstInstance; };");
END_SHADER_PARAMETER_HLSL_TYPE()

BEGIN_SHADER_STRUCT(jHiZCandidate)
    SHADER_STRUCT_MEMBER(uint32, MinimumX)
    SHADER_STRUCT_MEMBER(uint32, MinimumY)
    SHADER_STRUCT_MEMBER(uint32, MaximumX)
    SHADER_STRUCT_MEMBER(uint32, MaximumY)
    SHADER_STRUCT_MEMBER(float, NearestDepth)
    SHADER_STRUCT_MEMBER(uint32, Testable)
    SHADER_STRUCT_MEMBER(uint32, IndexCount)
    SHADER_STRUCT_MEMBER(uint32, InstanceCount)
    SHADER_STRUCT_MEMBER(uint32, FirstIndex)
    SHADER_STRUCT_MEMBER(int32, BaseVertex)
    SHADER_STRUCT_MEMBER(uint32, FirstInstance)
    SHADER_STRUCT_MEMBER(uint32, Padding)
END_SHADER_STRUCT()
static_assert(sizeof(jHiZCandidate) == 48);

BEGIN_SHADER_PARAMETER_SET(jHiZDepthParameters)
    SHADER_UNIFORM_BUFFER(jHiZDepthData, HiZDepth)
END_SHADER_PARAMETER_SET()
BEGIN_SHADER_PARAMETER_SET(jHiZSeedParameters)
    SHADER_UNIFORM_BUFFER(jHiZDispatchData, HiZDispatch)
    SHADER_TEXTURE2D_SRV(HiZDepthInput)
    SHADER_RW_STRUCTURED_BUFFER(float, HiZPyramidOutput)
END_SHADER_PARAMETER_SET()
BEGIN_SHADER_PARAMETER_SET(jHiZReduceParameters)
    SHADER_UNIFORM_BUFFER(jHiZDispatchData, HiZDispatch)
    SHADER_RW_STRUCTURED_BUFFER(float, HiZPyramidOutput)
END_SHADER_PARAMETER_SET()
BEGIN_SHADER_PARAMETER_SET(jHiZCullParameters)
    SHADER_UNIFORM_BUFFER(jHiZDispatchData, HiZDispatch)
    SHADER_STRUCTURED_BUFFER(float, HiZPyramidInput)
    SHADER_STRUCTURED_BUFFER(jHiZCandidate, HiZCandidates)
    SHADER_RW_STRUCTURED_BUFFER(jHiZIndexedArguments, HiZArguments)
END_SHADER_PARAMETER_SET()
