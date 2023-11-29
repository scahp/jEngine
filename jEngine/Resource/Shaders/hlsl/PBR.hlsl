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
    float NdotL = max(dot(InN, InL), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, InRoughness);
    float ggx1 = GeometrySchlickGGX(NdotL, InRoughness);

    return ggx1 * ggx2;
}

float GeometrySmith(float InRoughness, float InNdotV, float InNdotN)
{
    float ggx2 = GeometrySchlickGGX(InNdotV, InRoughness);
    float ggx1 = GeometrySchlickGGX(InNdotN, InRoughness);

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

/** Reverses all the 32 bits. */
uint ReverseBits32(uint bits)
{
    //#if SM5_PROFILE || COMPILER_METAL
    //	return reversebits( bits );
    //#else
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x00ff00ff) << 8) | ((bits & 0xff00ff00) >> 8);
    bits = ((bits & 0x0f0f0f0f) << 4) | ((bits & 0xf0f0f0f0) >> 4);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xcccccccc) >> 2);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xaaaaaaaa) >> 1);
    return bits;
    //#endif
}

float2 Hammersley(uint Index, uint NumSamples, uint2 Random)
{
    float E1 = frac((float)Index / NumSamples + float(Random.x & 0xffff) / (1 << 16));
    float E2 = float(ReverseBits32(Index) ^ Random.y) * 2.3283064365386963e-10;
    return float2(E1, E2);
}

float3 PrefilterEnvMap(float Roughness, float3 R, Texture2D EnvMap, SamplerState EnvMapSampler)
{
    float3 N = R;
    float3 V = R;
    float3 PrefilteredColor = 0;
    float TotalWeight = 0;
    const uint NumSamples = 1024;

    for (uint i = 0; i < NumSamples; i++)
    {
        float2 Xi = Hammersley(i, NumSamples, 0);
        float3 H = ImportanceSampleGGX(Xi, Roughness, N);
        float3 L = 2 * dot(V, H) * H - V;
        float NoL = saturate(dot(N, L));
        if (NoL > 0)
        {
            float2 uv = GetSphericalMap_TwoMirrorBall(L);
            PrefilteredColor += EnvMap.SampleLevel(EnvMapSampler, uv, 0).rgb * NoL;
            TotalWeight += NoL;
        }
    }
    return PrefilteredColor / TotalWeight;
}

//float G_Smith(float a, float nDotV, float nDotL)
//{
//    return GGX(nDotL, a * a) * GGX(nDotV, a * a);
//}

float2 IntegrateBRDF(float Roughness, float NoV, float3 N) 
{
    float3 V; V.x = sqrt(1.0f - NoV * NoV); // sin 
    V.y = 0; 
    V.z = NoV; // cos 
    float A = 0;
    float B = 0; 
    
    const uint NumSamples = 1024; 
    for(int i = 0; i < NumSamples; i++ ) 
    { 
        float2 Xi = Hammersley( i, NumSamples, 0 ); 
        float3 H = ImportanceSampleGGX( Xi, Roughness, N ); 
        float3 L = 2 * dot( V, H ) * H - V; 
        
        float NoL = saturate( L.z ); 
        float NoH = saturate( H.z ); 
        float VoH = saturate(dot( V, H ) ); 
        
        if( NoL > 0 ) 
        { 
            float G = GeometrySmith( Roughness , NoV, NoL );

            float G_Vis = G * VoH / (NoH * NoV); 
            float Fc = pow( 1 - VoH, 5 ); 
            A += (1 - Fc) * G_Vis; 
            B += Fc * G_Vis; 
        } 
    } 
    
    return float2( A, B ) / NumSamples; 
}

// From UnrealEngine 5.3.2
half2 EnvBRDFApproxLazarov(half Roughness, half NoV)
{
    // [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
    // Adaptation to fit our G term.
    const half4 c0 = { -1, -0.0275, -0.572, 0.022 };
    const half4 c1 = { 1, 0.0425, 1.04, -0.04 };
    half4 r = Roughness * c0 + c1;
    half a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    half2 AB = half2(-1.04, 1.04) * a004 + r.zw;
    return AB;
}

// From UnrealEngine 5.3.2
half3 EnvBRDFApprox(half3 SpecularColor, half Roughness, half NoV)
{
    half2 AB = EnvBRDFApproxLazarov(Roughness, NoV);

    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    // Note: this is needed for the 'specular' show flag to work, since it uses a SpecularColor of 0
    float F90 = saturate(50.0 * SpecularColor.g);

    return SpecularColor * AB.x + F90 * AB.y;
}
