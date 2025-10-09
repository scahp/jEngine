#include "Common.hlsl"

Texture2D InDepthTexture : register(t1);
RWTexture2D<float> OutLinearDepthTexture : register(u0);

struct jLinearDepthUniformBuffer
{
    float4x4 InvP;
    float2 ScreenSize;
    float2 Padding;
};

cbuffer ComputeParam : register(b2)
{
    jLinearDepthUniformBuffer ComputeParam;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;
    if (p.x >= ComputeParam.ScreenSize.x || p.y >= ComputeParam.ScreenSize.y)
        return;

    float depth = InDepthTexture.Load(int3(p, 0)).r;

    // In Reverse Z, far plane is 0.0 and near plane is 1.0
    // To reconstruct world position, we need to unproject
    float2 uv = (p + 0.5) / ComputeParam.ScreenSize;
    uv.y = 1.0 - uv.y;
    
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ComputeParam.InvP, clipPos);
    viewPos /= viewPos.w;

    // viewPos.z is linear depth in view space
    OutLinearDepthTexture[p] = viewPos.z;
}
