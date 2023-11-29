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

Texture2D EnvMap : register(t0, space0);
SamplerState EnvMapSampler : register(s0, space0);
RWTexture2D<float4> Result : register(u1, space0);
cbuffer MipParam : register(b2, space0) { MipUniformBuffer MipParam; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    if (GlobalInvocationID.x >= MipParam.width || GlobalInvocationID.y >= MipParam.height)
        return;

    float2 Temp = GlobalInvocationID.xy / float2(MipParam.width, MipParam.height);
    Temp -= float2(0.5, 0.5);

    if (length(Temp) >= 0.5)
        return;

    float3 N = GetNormalFromTexCoord_TwoMirrorBall(GlobalInvocationID.xy / float2(MipParam.width, MipParam.height));
    float roughness = (float)MipParam.mip / (float)(MipParam.maxMip - 1);
    float3 PrefilteredColor = PrefilterEnvMap(roughness, N, EnvMap, EnvMapSampler);

    Result[int2(GlobalInvocationID.xy)] = float4(PrefilteredColor, 0.0f);
}
