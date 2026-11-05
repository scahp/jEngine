// No resource declarations: production parameter sets define each input ABI.
#ifndef USE_MSAA
#define USE_MSAA 0
#endif
#ifndef USE_SUBSURFACE
#define USE_SUBSURFACE 0
#endif
#ifndef USE_SKIN_MATERIAL
#define USE_SKIN_MATERIAL 0
#endif
#include "Features/skinnormalpacking.hlsl"
#include "Features/skinshading.hlsl"

float3 JE_DeferredShadingNormal(float4 packed)
{
#if USE_SKIN_MATERIAL
    return JE_SkinOctDecode(packed.zw);
#else
    return packed.xyz * 2.0 - 1.0;
#endif
}

#if USE_SKIN_MATERIAL
// This model has its own cosine-weighted Kelemen lobe and energy-corrected
// diffuse. Apply the light's actual radiance to both exactly once.
void JE_DeferredSkin(float3 L, float3 N, float3 V, float3 albedo, float m,
    float4 properties, float3 radiance, out float3 total, out float3 diffuse)
{
    total = 0.0; diffuse = 0.0;
    if (!all(isfinite(radiance)) || !any(radiance > 0.0)) return;
    float3 normalizedN, normalizedL, normalizedV;
    if (!JE_SkinNormalize(N, normalizedN) || !JE_SkinNormalize(L, normalizedL) ||
        !JE_SkinNormalize(V, normalizedV)) return;
    N = normalizedN; L = normalizedL; V = normalizedV;
    const float NoL = saturate(dot(N, L));
    const float rho = clamp(properties.w, 0.0, 8.0);
    const float energy = JE_SkinEnergyLookup(SkinEnergyLUT, SkinEnergyLUTSampler, NoL, m);
    diffuse = albedo * saturate(properties.y) * JE_SkinDiffuseEnergy(rho, energy) * NoL * radiance;
    total = diffuse + JE_SkinKelemenSpecular(N, L, V, m, rho) * radiance;
}
#endif
struct JELightOutput
{
    float4 Color : SV_TARGET0;
#if USE_SUBSURFACE
    float4 Diffuse : SV_TARGET1;
#endif
};
JELightOutput JE_LightOutput(float4 color, float3 diffuse)
{
    JELightOutput result;
    result.Color = color;
#if USE_SUBSURFACE
    // Thickness was seeded exactly once by the geometry pass.
    result.Diffuse = float4(diffuse, 0.0);
#endif
    return result;
}
