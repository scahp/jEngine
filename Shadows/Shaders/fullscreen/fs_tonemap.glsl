#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform sampler2D tex_object2;
uniform vec2 PixelSize;
uniform int UseTonemap;
uniform float AutoExposureKeyValue;

in vec2 TexCoord_;
out vec4 FragColor;

// http://filmicworlds.com/blog/filmic-tonemapping-operators/
vec3 Uncharted2Tonemap(vec3 x)
{
	float A = 0.15;		// Shoulder Strength
	float B = 0.50;		// Linear Strength
	float C = 0.10;		// Linear Angle
	float D = 0.20;		// Toe Strength
	float E = 0.02;		// Toe Numerator
	float F = 0.30;		// Toe Denominator
	float W = 11.2;		// Linear White Point Value

	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilmTonemap(vec3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3(0.0), vec3(1.0));
}

// http://filmicworlds.com/blog/filmic-tonemapping-with-piecewise-power-curves/		// wait for implementation

// Applies the filmic curve from John Hable's presentation
vec3 FilmicToneMapALU(vec3 linearColor)
{
	vec3 color = max(vec3(0.0), linearColor - vec3(0.004));
	color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);
	
	// result has 1/2.2 baked in
	// return pow(color, 2.2);

	return color;
}

// Retrieves the log-average luminance from the texture
float GetAvgLuminance(sampler2D lumTex)
{
    return texelFetch(lumTex, ivec2(0, 0), 0).x;
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
	vec4 color = texture(tex_object, TexCoord_);
	if (UseTonemap > 0)
	{
		float avgLuminance = GetAvgLuminance(tex_object2);
		float exposure = 0.0;
		color.xyz = CalcExposedColor(color.xyz, avgLuminance, 0.0, exposure);

		// Filmic Tonemap
		//color.xyz = FilmicToneMapALU(color.xyz);

		vec3 cur = Uncharted2Tonemap(color.xyz);
		float W = 11.2;		// Linear White Point Value
		vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
		color.xyz = cur * whiteScale;
		color.xyz = pow(color.xyz, vec3(1.0 / 2.2));		// from Linear color to sRGB
		
		// ACES Filmic Tonemap
		//color.xyz = ACESFilmTonemap(color.xyz);
		//color.xyz = pow(color.xyz, vec3(1.0 / 2.2));		// from Linear color to sRGB
	}
	else
	{
		color.xyz = pow(color.xyz, vec3(1.0 / 2.2));		// from Linear color to sRGB
	}
	FragColor = vec4(color.xyz, 1.0);
}