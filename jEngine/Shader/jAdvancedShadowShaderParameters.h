#pragma once

#include "Shader/jCommonShaderParameters.h"

// A common projection/visibility contract consumed by all raster light types.
// No persistent scene data or graphics objects are owned by this value block.
BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jAdvancedShadowData)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Matrix, ShadowVP, 6)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeSplits)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, MapSizeAndInvSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, ModeParams)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, FilterParams)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, LightPositionAndNear)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, FarAndDeepParams)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CameraPositionAndType)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CameraForwardAndOpacity)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, AtlasParams)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SamplingParams)
END_SHADER_UNIFORM_BUFFER_STRUCT()

static_assert(sizeof(jAdvancedShadowData) == 544);

BEGIN_SHADER_PARAMETER_SET(jAdvancedShadowRasterParameters)
    SHADER_UNIFORM_BUFFER(jAdvancedShadowData, AdvancedShadow)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jAdvancedShadowComputeParameters)
    SHADER_UNIFORM_BUFFER(jAdvancedShadowData, AdvancedShadow)
    SHADER_TEXTURE2D_SRV(ShadowInput)
    SHADER_RW_TEXTURE2D(ShadowResult)
END_SHADER_PARAMETER_SET()
