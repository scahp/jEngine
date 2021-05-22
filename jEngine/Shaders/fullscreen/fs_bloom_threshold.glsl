#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform sampler2D tex_object2;

uniform float BloomThreshold;
uniform float AutoExposureKeyValue;

in vec2 TexCoord_;
out vec4 FragColor;

// Retrieves the log-average luminance from the texture
float GetAvgLuminance(sampler2D lumTex)
{
	return texelFetch(lumTex, ivec2(0, 0), 0).x;
}

// Approximates luminance from an RGB value
float GetLuminance(vec3 color)
{
	return max(dot(color, vec3(0.299f, 0.587f, 0.114f)), 0.0001f);
}

// Determines the color based on exposure settings
vec3 CalcExposedColor(vec3 color, float avgLuminance, float threshold, out float exposure)
{
	// Use geometric mean
	avgLuminance = max(avgLuminance, 0.001);
	float keyValue = AutoExposureKeyValue;
	float linearExposure = (keyValue / avgLuminance);
	exposure = log2(max(linearExposure, 0.0001));
	exposure -= threshold;
	return exp2(exposure) * color;
}

void main()
{
	vec3 color = texture(tex_object, TexCoord_).xyz;

	float avgLuminance = GetAvgLuminance(tex_object2);
	float exposure = 0.0;
	float pixelLuminance = GetLuminance(color);
	color = CalcExposedColor(color, avgLuminance, BloomThreshold, exposure);

	if (dot(color, vec3(0.333)) <= 0.001)
		color = vec3(0.0);

	FragColor = vec4(color, 1.0);
}
