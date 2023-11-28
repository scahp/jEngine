// Reference
// https://learnopengl.com/PBR/Lighting

#include "Sphericalmap.hlsl"

float DistributionGGX(float3 InN, float3 InH, float InRoughness)
{
    float a = InRoughness * InRoughness;
    float a2 = a * a;
    float NdotH = max(dot(InN, InH), 0.0f);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float InNdotV, float InRoughness)
{
    float r = (InRoughness + 1.0f);
    float k = (r * r) / 8.0f;
    
    float num = InNdotV;
    float denom = InNdotV * (1.0f - k) + k;

    return num / denom;
}

float GeometrySmith(float3 InN, float3 InV, float3 InL, float InRoughness)
{
    float NdotV = max(dot(InN, InV), 0.0f);
    float NdotL = max(dot(InN, InV), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, InRoughness);
    float ggx1 = GeometrySchlickGGX(NdotL, InRoughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float InCosTheta, float3 InF0)
{
    return InF0 + (1.0f - InF0) * pow(clamp(1.0f - InCosTheta, 0.0f, 1.0f), 5.0f);
}

float3 PBR(float3 InL, float3 InN, float3 InV, float3 InAlbedo, float3 InLightColor, float InDistToLight, float InMetallic, float InRoughness)
{
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, InAlbedo, InMetallic);
    
    float3 H = normalize(InV + InL);
    float attenuation = 1.0f / (InDistToLight * InDistToLight);
    float3 radiance = attenuation * InLightColor;
    
    float NDF = DistributionGGX(InN, H, InRoughness);
    float G = GeometrySmith(InN, InV, InL, InRoughness);
    float3 F = FresnelSchlick(max(dot(H, InV), 0.0f), F0);
    
    float3 kS = F;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= (1.0f - InMetallic);
    
    float3 numerator = NDF * G * F;
    float denominator = 4.0f * max(dot(InN, InV), 0.0f) * max(dot(InN, InL), 0.0f) + 0.0001f;
    float3 specular = numerator / denominator;
    
    float NdotL = max(dot(InN, InL), 0.0f);
    float3 Lo = (kD * InAlbedo / PI + specular) * radiance * NdotL;
    return Lo;
}

// https://learnopengl.com/PBR/IBL/Diffuse-irradiance
// To account for roughness for IBL Diffuse part, because IBL don't have half vector which introduced by roughness.
float3 FresnelSchlickRoughness(float InCosTheta, float3 InF0, float InRoughness)
{
    float3 InvRoughness = float3(1.0f - InRoughness, 1.0f - InRoughness, 1.0f - InRoughness);
    return InF0 + (max(InvRoughness, InF0) - InF0) * pow(clamp(1.0 - InCosTheta, 0.0, 1.0), 5.0);
}

float3 IBL_DiffusePart(float3 InN, float3 InV, float3 InAlbedo, float InMetallic, float InRoughness, Texture2D InIrradianceMap, SamplerState InIrradianceMapSamplerState)
{
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, InAlbedo, InMetallic);

    float3 F = FresnelSchlickRoughness(max(dot(InN, InV), 0.0f), F0, InRoughness);
    float3 kS = F;

    float3 kD = 1.0 - kS;
    float2 uv = GetSphericalMap_TwoMirrorBall(InN);
    uv.y = 1.0 - uv.y;

    float3 irradiance = InIrradianceMap.Sample(InIrradianceMapSamplerState, uv).rgb;
    float3 diffuse = irradiance * InAlbedo;
    float ao = 1;
    float3 ambient = (kD * diffuse) * ao;

    return ambient;
}