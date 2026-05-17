#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#define SURFEL_GI_GUIDE_DIM 4
#define SURFEL_GI_GUIDE_LOBE_COUNT (SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM)
#define SURFEL_GI_GUIDE_TOTAL_FLOATS (SURFEL_GI_GUIDE_LOBE_COUNT + SURFEL_GI_GUIDE_DIM)

float GetPackedFloat(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packed = packedArray[packIndex];
    return (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
}

uint GetPackedUint(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return (uint)round(GetPackedFloat(packedArray, cascadeIndex));
}

int GetPackedInt(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return (int)round(GetPackedFloat(packedArray, cascadeIndex));
}

int3 ModWrap3(int3 v, int3 dim)
{
    int3 r = v % dim;
    if (r.x < 0) r.x += dim.x;
    if (r.y < 0) r.y += dim.y;
    if (r.z < 0) r.z += dim.z;
    return r;
}

int3 GetCascadeDim(uint cascadeIndex)
{
    const int x = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimXPacked, cascadeIndex), 1);
    const int y = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimYPacked, cascadeIndex), 1);
    const int z = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimZPacked, cascadeIndex), 1);
    return int3(x, y, z);
}

int3 GetCascadeRing(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeRingOffsetXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeRingOffsetYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeRingOffsetZPacked, cascadeIndex));
}

int3 GetCascadeDelta(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeDeltaCellXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeDeltaCellYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeDeltaCellZPacked, cascadeIndex));
}

bool GetCascadeClearAll(uint cascadeIndex)
{
    return GetPackedFloat(ComputeCommon.CascadeClearAllPacked, cascadeIndex) > 0.5;
}

uint GetCascadeCellBase(uint cascadeIndex)
{
    return GetPackedUint(ComputeCommon.CascadeCellBasePacked, cascadeIndex);
}

uint GetCascadeCellCount(uint cascadeIndex)
{
    return max(GetPackedUint(ComputeCommon.CascadeCellCountPacked, cascadeIndex), 1u);
}

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
}

uint GetCascadeIndexFromCellLinear(uint cellLinear)
{
    [loop] for (uint cascade = 0u; cascade < (uint)SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        const uint base = GetCascadeCellBase(cascade);
        const uint count = GetCascadeCellCount(cascade);
        if (cellLinear >= base && cellLinear < (base + count))
            return cascade;
    }
    return (uint)(SURFEL_GI_CASCADE_COUNT - 1);
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

    const uint cascadeIndex = GetCascadeIndexFromCellLinear(cellLinear);
    const uint cascadeBase = GetCascadeCellBase(cascadeIndex);
    const uint localLinear = cellLinear - cascadeBase;
    const int3 dim = GetCascadeDim(cascadeIndex);
    const uint dimX = (uint)max(dim.x, 1);
    const uint dimY = (uint)max(dim.y, 1);
    const uint cascadeCellCount = GetCascadeCellCount(cascadeIndex);
    if (localLinear >= cascadeCellCount)
        return;

    uint tmp = localLinear;
    int3 phys;
    phys.x = (int)(tmp % dimX);
    tmp /= dimX;
    phys.y = (int)(tmp % dimY);
    phys.z = (int)(tmp / dimY);

    const int3 ring = GetCascadeRing(cascadeIndex);
    const int3 localCell = ModWrap3(phys - ring, dim);
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
