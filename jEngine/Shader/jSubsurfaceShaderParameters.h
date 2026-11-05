#pragma once

#include "Shader/jCommonShaderParameters.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSubsurfaceViewData)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SizeAndInvSize)
    // strength, plane-depth rejection in world units, minimum normal dot, max pixels.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, FilterParams)
    // radius scale, actual depth clear value, reserved, reserved.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, PassParams)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSubsurfaceProfileData)
    // Profile-major: 8 profiles * 17 samples; rgb normalized weights, w offset.
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, Kernel, 136)
    // xyz channel support distances, w maximum support distance, all world units.
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, RadiusWorld, 8)
END_SHADER_UNIFORM_BUFFER_STRUCT()

static_assert(sizeof(jSubsurfaceViewData) == 112);
static_assert(sizeof(jSubsurfaceProfileData) == 2304);

BEGIN_SHADER_PARAMETER_SET(jSubsurfaceComputeParameters)
    SHADER_UNIFORM_BUFFER(jSubsurfaceViewData, SSSView)
    SHADER_UNIFORM_BUFFER(jSubsurfaceProfileData, SSSProfiles)
    SHADER_TEXTURE2D_SRV(SSSSceneColor)
    SHADER_TEXTURE2D_SRV(SSSOriginalDiffuse)
    SHADER_TEXTURE2D_SRV(SSSInputDiffuse)
    SHADER_TEXTURE2D_SRV(SSSMask)
    SHADER_TEXTURE2D_SRV(SSSDepth)
    SHADER_TEXTURE2D_SRV(SSSNormal)
    SHADER_RW_TEXTURE2D(SSSResult)
END_SHADER_PARAMETER_SET()
