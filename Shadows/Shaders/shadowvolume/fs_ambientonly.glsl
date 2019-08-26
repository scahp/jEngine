#version 330 core

#preprocessor

#include "common.glsl"

precision mediump float;

uniform vec3 Eye;

uniform jAmbientLight AmbientLight;

uniform int Collided;

#if defined(USE_TEXTURE)
uniform sampler2D tex_object2;
uniform int TextureSRGB[1];
#endif // USE_TEXTURE

#if defined(USE_MATERIAL)
uniform jMaterial Material;
#endif // USE_MATERIAL


in vec3 Pos_;
in vec4 Color_;
in vec3 Normal_;

out vec4 color;

void main()
{
	vec4 diffuse = Color_;
	if (Collided != 0)
		diffuse = vec4(1.0, 1.0, 1.0, 1.0);

#if defined(USE_TEXTURE)
	if (TextureSRGB[0] > 0)
	{
		// from sRGB to Linear color
		vec4 tempColor = texture(tex_object2, TexCoord_);
		diffuse.xyz *= pow(tempColor.xyz, vec3(2.2));
		diffuse.w *= tempColor.w;
	}
	else
	{
		diffuse *= texture(tex_object2, TexCoord_);
	}
#endif // USE_TEXTURE

#if defined(USE_MATERIAL)
	diffuse.xyz *= Material.Diffuse;
	diffuse.xyz += Material.Emissive;
#endif // USE_MATERIAL

    color = vec4(GetAmbientLight(AmbientLight) * diffuse.xyz, diffuse.w);
}