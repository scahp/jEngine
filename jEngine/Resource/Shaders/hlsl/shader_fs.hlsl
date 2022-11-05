
#ifndef USE_VARIABLE_SHADING_RATE
#define USE_VARIABLE_SHADING_RATE 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL0;
    float4 DirectionalLightShadowPosition : TEXCOORD1;
    float4 SpotLightShadowPosition : TEXCOORD2;
    float4 WorldPos : TEXCOORD3;
};

struct jDirectionalLightUniformBuffer
{
    float3 Direction;
    float SpecularPow;

    float3 Color;
    float padding0;

    float3 DiffuseIntensity;
    float padding1;

    float3 SpecularIntensity;
    float padding2;

    float4x4 ShadowVP;
    float4x4 ShadowV;
		
    float3 LightPos;
    float padding3;

    float2 ShadowMapSize;
    float Near;
    float Far;
};

struct jPointLightUniformBufferData
{
    float3 Position;
    float SpecularPow;

    float3 Color;
    float MaxDistance;

    float3 DiffuseIntensity;
    float padding0;

    float3 SpecularIntensity;
    float padding1;

    float4x4 ShadowVP[6];
};

struct jSpotLightUniformBufferData
{
    float3 Position;
    float MaxDistance;

    float3 Direction;
    float PenumbraRadian;

    float3 Color;
    float UmbraRadian;

    float3 DiffuseIntensity;
    float SpecularPow;

    float3 SpecularIntensity;
    float padding0;

    float4x4 ShadowVP;
};

struct ViewUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 VP;
    float3 EyeWorld;
    float padding0;
};

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 InvM;
};

cbuffer ViewParam : register(b0,space0) { ViewUniformBuffer ViewParam; }

cbuffer DirectionalLight : register(b0, space1) { jDirectionalLightUniformBuffer DirectionalLight; }
Texture2D DirectionalLightShadowMap : register(t1, space1);
SamplerState DirectionalLightShadowMapSampler : register(s1, space1);

cbuffer PointLight : register(b0, space2) { jPointLightUniformBufferData PointLight; }
TextureCube PointLightShadowCubeMap : register(t1, space2);
SamplerState PointLightShadowMapSampler : register(s1, space2);

cbuffer SpotLight : register(b0, space3) { jSpotLightUniformBufferData SpotLight; }
Texture2D SpotLightShadowMap : register(t1, space3);
SamplerState SpotLightShadowMapSampler : register(s1, space3);

cbuffer RenderObjectParam : register(b0, space4) { RenderObjectUniformBuffer RenderObjectParam; }

struct PushConsts
{
    float4 Color;
    bool ShowVRSArea;
    bool ShowGrid;
};
[[vk::push_constant]] PushConsts pushConsts;

float WindowingFunction(float value, float maxValue)
{
    return pow(max(0.0, 1.0 - pow(value / maxValue, 4.0)), 2.0);
}

float DistanceAttenuation(float distance, float maxDistance)
{
    const float refDistance = 50.0;
    float attenuation = (refDistance * refDistance) / ((distance * distance) + 1.0);
    return attenuation * WindowingFunction(distance, maxDistance);
}

float DiretionalFalloff(float lightRadian, float penumbraRadian, float umbraRadian)
{
    float t = clamp((cos(lightRadian) - cos(umbraRadian)) / (cos(penumbraRadian) - cos(umbraRadian)), 0.0, 1.0);
    return t * t;
}

float3 GetDirectionalLightDiffuse(jDirectionalLightUniformBuffer light, float3 normal)
{
    return light.Color * clamp(dot(-light.Direction, normal), 0.0, 1.0) * light.DiffuseIntensity;
}

float3 GetDirectionalLightSpecular(jDirectionalLightUniformBuffer light, float3 reflectLightDir, float3 viewDir)
{
    return light.Color * pow(clamp(dot(reflectLightDir, viewDir), 0.0, 1.0), light.SpecularPow) * light.SpecularIntensity;
}

float3 GetDirectionalLight(jDirectionalLightUniformBuffer light, float3 normal, float3 viewDir)
{
    float3 lightDir = normalize(-light.Direction);
    float3 reflectLightDir = 2.0 * clamp(dot(lightDir, normal), 0.0, 1.0) * normal - lightDir;
    return (GetDirectionalLightDiffuse(light, normal) + GetDirectionalLightSpecular(light, reflectLightDir, viewDir));
}

float3 GetPointLightDiffuse(jPointLightUniformBufferData light, float3 normal, float3 lightDir)
{
    return light.Color * clamp(dot(lightDir, normal), 0.0, 1.0) * light.DiffuseIntensity;
}

float3 GetPointLightSpecular(jPointLightUniformBufferData light, float3 reflectLightDir, float3 viewDir)
{
    return light.Color * pow(clamp(dot(reflectLightDir, viewDir), 0.0, 1.0), light.SpecularPow) * light.SpecularIntensity;
}

float3 GetPointLight(jPointLightUniformBufferData light, float3 normal, float3 pixelPos, float3 viewDir)
{
    float3 lightDir = light.Position - pixelPos;
    float pointLightDistance = length(lightDir);
    lightDir = normalize(lightDir);
    float3 reflectLightDir = 2.0 * clamp(dot(lightDir, normal), 0.0, 1.0) * normal - lightDir;

    return (GetPointLightDiffuse(light, normal, lightDir) + GetPointLightSpecular(light, reflectLightDir, viewDir)) * DistanceAttenuation(pointLightDistance, light.MaxDistance);
}

float3 GetSpotLightDiffuse(jSpotLightUniformBufferData light, float3 normal, float3 lightDir)
{
    return light.Color * clamp(dot(lightDir, normal), 0.0, 1.0) * light.DiffuseIntensity;
}

float3 GetSpotLightSpecular(jSpotLightUniformBufferData light, float3 reflectLightDir, float3 viewDir)
{
    return light.Color * pow(clamp(dot(reflectLightDir, viewDir), 0.0, 1.0), light.SpecularPow) * light.SpecularIntensity;
}

float3 GetSpotLight(jSpotLightUniformBufferData light, float3 normal, float3 pixelPos, float3 viewDir)
{
    float3 lightDir = light.Position - pixelPos;
    float distance = length(lightDir);
    lightDir = normalize(lightDir);
    float3 reflectLightDir = 2.0 * clamp(dot(lightDir, normal), 0.0, 1.0) * normal - lightDir;

    float lightRadian = acos(dot(lightDir, -light.Direction));

    return (GetSpotLightDiffuse(light, normal, lightDir)
        + GetSpotLightSpecular(light, reflectLightDir, viewDir))
        * DistanceAttenuation(distance, light.MaxDistance)
        * DiretionalFalloff(lightRadian, light.PenumbraRadian, light.UmbraRadian);
}

float4 main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
) : SV_TARGET
{
    float3 light = 0.0f;
    float3 ViewWorld = normalize(ViewParam.EyeWorld - input.WorldPos.xyz);

    // Point light shadow map
    float3 LightDir = input.WorldPos.xyz - PointLight.Position;
    float DistanceToLight = length(LightDir);
    if (DistanceToLight <= PointLight.MaxDistance)
    {
        float NormalizedDistance = DistanceToLight / PointLight.MaxDistance;
        float shadowMapDist = PointLightShadowCubeMap.Sample(PointLightShadowMapSampler, LightDir.xyz).r;

        const float ShadowBias = 0.005f;
        if (NormalizedDistance <= shadowMapDist + ShadowBias)
        {
            light = GetPointLight(PointLight, input.Normal, input.WorldPos.xyz, ViewWorld);
        }
    }

    // Directional light shadow map
    float3 DirectionalLightShadowPosition = input.DirectionalLightShadowPosition.xyz / input.DirectionalLightShadowPosition.w;
    DirectionalLightShadowPosition.y = -DirectionalLightShadowPosition.y;

    float3 DirectionalLightLit = GetDirectionalLight(DirectionalLight, input.Normal, ViewWorld);
    if (-1.0 <= DirectionalLightShadowPosition.z && DirectionalLightShadowPosition.z <= 1.0)
    {
        float shadowMapDist = DirectionalLightShadowMap.Sample(DirectionalLightShadowMapSampler, DirectionalLightShadowPosition.xy * 0.5 + 0.5).r;
        if (DirectionalLightShadowPosition.z > shadowMapDist + 0.001)
        {
            DirectionalLightLit = 0.0f;
        }
    }

    // Spot light shadow map

    float3 SpotLightShadowPosition = input.SpotLightShadowPosition.xyz / input.SpotLightShadowPosition.w;
    SpotLightShadowPosition.y = -SpotLightShadowPosition.y;

    float3 SpotLightLit = GetSpotLight(SpotLight, input.Normal, input.WorldPos.xyz, ViewWorld);

    if (-1.0 <= SpotLightShadowPosition.z && SpotLightShadowPosition.z <= 1.0)
    {
        float shadowMapDist = SpotLightShadowMap.Sample(SpotLightShadowMapSampler, SpotLightShadowPosition.xy * 0.5 + 0.5).r;
        if (SpotLightShadowPosition.z > shadowMapDist + 0.001)
        {
            SpotLightLit = 0.0f;
        }
    }

    float4 color = (1.0 / 3.14) * float4(pushConsts.Color.rgb * input.Color.xyz * (light + DirectionalLightLit + SpotLightLit), 1.0);

#if USE_VARIABLE_SHADING_RATE
    if (pushConsts.ShowVRSArea)
    {
        const uint SHADING_RATE_PER_PIXEL = 0x0;
        const uint SHADING_RATE_PER_2X1_PIXELS = 6;
        const uint SHADING_RATE_PER_1X2_PIXELS = 7;
        const uint SHADING_RATE_PER_2X2_PIXELS = 8;
        const uint SHADING_RATE_PER_4X2_PIXELS = 9;
        const uint SHADING_RATE_PER_2X4_PIXELS = 10;
    
        switch (shadingRate)
        {
            case SHADING_RATE_PER_PIXEL:
                return color * float4(0.0, 0.8, 0.4, 1.0);
            case SHADING_RATE_PER_2X1_PIXELS:
                return color * float4(0.2, 0.6, 1.0, 1.0);
            case SHADING_RATE_PER_1X2_PIXELS:
                return color * float4(0.0, 0.4, 0.8, 1.0);
            case SHADING_RATE_PER_2X2_PIXELS:
                return color * float4(1.0, 1.0, 0.2, 1.0);
            case SHADING_RATE_PER_4X2_PIXELS:
                return color * float4(0.8, 0.8, 0.0, 1.0);
            case SHADING_RATE_PER_2X4_PIXELS:
                return color * float4(1.0, 0.4, 0.2, 1.0);
            default:
                return color * float4(0.8, 0.0, 0.0, 1.0);
        }
    }
#endif

    // Draw Grid by using shadow position
    if (pushConsts.ShowGrid)
    {
        float2 center = (input.DirectionalLightShadowPosition.xy * 0.5 + 0.5);
        
        float resolution = 100.0;
        float cellSpace = 1.0;
        float2 cells = abs(frac(center * resolution / cellSpace) - 0.5);
        
        float distToEdge = (0.5 - max(cells.x, cells.y)) * cellSpace;
        
        float lineWidth = 0.1;
        float lines = smoothstep(0.0, lineWidth, distToEdge);
        color = lerp(float4(0.0, 1.0, 0.0, 1.0), color, lines);
    }
    
    return color;
}
