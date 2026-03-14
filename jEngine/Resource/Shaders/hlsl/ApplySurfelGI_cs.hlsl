#include "Common.hlsl"

// Final SurfelGI shading pass.
// Input texture meaning:
// - SceneColorTexture: current shaded color before SurfelGI diffuse indirect is added
// - SurfelGITexture: screen-space irradiance resolved from nearby surfels
// - AlbedoTexture: diffuse reflectance needed to convert irradiance into reflected radiance
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadId.xy;
    if (p.x >= ApplySurfelGIUniformBuffer.SceneWidth || p.y >= ApplySurfelGIUniformBuffer.SceneHeight)
        return;

    const float2 uv = (float2(p) + 0.5f) / float2(ApplySurfelGIUniformBuffer.SceneWidth, ApplySurfelGIUniformBuffer.SceneHeight);
    const float3 sceneColor = SceneColorTexture.Load(int3(p, 0)).rgb;
    const float3 surfelIrradiance = SurfelGITexture.SampleLevel(SurfelGITextureSampler, uv, 0).rgb;
    const float3 albedo = AlbedoTexture.SampleLevel(AlbedoTextureSampler, uv, 0).rgb;
    // Lambert diffuse BRDF = albedo / PI, so irradiance becomes outgoing diffuse light through
    // this multiplication. The extra intensity factor is an art/debug control on top.
    const float3 indirectDiffuse = surfelIrradiance * (albedo / PI) * ApplySurfelGIUniformBuffer.SurfelGIIntensity;

    OutColorTexture[p] = float4(sceneColor + indirectDiffuse, 1.0);
}
