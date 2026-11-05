#pragma once
#include "Shader/jCommonShaderParameters.h"

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jTextureSubsurfaceData)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, LightVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, KeyPositionAndReference)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, KeyColorAndType)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, DirectionAndFar)
    // columns, rows, tile pixels, selected surface count.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Atlas)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CaptureSizeAndInvSize)
    // world units/mm, extinction/mm, thickness scale, capture bias world.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Physical)
    // transmission enabled, Gaussian step scale, global strength, max light distance.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Features)
    // current surface tile, exact ID low10, high10, selected capture surface.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Draw)
    // incremental sigma mm, axis (0 horizontal/1 vertical), level, reserved.
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Filter)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, EyeWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, OutputSizeAndInvSize)
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, SurfaceLookup, 4)
    // normalized RGB lobe weight, absolute variance mm^2.
    SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(Vector4, Lobe, 6)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvLightVP)
END_SHADER_UNIFORM_BUFFER_STRUCT()
static_assert(sizeof(jTextureSubsurfaceData)==464);

BEGIN_SHADER_PARAMETER_SET(jTextureSubsurfaceRasterParameters)
    SHADER_UNIFORM_BUFFER(jTextureSubsurfaceData, TextureSSS)
    SHADER_TEXTURE2D_SRV(TextureSSSCapture0)
    SHADER_TEXTURE2D_SRV(TextureSSSCapture1)
    SHADER_TEXTURE2D_SRV(TextureSSSEnergyLUT)
END_SHADER_PARAMETER_SET()

// The runtime's inline binding array holds15 entries. Keep only resources the
// phase can address; shader dead-code elimination does not shrink that array.
BEGIN_SHADER_PARAMETER_SET(jTextureSubsurfaceFilterParameters)
    SHADER_UNIFORM_BUFFER(jTextureSubsurfaceData, TextureSSS)
    SHADER_TEXTURE2D_SRV(TextureSSSInput)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel0)
    SHADER_TEXTURE2D_SRV(TextureSSSStretch)
    SHADER_TEXTURE2D_SRV(TextureSSSEntryUV)
    SHADER_TEXTURE2D_SRV(TextureSSSFinish)
    SHADER_RW_TEXTURE2D(TextureSSSResult)
    SHADER_RW_TEXTURE2D_UINT(TextureSSSSurfaceFlags)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jTextureSubsurfaceFinishParameters)
    SHADER_UNIFORM_BUFFER(jTextureSubsurfaceData, TextureSSS)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel0)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel1)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel2)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel3)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel4)
    SHADER_TEXTURE2D_SRV(TextureSSSLevel5)
    SHADER_TEXTURE2D_SRV(TextureSSSStretch)
    SHADER_TEXTURE2D_SRV(TextureSSSEntryUV)
    SHADER_TEXTURE2D_SRV(TextureSSSFinish)
    SHADER_TEXTURE2D_SRV(TextureSSSCoverage)
    SHADER_RW_TEXTURE2D(TextureSSSResult)
    SHADER_RW_TEXTURE2D_UINT(TextureSSSSurfaceFlags)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jTextureSubsurfaceComposeParameters)
    SHADER_UNIFORM_BUFFER(jTextureSubsurfaceData, TextureSSS)
    SHADER_TEXTURE2D_SRV(TextureSSSScene)
    SHADER_TEXTURE2D_SRV(TextureSSSScreenDiffuse)
    SHADER_TEXTURE2D_SRV(TextureSSSAlbedo)
    SHADER_TEXTURE2D_SRV(TextureSSSMask)
    SHADER_TEXTURE2D_SRV(TextureSSSSkinUV)
    SHADER_TEXTURE2D_SRV(TextureSSSStretch)
    SHADER_TEXTURE2D_SRV(TextureSSSEntryUV)
    SHADER_TEXTURE2D_SRV(TextureSSSInput)
    SHADER_RW_TEXTURE2D_UINT(TextureSSSSurfaceFlags)
    SHADER_RW_TEXTURE2D(TextureSSSResult)
    SHADER_RW_TEXTURE2D(TextureSSSCompletedDiffuse)
END_SHADER_PARAMETER_SET()
