#version 330 core

precision mediump float;

uniform sampler2D tex_object2;
uniform int UseTexture;

in vec2 TexCoord_;
in vec4 Color_;

out vec4 color;

void main()
{
	if (UseTexture > 0)
		color = texture2D(tex_object2, TexCoord_);
	else
		color = Color_;
}