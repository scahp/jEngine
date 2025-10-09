#include "Common.hlsl"

RWTexture2D<float4> OutTexture : register(u0);
Texture2D InTexture : register(t1);
Texture2D NormalTexture : register(t2);
Texture2D DepthTexture : register(t3);

cbuffer A_TrousUniformBuffer : register(b4)
{
    int g_StepSize;
    float g_Sigma_Color;
    float g_Sigma_Normal;
    float g_Sigma_Depth;
    int g_KernelSize;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;
    uint2 dim;
    OutTexture.GetDimensions(dim.x, dim.y);

    if (p.x >= dim.x || p.y >= dim.y)
        return;

    float4 centerColor = InTexture.Load(int3(p, 0));
    float centerDepth = DepthTexture.Load(int3(p, 0)).r;
    float3 centerNormal = NormalTexture.Load(int3(p, 0)).xyz;

    float totalWeight = 0.0;
    float4 filteredColor = float4(0.0, 0.0, 0.0, 0.0);

    const int k_half = g_KernelSize / 2;

    //[unroll]
    for (int j = -k_half; j <= k_half; ++j)
    {
        //[unroll]
        for (int i = -k_half; i <= k_half; ++i)
        {
            int2 offset = int2(i, j) * g_StepSize;
            int2 samplePos = p + offset;

            if (samplePos.x < 0 || samplePos.x >= dim.x || samplePos.y < 0 || samplePos.y >= dim.y)
                continue;

            float4 sampleColor = InTexture.Load(int3(samplePos, 0));
            float sampleDepth = DepthTexture.Load(int3(samplePos, 0)).r;
            float3 sampleNormal = NormalTexture.Load(int3(samplePos, 0)).xyz;

            // Edge-avoiding weights
            float colorWeight = exp(-max(0, dot(centerColor.rgb - sampleColor.rgb, centerColor.rgb - sampleColor.rgb)) / (g_Sigma_Color * g_Sigma_Color));
            float depthWeight = exp(-abs(centerDepth - sampleDepth) / (g_Sigma_Depth * g_Sigma_Depth));
            float normalWeight = exp(-(1.0 - dot(centerNormal, sampleNormal)) / (g_Sigma_Normal * g_Sigma_Normal));

            float weight = colorWeight * depthWeight * normalWeight;

            filteredColor += sampleColor * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0)
    {
        OutTexture[p] = filteredColor / totalWeight;
    }
    else
    {
        OutTexture[p] = centerColor;
    }
}