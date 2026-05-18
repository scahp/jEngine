#include "common.hlsl"

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint cellLinear = DTid.x;
    if (cellLinear >= max((uint)ComputeCommon.SurfelPageTableCapacity, 1u))
        return;

    VisibleCellCounterBuffer[cellLinear] = 0u;
}
