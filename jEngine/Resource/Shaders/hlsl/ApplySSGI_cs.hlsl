#include "Common.hlsl"

Texture2D SceneColorTexture : register(t1);
Texture2D SSGITexture : register(t2);
cbuffer ApplySSGIUniformBuffer : register(b3)
{
    float g_SSGIIntensity;
    float3 g_Padding;
};
RWTexture2D<float4> OutColorTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;
    float4 sceneColor = SceneColorTexture.Load(int3(p, 0));
    float4 ssgiColor = SSGITexture.Load(int3(p, 0));

    OutColorTexture[p] = sceneColor + ssgiColor * g_SSGIIntensity;
}
