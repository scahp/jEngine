#version 330 core

#include "common.glsl"

precision mediump float;

in vec3 Pos_;

layout (std140) uniform DirectionalLightShadowMapBlock
{
	mat4 ShadowVP;
	mat4 ShadowV;
	vec3 LightPos;      // Directional Light Pos 임시
	float LightZNear;
	float LightZFar;
};

void main()
{
    vec3 lightDir = Pos_ - LightPos;
    float distSquared = dot(lightDir.xyz, lightDir.xyz);
    
    gl_FragData[0].x = sqrt(distSquared);
    gl_FragData[0].y = distSquared;

    // float z = gl_FragCoord.z;
    // gl_FragData[0].x = z;
    // gl_FragData[0].y = z;
    // gl_FragData[0].w = 1.0;
}