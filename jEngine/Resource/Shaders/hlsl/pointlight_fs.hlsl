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
    float2 GBufferData0 = GBuffer0.SubpassLoad();
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

    float3 ViewWorld = normalize(ViewParam.EyeWorld - WorldPos);
    float3 LightDir = normalize(WorldPos.xyz - PointLight.Position);
    float DistanceToLight = length(WorldPos.xyz - PointLight.Position);

    float Lit = 1.0f;
#if USE_SHADOW_MAP
    // Point light shadow map
    if (DistanceToLight <= PointLight.MaxDistance)
    {
        float NormalizedDistance = DistanceToLight / PointLight.MaxDistance;

        const float Bias = 0.02f;
        Lit = PointLightShadowCubeMap.SampleCmpLevelZero(PointLightShadowCubeMapSampler, LightDir.xyz, NormalizedDistance - Bias);
    }
#endif // USE_SHADOW_MAP

#if USE_PBR
    float PointLightAttenuate = DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0f / PointLight.MaxDistance);

    float3 L = -LightDir;
    float3 N = WorldNormal;
    float3 V = ViewWorld;
    color.xyz = PBR(L, N, V, Albedo, PointLight.Color, DistanceToLight, Metallic, Roughness) * PointLightAttenuate * Lit;
    color.w = 1.0f;
#else // USE_PBR
    float3 PointLightLit = GetPointLight(PointLight, WorldNormal, WorldPos.xyz, ViewWorld) * Lit;
    color = (1.0 / 3.141592653) * float4(Albedo * PointLightLit, 1.0);
#endif // USE_PBR

    return color;
}
