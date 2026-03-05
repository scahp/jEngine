#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#define SURFEL_GI_ENABLE_OVERFLOW 0

struct CommonComputeUniformBuffer
{
    float4x4 InvP;
    float4x4 V;
    float4x4 InvV;
    float2 ScreenSize;
    float MergeDistanceScale;
    float NormalThreshold;
    float DepthEdgeScale;
    float NormalEdgeScale;
    int UseCenterSpawnBias;
    float NearKeepRadius;
    float NearSpawnBias;
    float FrustumInteriorScale;
    float FarNearFactorThreshold;
    float FarMaxDistanceMultiplier;
    float ReplaceNearDelta;
    float StaleAgeDivisor;
    float MinRadius;
    float MaxDistance;
    int FrameNumber;
    int TileSize;
    int MaxSurfels;
    int SurfelPageSize;
    int SurfelPageTableCapacity;
    int SpawnBudget;
    int TTLInFrames;
    float GridCellSize;
    float4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeStartDistancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRadiusScalePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    int SpawnHysteresisFrames;
    int DeleteHysteresisFrames;
    float RadiusScale;
    float FaceMarginRadiusScale;
    float4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeCellCountPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeDeltaCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeDeltaCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeDeltaCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClearAllPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
};

struct SurfelData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

struct VisibleCellEntry
{
    int4 CellCascade;
};

struct VisibleCellCounter
{
    uint Count;
    uint3 Padding;
};

StructuredBuffer<VisibleCellEntry> VisibleCellWorklist : register(t0, space0);
StructuredBuffer<VisibleCellCounter> VisibleCellCounterBuffer : register(t1, space0);
RWStructuredBuffer<SurfelData> SurfelPool : register(u3, space0);
StructuredBuffer<uint> SurfelCellPageTable : register(t4, space0);

cbuffer ComputeCommon : register(b2, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

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

uint GetSlotsPerCell(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint maxSlotsPerCell = min(max((uint)ComputeCommon.SurfelPageSize, 1u), 5u);
    const uint clampedDesired = clamp(desiredSlotsPerCell, 1u, maxSlotsPerCell);
    return min(max(1u, maxSurfels), clampedDesired);
}

uint GetCellCount(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    return max(1u, maxSurfels / slotsPerCell);
}

int PositiveModulo(int value, int divisor)
{
    const int m = value % divisor;
    return (m < 0) ? (m + divisor) : m;
}

uint GetCascadePartitionOffset(uint maxSurfels, uint cascadeIndex)
{
    const uint cascadeCount = (uint)SURFEL_GI_CASCADE_COUNT;
    const uint c = min(cascadeIndex, cascadeCount);
    const uint base = maxSurfels / cascadeCount;
    const uint rem = maxSurfels % cascadeCount;
    return base * c + min(c, rem);
}

uint GetCascadePartitionCapacity(uint maxSurfels, uint cascadeIndex)
{
    const uint cascadeCount = (uint)SURFEL_GI_CASCADE_COUNT;
    const uint c = min(cascadeIndex, cascadeCount - 1u);
    const uint base = maxSurfels / cascadeCount;
    const uint rem = maxSurfels % cascadeCount;
    return max(1u, base + ((c < rem) ? 1u : 0u));
}

uint GetDesiredSlotsPerCell(uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packed = ComputeCommon.SurfelsPerCellPacked[packIndex];
    const float value = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
    return max((uint)round(value), 1u);
}

uint HashCellWithCascade(int3 cellCoord, uint cascadeIndex)
{
    uint h = Hash3(cellCoord);
    h ^= HashU32(cascadeIndex * 0x9e3779b9u);
    return HashU32(h);
}

int3 GetClipmapGridDim(uint cellCount, uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packedX = ComputeCommon.CascadeClipmapGridDimXPacked[packIndex];
    const float4 packedY = ComputeCommon.CascadeClipmapGridDimYPacked[packIndex];
    const float4 packedZ = ComputeCommon.CascadeClipmapGridDimZPacked[packIndex];
    const float dimXf = (lane == 0u) ? packedX.x : ((lane == 1u) ? packedX.y : ((lane == 2u) ? packedX.z : packedX.w));
    const float dimYf = (lane == 0u) ? packedY.x : ((lane == 1u) ? packedY.y : ((lane == 2u) ? packedY.z : packedY.w));
    const float dimZf = (lane == 0u) ? packedZ.x : ((lane == 1u) ? packedZ.y : ((lane == 2u) ? packedZ.z : packedZ.w));
    uint dimX = max((uint)round(dimXf), 1u);
    uint dimY = max((uint)round(dimYf), 1u);
    uint dimZ = max((uint)round(dimZf), 1u);
    const uint capacity = max(1u, dimX * dimY * dimZ);
    if (capacity < cellCount)
    {
        const uint xy = max(1u, dimX * dimY);
        dimZ = max(dimZ, (cellCount + xy - 1u) / xy);
    }
    return int3((int)dimX, (int)dimY, (int)dimZ);
}

float GetCascadeScale(uint cascadeIndex);

uint GetClipmapLocalLinearIndex(int3 cellCoord, uint maxSurfels, uint desiredSlotsPerCell, uint cascadeIndex)
{
    const uint cascadeCapacity = GetCascadePartitionCapacity(maxSurfels, cascadeIndex);
    const uint cellCount = GetCellCount(cascadeCapacity, desiredSlotsPerCell);
    const int3 gridDim = GetClipmapGridDim(cellCount, cascadeIndex);
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * GetCascadeScale(cascadeIndex);
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
    const int dimX = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimXPacked, cascadeIndex), 1);
    const int dimY = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimYPacked, cascadeIndex), 1);
    const int dimZ = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimZPacked, cascadeIndex), 1);
    return int3(dimX, dimY, dimZ);
}

int3 GetCascadeOriginCell(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeOriginCellXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeOriginCellYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeOriginCellZPacked, cascadeIndex));
}

int3 GetCascadeRingOffset(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeRingOffsetXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeRingOffsetYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeRingOffsetZPacked, cascadeIndex));
}

uint GetCascadeCellBase(uint cascadeIndex)
{
    return GetPackedUint(ComputeCommon.CascadeCellBasePacked, cascadeIndex);
}

bool TryWorldCellToLinear(int3 worldCell, uint cascadeIndex, out uint outCellLinear)
{
    const int3 dim = GetCascadeDim(cascadeIndex);
    const int3 local = worldCell - GetCascadeOriginCell(cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    const int3 phys = ModWrap3(local + GetCascadeRingOffset(cascadeIndex), dim);
    const uint localLinear = (uint)(phys.x + dim.x * (phys.y + dim.y * phys.z));
    outCellLinear = GetCascadeCellBase(cascadeIndex) + localLinear;
    return true;
}

bool TryGetCellBaseIndex(int3 cellCoord, uint maxSurfels, uint cascadeIndex, out uint outCellBaseIndex)
{
    uint cellLinear = 0u;
    if (!TryWorldCellToLinear(cellCoord, cascadeIndex, cellLinear))
        return false;
    if (cellLinear >= max((uint)ComputeCommon.SurfelPageTableCapacity, 1u))
        return false;

    const uint pageSize = GetPageSize(maxSurfels);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return false;
    const uint base = cellLinear * pageSize;
    if (base >= maxSurfels)
        return false;
    outCellBaseIndex = base;
    return true;
}

uint GetCascadeBucketCount(uint maxSurfels, uint cascadeIndex)
{
    const uint desiredSlotsPerCell = GetDesiredSlotsPerCell(cascadeIndex);
    const uint cascadeCapacity = GetCascadePartitionCapacity(maxSurfels, cascadeIndex);
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
    const uint localBucketCount = GetCellCount(GetCascadePartitionCapacity(maxSurfels, cascadeIndex), desiredSlotsPerCell);
    const uint localLinearIndex = GetClipmapLocalLinearIndex(cellCoord, maxSurfels, desiredSlotsPerCell, cascadeIndex);
    return GetCascadeBucketOffset(maxSurfels, cascadeIndex) + (localLinearIndex % localBucketCount);
}

float GetCascadeScale(uint cascadeIndex)
{
    float scale = 1.0;
    [loop] for (uint i = 1u; i <= cascadeIndex && i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const uint packIndex = i >> 2u;
        const uint lane = i & 3u;
        const float4 packed = ComputeCommon.CascadeCellScaleFromPrevPacked[packIndex];
        const float value = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
        scale *= max(value, 1.0);
    }
    return scale;
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint worklistIndex = GlobalInvocationID.x;
    const uint workCount = VisibleCellCounterBuffer[0].Count;
    if (worklistIndex >= workCount)
        return;

    const VisibleCellEntry entry = VisibleCellWorklist[worklistIndex];
    const int3 cellCoord = entry.CellCascade.xyz;
    const uint cascadeIndex = (uint)clamp(entry.CellCascade.w, 0, SURFEL_GI_CASCADE_COUNT - 1);
    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint desiredSlotsPerCell = GetDesiredSlotsPerCell(cascadeIndex);
    const uint cascadeCapacity = GetCascadePartitionCapacity(maxSurfels, cascadeIndex);
    const uint slotsPerCell = GetSlotsPerCell(cascadeCapacity, desiredSlotsPerCell);
    uint cellBaseIndex = 0u;
    if (!TryGetCellBaseIndex(cellCoord, maxSurfels, cascadeIndex, cellBaseIndex))
        return;
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * GetCascadeScale(cascadeIndex);

    [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
    {
        const uint surfelIndex = cellBaseIndex + slot;
        SurfelData s = SurfelPool[surfelIndex];
        if (s.Extra.y <= 0.5)
            continue;
        if ((uint)round(s.Extra.w) != cascadeIndex)
            continue;

        const int3 surfelCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
        if (any(surfelCellCoord != cellCoord))
            continue;

        s.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
        s.Extra.y = 1.0;
        SurfelPool[surfelIndex] = s;
    }

#if SURFEL_GI_ENABLE_OVERFLOW
    const uint bucketIndex = min(GetCellBucketIndex(cellCoord, maxSurfels, desiredSlotsPerCell, cascadeIndex), maxSurfels - 1u);
    int overflowNode = OverflowHeads[bucketIndex].Head;
    [loop] for (uint iter = 0u; iter < 32u && overflowNode >= 0; ++iter)
    {
        const uint nodeIndex = (uint)overflowNode;
        if (nodeIndex >= maxSurfels)
            break;

        OverflowNode node = OverflowNodes[nodeIndex];
        SurfelData s = node.Surfel;
        if (s.Extra.y > 0.5 && (uint)round(s.Extra.w) == cascadeIndex)
        {
            const int3 surfelCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
            if (all(surfelCellCoord == cellCoord))
            {
                s.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
                s.Extra.y = 1.0;
                node.Surfel = s;
                OverflowNodes[nodeIndex] = node;
            }
        }

        overflowNode = node.Next;
    }
#endif
}
