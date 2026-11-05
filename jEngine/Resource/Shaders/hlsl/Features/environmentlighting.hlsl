#ifndef JENGINE_ENVIRONMENT_LIGHTING_HLSL
#define JENGINE_ENVIRONMENT_LIGHTING_HLSL

// Real orthonormal positive SH basis, matching jEnvironmentLighting. Coefficients
// are linear radiance, not preconvolved irradiance. No resources or bindings live
// in this library, so Forward surfaces and Deferred reconstruction share it.
void EnvironmentSHBasis(float3 direction, out float basis[9])
{
    direction *= rsqrt(max(dot(direction, direction), 1.0e-12));
    const float x = direction.x;
    const float y = direction.y;
    const float z = direction.z;
    basis[0] = 0.2820947918;
    basis[1] = 0.4886025119 * y;
    basis[2] = 0.4886025119 * z;
    basis[3] = 0.4886025119 * x;
    basis[4] = 1.0925484306 * x * y;
    basis[5] = 1.0925484306 * y * z;
    basis[6] = 0.3153915653 * (3.0 * z * z - 1.0);
    basis[7] = 1.0925484306 * x * z;
    basis[8] = 0.5462742153 * (x * x - y * y);
}

float3 EvaluateSHRadiance(float4 radianceSH[9], float3 direction)
{
    float basis[9];
    EnvironmentSHBasis(direction, basis);
    float3 radiance = 0.0;
    [unroll] for (uint i = 0; i < 9; ++i)
        radiance += radianceSH[i].rgb * basis[i];
    return max(0.0, radiance);
}

float3 EvaluateSHDiffuseIrradiance(float4 radianceSH[9], float3 worldNormal)
{
    float basis[9];
    EnvironmentSHBasis(worldNormal, basis);
    // Cosine-kernel convolution: A0=PI, A1=2PI/3, A2=PI/4.
    float3 irradiance = radianceSH[0].rgb * (basis[0] * 3.14159265359);
    [unroll] for (uint i = 1; i < 4; ++i)
        irradiance += radianceSH[i].rgb * (basis[i] * 2.09439510239);
    [unroll] for (uint j = 4; j < 9; ++j)
        irradiance += radianceSH[j].rgb * (basis[j] * 0.785398163397);
    return max(0.0, irradiance);
}

float3 EvaluateSHDiffuse(float4 radianceSH[9], float3 worldNormal,
    float3 linearAlbedo, float metallic, float intensity)
{
    return EvaluateSHDiffuseIrradiance(radianceSH, worldNormal) *
        (max(0.0, linearAlbedo) * (1.0 - saturate(metallic)) * 0.318309886184) * max(0.0, intensity);
}

#endif
