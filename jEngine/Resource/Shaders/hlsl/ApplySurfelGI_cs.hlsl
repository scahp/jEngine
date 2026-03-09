#include "Common.hlsl"

// Final SurfelGI shading pass.
// Input texture meaning:
// - SceneColorTexture: current shaded color before SurfelGI diffuse indirect is added
// - SurfelGITexture: screen-space irradiance resolved from nearby surfels
// - AlbedoTexture: diffuse reflectance needed to convert irradiance into reflected radiance
Texture2D SceneColorTexture : register(t1);
Texture2D SurfelGITexture : register(t2);
SamplerState SurfelGISampler : register(s2);
Texture2D AlbedoTexture : register(t3);
SamplerState AlbedoSampler : register(s3);

cbuffer ApplySurfelGIUniformBuffer : register(b4)
{
    float g_SurfelGIIntensity;
    int g_SceneWidth;
    int g_SceneHeight;
    int g_Padding0;
};

RWTexture2D<float4> OutColorTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadId.xy;
    if (p.x >= g_SceneWidth || p.y >= g_SceneHeight)
        return;

    const float2 uv = (float2(p) + 0.5f) / float2(g_SceneWidth, g_SceneHeight);
    const float3 sceneColor = SceneColorTexture.Load(int3(p, 0)).rgb;
    const float3 surfelIrradiance = SurfelGITexture.SampleLevel(SurfelGISampler, uv, 0).rgb;
    const float3 albedo = AlbedoTexture.SampleLevel(AlbedoSampler, uv, 0).rgb;
    // Lambert diffuse BRDF = albedo / PI, so irradiance becomes outgoing diffuse light through
    // this multiplication. The extra intensity factor is an art/debug control on top.
    const float3 indirectDiffuse = surfelIrradiance * (albedo / PI) * g_SurfelGIIntensity;

    OutColorTexture[p] = float4(sceneColor + indirectDiffuse, 1.0);
}
