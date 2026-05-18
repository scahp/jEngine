#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif

static const int SURFEL_GI_SURFEL_STATE_NEW = 0;
static const int SURFEL_GI_SURFEL_STATE_STABLE_KEEP = 1;
static const int SURFEL_GI_SURFEL_STATE_MERGED_MOVED = 2;
static const int SURFEL_GI_SURFEL_STATE_REPLACED_FAR = 3;
static const int SURFEL_GI_SURFEL_STATE_STOLEN_FAR = 4;
static const int SURFEL_GI_SURFEL_STATE_DORMANT = 5;
static const int SURFEL_GI_SURFEL_STATE_REVIVED_DORMANT = 6;
static const int SURFEL_GI_SURFEL_STATE_NORMAL_MISMATCH_REPLACED = 7;

float SurfelGIGetPackedFloat(float4 packedValue, uint cascadeIndex)
{
    return packedValue[min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1))];
}

uint SurfelGIGetDesiredSlotsPerCell(float4 surfelsPerCellPacked, uint cascadeIndex)
{
    return max((uint)round(SurfelGIGetPackedFloat(surfelsPerCellPacked, cascadeIndex)), 1u);
}

float SurfelGIGetCascadeScale(float4 cascadeCellScalePacked, uint cascadeIndex)
{
    return max(SurfelGIGetPackedFloat(cascadeCellScalePacked, cascadeIndex), 1.0);
}

float SurfelGIGetCascadeRadiusScale(float4 cascadeRadiusScalePacked, uint cascadeIndex)
{
    return max(SurfelGIGetPackedFloat(cascadeRadiusScalePacked, cascadeIndex), 0.05);
}

float SurfelGIGetCascadeCellSize(float4 cascadeCellSizePacked, uint cascadeIndex)
{
    return max(SurfelGIGetPackedFloat(cascadeCellSizePacked, cascadeIndex), 0.1);
}

float SurfelGIGetCascadeStartDistance(float4 cascadeStartDistancePacked, uint cascadeIndex)
{
    return max(SurfelGIGetPackedFloat(cascadeStartDistancePacked, cascadeIndex), 0.0);
}

uint SurfelGIGetCascadeIndexByDistance(float4 cascadeStartDistancePacked, float cameraDistance)
{
    uint cascade = 0u;
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const float startDistance = SurfelGIGetCascadeStartDistance(cascadeStartDistancePacked, i);
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
    float4 cascadeClipmapGridDimXPacked,
    float4 cascadeClipmapGridDimYPacked,
    float4 cascadeClipmapGridDimZPacked,
    uint cascadeIndex)
{
    return int3(
        max((int)round(SurfelGIGetPackedFloat(cascadeClipmapGridDimXPacked, cascadeIndex)), 1),
        max((int)round(SurfelGIGetPackedFloat(cascadeClipmapGridDimYPacked, cascadeIndex)), 1),
        max((int)round(SurfelGIGetPackedFloat(cascadeClipmapGridDimZPacked, cascadeIndex)), 1));
}

int3 SurfelGIGetCascadeOriginCell(
    float4 cascadeOriginCellXPacked,
    float4 cascadeOriginCellYPacked,
    float4 cascadeOriginCellZPacked,
    uint cascadeIndex)
{
    return int3(
        (int)round(SurfelGIGetPackedFloat(cascadeOriginCellXPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(cascadeOriginCellYPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(cascadeOriginCellZPacked, cascadeIndex)));
}

int3 SurfelGIGetCascadeRingOffset(
    float4 cascadeRingOffsetXPacked,
    float4 cascadeRingOffsetYPacked,
    float4 cascadeRingOffsetZPacked,
    uint cascadeIndex)
{
    return int3(
        (int)round(SurfelGIGetPackedFloat(cascadeRingOffsetXPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(cascadeRingOffsetYPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(cascadeRingOffsetZPacked, cascadeIndex)));
}

uint SurfelGIGetCascadeCellBase(float4 cascadeCellBasePacked, uint cascadeIndex)
{
    return (uint)round(SurfelGIGetPackedFloat(cascadeCellBasePacked, cascadeIndex));
}

uint SurfelGIGetCascadeCellCount(float4 cascadeCellCountPacked, uint cascadeIndex)
{
    return max((uint)round(SurfelGIGetPackedFloat(cascadeCellCountPacked, cascadeIndex)), 1u);
}

uint SurfelGIGetCascadeIndexFromCellLinear(float4 cascadeCellBasePacked, float4 cascadeCellCountPacked, uint cellLinear)
{
    [loop] for (uint cascade = 0u; cascade < (uint)SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        const uint base = SurfelGIGetCascadeCellBase(cascadeCellBasePacked, cascade);
        const uint count = SurfelGIGetCascadeCellCount(cascadeCellCountPacked, cascade);
        if (cellLinear >= base && cellLinear < (base + count))
            return cascade;
    }
    return (uint)(SURFEL_GI_CASCADE_COUNT - 1);
}

bool SurfelGITryCellLinearToWorldCell(
    uint cellLinear,
    float4 cascadeClipmapGridDimXPacked,
    float4 cascadeClipmapGridDimYPacked,
    float4 cascadeClipmapGridDimZPacked,
    float4 cascadeOriginCellXPacked,
    float4 cascadeOriginCellYPacked,
    float4 cascadeOriginCellZPacked,
    float4 cascadeRingOffsetXPacked,
    float4 cascadeRingOffsetYPacked,
    float4 cascadeRingOffsetZPacked,
    float4 cascadeCellBasePacked,
    float4 cascadeCellCountPacked,
    out uint outCascadeIndex,
    out int3 outCellCoord)
{
    outCascadeIndex = SurfelGIGetCascadeIndexFromCellLinear(cascadeCellBasePacked, cascadeCellCountPacked, cellLinear);

    const uint cascadeBase = SurfelGIGetCascadeCellBase(cascadeCellBasePacked, outCascadeIndex);
    const uint cascadeCellCount = SurfelGIGetCascadeCellCount(cascadeCellCountPacked, outCascadeIndex);
    if (cellLinear < cascadeBase || cellLinear >= cascadeBase + cascadeCellCount)
        return false;

    const uint localLinear = cellLinear - cascadeBase;
    const int3 dim = SurfelGIGetCascadeDim(
        cascadeClipmapGridDimXPacked,
        cascadeClipmapGridDimYPacked,
        cascadeClipmapGridDimZPacked,
        outCascadeIndex);
    const uint dimX = (uint)max(dim.x, 1);
    const uint dimY = (uint)max(dim.y, 1);

    uint tmp = localLinear;
    int3 phys;
    phys.x = (int)(tmp % dimX);
    tmp /= dimX;
    phys.y = (int)(tmp % dimY);
    phys.z = (int)(tmp / dimY);

    const int3 local = SurfelGIModWrap3(phys - SurfelGIGetCascadeRingOffset(
        cascadeRingOffsetXPacked,
        cascadeRingOffsetYPacked,
        cascadeRingOffsetZPacked,
        outCascadeIndex), dim);
    outCellCoord = SurfelGIGetCascadeOriginCell(
        cascadeOriginCellXPacked,
        cascadeOriginCellYPacked,
        cascadeOriginCellZPacked,
        outCascadeIndex) + local;
    return true;
}

bool SurfelGITryWorldCellToLinear(
    int3 worldCell,
    uint cascadeIndex,
    float4 cascadeClipmapGridDimXPacked,
    float4 cascadeClipmapGridDimYPacked,
    float4 cascadeClipmapGridDimZPacked,
    float4 cascadeOriginCellXPacked,
    float4 cascadeOriginCellYPacked,
    float4 cascadeOriginCellZPacked,
    float4 cascadeRingOffsetXPacked,
    float4 cascadeRingOffsetYPacked,
    float4 cascadeRingOffsetZPacked,
    float4 cascadeCellBasePacked,
    out uint outCellLinear)
{
    outCellLinear = 0u;
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

bool SurfelGITryGetCellBaseIndex(
    int3 cellCoord,
    uint maxSurfels,
    uint pageSize,
    uint pageTableCapacity,
    uint cascadeIndex,
    float4 cascadeClipmapGridDimXPacked,
    float4 cascadeClipmapGridDimYPacked,
    float4 cascadeClipmapGridDimZPacked,
    float4 cascadeOriginCellXPacked,
    float4 cascadeOriginCellYPacked,
    float4 cascadeOriginCellZPacked,
    float4 cascadeRingOffsetXPacked,
    float4 cascadeRingOffsetYPacked,
    float4 cascadeRingOffsetZPacked,
    float4 cascadeCellBasePacked,
    out uint outCellBaseIndex)
{
    outCellBaseIndex = 0u;
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
