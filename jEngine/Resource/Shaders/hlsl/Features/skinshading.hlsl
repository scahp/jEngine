#ifndef JE_SKIN_SHADING_FUNCTIONS
#define JE_SKIN_SHADING_FUNCTIONS

// Guarded Kelemen / Beckmann model adapted from scahp/Shadows Skin51249d:
// skin_final_fs.glsl and skin_hemispherebrdfbacker.glsl (MIT).
// m is Beckmann roughness and rho_s is specular brightness; neither is GGX
// alpha or F0. Standard PBR callers need not include/use this material model.
static const float JE_SKIN_MIN_ROUGHNESS = 0.02;
static const float JE_SKIN_MAX_ROUGHNESS = 1.0;
static const float JE_SKIN_MAX_SPECULAR_WEIGHT = 8.0;
static const float JE_SKIN_F0 = 0.02777778;

bool JE_SkinNormalize(float3 input, out float3 output)
{
    output = 0.0;
    if (!all(isfinite(input))) return false;
    const float scale = max(max(abs(input.x), abs(input.y)), abs(input.z));
    if (!(scale > 0.0)) return false;
    const float3 scaled = input / scale;
    output = scaled * rsqrt(dot(scaled, scaled));
    return all(isfinite(output));
}

float JE_SkinBeckmannPH(float normalDotHalf, float roughness)
{
    if (!isfinite(normalDotHalf) || !isfinite(roughness)) return 0.0;
    const float cosine = saturate(abs(normalDotHalf));
    if (cosine <= 1.e-6) return 0.0;
    const float m = clamp(roughness, JE_SKIN_MIN_ROUGHNESS, JE_SKIN_MAX_ROUGHNESS);
    const float cosine2 = cosine * cosine;
    const float m2 = m * m;
    // Source PH intentionally has no 1/PI normalization.
    return exp(-(1.0 - cosine2) / (cosine2 * m2)) / (m2 * cosine2 * cosine2);
}

float JE_SkinKelemenSpecular(float3 normal, float3 toLight, float3 toEye, float roughness, float specularWeight)
{
    float3 n, l, v;
    if (!isfinite(roughness) || !isfinite(specularWeight) || specularWeight <= 0.0 ||
        !JE_SkinNormalize(normal, n) || !JE_SkinNormalize(toLight, l) || !JE_SkinNormalize(toEye, v)) return 0.0;
    const float normalDotLight = dot(n, l);
    if (normalDotLight <= 0.0) return 0.0;
    const float3 halfVector = l + v;
    const float halfLength2 = dot(halfVector, halfVector);
    if (halfLength2 <= 1.e-12) return 0.0;
    const float3 halfUnit = halfVector * rsqrt(halfLength2);
    const float base = 1.0 - saturate(dot(v, halfUnit));
    const float fresnel = JE_SKIN_F0 + (1.0 - JE_SKIN_F0) * base * base * base * base * base;
    const float result = normalDotLight * min(specularWeight, JE_SKIN_MAX_SPECULAR_WEIGHT) *
        JE_SkinBeckmannPH(dot(n, halfUnit), roughness) * fresnel / halfLength2;
    return isfinite(result) ? max(result, 0.0) : 0.0;
}

float JE_SkinDiffuseEnergy(float specularWeight, float integratedSpecular)
{
    if (!isfinite(specularWeight) || !isfinite(integratedSpecular) || specularWeight < 0.0 ||
        specularWeight > JE_SKIN_MAX_SPECULAR_WEIGHT || integratedSpecular < 0.0) return 1.0;
    return max(1.0 - specularWeight * integratedSpecular, 0.0);
}

// R32F linear filtering is optional on Vulkan/Metal. Keep the explicit sampler
// argument to match other engine helpers, but interpolate four integer loads.
float JE_SkinEnergyLookup(Texture2D<float4> energyTexture, SamplerState energySampler, float normalDotLight, float roughness)
{
    if (!isfinite(normalDotLight) || !isfinite(roughness)) return 0.0;
    uint width, height;
    energyTexture.GetDimensions(width, height);
    if (width == 0 || height == 0) return 0.0;
    const float2 uv = float2(saturate(normalDotLight), clamp(roughness, JE_SKIN_MIN_ROUGHNESS, JE_SKIN_MAX_ROUGHNESS));
    const float2 pixel = clamp(uv * float2(width, height) - 0.5, 0.0, float2(width - 1, height - 1));
    const uint2 first = uint2(floor(pixel));
    const uint2 last = min(first + 1, uint2(width - 1, height - 1));
    const float2 fraction = frac(pixel);
    const float a = energyTexture.Load(int3(first, 0)).r;
    const float b = energyTexture.Load(int3(uint2(last.x, first.y), 0)).r;
    const float c = energyTexture.Load(int3(uint2(first.x, last.y), 0)).r;
    const float d = energyTexture.Load(int3(last, 0)).r;
    const float result = lerp(lerp(a, b, fraction.x), lerp(c, d, fraction.x), fraction.y);
    return isfinite(result) ? max(result, 0.0) : 0.0;
}

#endif
