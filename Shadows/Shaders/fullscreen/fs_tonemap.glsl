#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform vec2 PixelSize;
uniform int UseTonemap;

in vec2 TexCoord_;
out vec4 FragColor;

// Applies the filmic curve from John Hable's presentation
float FilmicToneMapALU(float linearColor)
{
	float color = max(0.0, linearColor - 0.004);
	color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7f) + 0.06f);
	
	// result has 1/2.2 baked in
	return pow(color, 2.2);
}

vec3 FilmicToneMapALU(vec3 linearColor)
{
	linearColor.x = FilmicToneMapALU(linearColor.x);
	linearColor.y = FilmicToneMapALU(linearColor.y);
	linearColor.z = FilmicToneMapALU(linearColor.z);
	return linearColor;
}

void main()
{
	vec4 color = texture(tex_object, TexCoord_);
	if (UseTonemap > 0)
	{
		//float exposure = exp2(2.0);
		float exposure = 1.0;
		color.xyz = FilmicToneMapALU(exposure * color.xyz);
	}
	FragColor = color;
}