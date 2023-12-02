#include "common.hlsl"
#include "Sphericalmap.hlsl"

// IrradianceMap Size
struct RTSizeUniformBuffer
{
    int width;
    int height;
};

TextureCube<float4> TexHDR : register(t0);
SamplerState TexHDRSamplerState : register(s0);
RWTexture2DArray<float4> IrradianceMap : register(u1);
cbuffer RTSizeParam : register(b2) { RTSizeUniformBuffer RTSizeParam; }

float3 GenerateIrradiance(float3 InNormal)
{
    float3 up = normalize(float3(0.0, 1.0, 0.0));
    if (abs(dot(InNormal, up)) > 0.99)
        up = float3(0.0, 0.0, 1.0);
    float3 right = normalize(cross(up, InNormal));
    up = normalize(cross(InNormal, right));

    float nrSamples = 0.0;

    float3 irradiance = float3(0.0, 0.0, 0.0);
    //float sampleDelta = 0.005f;
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
            float3 curRGB = TexHDR.SampleLevel(TexHDRSamplerState, sampleVec, 0).xyz;

            irradiance += curRGB * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * (irradiance * (1.0 / float(nrSamples)));
    return irradiance;
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{   
    if (GlobalInvocationID.x >= RTSizeParam.width || GlobalInvocationID.y >= RTSizeParam.height)
        return;
    
    float3 dir = 0;
    int i = GroupID.z;
    //for (int i = 0; i < 1; ++i)
    {

// We need to invert UV.v, because DirectX's texture UV.v is growing downside but 3D space is growing upside when Y value is increasing
// When the UV.v of DirectX's texture coordinate increase, reading texturegrowing up to downside, but in the 3D space 
#define NeedYFlip 1

        float2 uv = GlobalInvocationID.xy / float2(RTSizeParam.width - 1, RTSizeParam.height - 1);
        uv = uv * 2 - float2(1, 1);
        if (i == 0)                             // +x
            dir = float3(1, uv.y, -uv.x);
        else if (i == 1)                        // -x
            dir = float3(-1, uv.y, uv.x);
#if NeedYFlip
        else if (i == 3)                        // +y
            dir = float3(uv.x, 1, -uv.y);
        else if (i == 2)                        // -y
            dir = float3(uv.x, -1, uv.y);
#else
        else if (i == 2)                        // +y
            dir = float3(uv.x, 1, -uv.y);
        else if (i == 3)                        // -y
            dir = float3(uv.x, -1, uv.y);
#endif // NeedYFlip
        else if (i == 4)                        // +z
            dir = float3(uv.x, uv.y, 1);
        else if (i == 5)                        // -z
            dir = float3(-uv.x, uv.y, -1);
        dir = normalize(dir);

#if NeedYFlip
        dir.y = -dir.y;
#endif

        float3 Color = GenerateIrradiance(dir);
        IrradianceMap[int3(GlobalInvocationID.xy, i)] = float4(Color, 0.0);
    }
}
