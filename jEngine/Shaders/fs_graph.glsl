#version 330 core

precision mediump float;

uniform vec4 LineColor;
uniform vec4 GuardLineColor;

flat in int InstanceId;
out vec4 color;

void main()
{
	if (InstanceId < 2)
		color = GuardLineColor;
	else
		color = LineColor;
}