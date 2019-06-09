#version 330 core

#preprocessor

#include "common.glsl"

precision mediump float;

layout(location = 0) in vec3 Pos;
layout(location = 1) in vec4 Color;
layout(location = 2) in vec3 Normal;

#if defined(USE_TEXTURE)
layout(location = 3) in vec2 TexCoord;
#endif // USE_TEXTURE

uniform mat4 MVP;
uniform mat4 MV;
uniform mat4 M;
uniform mat4 ShadowVP;
uniform mat4 ShadowV;

// out vec3 ShadowPos_;
// out vec3 ShadowCameraPos_;
out vec3 Pos_;
out vec4 Color_;
out vec3 Normal_;

#if defined(USE_TEXTURE)
out vec2 TexCoord_;
#endif // USE_TEXTURE

void main()
{
#if defined(USE_TEXTURE)
    TexCoord_ = TexCoord;
#endif // USE_TEXTURE

    Color_ = Color;
    Normal_ = TransformNormal(M, Normal);
    Pos_ = TransformPos(M, Pos);

    // vec4 shadowPos = (ShadowVP * vec4(Pos_, 1.0));
    // shadowPos /= shadowPos.w;
    // ShadowPos_.xyz = shadowPos.xyz * 0.5 + 0.5;        // Transform NDC space coordinate from [-1.0 ~ 1.0] into [0.0 ~ 1.0].

    // vec4 shadowCameraPos = (ShadowV * vec4(Pos_, 1.0));
    // shadowCameraPos /= shadowCameraPos.w;
    // ShadowCameraPos_ = shadowCameraPos.xyz;
    gl_Position = MVP * vec4(Pos, 1.0);
}