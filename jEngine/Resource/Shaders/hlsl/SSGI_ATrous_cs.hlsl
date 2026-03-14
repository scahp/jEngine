#include "Common.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;
    uint2 dim;
    OutTexture.GetDimensions(dim.x, dim.y);

    if (p.x >= dim.x || p.y >= dim.y)
        return;

    // Calculate UV for sampling full resolution GBuffer
    float2 uv = (float2(p) + 0.5) / float2(dim);

    float4 centerColor = InTexture.Load(int3(p, 0));
    float centerDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).r;
    float3 centerNormal = NormalTexture.SampleLevel(NormalTextureSampler, uv, 0).xyz;

    float totalWeight = 0.0;
    float4 filteredColor = float4(0.0, 0.0, 0.0, 0.0);

    const int k_half = A_TrousUniformBuffer.g_KernelSize / 2;

    //[unroll]
    for (int j = -k_half; j <= k_half; ++j)
    {
        //[unroll]
        for (int i = -k_half; i <= k_half; ++i)
        {
            int2 offset = int2(i, j) * A_TrousUniformBuffer.g_StepSize;
            int2 samplePos = p + offset;

            if (samplePos.x < 0 || samplePos.x >= dim.x || samplePos.y < 0 || samplePos.y >= dim.y)
                continue;

            // Calculate UV for this sample position
            float2 sampleUV = (float2(samplePos) + 0.5) / float2(dim);

            float4 sampleColor = InTexture.Load(int3(samplePos, 0));
            float sampleDepth = DepthTexture.SampleLevel(DepthTextureSampler, sampleUV, 0).r;
            float3 sampleNormal = NormalTexture.SampleLevel(NormalTextureSampler, sampleUV, 0).xyz;

            // Edge-avoiding weights
            float colorWeight = exp(-max(0, dot(centerColor.rgb - sampleColor.rgb, centerColor.rgb - sampleColor.rgb)) / (A_TrousUniformBuffer.g_Sigma_Color * A_TrousUniformBuffer.g_Sigma_Color));
            float depthWeight = exp(-abs(centerDepth - sampleDepth) / (A_TrousUniformBuffer.g_Sigma_Depth * A_TrousUniformBuffer.g_Sigma_Depth));
            float normalWeight = exp(-(1.0 - dot(centerNormal, sampleNormal)) / (A_TrousUniformBuffer.g_Sigma_Normal * A_TrousUniformBuffer.g_Sigma_Normal));

            // Enhance edge preservation at higher iterations
            if (A_TrousUniformBuffer.g_StepSize > 4)
            {
                // Use power of 1.5 for gentler edge enhancement
                depthWeight = pow(depthWeight, 1.5);
                normalWeight = pow(normalWeight, 1.5);
            }

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
