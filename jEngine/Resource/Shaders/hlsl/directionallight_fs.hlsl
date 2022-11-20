#include "common.hlsl"
#include "lightutil.hlsl"

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
#else // USE_SUBPASS
Texture2D GBuffer0 : register(t0, space0);
SamplerState GBuffer0SamplerState : register(s0, space0);

Texture2D GBuffer1 : register(t1, space0);
SamplerState GBuffer1SamplerState : register(s1, space0);

Texture2D GBuffer2 : register(t2, space0);
SamplerState GBuffer2SamplerState : register(s2, space0);
#endif // USE_SUBPASS

cbuffer ViewParam : register(b0, space1) { ViewUniformBuffer ViewParam; }

cbuffer DirectionalLight : register(b0, space2) { jDirectionalLightUniformBuffer DirectionalLight; }
#if USE_SHADOW_MAP
Texture2D DirectionalLightShadowMap : register(t1, space2);
SamplerComparisonState DirectionalLightShadowMapSampler : register(s1, space2);
#endif

float4 main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
) : SV_TARGET
{
    float4 color = 0;

#if USE_SUBPASS
    float3 WorldPos = GBuffer0.SubpassLoad().xyz;
    float3 WorldNormal = GBuffer1.SubpassLoad().xyz;
    float3 Albedo = GBuffer2.SubpassLoad().xyz;
#else   // USE_SUBPASS
    float3 WorldPos = GBuffer0.Sample(GBuffer0SamplerState, input.TexCoord).xyz;
    float3 WorldNormal = GBuffer1.Sample(GBuffer1SamplerState, input.TexCoord).xyz;
    float3 Albedo = GBuffer2.Sample(GBuffer2SamplerState, input.TexCoord).xyz;
#endif  // USE_SUBPASS

    float3 ViewWorld = normalize(ViewParam.EyeWorld - WorldPos);

    // Directional light shadow map
    float4 DirectionalLightShadowPosition = mul(DirectionalLight.ShadowVP, float4(WorldPos, 1.0));
    DirectionalLightShadowPosition = DirectionalLightShadowPosition / DirectionalLightShadowPosition.w;
    DirectionalLightShadowPosition.y = -DirectionalLightShadowPosition.y;

    float3 DirectionalLightLit = GetDirectionalLight(DirectionalLight, WorldNormal, ViewWorld);
#if USE_SHADOW_MAP
    if (-1.0 <= DirectionalLightShadowPosition.z && DirectionalLightShadowPosition.z <= 1.0)
    {
        const float Bias = 0.01f;
        float Shadow = DirectionalLightShadowMap.SampleCmpLevelZero(
            DirectionalLightShadowMapSampler, (DirectionalLightShadowPosition.xy * 0.5 + 0.5), (DirectionalLightShadowPosition.z - Bias));
        DirectionalLightLit *= Shadow;
    }
#endif

    color = (1.0 / 3.141592653) * float4(Albedo * DirectionalLightLit, 1.0);
    return color;
}
