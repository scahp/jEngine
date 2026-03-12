#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)

float SurfelGIGetPackedFloat(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packed = packedArray[packIndex];
    return (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
}

int SurfelGIGetPackedInt(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return (int)round(SurfelGIGetPackedFloat(packedArray, cascadeIndex));
}

uint SurfelGIGetPackedUint(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return (uint)round(SurfelGIGetPackedFloat(packedArray, cascadeIndex));
}

float SurfelGIGetCascadeScale(float4 cascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    float scale = 1.0;
    [loop] for (uint i = 1u; i <= cascadeIndex && i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        scale *= max(SurfelGIGetPackedFloat(cascadeCellScaleFromPrevPacked, i), 1.0);
    }
    return scale;
}

float SurfelGIGetCascadeCellSize(float4 cascadeCellSizePacked[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return max(SurfelGIGetPackedFloat(cascadeCellSizePacked, cascadeIndex), 0.1);
}

uint SurfelGIGetCascadeIndexByDistance(float4 cascadeStartDistancePacked[SURFEL_GI_CASCADE_PACKED_COUNT], float cameraDistance)
{
    uint cascade = 0u;
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const float startDistance = max(SurfelGIGetPackedFloat(cascadeStartDistancePacked, i), 0.0);
        if (cameraDistance >= startDistance)
        {
            cascade = i;
        }
    }
    return cascade;
}

int3 SurfelGIModWrap3(int3 value, int3 dim)
{
    int3 result = value % dim;
    if (result.x < 0) result.x += dim.x;
    if (result.y < 0) result.y += dim.y;
    if (result.z < 0) result.z += dim.z;
    return result;
}

int3 SurfelGIGetCascadeDim(
    float4 cascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    uint cascadeIndex)
{
    return int3(
        max(SurfelGIGetPackedInt(cascadeClipmapGridDimXPacked, cascadeIndex), 1),
        max(SurfelGIGetPackedInt(cascadeClipmapGridDimYPacked, cascadeIndex), 1),
        max(SurfelGIGetPackedInt(cascadeClipmapGridDimZPacked, cascadeIndex), 1));
}

int3 SurfelGIGetCascadeOriginCell(
    float4 cascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    uint cascadeIndex)
{
    return int3(
        SurfelGIGetPackedInt(cascadeOriginCellXPacked, cascadeIndex),
        SurfelGIGetPackedInt(cascadeOriginCellYPacked, cascadeIndex),
        SurfelGIGetPackedInt(cascadeOriginCellZPacked, cascadeIndex));
}

int3 SurfelGIGetCascadeRingOffset(
    float4 cascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    uint cascadeIndex)
{
    return int3(
        SurfelGIGetPackedInt(cascadeRingOffsetXPacked, cascadeIndex),
        SurfelGIGetPackedInt(cascadeRingOffsetYPacked, cascadeIndex),
        SurfelGIGetPackedInt(cascadeRingOffsetZPacked, cascadeIndex));
}

uint SurfelGIGetCascadeCellBase(float4 cascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return SurfelGIGetPackedUint(cascadeCellBasePacked, cascadeIndex);
}

uint SurfelGIGetCascadeCellBaseFromDims(
    float4 cascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    uint cascadeIndex)
{
    uint cellBase = 0u;
    [loop] for (uint i = 0u; i < min(cascadeIndex, (uint)SURFEL_GI_CASCADE_COUNT); ++i)
    {
        const int3 dim = SurfelGIGetCascadeDim(
            cascadeClipmapGridDimXPacked,
            cascadeClipmapGridDimYPacked,
            cascadeClipmapGridDimZPacked,
            i);
        cellBase += (uint)(dim.x * dim.y * dim.z);
    }
    return cellBase;
}

bool SurfelGITryWorldCellToLinear(
    int3 worldCell,
    uint cascadeIndex,
    float4 cascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    out uint outCellLinear)
{
    const int3 dim = SurfelGIGetCascadeDim(
        cascadeClipmapGridDimXPacked,
        cascadeClipmapGridDimYPacked,
        cascadeClipmapGridDimZPacked,
        cascadeIndex);
    const int3 local = worldCell - SurfelGIGetCascadeOriginCell(
        cascadeOriginCellXPacked,
        cascadeOriginCellYPacked,
        cascadeOriginCellZPacked,
        cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    const int3 phys = SurfelGIModWrap3(local + SurfelGIGetCascadeRingOffset(
        cascadeRingOffsetXPacked,
        cascadeRingOffsetYPacked,
        cascadeRingOffsetZPacked,
        cascadeIndex), dim);
    const uint localLinear = (uint)(phys.x + dim.x * (phys.y + dim.y * phys.z));
    outCellLinear = SurfelGIGetCascadeCellBase(cascadeCellBasePacked, cascadeIndex) + localLinear;
    return true;
}

bool SurfelGITryWorldCellToLinearFromDims(
    int3 worldCell,
    uint cascadeIndex,
    float4 cascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    out uint outCellLinear)
{
    const int3 dim = SurfelGIGetCascadeDim(
        cascadeClipmapGridDimXPacked,
        cascadeClipmapGridDimYPacked,
        cascadeClipmapGridDimZPacked,
        cascadeIndex);
    const int3 local = worldCell - SurfelGIGetCascadeOriginCell(
        cascadeOriginCellXPacked,
        cascadeOriginCellYPacked,
        cascadeOriginCellZPacked,
        cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    const int3 phys = SurfelGIModWrap3(local + SurfelGIGetCascadeRingOffset(
        cascadeRingOffsetXPacked,
        cascadeRingOffsetYPacked,
        cascadeRingOffsetZPacked,
        cascadeIndex), dim);
    const uint localLinear = (uint)(phys.x + dim.x * (phys.y + dim.y * phys.z));
    outCellLinear = SurfelGIGetCascadeCellBaseFromDims(
        cascadeClipmapGridDimXPacked,
        cascadeClipmapGridDimYPacked,
        cascadeClipmapGridDimZPacked,
        cascadeIndex) + localLinear;
    return true;
}

bool SurfelGITryGetCellBaseIndex(
    int3 cellCoord,
    uint maxSurfels,
    uint pageSize,
    uint pageTableCapacity,
    uint cascadeIndex,
    float4 cascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    out uint outCellBaseIndex)
{
    uint cellLinear = 0u;
    if (!SurfelGITryWorldCellToLinear(
        cellCoord,
        cascadeIndex,
        cascadeClipmapGridDimXPacked,
        cascadeClipmapGridDimYPacked,
        cascadeClipmapGridDimZPacked,
        cascadeOriginCellXPacked,
        cascadeOriginCellYPacked,
        cascadeOriginCellZPacked,
        cascadeRingOffsetXPacked,
        cascadeRingOffsetYPacked,
        cascadeRingOffsetZPacked,
        cascadeCellBasePacked,
        cellLinear))
    {
        return false;
    }

    if (cellLinear >= max(pageTableCapacity, 1u))
        return false;

    const uint safePageSize = min(max(pageSize, 1u), max(maxSurfels, 1u));
    if (cellLinear > ((maxSurfels - 1u) / max(safePageSize, 1u)))
        return false;

    const uint base = cellLinear * safePageSize;
    if (base >= maxSurfels)
        return false;

    outCellBaseIndex = base;
    return true;
}

bool SurfelGITryGetCellBaseIndexFromDims(
    int3 cellCoord,
    uint maxSurfels,
    uint pageSize,
    uint pageTableCapacity,
    uint cascadeIndex,
    float4 cascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    float4 cascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT],
    out uint outCellBaseIndex)
{
    uint cellLinear = 0u;
    if (!SurfelGITryWorldCellToLinearFromDims(
        cellCoord,
        cascadeIndex,
        cascadeClipmapGridDimXPacked,
        cascadeClipmapGridDimYPacked,
        cascadeClipmapGridDimZPacked,
        cascadeOriginCellXPacked,
        cascadeOriginCellYPacked,
        cascadeOriginCellZPacked,
        cascadeRingOffsetXPacked,
        cascadeRingOffsetYPacked,
        cascadeRingOffsetZPacked,
        cellLinear))
    {
        return false;
    }

    if (cellLinear >= max(pageTableCapacity, 1u))
        return false;

    const uint safePageSize = min(max(pageSize, 1u), max(maxSurfels, 1u));
    if (cellLinear > ((maxSurfels - 1u) / max(safePageSize, 1u)))
        return false;

    const uint base = cellLinear * safePageSize;
    if (base >= maxSurfels)
        return false;

    outCellBaseIndex = base;
    return true;
}
