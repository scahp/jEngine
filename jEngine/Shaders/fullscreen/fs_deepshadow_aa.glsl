#version 430 core
precision mediump float;

uniform sampler2D tex_object;
uniform vec2 PixelSize;

in vec2 TexCoord_;
out vec4 FragColor;

#define AA_WEIGHT 0.5f

void main()
{
	const vec2 delta[8] =
	{
		vec2(-1, 1), vec2(1, -1), vec2(-1, 1), vec2(1, 1),
		vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1)
	};
	
	vec4 color = vec4(0.0,0.0,0.0,0.0);
	for(int i = 0 ; i < 8 ; i++)
		color += texture(tex_object, TexCoord_ + delta[i] * PixelSize * AA_WEIGHT);
	
	color += 2.0f * texture(tex_object, TexCoord_);
	FragColor = color * 1.0f / 10.0f;
}
