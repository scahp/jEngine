#include "common.hlsl"
// Advanced shadow shader injection point.
#include "lightutil.hlsl"
#include "PBR.hlsl"
#include "shadowfiltering.hlsl"
#include "Features/advancedshadowlighting.hlsl"
#include "Features/deferredlighting.hlsl"

#ifndef USE_VARIABLE_SHADING_RATE
#define USE_VARIABLE_SHADING_RATE 0
#endif

#ifndef USE_SUBPASS
#define USE_SUBPASS 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
#if USE_MSAA
    sample float2 TexCoord : TEXCOORD0;
#else
    float2 TexCoord : TEXCOORD0;
#endif
};

JELightOutput main(VSOutput input
#if USE_MSAA
    , uint sampleIndex : SV_SampleIndex
#endif
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
)
{
    float4 color = 0;

#if USE_MSAA
    const int2 pixel = int2(input.Pos.xy);
    float4 GBufferData0 = GBuffer0.Load(pixel, sampleIndex);
    float4 GBufferData1 = GBuffer1.Load(pixel, sampleIndex);
    float4 GBufferData2 = GBuffer2.Load(pixel, sampleIndex);
    float DepthValue = DepthTexture.Load(pixel, sampleIndex);
    float4 MaterialVisibility = GBuffer4.Load(pixel, sampleIndex);
#elif USE_SUBPASS
    float4 GBufferData0 = GBuffer0.SubpassLoad();
    float4 GBufferData1 = GBuffer1.SubpassLoad();
    float4 GBufferData2 = GBuffer2.SubpassLoad();
    float4 MaterialVisibility = GBuffer4.SubpassLoad();
    float DepthValue = DepthTexture.SubpassLoad();
#else   // USE_SUBPASS
    float4 GBufferData0 = GBuffer0.Sample(GBuffer0Sampler, input.TexCoord);
    float4 GBufferData1 = GBuffer1.Sample(GBuffer1Sampler, input.TexCoord);
    float4 GBufferData2 = GBuffer2.Sample(GBuffer2Sampler, input.TexCoord);
    float4 MaterialVisibility = GBuffer4.Sample(GBuffer4Sampler, input.TexCoord);
    float DepthValue = DepthTexture.Sample(DepthTextureSampler, input.TexCoord).x;
#endif  // USE_SUBPASS

    if (DepthValue >= 1.0) return JE_LightOutput(0.0, 0.0);
    float3 diffuse = 0.0;
    float3 WorldPos = CalcWorldPositionFromDepth(DepthValue, input.TexCoord, ViewParam.InvVP);
    //float3 WorldNormal = normalize(DecodeOctNormal(GBufferData0.xy)); // Need to normalize again to avoid noise of specular light, even though it is stored normalized normal at GBuffer.
    float3 WorldNormal = JE_DeferredShadingNormal(GBufferData0);
    float3 Albedo = GBufferData1.xyz;
    float Metallic = GBufferData2.z;
    float Roughness = GBufferData2.w;
    
    //return float4(Albedo, 1.0);

    float3 ViewWorld = normalize(ViewParam.EyeWorld - WorldPos);

    // Directional light shadow map
    float4 DirectionalLightShadowPosition = mul(DirectionalLight.ShadowVP, float4(WorldPos, 1.0));
    DirectionalLightShadowPosition = DirectionalLightShadowPosition / DirectionalLightShadowPosition.w;
    DirectionalLightShadowPosition.y = -DirectionalLightShadowPosition.y;

    float Lit = 1.0f;
#if USE_SHADOW_MAP
    if (DirectionalShadow.SamplingParams.z > 0.5)
        Lit = JE_SampleAdvancedLightShadow(DirectionalShadow, DirectionalAdvancedShadowMap,
            DirectionalDeepShadow0, DirectionalDeepShadow1, DirectionalAdvancedShadowMapSampler,
            WorldPos, ViewParam.VP);
    else if (-1.0 <= DirectionalLightShadowPosition.z && DirectionalLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.002f;
        Lit = SampleShadowVisibility2D(DirectionalLightShadowMap, DirectionalLightShadowMapSampler,
            DirectionalLightShadowPosition.xy * 0.5 + 0.5, DirectionalLightShadowPosition.z - Bias,
            ViewParam.ShadowFilterParams.y, (int)ViewParam.ShadowFilterParams.x);
    }
#endif // USE_SHADOW_MAP

    // The material horizon was baked/evaluated for the primary directional
    // light. Other directional lights keep their own unmodified visibility.
    const float3 toThisLight = -DirectionalLight.Direction;
    const float directionLengthSquared = dot(toThisLight, toThisLight);
    if (ViewParam.MaterialLightDirection.w > 0.5 && directionLengthSquared > 1.0e-12 &&
        dot(toThisLight * rsqrt(directionLengthSquared), ViewParam.MaterialLightDirection.xyz) > 0.999)
        Lit *= saturate(MaterialVisibility.x);

    float3 L = -DirectionalLight.Direction;
    float3 N = WorldNormal;
    float3 V = ViewWorld;
    const float DistanceToLight = 1.0f;     // Directional light from the Sun is not having attenuation by using distance
#if USE_PBR
    color.xyz = PBR(L, N, V, Albedo, DirectionalLight.Color, DistanceToLight, Metallic, Roughness) * Lit;
#if USE_SUBSURFACE
    diffuse = PBRDiffuse(L, N, V, Albedo, DirectionalLight.Color, DistanceToLight, Metallic) * Lit;
#endif
    color.w = 1.0f;
#else // USE_PBR
#if USE_SUBSURFACE
    diffuse = Albedo / PI * GetDirectionalLightDiffuse(DirectionalLight, WorldNormal) * Lit;
#endif
    float3 DirectionalLightLit = GetDirectionalLight(DirectionalLight, WorldNormal, ViewWorld) * Lit;
    color = (1.0 / 3.141592653) * float4(Albedo * DirectionalLightLit, 1.0);
#endif // USE_PBR

#if USE_SKIN_MATERIAL
    if (((uint)round(GBufferData1.a) & 255u) == 1u)
        JE_DeferredSkin(L, N, V, Albedo, Roughness, MaterialVisibility,
            DirectionalLight.Color * Lit, color.rgb, diffuse);
#endif

    // ambient from IBL
    // if (Lit > 0)
    if (0)
    {
        // todo : Need to split shader, because it is possible that IBL without directional light
        float3 DiffusePart = IBL_DiffusePart(N, V, Albedo, Metallic, Roughness, IrradianceMap, IrradianceMapSampler);

        float NoV = saturate(dot(N, V));
        float3 R = 2 * dot(V, N) * N - V;
        R = normalize(R);
        float3 PrefilteredColor = PrefilteredEnvMap.SampleLevel(PrefilteredEnvMapSampler, R, Roughness * (8 - 1)).rgb;
        
        float3 F0 = float3(0.04f, 0.04f, 0.04f);
        float3 SpecularColor = lerp(F0, Albedo, Metallic);

        float3 SpecularPart = PrefilteredColor * EnvBRDFApprox(SpecularColor, Roughness, NoV);
        color.xyz += (DiffusePart + SpecularPart);
    }
    return JE_LightOutput(color, diffuse);
}
