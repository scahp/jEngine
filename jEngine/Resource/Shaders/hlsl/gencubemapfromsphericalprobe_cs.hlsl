#include "common.hlsl"
#include "Sphericalmap.hlsl"
#include "PBR.hlsl"

// Mipmap info of Cubemap
struct MipUniformBuffer
{
    int width;
    int height;
    int mip;
    int maxMip;
};

// Spheremap_TwoMirrorBall(From)
Texture2D EnvMap : register(t0, space0);
SamplerState EnvMapSampler : register(s0, space0);

// Cubemap(To)
RWTexture2DArray<float4> Result : register(u1, space0);
cbuffer MipParam : register(b2, space0) { MipUniformBuffer MipParam; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= MipParam.width || GlobalInvocationID.y >= MipParam.height)
        return;

    float2 uv = GlobalInvocationID.xy / float2(MipParam.width - 1, MipParam.height - 1);
    uv = uv * 2 - float2(1, 1);

    uint3 pos;
    pos.x = GlobalInvocationID.x;
    pos.y = GlobalInvocationID.y;

// We need to invert UV.v, because DirectX's texture UV.v is growing downside but 3D space is growing upside when Y value is increasing
// When the UV.v of DirectX's texture coordinate increase, reading texturegrowing up to downside, but in the 3D space 
#define NeedYFlip 1

    int i = GroupID.z;
    {
        float3 dir = 0;
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
        float2 spherical_uv = GetSphericalMap_TwoMirrorBall(dir);
        float4 Color = EnvMap.SampleLevel(EnvMapSampler, spherical_uv, 0);

        pos.z = i;
        Result[pos] = Color;
    }
}
