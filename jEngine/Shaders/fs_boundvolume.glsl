﻿#version 330 core

precision mediump float;

uniform vec4 Color;

out vec4 color;

void main()
{
    color = vec4(Color);
}