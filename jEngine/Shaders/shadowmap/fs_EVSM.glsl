#version 330 core

#include "common.glsl"

precision mediump float;

varying vec3 Pos_;
uniform float ESM_C;

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
    float distFromLight = (sqrt(distSquared) - LightZNear) / (LightZFar - LightZNear);
    float ez = exp(distFromLight * ESM_C);
    
    gl_FragData[0].x = ez;
    gl_FragData[0].y = ez * ez;
    gl_FragData[0].w = 1.0;
}