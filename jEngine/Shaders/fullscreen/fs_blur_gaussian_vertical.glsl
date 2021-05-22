#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform vec2 RenderTargetSize;
uniform float BloomBlurSigma;

in vec2 TexCoord_;
out vec4 FragColor;

// Calculates the gaussian blur weight for a given distance and sigmas
float CalcGaussianWeight(int sampleDist, float sigma)
{
	float g = 1.0f / sqrt(2.0 * 3.14159 * sigma * sigma);
	return (g * exp(-(sampleDist * sampleDist) / (2 * sigma * sigma)));
}

// Performs a gaussian blur in one direction
vec4 Blur(sampler2D texObject, vec2 texCoord, vec2 texScale, float sigma)
{
	vec4 color = vec4(0.0);
	for (int i = -6; i < 6; i++)
	{
		float weight = CalcGaussianWeight(i, sigma);
		vec2 newTexCoord = texCoord;
		newTexCoord += (i / RenderTargetSize) * texScale;
		color += texture(texObject, newTexCoord) * weight;
	}

	return color;
}

void main()
{
	FragColor = Blur(tex_object, TexCoord_, vec2(0.0, 1.0), BloomBlurSigma);
}
