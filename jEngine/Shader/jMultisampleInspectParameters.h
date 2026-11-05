#pragma once
#include "jShaderParameterSet.h"
BEGIN_SHADER_PARAMETER_SET(jMultisampleInspectParameters)
    SHADER_TEXTURE2D_MS(ColorMS)
    SHADER_TEXTURE2D_MS(NormalMS)
    SHADER_DEPTH2D_MS(DepthMS)
    SHADER_TEXTURE2D_SRV(ResolvedColor)
    SHADER_TEXTURE2D_SRV(ResolvedNormal)
    SHADER_TEXTURE2D_SRV(ResolvedDepth)
    SHADER_RW_TEXTURE2D(SampleColors)
    SHADER_RW_TEXTURE2D(SampleSurfaces)
    SHADER_RW_TEXTURE2D(ResolvedColors)
    SHADER_RW_TEXTURE2D(ResolvedSurfaces)
END_SHADER_PARAMETER_SET()
