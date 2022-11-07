
#ifndef USE_VARIABLE_SHADING_RATE
#define USE_VARIABLE_SHADING_RATE 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct ViewUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 VP;
    float3 EyeWorld;
    float padding0;
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

cbuffer ViewParam : register(b0, space0) { ViewUniformBuffer ViewParam; }

cbuffer DirectionalLight : register(b0, space1) { jDirectionalLightUniformBuffer DirectionalLight; }
Texture2D DirectionalLightShadowMap : register(t1, space1);
SamplerComparisonState DirectionalLightShadowMapSampler : register(s1, space1);

Texture2D GBuffer0 : register(t0, space2);
SamplerState GBuffer0SamplerState : register(s0, space2);

Texture2D GBuffer1 : register(t1, space2);
SamplerState GBuffer1SamplerState : register(s1, space2);

Texture2D GBuffer2 : register(t2, space2);
SamplerState GBuffer2SamplerState : register(s2, space2);

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

float4 main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
) : SV_TARGET
{
    float4 color = 0;

    float3 WorldPos = GBuffer0.Sample(GBuffer0SamplerState, input.TexCoord).xyz;
    float3 WorldNormal = GBuffer1.Sample(GBuffer1SamplerState, input.TexCoord).xyz;
    float3 Albedo = GBuffer2.Sample(GBuffer2SamplerState, input.TexCoord).xyz;

    float3 ViewWorld = normalize(ViewParam.EyeWorld - WorldPos);

    // Directional light shadow map
    float4 DirectionalLightShadowPosition = mul(DirectionalLight.ShadowVP, float4(WorldPos, 1.0));
    DirectionalLightShadowPosition = DirectionalLightShadowPosition / DirectionalLightShadowPosition.w;
    DirectionalLightShadowPosition.y = -DirectionalLightShadowPosition.y;

    float3 DirectionalLightLit = GetDirectionalLight(DirectionalLight, WorldNormal, ViewWorld);
    if (-1.0 <= DirectionalLightShadowPosition.z && DirectionalLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.01f;
        float Shadow = DirectionalLightShadowMap.SampleCmpLevelZero(
            DirectionalLightShadowMapSampler, (DirectionalLightShadowPosition.xy * 0.5 + 0.5), (DirectionalLightShadowPosition.z - Bias));
        DirectionalLightLit *= Shadow;
    }

    color = (1.0 / 3.141592653) * float4(Albedo * DirectionalLightLit, 1.0);
    return color;
}
