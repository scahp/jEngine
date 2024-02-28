#include "common.hlsl"

float DistributionGGX(float3 InN, float3 InH, float InRoughness)
{
    float a = InRoughness * InRoughness;
    float a2 = a * a;
    float NdotH = max(dot(InN, InH), 0.0f);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return num / (denom + 0.000001);        // To avoid divide by zero, added 0.000001 to denom
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
    float NdotL = max(dot(InN, InL), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, InRoughness);
    float ggx1 = GeometrySchlickGGX(NdotL, InRoughness);

    return ggx1 * ggx2;
}

float GeometrySmith(float InRoughness, float InNdotV, float InNdotL)
{
    float ggx2 = GeometrySchlickGGX(InNdotV, InRoughness);
    float ggx1 = GeometrySchlickGGX(InNdotL, InRoughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float InCosTheta, float3 InF0)
{
    return InF0 + (1.0f - InF0) * pow(clamp(1.0f - InCosTheta, 0.0f, 1.0f), 5.0f);
}

float3 ImportanceSampleGGX(float2 Xi, float Roughness, float3 N)
{
    float a = Roughness * Roughness;
    float Phi = 2 * PI * Xi.x;
    float CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
    float SinTheta = sqrt(1 - CosTheta * CosTheta);

    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    float3 UpVector = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 TangentX = normalize(cross(UpVector, N));
    float3 TangentY = cross(N, TangentX);

    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

