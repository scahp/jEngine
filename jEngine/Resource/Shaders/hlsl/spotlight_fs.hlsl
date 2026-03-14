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

float4 main(VSOutput input) : SV_TARGET
{
    float2 UV = (input.ClipPos.xy / input.ClipPos.w) * 0.5 + 0.5;
    UV.y = 1.0 - UV.y;

    float4 color = 0;
#if USE_SUBPASS
    float3 GBufferData0 = GBuffer0.SubpassLoad();
    float3 GBufferData1 = GBuffer1.SubpassLoad();
    float4 GBufferData2 = GBuffer2.SubpassLoad();
    float DepthValue = DepthTexture.SubpassLoad();
#else   // USE_SUBPASS
    float3 GBufferData0 = GBuffer0.Sample(GBuffer0Sampler, UV);
    float3 GBufferData1 = GBuffer1.Sample(GBuffer1Sampler, UV);
    float4 GBufferData2 = GBuffer2.Sample(GBuffer2Sampler, UV);
    float DepthValue = DepthTexture.Sample(DepthTextureSampler, UV).x;
#endif  // USE_SUBPASS

    float3 WorldPos = CalcWorldPositionFromDepth(DepthValue, UV, ViewParam.InvVP);
    //float3 WorldNormal = normalize(DecodeOctNormal(GBufferData0.xy)); // Need to normalize again to avoid noise of specular light, even though it is stored normalized normal at GBuffer.
    float3 WorldNormal = GBufferData0.xyz * 2 - 1;
    float3 Albedo = GBufferData1.xyz;
    float Metallic = GBufferData2.z;
    float Roughness = GBufferData2.w;

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
    }
#endif // USE_SHADOW_MAP

#if USE_PBR
    float lightRadian = acos(dot(-LightDir, -SpotLight.Direction));
    float SpotLightAttenuate = DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0f / SpotLight.MaxDistance)
        * DiretionalFalloff(lightRadian, SpotLight.PenumbraRadian, SpotLight.UmbraRadian);

    float3 L = -LightDir;
    float3 N = WorldNormal;
    float3 V = ViewWorld;
    color.xyz = PBR(L, N, V, Albedo, SpotLight.Color, DistanceToLight, Metallic, Roughness) * SpotLightAttenuate * Lit;
    color.w = 1.0f;
#else // USE_PBR
    float3 SpotLightLit = GetSpotLight(SpotLight, WorldNormal, WorldPos.xyz, ViewWorld) * Lit;
    color = (1.0 / 3.141592653) * float4(Albedo * SpotLightLit, 1.0);
#endif // USE_PBR

    return color;
}
