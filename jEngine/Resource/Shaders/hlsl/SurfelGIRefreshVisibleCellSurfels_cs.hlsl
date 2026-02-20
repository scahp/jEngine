#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)

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
    int SpawnBudget;
    int TTLInFrames;
    float GridCellSize;
    float4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeStartDistancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRadiusScalePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    int SpawnHysteresisFrames;
    int DeleteHysteresisFrames;
    float RadiusScale;
    float4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 OverlapAllowancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
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
    const uint clampedDesired = clamp(desiredSlotsPerCell, 1u, 8u);
    return min(max(1u, maxSurfels), clampedDesired);
}

uint GetCellCount(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    return max(1u, maxSurfels / slotsPerCell);
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

uint GetCellBaseIndex(int3 cellCoord, uint maxSurfels, uint desiredSlotsPerCell, uint cascadeIndex)
{
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    const uint cellCount = GetCellCount(maxSurfels, desiredSlotsPerCell);
    const uint cellHash = HashCellWithCascade(cellCoord, cascadeIndex) % cellCount;
    return cellHash * slotsPerCell;
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
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    const uint cellBaseIndex = GetCellBaseIndex(cellCoord, maxSurfels, desiredSlotsPerCell, cascadeIndex);
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
}
