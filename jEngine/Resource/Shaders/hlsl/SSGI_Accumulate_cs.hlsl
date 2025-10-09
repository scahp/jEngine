#include "Common.hlsl"

Texture2D<float4> InSSGI : register(t1);
Texture2D<float4> InPrevSSGIAccum : register(t2);

cbuffer SSGIAccumUniformBuffer : register(b3)
{
    int g_Width;
    int g_Height;
    float g_SSGIAccumBlendFactor;
    int g_Padding;
};

RWTexture2D<float4> OutSSGIAccum : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;
    float4 currentSSGI = InSSGI.Load(int3(p, 0));
    float4 prevSSGIAccum = InPrevSSGIAccum.Load(int3(p, 0));

    float blendFactor = g_SSGIAccumBlendFactor;
    OutSSGIAccum[p] = lerp(currentSSGI, prevSSGIAccum, blendFactor);
}
