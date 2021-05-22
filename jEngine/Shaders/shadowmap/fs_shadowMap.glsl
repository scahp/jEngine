#version 330 core

#include "common.glsl"

precision mediump float;

void main()
{
    gl_FragData[0].x = gl_FragCoord.z;
    gl_FragData[0].w = 1.0;
}