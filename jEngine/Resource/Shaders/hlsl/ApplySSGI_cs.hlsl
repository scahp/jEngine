#include "Common.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;

    if (p.x >= ApplySSGIUniformBuffer.g_SceneWidth || p.y >= ApplySSGIUniformBuffer.g_SceneHeight)
        return;

    float4 sceneColor = SceneColorTexture.Load(int3(p, 0));

    // Calculate UV coordinates for upsampling
    float2 uv = (float2(p) + 0.5f) / float2(ApplySSGIUniformBuffer.g_SceneWidth, ApplySSGIUniformBuffer.g_SceneHeight);

    // Sample SSGI texture with bilinear filtering for upsampling
    float4 ssgiColor = SSGITexture.SampleLevel(SSGITextureSampler, uv, 0);

    if (ApplySSGIUniformBuffer.g_ShowSSGIOnly)
    {
        // Show only SSGI result for debugging
        OutColorTexture[p] = ssgiColor * ApplySSGIUniformBuffer.g_SSGIIntensity;
    }
    else
    {
        // Normal mode: add SSGI to scene color
        OutColorTexture[p] = sceneColor + ssgiColor * ApplySSGIUniformBuffer.g_SSGIIntensity;
    }
}
