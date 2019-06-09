#version 330 core

#include "common.glsl"

precision mediump float;

in vec3 Pos_;
uniform vec3 LightPos;

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