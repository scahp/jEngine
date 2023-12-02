#include "common.hlsl"
#include "Sphericalmap.hlsl"
#include "PBR.hlsl"

struct MipUniformBuffer
{
    int width;
    int height;
    int mip;
    int maxMip;
};

TextureCube<float4> TexHDR : register(t0, space0);
SamplerState TexHDRSamplerState : register(s0, space0);
RWTexture2DArray<float4> Result : register(u1, space0);
cbuffer MipParam : register(b2, space0) { MipUniformBuffer MipParam; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= MipParam.width || GlobalInvocationID.y >= MipParam.height)
        return;

    float3 dir = 0;
    int i = GroupID.z;
    {
        float2 uv = GlobalInvocationID.xy / float2(MipParam.width - 1, MipParam.height - 1);
        uv = uv * 2 - float2(1, 1);
        if (i == 0)                             // +x
            dir = float3(1, uv.y, -uv.x);
        else if (i == 1)                        // -x
            dir = float3(-1, uv.y, uv.x);
        else if (i == 3)                        // +y
            dir = float3(uv.x, 1, -uv.y);
        else if (i == 2)                        // -y
            dir = float3(uv.x, -1, uv.y);
        else if (i == 4)                        // +z
            dir = float3(uv.x, uv.y, 1);
        else if (i == 5)                        // -z
            dir = float3(-uv.x, uv.y, -1);
        dir = normalize(dir);
        dir.y = -dir.y;

        float roughness = (float)MipParam.mip / (float)(MipParam.maxMip - 1);
        float3 PrefilteredColor = PrefilterEnvMap(roughness, dir, TexHDR, TexHDRSamplerState);
        Result[int3(GlobalInvocationID.xy, i)] = float4(PrefilteredColor, 0.0);
    }
}
