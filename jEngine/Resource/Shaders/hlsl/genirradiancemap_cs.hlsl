#include "common.hlsl"
#include "Sphericalmap.hlsl"

Texture2D TexHDR : register(t0);
RWTexture2D<float4> HDRRT : register(u1);

float3 GenerateIrradiance(float3 InNormal)
{
    float3 up = normalize(float3(0.0, 1.0, 0.0));
    if (abs(dot(InNormal, up)) > 0.99)
        up = float3(0.0, 0.0, 1.0);
    float3 right = normalize(cross(up, InNormal));
    up = normalize(cross(InNormal, right));

    float nrSamples = 0.0;

    float3 irradiance = float3(0.0, 0.0, 0.0);
    // float sampleDelta = 0.005f;
    float sampleDelta = 0.01f;
    for (float phi = sampleDelta; phi <= 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = sampleDelta; theta <= 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta)); // tangent space to world
            float3 sampleVec =
                tangentSample.x * right +
                tangentSample.y * up +
                tangentSample.z * InNormal;

            sampleVec = normalize(sampleVec);
            float3 curRGB = TexHDR[int2(GetSphericalMap_TwoMirrorBall(sampleVec) * float2(1000, 1000))].xyz;

            irradiance += curRGB * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * (irradiance * (1.0 / float(nrSamples)));
    return irradiance;
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= 1000 || GlobalInvocationID.y >= 1000)
        return;
    
    float2 Temp = GlobalInvocationID.xy / float2(1000, 1000);
    Temp -= float2(0.5, 0.5);

    if (length(Temp) > 0.5)
        return;

    float3 dir = GetNormalFromTexCoord_TwoMirrorBall(GlobalInvocationID.xy / float2(1000, 1000));
    float3 Color = GenerateIrradiance(dir);
    HDRRT[int2(GlobalInvocationID.xy)] = float4(Color, 0.0);
}
