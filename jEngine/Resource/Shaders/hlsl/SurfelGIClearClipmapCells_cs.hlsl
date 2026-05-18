#include "SurfelGIClipmapLookup.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_GUIDE_DIM 4
#define SURFEL_GI_GUIDE_LOBE_COUNT (SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM)
#define SURFEL_GI_GUIDE_TOTAL_FLOATS (SURFEL_GI_GUIDE_LOBE_COUNT + SURFEL_GI_GUIDE_DIM)

int3 GetCascadeDelta(uint cascadeIndex)
{
    return int3(
        (int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeDeltaCellXPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeDeltaCellYPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeDeltaCellZPacked, cascadeIndex)));
}

bool GetCascadeClearAll(uint cascadeIndex)
{
    return SurfelGIGetPackedFloat(ComputeCommon.CascadeClearAllPacked, cascadeIndex) > 0.5;
}

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
}

bool ShouldClearCell(int3 localCell, int3 dim, int3 delta, bool clearAll)
{
    if (clearAll)
        return true;

    bool shouldClear = false;
    if (delta.x > 0) shouldClear = shouldClear || (localCell.x >= (dim.x - delta.x));
    else if (delta.x < 0) shouldClear = shouldClear || (localCell.x < -delta.x);

    if (delta.y > 0) shouldClear = shouldClear || (localCell.y >= (dim.y - delta.y));
    else if (delta.y < 0) shouldClear = shouldClear || (localCell.y < -delta.y);

    if (delta.z > 0) shouldClear = shouldClear || (localCell.z >= (dim.z - delta.z));
    else if (delta.z < 0) shouldClear = shouldClear || (localCell.z < -delta.z);

    return shouldClear;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint cellLinear = DTid.x;
    const uint cellCapacity = max((uint)ComputeCommon.SurfelPageTableCapacity, 1u);
    if (cellLinear >= cellCapacity)
        return;

    const uint cascadeIndex = SurfelGIGetCascadeIndexFromCellLinear(
        ComputeCommon.CascadeCellBasePacked,
        ComputeCommon.CascadeCellCountPacked,
        cellLinear);
    const uint cascadeBase = SurfelGIGetCascadeCellBase(ComputeCommon.CascadeCellBasePacked, cascadeIndex);
    const uint localLinear = cellLinear - cascadeBase;
    const int3 dim = SurfelGIGetCascadeDim(
        ComputeCommon.CascadeClipmapGridDimXPacked,
        ComputeCommon.CascadeClipmapGridDimYPacked,
        ComputeCommon.CascadeClipmapGridDimZPacked,
        cascadeIndex);
    const uint dimX = (uint)max(dim.x, 1);
    const uint dimY = (uint)max(dim.y, 1);
    const uint cascadeCellCount = SurfelGIGetCascadeCellCount(ComputeCommon.CascadeCellCountPacked, cascadeIndex);
    if (localLinear >= cascadeCellCount)
        return;

    uint tmp = localLinear;
    int3 phys;
    phys.x = (int)(tmp % dimX);
    tmp /= dimX;
    phys.y = (int)(tmp % dimY);
    phys.z = (int)(tmp / dimY);

    const int3 ring = SurfelGIGetCascadeRingOffset(
        ComputeCommon.CascadeRingOffsetXPacked,
        ComputeCommon.CascadeRingOffsetYPacked,
        ComputeCommon.CascadeRingOffsetZPacked,
        cascadeIndex);
    const int3 localCell = SurfelGIModWrap3(phys - ring, dim);
    const int3 delta = GetCascadeDelta(cascadeIndex);
    const bool clearAll = GetCascadeClearAll(cascadeIndex);
    if (!ShouldClearCell(localCell, dim, delta, clearAll))
        return;

    CellSurfelCount[cellLinear] = 0u;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint pageSize = GetPageSize(maxSurfels);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return;
    const uint baseSurfel = cellLinear * pageSize;
    if (baseSurfel >= maxSurfels)
        return;

    [loop] for (uint slot = 0u; slot < pageSize; ++slot)
    {
        const uint surfelIndex = baseSurfel + slot;
        if (surfelIndex >= maxSurfels)
            break;

        SurfelPool[surfelIndex] = (jSurfelGPU)0;
        SurfelIrradianceBuffer[surfelIndex] = (jSurfelIrradianceGPU)0;
        const uint guidingBaseIndex = surfelIndex * SURFEL_GI_GUIDE_TOTAL_FLOATS;
        [unroll] for (uint guideIndex = 0u; guideIndex < SURFEL_GI_GUIDE_TOTAL_FLOATS; ++guideIndex)
        {
            SurfelGuidingBuffer[guidingBaseIndex + guideIndex] = 0.0;
        }
    }
}
