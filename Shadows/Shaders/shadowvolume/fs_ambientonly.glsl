#version 330 core

#preprocessor

#include "common.glsl"

precision mediump float;

uniform vec3 Eye;

uniform jAmbientLight AmbientLight;

uniform int Collided;

in vec3 Pos_;
in vec4 Color_;
in vec3 Normal_;

out vec4 color;

void main()
{
    vec3 diffuse = Color_.xyz;
    if (Collided != 0)
        diffuse = vec3(1.0, 1.0, 1.0);

    vec3 finalColor = vec3(0.0, 0.0, 0.0);
    finalColor += GetAmbientLight(AmbientLight);

    color = vec4(finalColor, Color_.w);
}