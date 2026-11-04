#pragma once

#include "Shader/jShaderParameterSet.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(RenderObjectUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, M)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvM)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Metallic)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Roughness)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, ObjectID)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, RenderObjectID)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, BaseColorOpacity)
    // xyz is linear emissive radiance; w selects authored material properties.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, EmissiveOverride)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, SurfaceFlags)
    SHADER_UNIFORM_BUFFER_MEMBER(float, SurfaceAlphaCutoff)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, IndexElementSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, SurfacePadding)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(ViewUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, V)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, P)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, VP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, PrevVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, EyeWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(float, padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, ScreenRect)
    // x = comparison filter mode, y = radius in shadow-map texels; zw reserved.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, ShadowFilterParams)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jViewShaderParameters)
    SHADER_UNIFORM_BUFFER(ViewUniformBuffer, ViewParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jRenderObjectShaderParameters)
    SHADER_UNIFORM_BUFFER(RenderObjectUniformBuffer, RenderObjectParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jMaterialShaderParameters)
    SHADER_TEXTURE2D(DiffuseTexture)
    SHADER_TEXTURE2D(NormalTexture)
    SHADER_TEXTURE2D(RMTexture)
    SHADER_TEXTURECUBE(EnvTexture)
END_SHADER_PARAMETER_SET()
