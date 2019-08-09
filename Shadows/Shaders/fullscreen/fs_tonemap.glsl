#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform sampler2D tex_object2;
uniform vec2 PixelSize;
uniform int UseTonemap;
uniform float AutoExposureKeyValue;

in vec2 TexCoord_;
out vec4 FragColor;

// Applies the filmic curve from John Hable's presentation
float FilmicToneMapALU(float linearColor)
{
	float color = max(0.0, linearColor - 0.004);
	color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7f) + 0.06f);
	
	// result has 1/2.2 baked in
	// return pow(color, 2.2);

	return color;
}

vec3 FilmicToneMapALU(vec3 linearColor)
{
	linearColor.x = FilmicToneMapALU(linearColor.x);
	linearColor.y = FilmicToneMapALU(linearColor.y);
	linearColor.z = FilmicToneMapALU(linearColor.z);
	return linearColor;
}

// Retrieves the log-average luminance from the texture
float GetAvgLuminance(sampler2D lumTex)
{
    return textureLod(lumTex, vec2(0.0, 0.0), 0.0).x;
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
		color.xyz = FilmicToneMapALU(color.xyz);
	}
	else
	{
		color.xyz = pow(color.xyz, vec3(1.0 / 2.2));		// from Linear color to sRGB
	}
	FragColor = color;
}