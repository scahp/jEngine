#version 330 core

precision mediump float;

void main()
{
    gl_FragData[0].x = gl_FragCoord.z;
    gl_FragData[0].w = 1.0;
}