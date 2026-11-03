#include "SurfelGIClipmapLookup.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint tileSize = max((uint)ComputeCommon.TileSize, 1u);
    const uint2 size = (uint2(ComputeCommon.ScreenSize) + tileSize - 1u) / tileSize;
    if (any(DTid.xy >= size)) return;
    const uint candidateIndex = DTid.y * size.x + DTid.x;
    const jSurfelCandidateGPU candidate = CandidateBuffer[candidateIndex];
    if (candidate.Priority == 0u) return;
    const uint cascade = candidate.Surfel.CascadeIndex;
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, cascade);
    uint cellLinear;
    if (!SurfelGITryWorldCellToLinear(int3(floor(candidate.Surfel.PositionRadius.xyz / cellSize)), cascade,
        ComputeCommon.CascadeClipmapGridDimXPacked, ComputeCommon.CascadeClipmapGridDimYPacked, ComputeCommon.CascadeClipmapGridDimZPacked,
        ComputeCommon.CascadeOriginCellXPacked, ComputeCommon.CascadeOriginCellYPacked, ComputeCommon.CascadeOriginCellZPacked,
        ComputeCommon.CascadeRingOffsetXPacked, ComputeCommon.CascadeRingOffsetYPacked, ComputeCommon.CascadeRingOffsetZPacked,
        ComputeCommon.CascadeCellBasePacked, cellLinear)) return;
    if (cellLinear >= (uint)ComputeCommon.SurfelPageTableCapacity) return;
    if (candidate.Priority == WinnerScoreBuffer[cellLinear])
    {
        uint ignored;
        InterlockedMin(WinnerIndexBuffer[cellLinear], candidateIndex, ignored);
    }
}
