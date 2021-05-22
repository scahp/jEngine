#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform sampler2D tex_object2;
uniform vec2 PixelSize;

uniform float TimeDeltaSecond;
uniform float AdaptationRate;

in vec2 TexCoord_;
out vec4 FragColor;

void main()
{
	float currentLuminance = exp(textureLod(tex_object, vec2(0.5, 0.5), 10.0).x);
	float lastLuminance = texelFetch(tex_object2, ivec2(0, 0), 0).x;

	// Adapt the luminance using Pattanaik's technique
	float adaptedLuminance = lastLuminance + (currentLuminance - lastLuminance) * (1.0 - exp(-TimeDeltaSecond * AdaptationRate));
	
	FragColor = vec4(adaptedLuminance, 0.0, 0.0, 1.0);
}