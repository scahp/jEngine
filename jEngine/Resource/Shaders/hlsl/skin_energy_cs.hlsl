#include "common.hlsl"
// Typed jSkinEnergyBuildParameters are injected here by the shader loader.
#include "Features/skinshading.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    const uint width = uint(SkinEnergyBuild.DimensionsAndRoughness.x);
    const uint terms = uint(SkinEnergyBuild.DimensionsAndRoughness.y);
    if (width != 64 || terms != 80 || any(thread.xy >= width)) return;
    const float normalDotView = (float(thread.x) + 0.5) / float(width);
    const float m = clamp((float(thread.y) + 0.5) / float(width),
        SkinEnergyBuild.DimensionsAndRoughness.z, SkinEnergyBuild.DimensionsAndRoughness.w);
    const float3 normal = float3(0.0, 0.0, 1.0);
    const float3 view = float3(0.0, sqrt(max(1.0 - normalDotView * normalDotView, 0.0)), normalDotView);
    const float skinPi = 3.14159265358979323846;
    float sum = 0.0;
    // Endpoint-inclusive source quadrature, deliberately weighted by 1/80.
    // Fixed 64^2 output and 80^2 terms bound startup to 26,214,400 samples.
    [loop] for (uint i = 0; i < 80; ++i)
    {
        const float phi = float(i) / 79.0 * 2.0 * skinPi;
        float sinePhi, cosinePhi;
        sincos(phi, sinePhi, cosinePhi);
        float localSum = 0.0;
        [loop] for (uint j = 0; j < 80; ++j)
        {
            const float theta = float(j) / 79.0 * skinPi * 0.5;
            float sineTheta, cosineTheta;
            sincos(theta, sineTheta, cosineTheta);
            const float3 light = float3(sinePhi * sineTheta, cosinePhi * sineTheta, cosineTheta);
            localSum += JE_SkinKelemenSpecular(normal, light, view, m, 1.0) * sineTheta;
        }
        sum += localSum * (skinPi * 0.5) / 80.0;
    }
    const float energy = sum * (2.0 * skinPi) / 80.0;
    SkinEnergyResult[thread.xy] = isfinite(energy) ? max(energy, 0.0) : 0.0;
}
