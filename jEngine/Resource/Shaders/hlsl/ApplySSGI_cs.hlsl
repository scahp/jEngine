#include "Common.hlsl"

Texture2D SceneColorTexture : register(t1);
Texture2D SSGITexture : register(t2);
SamplerState SSGISampler : register(s2);
cbuffer ApplySSGIUniformBuffer : register(b3)
{
    float g_SSGIIntensity;
    int g_SceneWidth;
    int g_SceneHeight;
    int g_ShowSSGIOnly;
};
RWTexture2D<float4> OutColorTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;

    if (p.x >= g_SceneWidth || p.y >= g_SceneHeight)
        return;

    float4 sceneColor = SceneColorTexture.Load(int3(p, 0));

    // Calculate UV coordinates for upsampling
    float2 uv = (float2(p) + 0.5f) / float2(g_SceneWidth, g_SceneHeight);

    // Sample SSGI texture with bilinear filtering for upsampling
    float4 ssgiColor = SSGITexture.SampleLevel(SSGISampler, uv, 0);

    if (g_ShowSSGIOnly)
    {
        // Show only SSGI result for debugging
        OutColorTexture[p] = ssgiColor * g_SSGIIntensity;
    }
    else
    {
        // Normal mode: add SSGI to scene color
        OutColorTexture[p] = sceneColor + ssgiColor * g_SSGIIntensity;
    }
}
