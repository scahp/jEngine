#include "common.hlsl"
// Advanced shadow shader injection point.
#include "lightutil.hlsl"
#include "PBR.hlsl"
#include "shadowfiltering.hlsl"
#include "Features/advancedshadowlighting.hlsl"
#include "Features/deferredlighting.hlsl"

#ifndef USE_SUBPASS
#define USE_SUBPASS 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
#if USE_MSAA
    sample float4 ClipPos : TEXCOORD0;
#else
    float4 ClipPos : TEXCOORD0;
#endif
};

JELightOutput main(VSOutput input
#if USE_MSAA
    , uint sampleIndex : SV_SampleIndex
#endif
)
{
    float2 UV = (input.ClipPos.xy / input.ClipPos.w) * 0.5 + 0.5;
    UV.y = 1.0 - UV.y;

    float4 color = 0;
#if USE_MSAA
    const int2 pixel = int2(input.Pos.xy);
    float4 GBufferData0 = GBuffer0.Load(pixel, sampleIndex);
    float4 GBufferData1 = GBuffer1.Load(pixel, sampleIndex);
    float4 GBufferData2 = GBuffer2.Load(pixel, sampleIndex);
    float4 MaterialVisibility = GBuffer4.Load(pixel, sampleIndex);
    float DepthValue = DepthTexture.Load(pixel, sampleIndex);
#elif USE_SUBPASS
    float4 GBufferData0 = GBuffer0.SubpassLoad();
    float4 GBufferData1 = GBuffer1.SubpassLoad();
    float4 GBufferData2 = GBuffer2.SubpassLoad();
    float4 MaterialVisibility = GBuffer4.SubpassLoad();
    float DepthValue = DepthTexture.SubpassLoad();
#else   // USE_SUBPASS
    float4 GBufferData0 = GBuffer0.Sample(GBuffer0Sampler, UV);
    float4 GBufferData1 = GBuffer1.Sample(GBuffer1Sampler, UV);
    float4 GBufferData2 = GBuffer2.Sample(GBuffer2Sampler, UV);
    float4 MaterialVisibility = GBuffer4.Sample(GBuffer4Sampler, UV);
    float DepthValue = DepthTexture.Sample(DepthTextureSampler, UV).x;
#endif  // USE_SUBPASS

    if (DepthValue >= 1.0) return JE_LightOutput(0.0, 0.0);
    float3 diffuse = 0.0;
    float3 WorldPos = CalcWorldPositionFromDepth(DepthValue, UV, ViewParam.InvVP);
    //float3 WorldNormal = normalize(DecodeOctNormal(GBufferData0.xy)); // Need to normalize again to avoid noise of specular light, even though it is stored normalized normal at GBuffer.
    float3 WorldNormal = JE_DeferredShadingNormal(GBufferData0);
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
    if (SpotShadow.SamplingParams.z > 0.5)
        Lit = JE_SampleAdvancedLightShadow(SpotShadow, SpotAdvancedShadowMap,
            SpotDeepShadow0, SpotDeepShadow1, SpotAdvancedShadowMapSampler, WorldPos, ViewParam.VP);
    else if (-1.0 <= SpotLightShadowPosition.z && SpotLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.01f;
        #if USE_REVERSEZ
        Lit = 1.0f - SampleShadowVisibility2D(SpotLightShadowMap, SpotLightShadowMapSampler,
            SpotLightShadowPosition.xy * 0.5 + 0.5, SpotLightShadowPosition.z + Bias,
            ViewParam.ShadowFilterParams.y, (int)ViewParam.ShadowFilterParams.x);
        #else
        Lit = SampleShadowVisibility2D(SpotLightShadowMap, SpotLightShadowMapSampler,
            SpotLightShadowPosition.xy * 0.5 + 0.5, SpotLightShadowPosition.z - Bias,
            ViewParam.ShadowFilterParams.y, (int)ViewParam.ShadowFilterParams.x);
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
#if USE_SUBSURFACE
    diffuse = PBRDiffuse(L, N, V, Albedo, SpotLight.Color, DistanceToLight, Metallic) * SpotLightAttenuate * Lit;
#endif
    color.w = 1.0f;
#else // USE_PBR
#if USE_SUBSURFACE
    diffuse = Albedo / PI * GetSpotLightDiffuse(SpotLight, WorldNormal, -LightDir) * DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0 / SpotLight.MaxDistance) * DiretionalFalloff(acos(clamp(dot(-LightDir, -SpotLight.Direction), -1.0, 1.0)), SpotLight.PenumbraRadian, SpotLight.UmbraRadian) * Lit;
#endif
    float3 SpotLightLit = GetSpotLight(SpotLight, WorldNormal, WorldPos.xyz, ViewWorld) * Lit;
    color = (1.0 / 3.141592653) * float4(Albedo * SpotLightLit, 1.0);
#endif // USE_PBR

#if USE_SKIN_MATERIAL
    if (((uint)round(GBufferData1.a) & 255u) == 1u)
        JE_DeferredSkin(-LightDir, WorldNormal, ViewWorld, Albedo, Roughness, MaterialVisibility,
            SpotLight.Color * (DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0f / SpotLight.MaxDistance) * DiretionalFalloff(acos(clamp(dot(-LightDir, -SpotLight.Direction), -1.0, 1.0)), SpotLight.PenumbraRadian, SpotLight.UmbraRadian)) * Lit /
                max(DistanceToLight * DistanceToLight, 1.0e-6), color.rgb, diffuse);
#endif

    return JE_LightOutput(color, diffuse);
}
