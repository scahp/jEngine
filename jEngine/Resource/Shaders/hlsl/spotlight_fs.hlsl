
#ifndef USE_SUBPASS
#define USE_SUBPASS 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 ClipPos : TEXCOORD0;
};

struct ViewUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 VP;
    float3 EyeWorld;
    float padding0;
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

// space 0
#if USE_SUBPASS
[[vk::input_attachment_index(0)]] [[vk::binding(0)]] SubpassInput GBuffer0;
[[vk::input_attachment_index(1)]] [[vk::binding(1)]] SubpassInput GBuffer1;
[[vk::input_attachment_index(2)]] [[vk::binding(2)]] SubpassInput GBuffer2;
#else // USE_SUBPASS
Texture2D GBuffer0 : register(t0, space0);
SamplerState GBuffer0SamplerState : register(s0, space0);

Texture2D GBuffer1 : register(t1, space0);
SamplerState GBuffer1SamplerState : register(s1, space0);

Texture2D GBuffer2 : register(t2, space0);
SamplerState GBuffer2SamplerState : register(s2, space0);
#endif  // USE_SUBPASS

cbuffer ViewParam : register(b0, space1) { ViewUniformBuffer ViewParam; }

cbuffer SpotLight : register(b0, space2) { jSpotLightUniformBufferData SpotLight; }
Texture2D SpotLightShadowMap : register(t1, space2);
SamplerComparisonState SpotLightShadowMapSampler : register(s1, space2);


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

float4 main(VSOutput input) : SV_TARGET
{
    float2 UV = (input.ClipPos.xy / input.ClipPos.w) * 0.5 + 0.5;
    UV.y = 1.0 - UV.y;

    float4 color = 0;

#if USE_SUBPASS
    float3 WorldPos = GBuffer0.SubpassLoad().xyz;
    float3 WorldNormal = GBuffer1.SubpassLoad().xyz;
    float3 Albedo = GBuffer2.SubpassLoad().xyz;
#else   // USE_SUBPASS
    float3 WorldPos = GBuffer0.Sample(GBuffer0SamplerState, UV).xyz;
    float3 WorldNormal = GBuffer1.Sample(GBuffer1SamplerState, UV).xyz;
    float3 Albedo = GBuffer2.Sample(GBuffer2SamplerState, UV).xyz;
#endif  // USE_SUBPASS

    float3 PointLightLit = 0.0f;
    float3 ViewWorld = normalize(ViewParam.EyeWorld - WorldPos);

    // Spot light shadow map
    float4 SpotLightShadowPosition = mul(SpotLight.ShadowVP, float4(WorldPos, 1.0));
    SpotLightShadowPosition = SpotLightShadowPosition / SpotLightShadowPosition.w;
    SpotLightShadowPosition.y = -SpotLightShadowPosition.y;

    float3 SpotLightLit = 0.0f;
    if (-1.0 <= SpotLightShadowPosition.z && SpotLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.01f;
        float Shadow = SpotLightShadowMap.SampleCmpLevelZero(SpotLightShadowMapSampler, SpotLightShadowPosition.xy * 0.5 + 0.5, SpotLightShadowPosition.z - Bias);
        if (Shadow > 0.0f)
        {
            SpotLightLit = Shadow * GetSpotLight(SpotLight, WorldNormal, WorldPos.xyz, ViewWorld);
        }
    }

    color = (1.0 / 3.141592653) * float4(Albedo * SpotLightLit, 1.0);
    return color;
}
