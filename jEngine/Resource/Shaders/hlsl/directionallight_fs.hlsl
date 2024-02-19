#include "common.hlsl"
#include "lightutil.hlsl"
#include "PBR.hlsl"

#ifndef USE_VARIABLE_SHADING_RATE
#define USE_VARIABLE_SHADING_RATE 0
#endif

#ifndef USE_SUBPASS
#define USE_SUBPASS 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// space 0
#if USE_SUBPASS
[[vk::input_attachment_index(0)]] [[vk::binding(0)]] SubpassInput GBuffer0;
[[vk::input_attachment_index(1)]] [[vk::binding(1)]] SubpassInput GBuffer1;
[[vk::input_attachment_index(2)]] [[vk::binding(2)]] SubpassInput GBuffer2;
[[vk::input_attachment_index(3)]] [[vk::binding(3)]] SubpassInput<float> DepthTexture;
#else // USE_SUBPASS
Texture2D GBuffer0 : register(t0, space0);
SamplerState GBuffer0SamplerState : register(s0, space0);

Texture2D GBuffer1 : register(t1, space0);
SamplerState GBuffer1SamplerState : register(s1, space0);

Texture2D GBuffer2 : register(t2, space0);
SamplerState GBuffer2SamplerState : register(s2, space0);

Texture2D DepthTexture : register(t3, space0);
SamplerState DepthTextureSamplerState : register(s3, space0);
#endif // USE_SUBPASS

cbuffer ViewParam : register(b0, space1) { ViewUniformBuffer ViewParam; }

cbuffer DirectionalLight : register(b0, space2) { jDirectionalLightUniformBuffer DirectionalLight; }
#if USE_SHADOW_MAP
Texture2D DirectionalLightShadowMap : register(t1, space2);
SamplerComparisonState DirectionalLightShadowMapSampler : register(s1, space2);
#endif

TextureCube<float4> IrradianceMap : register(t0, space3);
SamplerState IrradianceMapSamplerState : register(s0, space3);

TextureCube<float4> PrefilteredEnvMap : register(t1, space3);
SamplerState PrefilteredEnvSamplerState : register(s1, space3);

float4 main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
) : SV_TARGET
{
    float4 color = 0;

#if USE_SUBPASS
    float2 GBufferData0 = GBuffer0.SubpassLoad();
    float3 GBufferData1 = GBuffer1.SubpassLoad();
    float4 GBufferData2 = GBuffer2.SubpassLoad();
    float DepthValue = DepthTexture.SubpassLoad();
#else   // USE_SUBPASS
    float3 GBufferData0 = GBuffer0.Sample(GBuffer0SamplerState, input.TexCoord);
    float3 GBufferData1 = GBuffer1.Sample(GBuffer1SamplerState, input.TexCoord);
    float4 GBufferData2 = GBuffer2.Sample(GBuffer2SamplerState, input.TexCoord);
    float DepthValue = DepthTexture.Sample(DepthTextureSamplerState, input.TexCoord).x;
#endif  // USE_SUBPASS

    float3 WorldPos = CalcWorldPositionFromDepth(DepthValue, input.TexCoord, ViewParam.InvVP);
    //float3 WorldNormal = normalize(DecodeOctNormal(GBufferData0.xy)); // Need to normalize again to avoid noise of specular light, even though it is stored normalized normal at GBuffer.
    float3 WorldNormal = GBufferData0.xyz * 2 - 1;
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
    if (-1.0 <= DirectionalLightShadowPosition.z && DirectionalLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.002f;
        Lit = DirectionalLightShadowMap.SampleCmpLevelZero(
            DirectionalLightShadowMapSampler, (DirectionalLightShadowPosition.xy * 0.5 + 0.5), (DirectionalLightShadowPosition.z - Bias));
    }
#endif // USE_SHADOW_MAP

    float3 L = -DirectionalLight.Direction;
    float3 N = WorldNormal;
    float3 V = ViewWorld;
    const float DistanceToLight = 1.0f;     // Directional light from the Sun is not having attenuation by using distance
#if USE_PBR
    color.xyz = PBR(L, N, V, Albedo, DirectionalLight.Color, DistanceToLight, Metallic, Roughness) * Lit;
    color.w = 1.0f;
#else // USE_PBR
    float3 DirectionalLightLit = GetDirectionalLight(DirectionalLight, WorldNormal, ViewWorld) * Lit;
    color = (1.0 / 3.141592653) * float4(Albedo * DirectionalLightLit, 1.0);
#endif // USE_PBR

    // ambient from IBL
    {
        // todo : Need to split shader, because it is possible that ILB without directional light
        float3 DiffusePart = IBL_DiffusePart(N, V, Albedo, Metallic, Roughness, IrradianceMap, IrradianceMapSamplerState);

        float NoV = saturate(dot(N, V));
        float3 R = 2 * dot(V, N) * N - V;
        R = normalize(R);
        float3 PrefilteredColor = PrefilteredEnvMap.SampleLevel(PrefilteredEnvSamplerState, R, Roughness * (8-1)).rgb;
        
        float3 F0 = float3(0.04f, 0.04f, 0.04f);
        float3 SpecularColor = lerp(F0, Albedo, Metallic);

        float3 SpecularPart = PrefilteredColor * EnvBRDFApprox(SpecularColor, Roughness, NoV);
        color.xyz += (DiffusePart + SpecularPart);
    }
    return color;
}
