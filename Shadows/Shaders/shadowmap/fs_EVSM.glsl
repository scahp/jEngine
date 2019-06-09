#version 330 core

#include "common.glsl"

precision mediump float;

varying vec3 Pos_;
uniform vec3 LightPos;
uniform float LightZNear;
uniform float LightZFar;
uniform float ESM_C;

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