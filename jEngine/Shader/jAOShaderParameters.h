#pragma once

#include "Shader/jShaderParameterSet.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jBilateralCommonComputeUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Width)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Height)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Sigma)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, KernelSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, SigmaForBilateral)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jBilateralKernelUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, Data, 150)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jBilateralFilteringCSParameters)
    SHADER_RW_TEXTURE2D(resultImage)
    SHADER_TEXTURE2D(inputImage)
    SHADER_TEXTURE2D(DepthBuffer)
    SHADER_UNIFORM_BUFFER(jBilateralCommonComputeUniformBuffer, ComputeCommon)
    SHADER_UNIFORM_BUFFER(jBilateralKernelUniformBuffer, Kernal)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jBilateralPSParameters)
    SHADER_UNIFORM_BUFFER(jBilateralCommonComputeUniformBuffer, Param)
    SHADER_TEXTURE2D(InTexture)
    SHADER_TEXTURE2D(DepthTexture)
END_SHADER_PARAMETER_SET()
