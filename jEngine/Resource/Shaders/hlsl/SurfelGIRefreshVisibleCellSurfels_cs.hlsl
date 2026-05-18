#include "common.hlsl"
#include "SurfelGIClipmapLookup.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif

uint HashU32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

uint Hash3(int3 v)
{
    uint h = 2166136261u;
    h = (h ^ asuint(v.x)) * 16777619u;
    h = (h ^ asuint(v.y)) * 16777619u;
    h = (h ^ asuint(v.z)) * 16777619u;
    return HashU32(h);
}

uint GetCellCount(uint maxSurfels, uint desiredSlotsPerCell)
{
    return max(1u, maxSurfels / desiredSlotsPerCell);
}

int PositiveModulo(int value, int divisor)
{
    const int m = value % divisor;
    return (m < 0) ? (m + divisor) : m;
}

uint GetCascadePartitionCapacity()
{
    return max((uint)ComputeCommon.CascadePartitionCapacity, 1u);
}

uint HashCellWithCascade(int3 cellCoord, uint cascadeIndex)
{
    uint h = Hash3(cellCoord);
    h ^= HashU32(cascadeIndex * 0x9e3779b9u);
    return HashU32(h);
}

int3 GetClipmapGridDim(uint cellCount, uint cascadeIndex)
{
    int dimX = max((int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeClipmapGridDimXPacked, cascadeIndex)), 1);
    int dimY = max((int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeClipmapGridDimYPacked, cascadeIndex)), 1);
    int dimZ = max((int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeClipmapGridDimZPacked, cascadeIndex)), 1);
    const uint capacity = max(1u, (uint)(dimX * dimY * dimZ));
    if (capacity < cellCount)
    {
        const uint xy = max(1u, (uint)(dimX * dimY));
        dimZ = max(dimZ, (int)((cellCount + xy - 1u) / xy));
    }
    return int3(dimX, dimY, dimZ);
}

uint GetClipmapLocalLinearIndex(int3 cellCoord, uint maxSurfels, uint desiredSlotsPerCell, uint cascadeIndex)
{
    const uint cascadeCapacity = GetCascadePartitionCapacity();
    const uint cellCount = GetCellCount(cascadeCapacity, desiredSlotsPerCell);
    const int3 gridDim = GetClipmapGridDim(cellCount, cascadeIndex);
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, cascadeIndex);
    const float3 cameraWorldPos = mul(ComputeCommon.InvV, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    const int3 cameraCell = int3(floor(cameraWorldPos / cellSize));
    const int3 originCell = cameraCell - (gridDim / 2);
    const int3 local = cellCoord - originCell;
    const int3 wrappedLocal = int3(
        PositiveModulo(local.x, max(gridDim.x, 1)),
        PositiveModulo(local.y, max(gridDim.y, 1)),
        PositiveModulo(local.z, max(gridDim.z, 1)));
    const uint linearIndex = (uint)(wrappedLocal.x + wrappedLocal.y * gridDim.x + wrappedLocal.z * gridDim.x * gridDim.y);
    return (linearIndex < cellCount) ? linearIndex : (linearIndex % cellCount);
}

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
}

bool TryCellLinearToWorldCell(uint cellLinear, out uint outCascadeIndex, out int3 outCellCoord)
{
    return SurfelGITryCellLinearToWorldCell(
        cellLinear,
        ComputeCommon.CascadeClipmapGridDimXPacked,
        ComputeCommon.CascadeClipmapGridDimYPacked,
        ComputeCommon.CascadeClipmapGridDimZPacked,
        ComputeCommon.CascadeOriginCellXPacked,
        ComputeCommon.CascadeOriginCellYPacked,
        ComputeCommon.CascadeOriginCellZPacked,
        ComputeCommon.CascadeRingOffsetXPacked,
        ComputeCommon.CascadeRingOffsetYPacked,
        ComputeCommon.CascadeRingOffsetZPacked,
        ComputeCommon.CascadeCellBasePacked,
        ComputeCommon.CascadeCellCountPacked,
        outCascadeIndex,
        outCellCoord);
}

uint GetCascadeBucketCount(uint maxSurfels, uint cascadeIndex)
{
    const uint desiredSlotsPerCell = SurfelGIGetDesiredSlotsPerCell(ComputeCommon.SurfelsPerCellPacked, cascadeIndex);
    const uint cascadeCapacity = GetCascadePartitionCapacity();
    return GetCellCount(cascadeCapacity, desiredSlotsPerCell);
}

uint GetCascadeBucketOffset(uint maxSurfels, uint cascadeIndex)
{
    uint offset = 0u;
    [loop] for (uint i = 0u; i < cascadeIndex && i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        offset += GetCascadeBucketCount(maxSurfels, i);
    }
    return offset;
}

uint GetCellBucketIndex(int3 cellCoord, uint maxSurfels, uint desiredSlotsPerCell, uint cascadeIndex)
{
    const uint localBucketCount = GetCellCount(GetCascadePartitionCapacity(), desiredSlotsPerCell);
    const uint localLinearIndex = GetClipmapLocalLinearIndex(cellCoord, maxSurfels, desiredSlotsPerCell, cascadeIndex);
    return GetCascadeBucketOffset(maxSurfels, cascadeIndex) + (localLinearIndex % localBucketCount);
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint cellLinear = GlobalInvocationID.x;
    if (cellLinear >= max((uint)ComputeCommon.SurfelPageTableCapacity, 1u))
        return;
    if (VisibleCellCounterBuffer[cellLinear] == 0u)
        return;

    uint cascadeIndex = 0u;
    int3 cellCoord = int3(0, 0, 0);
    if (!TryCellLinearToWorldCell(cellLinear, cascadeIndex, cellCoord))
        return;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint pageSize = GetPageSize(maxSurfels);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return;

    const uint cellBaseIndex = cellLinear * pageSize;
    if (cellBaseIndex >= maxSurfels)
        return;

    const uint desiredSlotsPerCell = min(SurfelGIGetDesiredSlotsPerCell(ComputeCommon.SurfelsPerCellPacked, cascadeIndex), pageSize);
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, cascadeIndex);
    const uint cellHash = HashCellWithCascade(cellCoord, cascadeIndex);

    [loop] for (uint slot = 0u; slot < desiredSlotsPerCell; ++slot)
    {
        const uint surfelIndex = cellBaseIndex + slot;
        jSurfelGPU s = SurfelPool[surfelIndex];
        if (s.CascadeIndex != cascadeIndex)
            continue;

        if (s.IsActive != 0u)
        {
            const int3 surfelCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
            if (any(surfelCellCoord != cellCoord))
                continue;

            s.LastSeenFrame = ComputeCommon.FrameNumber;
            s.IsActive = 1u;
            SurfelPool[surfelIndex] = s;
            continue;
        }

        const bool isDormant = (s.State == SURFEL_GI_SURFEL_STATE_DORMANT);
        if (!isDormant)
            continue;
        const int3 dormantCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
        const bool hashMatch = (s.OwnerCellHash == cellHash);
        const bool posCellMatch = all(dormantCellCoord == cellCoord);
        if (!hashMatch && !posCellMatch)
            continue;

        // Keep dormant slots alive while their owning cell is visible,
        // but do not reactivate them here (placement pass decides that).
        s.LastSeenFrame = ComputeCommon.FrameNumber;
        SurfelPool[surfelIndex] = s;
    }
}
