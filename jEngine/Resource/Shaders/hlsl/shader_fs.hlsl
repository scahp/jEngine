
#ifndef USE_VARIABLE_SHADING_RATE
#define USE_VARIABLE_SHADING_RATE 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float4 Color : COLOR0;
    [[vk::location(1)]] float2 TexCoord : TEXCOORD0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float4 ShadowPosition : TEXCOORD1;
    [[vk::location(4)]] float4 WorldPos : TEXCOORD2;
};

struct DirectionalLightUniformBuffer
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

cbuffer DirectionalLight : register(b0, space1) { DirectionalLightUniformBuffer DirectionalLight; }
Texture2D DirectionalLightShadowMap : register(t1, space1);
SamplerState DirectionalLightShadowMapSampler : register(s1, space1);

cbuffer PointLight : register(b0, space2) { jPointLightUniformBufferData PointLight; }
TextureCube PointLightShadowCubeMap : register(t1, space2);
SamplerState PointLightShadowMapSampler : register(s1, space2);

cbuffer RenderObjectParam : register(b0, space3) { RenderObjectUniformBuffer RenderObjectParam; }

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

float4 main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
) : SV_TARGET
{
    float3 light = 0.0f;

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
            float3 ViewWorld = normalize(ViewParam.EyeWorld - input.WorldPos.xyz);
            light = GetPointLight(PointLight, input.Normal, input.WorldPos.xyz, ViewWorld);
        }
    }

    // Directional light shadow map
    float DirectionalLightLit = 1.0f;
    if (-1.0 <= input.ShadowPosition.z && input.ShadowPosition.z <= 1.0)
    {
        float shadowMapDist = DirectionalLightShadowMap.Sample(DirectionalLightShadowMapSampler, input.ShadowPosition.xy * 0.5 + 0.5).r;
        if (input.ShadowPosition.z > shadowMapDist + 0.001)
        {
            DirectionalLightLit = 0.0f;
        }
    }

    DirectionalLightLit *= dot(input.Normal, -DirectionalLight.Direction);
    float4 color = (1.0 / 3.14) * float4(pushConsts.Color.rgb * input.Color.xyz * (light + DirectionalLightLit), 1.0);

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
        float2 center = (input.ShadowPosition.xy * 0.5 + 0.5);
        
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
