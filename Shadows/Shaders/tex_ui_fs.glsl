#version 330 core

precision mediump float;

uniform sampler2D tex_object;

in vec2 TexCoord_;

out vec4 color;

void main()
{
    color = texture2D(tex_object, TexCoord_);
}