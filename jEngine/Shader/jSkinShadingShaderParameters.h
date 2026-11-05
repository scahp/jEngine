#pragma once

#include "Shader/jCommonShaderParameters.h"
BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSkinEnergyBuildData)
    // width, quadrature terms, minimum m, maximum m.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, DimensionsAndRoughness)
END_SHADER_UNIFORM_BUFFER_STRUCT()
static_assert(sizeof(jSkinEnergyBuildData)==16);
BEGIN_SHADER_PARAMETER_SET(jSkinEnergyBuildParameters)
    SHADER_UNIFORM_BUFFER(jSkinEnergyBuildData,SkinEnergyBuild)
    SHADER_RW_TEXTURE2D_R32F(SkinEnergyResult)
END_SHADER_PARAMETER_SET()
// Lighting set4 only when skin materials and a valid LUT are present. The pure
// helper manually interpolates four Load taps; nearest descriptors are valid
// even when R32F linear filtering is unavailable on the backend.
BEGIN_SHADER_PARAMETER_SET(jSkinEnergyShaderParameters)
    SHADER_TEXTURE2D(SkinEnergyLUT)
END_SHADER_PARAMETER_SET()
