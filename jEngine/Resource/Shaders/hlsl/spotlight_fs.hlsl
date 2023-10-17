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

cbuffer ViewParam : register(b0, space0) { ViewUniformBuffer ViewParam; }
cbuffer SpotLight : register(b1, space0) { jSpotLightUniformBufferData SpotLight; }
#if USE_SHADOW_MAP
Texture2D SpotLightShadowMap : register(t3, space0);
SamplerComparisonState SpotLightShadowMapSampler : register(s3, space0);
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
    float3 LightDir = normalize(WorldPos.xyz - SpotLight.Position);
    float DistanceToLight = length(WorldPos.xyz - SpotLight.Position);

    // Spot light shadow map
    float4 SpotLightShadowPosition = mul(SpotLight.ShadowVP, float4(WorldPos, 1.0));
    SpotLightShadowPosition = SpotLightShadowPosition / SpotLightShadowPosition.w;
    SpotLightShadowPosition.y = -SpotLightShadowPosition.y;

    float Lit = 1.0f;
#if USE_SHADOW_MAP
    if (-1.0 <= SpotLightShadowPosition.z && SpotLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.01f;
        #if USE_REVERSEZ
        Lit = 1.0f - SpotLightShadowMap.SampleCmpLevelZero(SpotLightShadowMapSampler, SpotLightShadowPosition.xy * 0.5 + 0.5, SpotLightShadowPosition.z + Bias);
        #else
        Lit = SpotLightShadowMap.SampleCmpLevelZero(SpotLightShadowMapSampler, SpotLightShadowPosition.xy * 0.5 + 0.5, SpotLightShadowPosition.z - Bias);
        #endif
        if (Lit > 0.0f)
        {
            // SpotLightLit = Shadow * GetSpotLight(SpotLight, WorldNormal, WorldPos.xyz, ViewWorld);
        }
    }
#else
    //SpotLightLit = GetSpotLight(SpotLight, WorldNormal, WorldPos.xyz, ViewWorld);
#endif

    float lightRadian = acos(dot(-LightDir, -SpotLight.Direction));
    float SpotLightAttenuate = DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0f / SpotLight.MaxDistance)
        * DiretionalFalloff(lightRadian, SpotLight.PenumbraRadian, SpotLight.UmbraRadian);
    
    float3 L = -LightDir;
    float3 N = WorldNormal;
    float3 V = ViewWorld;
    color.xyz = PBR(L, N, V, Albedo, SpotLight.Color, DistanceToLight * 0.01f, Metallic, Roughness) * SpotLightAttenuate * Lit;
    color.w = 1.0f;
    return color;
    
    //color = (1.0 / 3.141592653) * float4(Albedo * SpotLightLit, 1.0);
    //return color;
}
