#include "common.hlsl"
#include "lightutil.hlsl"
#include "PBR.hlsl"

#ifndef USE_SUBPASS
#define USE_SUBPASS 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 ClipPos : TEXCOORD0;
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

cbuffer PointLight : register(b0, space2) { jPointLightUniformBufferData PointLight; }
#if USE_SHADOW_MAP
TextureCube PointLightShadowCubeMap : register(t1, space2);
SamplerComparisonState PointLightShadowMapSampler : register(s1, space2);
#endif

float4 main(VSOutput input) : SV_TARGET
{
    float2 UV = (input.ClipPos.xy / input.ClipPos.w) * 0.5 + 0.5;
    UV.y = 1.0 - UV.y;

    float4 color = 0;

#if USE_SUBPASS
    float4 GBufferData0 = GBuffer0.SubpassLoad();
    float4 GBufferData1 = GBuffer1.SubpassLoad();
    float4 GBufferData2 = GBuffer2.SubpassLoad();
#else   // USE_SUBPASS
    float4 GBufferData0 = GBuffer0.Sample(GBuffer0SamplerState, UV);
    float4 GBufferData1 = GBuffer1.Sample(GBuffer1SamplerState, UV);
    float4 GBufferData2 = GBuffer2.Sample(GBuffer2SamplerState, UV);
#endif  // USE_SUBPASS

    float3 WorldPos = GBufferData0.xyz;
    float Metallic = GBufferData0.w;
    float3 WorldNormal = normalize(GBufferData1.xyz);       // Need to normalize again to avoid noise of specular light, even though it is stored normalized normal at GBuffer.
    float Roughness = GBufferData1.w;
    float3 Albedo = GBufferData2.xyz;

    float3 PointLightLit = 0.0f;
    float3 ViewWorld = normalize(ViewParam.EyeWorld - WorldPos);
    float3 LightDir = normalize(WorldPos.xyz - PointLight.Position);
    float DistanceToLight = length(WorldPos.xyz - PointLight.Position);

#define ENABLE_PBR 0

    // Point light shadow map
    float Lit = 1.0f;
#if USE_SHADOW_MAP
    if (DistanceToLight <= PointLight.MaxDistance)
    {
        float NormalizedDistance = DistanceToLight / PointLight.MaxDistance;

        const float Bias = 0.02f;
        Lit = PointLightShadowCubeMap.SampleCmpLevelZero(PointLightShadowMapSampler, LightDir.xyz, NormalizedDistance - Bias);
        if (Lit > 0.0f)
        {
             PointLightLit = Lit * GetPointLight(PointLight, WorldNormal, WorldPos.xyz, ViewWorld);
        }
    }
#else
    #if !ENABLE_PBR
    PointLightLit = GetPointLight(PointLight, WorldNormal, WorldPos.xyz, ViewWorld);
    #endif
#endif

#if ENABLE_PBR
    float PointLightAttenuate = DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0f / PointLight.MaxDistance);

    float3 L = -LightDir;
    float3 N = WorldNormal;
    float3 V = ViewWorld;
    color.xyz = PBR(L, N, V, Albedo, PointLight.Color, DistanceToLight * 0.01f, Metallic, Roughness) * PointLightAttenuate * Lit;
    color.w = 1.0f;
    return color;
#else    
    color = (1.0 / 3.141592653) * float4(Albedo * PointLightLit, 1.0);
    return color;
#endif
}
