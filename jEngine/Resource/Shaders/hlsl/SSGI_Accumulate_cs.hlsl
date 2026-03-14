#include "Common.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 p = dispatchThreadId.xy;
    float4 currentSSGI = InSSGI.Load(int3(p, 0));
    float4 prevSSGIAccum = InPrevSSGIAccum.Load(int3(p, 0));

    float blendFactor = SSGIAccumUniformBuffer.g_SSGIAccumBlendFactor;
    OutSSGIAccum[p] = lerp(currentSSGI, prevSSGIAccum, blendFactor);
}
