#version 330 core

precision mediump float;

layout(location = 0) in vec3 Pos;

#define NUM_CASCADES 3

uniform mat4 MVP;
uniform mat4 gLightWVP[NUM_CASCADES];

out vec4 LightSpacePos[NUM_CASCADES];
out vec4 Pos_;

void main()
{
	for (int i = 0; i < NUM_CASCADES; i++) {
		LightSpacePos[i] = gLightWVP[i] * vec4(Pos, 1.0);
	}

	Pos_ = gLightWVP[0] * vec4(Pos, 1.0);

    gl_Position = MVP * vec4(Pos, 1.0);
}