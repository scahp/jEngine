#version 330 core

#preprocessor

#include "shadow.glsl"

precision mediump float;
precision mediump sampler2DArray;

#define MAX_NUM_OF_DIRECTIONAL_LIGHT 1
#define MAX_NUM_OF_POINT_LIGHT 1
#define MAX_NUM_OF_SPOT_LIGHT 1

uniform int UseAmbientLight;
uniform int NumOfDirectionalLight;
uniform int NumOfPointLight;
uniform int NumOfSpotLight;

uniform jAmbientLight AmbientLight;
layout (std140) uniform DirectionalLightBlock
{
	jDirectionalLight DirectionalLight[MAX_NUM_OF_DIRECTIONAL_LIGHT];
};

layout (std140) uniform PointLightBlock
{
	jPointLight PointLight[MAX_NUM_OF_POINT_LIGHT];
};

layout (std140) uniform SpotLightBlock
{
	jSpotLight SpotLight[MAX_NUM_OF_SPOT_LIGHT];
};

layout (std140) uniform DirectionalLightShadowMapBlock
{
	mat4 ShadowVP;
	mat4 ShadowV;
	vec3 LightPos;      // Directional Light Pos юс╫ц
	float LightZNear;
	float LightZFar;
};

layout (std140) uniform PointLightShadowMapBlock
{
	float PointLightZNear;
	float PointLightZFar;
};

layout (std140) uniform SpotLightShadowMapBlock
{
	float SpotLightZNear;
	float SpotLightZFar;
};

#if defined(USE_MATERIAL)
uniform jMaterial Material;
#endif // USE_MATERIAL

uniform vec3 Eye;
uniform int Collided;

#if defined(USE_TEXTURE)
uniform sampler2D tex_object2;
#endif // USE_TEXTURE

uniform sampler2D shadow_object_point;
uniform sampler2D shadow_object_spot;
uniform sampler2D shadow_object;
uniform vec2 ShadowMapSize;
uniform float PCF_Size_Directional;
uniform float PCF_Size_OmniDirectional;
uniform float ESM_C;
uniform float PointLightESM_C;
uniform float SpotLightESM_C;
uniform int UseTexture;
uniform int UseUniformColor;
uniform vec4 Color;
uniform int UseMaterial;
uniform int ShadowOn;


// in vec3 ShadowPos_;
// in vec3 ShadowCameraPos_;
in vec3 Pos_;
in vec4 Color_;
in vec3 Normal_;

#if defined(USE_TEXTURE)
in vec2 TexCoord_;
#endif // USE_TEXTURE

out vec4 color;

void main()
{
#if defined(USE_VSM)
    float vsmBiasForOmniDirectional = 0.1;
#endif // defined(USE_VSM) || defined(USE_EVSM)

#if defined(USE_EVSM)
    float evsmBiasForOmniDirectional = 0.1;
#endif // USE_EVSM

    vec3 normal = normalize(Normal_);
    vec3 viewDir = normalize(Eye - Pos_);

    vec4 tempShadowPos = (ShadowVP * vec4(Pos_, 1.0));
    tempShadowPos /= tempShadowPos.w;
    vec3 ShadowPos = tempShadowPos.xyz * 0.5 + 0.5;        // Transform NDC space coordinate from [-1.0 ~ 1.0] into [0.0 ~ 1.0].

    vec4 shadowCameraPos = (ShadowV * vec4(Pos_, 1.0));    

	vec4 diffuse = vec4(1.0);

#if defined(USE_TEXTURE)
	if (UseTexture > 0)
		diffuse *= texture(tex_object2, TexCoord_);
#else
	diffuse = Color_;
#endif // USE_TEXTURE

	if (UseUniformColor > 0)
		diffuse = Color;

    if (Collided != 0)
        diffuse = vec4(1.0, 1.0, 1.0, 1.0);

#if defined(USE_MATERIAL)
	if (UseMaterial > 0)
	{
		diffuse *= Material.Diffuse;
		diffuse.xyz += Material.Emissive;
	}
#endif // USE_MATERIAL

    bool shadow = false;
    vec3 finalColor = vec3(0.0, 0.0, 0.0);

    if (UseAmbientLight != 0)
        finalColor += GetAmbientLight(AmbientLight);

#if defined(USE_POISSON_SAMPLE)
        InitPoissonSamples(gl_FragCoord.xy);
#endif

    for(int i=0;i<MAX_NUM_OF_DIRECTIONAL_LIGHT;++i)
    {
        if (i >= NumOfDirectionalLight)
            break;

        float lit = 1.0;

		if (ShadowOn > 0)
		{
	#if defined(USE_PCSS)
			{
	#if defined(USE_POISSON_SAMPLE)
				lit = PCSS_PoissonSample(ShadowPos, -shadowCameraPos.z, shadow_object, LightZNear, ShadowMapSize);
	#else // USE_POISSON_SAMPLE
				lit = PCSS(ShadowPos, -shadowCameraPos.z, shadow_object, LightZNear, ShadowMapSize);
	#endif // USE_POISSON_SAMPLE
			}
	#elif defined(USE_PCF)
			{
	#if defined(USE_POISSON_SAMPLE)
				lit = PCF_PoissonSample(ShadowPos, vec2(PCF_Size_Directional, PCF_Size_Directional) * ShadowMapSize, shadow_object);
	#else // USE_POISSON_SAMPLE
				lit = PCF(ShadowPos, vec2(PCF_Size_Directional, PCF_Size_Directional) * ShadowMapSize, shadow_object);
	#endif // USE_POISSON_SAMPLE
			}
	#elif defined(USE_VSM)
			{
				lit = VSM(ShadowPos, LightPos, Pos_, shadow_object);
			}
	#elif defined(USE_ESM)
			{
				lit = ESM(ShadowPos, LightPos, Pos_, LightZNear, LightZFar, ESM_C, shadow_object);
			}
	#elif defined(USE_EVSM)
			{
				lit = EVSM(ShadowPos, LightPos, Pos_, LightZNear, LightZFar, ESM_C, shadow_object);
			}
	#else
			{
				lit = float(!IsShadowing(ShadowPos, shadow_object));
			}
	#endif
		}

        if (lit > 0.0)
        {
            jDirectionalLight light = DirectionalLight[i];
#if defined(USE_MATERIAL)
			if (UseMaterial > 0)
			{
				light.SpecularLightIntensity = Material.Specular;
				light.SpecularPow = Material.Shininess;
			}
#endif // USE_MATERIAL

            finalColor += GetDirectionalLight(light, normal, viewDir) * lit;
        }
    }

    for(int i=0;i<MAX_NUM_OF_POINT_LIGHT;++i)
    {
        if (i >= NumOfPointLight)
            break;

        vec3 toLight = PointLight[i].LightPos - Pos_;
        float distFromLightSqrt = dot(toLight, toLight);

        if (distFromLightSqrt > (PointLight[i].MaxDistance * PointLight[i].MaxDistance))
            continue;

        float lit = 1.0;

		if (ShadowOn > 0)
		{
	#if defined(USE_PCSS)
			{

	#if defined(USE_POISSON_SAMPLE)
				lit = PCSS_OmniDirectional_PoissonSample(Pos_, PointLight[i].LightPos, PointLightZNear, ShadowMapSize, shadow_object_point);
	#else // USE_POISSON_SAMPLE
				lit = PCSS_OmniDirectional(Pos_, PointLight[i].LightPos, PointLightZNear, ShadowMapSize, shadow_object_point);
	#endif // USE_POISSON_SAMPLE
			}
	#elif defined(USE_PCF)
			{
				vec2 radiusSquredUV = vec2(PCF_Size_OmniDirectional, PCF_Size_OmniDirectional) * ShadowMapSize;
	#if defined(USE_POISSON_SAMPLE)
				lit = PCF_OmniDirectional_PoissonSample(Pos_, PointLight[i].LightPos, radiusSquredUV, shadow_object_point);
	#else
				lit = PCF_OmniDirectional(Pos_, PointLight[i].LightPos, radiusSquredUV, shadow_object_point);
	#endif
			}
	#elif defined(USE_VSM)
			{
				lit = VSM_OmniDirectional(PointLight[i].LightPos, Pos_, shadow_object_point, vsmBiasForOmniDirectional);
			}
	#elif defined(USE_ESM)
			{
				lit = ESM_OmniDirectional(PointLight[i].LightPos, Pos_, PointLightZNear, PointLightZFar, PointLightESM_C, shadow_object_point);
			}
	#elif defined(USE_EVSM)
			{
				lit = EVSM_OmniDirectional(PointLight[i].LightPos, Pos_, PointLightZNear, PointLightZFar, PointLightESM_C, shadow_object_point, evsmBiasForOmniDirectional);
			}
	#else
			{
				lit = float(!IsShadowing(Pos_, PointLight[i].LightPos, shadow_object_point));
			}
	#endif
		}

        if (lit > 0.0)
        {
            jPointLight light = PointLight[i];
#if defined(USE_MATERIAL)
			if (UseMaterial > 0)
			{
				light.SpecularLightIntensity = Material.Specular;
				light.SpecularPow = Material.Shininess;
			}
#endif // USE_MATERIAL

			finalColor += GetPointLight(light, normal, Pos_, viewDir) * lit;
        }
    }
    
    for(int i=0;i<MAX_NUM_OF_SPOT_LIGHT;++i)
    {
        if (i >= NumOfSpotLight)
            break;

        vec3 toLight = SpotLight[i].LightPos - Pos_;
        float distFromLightSqrt = dot(toLight, toLight);

        if (distFromLightSqrt > (SpotLight[i].MaxDistance * SpotLight[i].MaxDistance))
            continue;

        float lit = 1.0;

		if (ShadowOn > 0)
		{
	#if defined(USE_PCSS)
			{
	#if defined(USE_POISSON_SAMPLE)
				lit = PCSS_OmniDirectional_PoissonSample(Pos_, SpotLight[i].LightPos, SpotLightZNear, ShadowMapSize, shadow_object_spot);
	#else // USE_POISSON_SAMPLE
				lit = PCSS_OmniDirectional(Pos_, SpotLight[i].LightPos, SpotLightZNear, ShadowMapSize, shadow_object_spot);
	#endif // USE_POISSON_SAMPLE
			}
	#elif defined(USE_PCF)
			{
				vec2 radiusSquredUV = vec2(PCF_Size_OmniDirectional, PCF_Size_OmniDirectional) * ShadowMapSize;
	#if defined(USE_POISSON_SAMPLE)
				lit = PCF_OmniDirectional_PoissonSample(Pos_, SpotLight[i].LightPos, radiusSquredUV, shadow_object_spot);
	#else // USE_POISSON_SAMPLE
				lit = PCF_OmniDirectional(Pos_, SpotLight[i].LightPos, radiusSquredUV, shadow_object_spot);
	#endif // USE_POISSON_SAMPLE
			}
	#elif defined(USE_VSM)
			{
				lit = VSM_OmniDirectional(SpotLight[i].LightPos, Pos_, shadow_object_spot, vsmBiasForOmniDirectional);
			}
	#elif defined(USE_ESM)
			{
				lit = ESM_OmniDirectional(SpotLight[i].LightPos, Pos_, SpotLightZNear, SpotLightZFar, SpotLightESM_C, shadow_object_spot);
			}
	#elif defined(USE_EVSM)
			{
				lit = EVSM_OmniDirectional(SpotLight[i].LightPos, Pos_, SpotLightZNear, SpotLightZFar, SpotLightESM_C, shadow_object_spot, evsmBiasForOmniDirectional);
			}
	#else
			{
				lit = float(!IsShadowing(Pos_, SpotLight[i].LightPos, shadow_object_spot));
			}
	#endif
		}

        if (lit > 0.0)
        {
            jSpotLight light = SpotLight[i];
#if defined(USE_MATERIAL)
			if (UseMaterial > 0)
			{
				light.SpecularLightIntensity = Material.Specular;
				light.SpecularPow = Material.Shininess;
			}
#endif // USE_MATERIAL

            finalColor += GetSpotLight(light, normal, Pos_, viewDir) * lit;
        }
    }

    color = vec4(finalColor * diffuse.xyz, diffuse.w);
}