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
    float MismatchAgeScale;
    int PageEvictMinAgeFrames;
    int CleanupSliceCount;
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
    float4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
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

struct SurfelCellPageEntry
{
    int4 CellCascade;
    uint State;
    uint3 Padding;
};

struct SurfelGIStats
{
    uint ActiveCount;
    uint DormantCount;
    uint MismatchCount;
    uint TTLRetireCount;
    uint PageGCCount;
    uint PageEvictCount;
    uint Padding0;
    uint Padding1;
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
StructuredBuffer<SurfelCellPageEntry> SurfelCellPageTable : register(t3, space0);
RWStructuredBuffer<SurfelData> SurfelPool : register(u4, space0);
RWStructuredBuffer<SurfelGIStats> SurfelGIStatsBuffer : register(u5, space0);

cbuffer ComputeCommon : register(b6, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

uint GetConsumedAge(float lastSeenFrame)
{
    const float rawAge = abs((float)ComputeCommon.FrameNumber - lastSeenFrame);
    return (uint)rawAge;
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint pageIndex = GlobalInvocationID.x;
    const uint pageCapacity = max((uint)ComputeCommon.SurfelPageTableCapacity, 1u);
    if (pageIndex >= pageCapacity)
        return;

    const uint winnerScore = WinnerScoreBuffer[pageIndex];
    if (winnerScore == 0u)
        return;

    const SurfelCellPageEntry pageEntry = SurfelCellPageTable[pageIndex];
    if (pageEntry.State != 2u)
        return;

    const uint winnerIndex = WinnerIndexBuffer[pageIndex];
    if (winnerIndex == 0xffffffffu)
        return;

    const SurfelCandidate c = CandidateBuffer[winnerIndex];
    if (c.Priority != winnerScore)
        return;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint pageSize = min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
    const uint base = pageIndex * pageSize;
    if (base >= maxSurfels)
        return;

    uint writeIndex = base;
    bool foundInactive = false;
    uint oldestAge = 0u;

    [loop] for (uint i = 0u; i < pageSize; ++i)
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
        const uint age = GetConsumedAge(s.NormalSeenFrame.w);
        if (age > oldestAge)
        {
            oldestAge = age;
            writeIndex = idx;
        }
    }

    SurfelData outSurfel = c.Surfel;
    outSurfel.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
    outSurfel.Extra.y = 1.0;
    SurfelPool[writeIndex] = outSurfel;

    uint oldValue = 0u;
    InterlockedAdd(SurfelGIStatsBuffer[0].ActiveCount, 1u, oldValue);
}
