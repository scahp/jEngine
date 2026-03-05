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

struct SurfelGIStats
{
    uint ActiveCount;
    uint DormantCount;
    uint MismatchCount;
    uint TTLRetireCount;
    uint PageGCCount;
    uint PageEvictCount;
    uint ReservoirOverflowCount;
    uint ReservoirRejectedCount;
};

struct SurfelIrradianceData
{
    float4 IrradianceAndWeight;
};

struct SurfelCandidate
{
    SurfelData Surfel;
    int4 CellCascade;
    uint Priority;
    uint3 Padding;
};

StructuredBuffer<SurfelCandidate> CandidateBuffer : register(t0, space0);
StructuredBuffer<uint> WinnerScoreBuffer : register(t1, space0);
StructuredBuffer<uint> WinnerIndexBuffer : register(t2, space0);
StructuredBuffer<uint> SurfelCellPageTable : register(t3, space0);
RWStructuredBuffer<SurfelData> SurfelPool : register(u4, space0);
RWStructuredBuffer<SurfelGIStats> SurfelGIStatsBuffer : register(u5, space0);
RWStructuredBuffer<SurfelIrradianceData> SurfelIrradianceBuffer : register(u7, space0);

cbuffer ComputeCommon : register(b6, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
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

uint GetCascadeCellBase(uint cascadeIndex)
{
    return GetPackedUint(ComputeCommon.CascadeCellBasePacked, cascadeIndex);
}

uint GetCascadeCellCount(uint cascadeIndex)
{
    return max(GetPackedUint(ComputeCommon.CascadeCellCountPacked, cascadeIndex), 1u);
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

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint cellLinear = GlobalInvocationID.x;
    const uint pageCapacity = max((uint)ComputeCommon.SurfelPageTableCapacity, 1u);
    if (cellLinear >= pageCapacity)
        return;

    const uint winnerScore = WinnerScoreBuffer[cellLinear];
    if (winnerScore == 0u)
        return;

    const uint winnerIndex = WinnerIndexBuffer[cellLinear];
    if (winnerIndex == 0xffffffffu)
        return;

    const SurfelCandidate c = CandidateBuffer[winnerIndex];
    if (c.Priority != winnerScore)
        return;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint pageSize = min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
    const uint pageCascade = GetCascadeIndexFromCellLinear(cellLinear);
    const uint desiredSlots = min(GetDesiredSlotsPerCell(pageCascade), pageSize);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return;
    const uint base = cellLinear * pageSize;
    if (base >= maxSurfels)
        return;

    uint writeIndex = base;
    bool foundInactive = false;

    [loop] for (uint i = 0u; i < desiredSlots; ++i)
    {
        const uint idx = base + i;
        if (idx >= maxSurfels)
            break;
        const SurfelData s = SurfelPool[idx];
        if (s.Extra.y <= 0.5)
        {
            writeIndex = idx;
            foundInactive = true;
            break;
        }
    }

    // Reservoir policy: do not replace already active surfels.
    // Place only when an inactive slot exists in the page.
    if (!foundInactive)
        return;

    const SurfelData existing = SurfelPool[writeIndex];
    const bool isDormantReuse = (existing.Extra.y <= 0.5) && (abs(existing.Extra.x - 5.0) < 0.5);
    SurfelData outSurfel;
    if (isDormantReuse)
    {
        // Re-activate dormant surfel in-place and blend attributes toward winner candidate.
        outSurfel = existing;
        const float oldWeight = max(existing.AlbedoWeight.w, 1.0);
        const float newWeight = min(oldWeight + 1.0, 64.0);
        const float blend = 1.0 / newWeight;
        outSurfel.NormalSeenFrame.xyz = normalize(lerp(existing.NormalSeenFrame.xyz, c.Surfel.NormalSeenFrame.xyz, blend));
        outSurfel.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
        outSurfel.AlbedoWeight.xyz = lerp(existing.AlbedoWeight.xyz, c.Surfel.AlbedoWeight.xyz, blend);
        outSurfel.AlbedoWeight.w = newWeight;
        outSurfel.Extra.x = 6.0;      // "revived dormant" for Surfel state debug (blue).
        outSurfel.Extra.y = 1.0;
        outSurfel.Extra.z = 0.0;
        outSurfel.Extra.w = (float)pageCascade;
    }
    else
    {
        outSurfel = c.Surfel;
        outSurfel.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
        outSurfel.Extra.y = 1.0;
    }
    SurfelPool[writeIndex] = outSurfel;

    SurfelIrradianceData outIrradiance;
    outIrradiance.IrradianceAndWeight = float4(0.0, 0.0, 0.0, 0.0);
    SurfelIrradianceBuffer[writeIndex] = outIrradiance;

    uint oldValue = 0u;
    InterlockedAdd(SurfelGIStatsBuffer[0].ActiveCount, 1u, oldValue);
}
