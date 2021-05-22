#version 330 core

precision mediump float;

layout(location = 0) in vec2 Pos;
layout(location = 1) in mat4 Transform;

uniform vec2 InvViewportSize;

flat out int InstanceId;

void main()
{
	vec4 Pos_ = Transform * vec4(Pos, 0.0f, 1.0f);

	Pos_.xy *= InvViewportSize;
	Pos_ = Pos_ * 2.0f - 1.0f;
	Pos_.y = -Pos_.y;

	Pos_.z = 0.5f;
	Pos_.w = 1.0f;
	gl_Position = Pos_;
	InstanceId = gl_InstanceID;
}