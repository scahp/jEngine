#version 330 core

precision mediump float;

layout(location = 0) in vec3 Pos;
layout(location = 1) in vec4 Color;

uniform mat4 MVP;

out vec2 TexCoord_;
out vec4 Color_;

void main()
{
    TexCoord_ = (Pos.xz + 0.5);
	Color_ = Color;
	gl_Position = MVP * vec4(Pos, 1.0);
}
