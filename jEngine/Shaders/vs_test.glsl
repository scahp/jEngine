#version 330 core

precision mediump float;

layout(location = 0) in vec3 Pos;
layout(location = 1) in vec4 Color;

#define NUM_CASCADES 3

uniform mat4 MVP;
uniform mat4 M;
uniform mat4 VP;
uniform mat4 MV;
uniform mat4 CascadeLightVP[NUM_CASCADES];

out vec4 LightSpacePos[NUM_CASCADES];
out vec4 Pos_;
out vec4 Color_;

void main()
{
	for (int i = 0; i < NUM_CASCADES; i++) 
		LightSpacePos[i] = CascadeLightVP[i] * M * vec4(Pos, 1.0);

    gl_Position = MVP * vec4(Pos, 1.0);
	Pos_ = M * vec4(Pos, 1.0);
	Color_ = Color;
}