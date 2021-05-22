#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform vec2 PixelSize;

in vec2 TexCoord_;
out vec4 FragColor;

// Approximates luminance from an RGB value
float GetLuminance(vec3 color)
{
    return max(dot(color, vec3(0.299f, 0.587f, 0.114f)), 0.0001f);
}

void main()
{
	vec4 color = texture(tex_object, TexCoord_);
	float luminance = log(max(GetLuminance(color.xyz), 0.00001f));
	FragColor = vec4(luminance, 0.0, 0.0, 1.0);
}