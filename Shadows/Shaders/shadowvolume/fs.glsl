#version 330 core

#preprocessor

#include "common.glsl"

precision mediump float;

#define MAX_NUM_OF_DIRECTIONAL_LIGHT 1
#define MAX_NUM_OF_POINT_LIGHT 3
#define MAX_NUM_OF_SPOT_LIGHT 3

uniform int NumOfDirectionalLight;
uniform int NumOfPointLight;
uniform int NumOfSpotLight;

uniform jDirectionalLight DirectionalLight[MAX_NUM_OF_DIRECTIONAL_LIGHT];
uniform jPointLight PointLight[MAX_NUM_OF_POINT_LIGHT];
uniform jSpotLight SpotLight[MAX_NUM_OF_SPOT_LIGHT];

uniform vec3 Eye;
uniform int Collided;

#if defined(USE_MATERIAL)
uniform jMaterial Material;
#endif // USE_MATERIAL

#if defined(USE_TEXTURE)
uniform sampler2D tex_object2;
#endif // USE_TEXTURE

in vec3 Pos_;
in vec4 Color_;
in vec3 Normal_;

#if defined(USE_TEXTURE)
in vec2 TexCoord_;
#endif // USE_TEXTURE

out vec4 color;

void main()
{
    vec3 normal = normalize(Normal_);
    vec3 viewDir = normalize(Eye - Pos_);

    vec4 diffuse = Color_;
    if (Collided != 0)
        diffuse = vec4(1.0, 1.0, 1.0, 1.0);

#if defined(USE_TEXTURE)
    diffuse *= texture(tex_object2, TexCoord_);
#endif // USE_TEXTURE

#if defined(USE_MATERIAL)
    diffuse.xyz *= Material.Diffuse;
    diffuse.xyz += Material.Emissive;
#endif // USE_MATERIAL

    vec3 finalColor = vec3(0.0, 0.0, 0.0);

    for(int i=0;i<MAX_NUM_OF_DIRECTIONAL_LIGHT;++i)
    {
        if (i >= NumOfDirectionalLight)
            break;
        
        jDirectionalLight light = DirectionalLight[i];
#if defined(USE_MATERIAL)
        light.SpecularLightIntensity = Material.Specular;
        light.SpecularPow = Material.Shininess;
#endif // USE_MATERIAL

        finalColor += GetDirectionalLight(light, normal, viewDir);
    }

     for(int i=0;i<MAX_NUM_OF_POINT_LIGHT;++i)
     {
         if (i >= NumOfPointLight)
             break;
            
         jPointLight light = PointLight[i];
 #if defined(USE_MATERIAL)
         light.SpecularLightIntensity = Material.Specular;
         light.SpecularPow = Material.Shininess;
 #endif // USE_MATERIAL
        
         finalColor += GetPointLight(light, normal, Pos_, viewDir);
     }
    
     for(int i=0;i<MAX_NUM_OF_SPOT_LIGHT;++i)
     {
         if (i >= NumOfSpotLight)
             break;

         jSpotLight light = SpotLight[i];
 #if defined(USE_MATERIAL)
         light.SpecularLightIntensity = Material.Specular;
         light.SpecularPow = Material.Shininess;
 #endif // USE_MATERIAL

         finalColor += GetSpotLight(light, normal, Pos_, viewDir);
     }

    color = vec4(finalColor * diffuse.xyz, diffuse.w);
}