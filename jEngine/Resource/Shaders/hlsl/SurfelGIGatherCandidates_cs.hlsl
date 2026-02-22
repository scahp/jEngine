#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#ifndef SURFEL_GI_ENABLE_STATE1_RETRY
    // 1: handle State==1 (allocating) with short spin/retry to reduce duplicate page allocations.
    // 0: keep legacy behavior (skip allocating slots and continue probing).
    #define SURFEL_GI_ENABLE_STATE1_RETRY 1
#endif

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
    uint ReservoirOverflowCount;
    uint ReservoirRejectedCount;
};

struct SurfelCandidate
{
    SurfelData Surfel;
    int4 CellCascade;
    uint Priority;
    uint3 Padding;
};

Texture2D DepthTexture : register(t0, space0);
SamplerState DepthTextureSampler : register(s0, space0);
Texture2D GBuffer0 : register(t1, space0);
SamplerState GBuffer0Sampler : register(s1, space0);
Texture2D GBuffer1 : register(t2, space0);
SamplerState GBuffer1Sampler : register(s2, space0);
Texture2D LinearDepthTexture : register(t3, space0);

cbuffer ComputeCommon : register(b4, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

RWStructuredBuffer<SurfelData> SurfelPool : register(u5, space0);
RWTexture2D<float4> DebugOutput : register(u6, space0);
RWTexture2D<float4> AttemptOutput : register(u7, space0);
RWStructuredBuffer<SurfelCellPageEntry> SurfelCellPageTable : register(u8, space0);
RWStructuredBuffer<SurfelGIStats> SurfelGIStatsBuffer : register(u9, space0);
RWStructuredBuffer<SurfelCandidate> CandidateBuffer : register(u10, space0);
RWStructuredBuffer<uint> WinnerScoreBuffer : register(u11, space0);
RWStructuredBuffer<uint> WinnerIndexBuffer : register(u12, space0);
RWStructuredBuffer<uint> WinnerLockBuffer : register(u13, space0);

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

uint HashCellWithCascade(int3 cellCoord, uint cascadeIndex)
{
    uint h = Hash3(cellCoord);
    h ^= HashU32(cascadeIndex * 0x9e3779b9u);
    return HashU32(h);
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

float GetCascadeRadiusScale(uint cascadeIndex)
{
    float scale = 1.0;
    [loop] for (uint i = 1u; i <= cascadeIndex && i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const uint packIndex = i >> 2u;
        const uint lane = i & 3u;
        const float4 packed = ComputeCommon.CascadeRadiusScalePacked[packIndex];
        const float value = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
        scale *= max(value, 0.05);
    }
    return scale;
}

uint GetCascadeIndexByDistance(float cameraDistance)
{
    uint cascade = 0u;
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const uint packIndex = i >> 2u;
        const uint lane = i & 3u;
        const float4 packed = ComputeCommon.CascadeStartDistancePacked[packIndex];
        const float startDistance = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
        if (cameraDistance >= max(startDistance, 0.0))
            cascade = i;
    }
    return cascade;
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

uint GetPageTableCapacity()
{
    return max((uint)ComputeCommon.SurfelPageTableCapacity, 1u);
}

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
}

bool PageEntryMatches(uint pageIndex, int3 cellCoord, uint cascadeIndex)
{
    const SurfelCellPageEntry e = SurfelCellPageTable[pageIndex];
    if (e.State != 2u)
        return false;
    return all(e.CellCascade.xyz == cellCoord) && ((uint)e.CellCascade.w == cascadeIndex);
}

bool TryClaimPageEntry(uint pageIndex, int3 cellCoord, uint cascadeIndex, out uint outPageIndex)
{
    uint claimedPrev = 0u;
    InterlockedCompareExchange(SurfelCellPageTable[pageIndex].State, 0u, 1u, claimedPrev);
    if (claimedPrev == 0u)
    {
        SurfelCellPageTable[pageIndex].CellCascade = int4(cellCoord, (int)cascadeIndex);
#if SURFEL_GI_ENABLE_STATE1_RETRY
        // Publish payload before flipping state to ACTIVE.
        DeviceMemoryBarrier();
#endif
        SurfelCellPageTable[pageIndex].State = 2u;
        outPageIndex = pageIndex;
        return true;
    }

    if (PageEntryMatches(pageIndex, cellCoord, cascadeIndex))
    {
        outPageIndex = pageIndex;
        return true;
    }

    return false;
}

bool TryGetOrCreatePageIndex(int3 cellCoord, uint cascadeIndex, out uint outPageIndex)
{
    const uint capacity = GetPageTableCapacity();
    const uint hash = HashCellWithCascade(cellCoord, cascadeIndex) % capacity;
    [loop] for (uint probe = 0u; probe < capacity; ++probe)
    {
        const uint pageIndex = (hash + probe) % capacity;
        const uint state = SurfelCellPageTable[pageIndex].State;
        if (state == 2u)
        {
            if (PageEntryMatches(pageIndex, cellCoord, cascadeIndex))
            {
                outPageIndex = pageIndex;
                return true;
            }
            continue;
        }

        #if SURFEL_GI_ENABLE_STATE1_RETRY
        if (state == 1u)
        {
            uint resolvedState = 1u;
            [loop] for (uint spin = 0u; spin < 64u; ++spin)
            {
                resolvedState = SurfelCellPageTable[pageIndex].State;
                if (resolvedState != 1u)
                    break;
            }

            if (resolvedState == 2u)
            {
                if (PageEntryMatches(pageIndex, cellCoord, cascadeIndex))
                {
                    outPageIndex = pageIndex;
                    return true;
                }
                continue;
            }

            if (resolvedState == 0u)
            {
                if (TryClaimPageEntry(pageIndex, cellCoord, cascadeIndex, outPageIndex))
                    return true;
            }
            continue;
        }
        #endif

        if (state == 0u)
        {
            if (TryClaimPageEntry(pageIndex, cellCoord, cascadeIndex, outPageIndex))
                return true;
        }
    }
    return false;
}

float SampleLinearDepthClamped(int2 pixel, int2 screenSize)
{
    int2 clampedPixel = clamp(pixel, int2(0, 0), screenSize - 1);
    return LinearDepthTexture.Load(int3(clampedPixel, 0)).x;
}

float ComputeOverlapFaceCount(float overlapPenalty, float candidateRadius)
{
    return (overlapPenalty <= 0.0001) ? 1.0 : saturate(1.0 - overlapPenalty / max(candidateRadius, 0.001));
}

float ComputeNonOverlapScoreNeighbor27(uint activeNeighborCount, float minSeparationNorm)
{
    return (activeNeighborCount == 0u) ? 1.0 : saturate(minSeparationNorm * 0.5);
}

float ComputeCenterProximityScore(float3 worldPos, int3 cellCoord, float cellSize, int useCenterSpawnBias)
{
    const float3 cellCenter = (float3(cellCoord) + 0.5) * cellSize;
    const float centerDistance = distance(worldPos, cellCenter) / max(cellSize * 0.8660254, 0.001);
    return (useCenterSpawnBias != 0) ? (1.0 - saturate(centerDistance)) : 1.0;
}

float ComposeReservoirPriority(float nonOverlapNeighborScore, float overlapFaceScore)
{
    // Strict non-overlap mode: only candidates without measurable overlap survive.
    const bool isStrictNonOverlapCandidate = (overlapFaceScore >= 0.999);
    const float noOverlapGate = isStrictNonOverlapCandidate ? 1.0 : 0.0;
    return saturate(noOverlapGate * nonOverlapNeighborScore);
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const int2 pixel = int2(GlobalInvocationID.xy);
    const int2 screenSize = int2(ComputeCommon.ScreenSize);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    const uint candidateIndex = (uint)(pixel.y * screenSize.x + pixel.x);
    AttemptOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
    DebugOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
        return;

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, ComputeCommon.InvP);
    const float3 worldPos = mul(ComputeCommon.InvV, float4(viewPos, 1.0)).xyz;
    const float3 worldNormal = normalize(GBuffer0.SampleLevel(GBuffer0Sampler, uv, 0).xyz * 2.0 - 1.0);
    const float3 albedo = GBuffer1.SampleLevel(GBuffer1Sampler, uv, 0).xyz;

    const float linearDepth = SampleLinearDepthClamped(pixel, screenSize);
    const float linearDepthRight = SampleLinearDepthClamped(pixel + int2(1, 0), screenSize);
    const float linearDepthDown = SampleLinearDepthClamped(pixel + int2(0, 1), screenSize);
    const float3 normalRight = normalize(GBuffer0.SampleLevel(GBuffer0Sampler, (float2(pixel + int2(1, 0)) + 0.5) / float2(screenSize), 0).xyz * 2.0 - 1.0);
    const float3 normalDown = normalize(GBuffer0.SampleLevel(GBuffer0Sampler, (float2(pixel + int2(0, 1)) + 0.5) / float2(screenSize), 0).xyz * 2.0 - 1.0);

    const float depthEdge = abs(linearDepthRight - linearDepth) + abs(linearDepthDown - linearDepth);
    const float normalEdge = 1.0 - saturate(0.5 * (dot(worldNormal, normalRight) + dot(worldNormal, normalDown)));
    const float complexity = saturate(depthEdge * ComputeCommon.DepthEdgeScale + normalEdge * ComputeCommon.NormalEdgeScale);
    const float nearFactor = saturate(1.0 - abs(viewPos.z) / max(ComputeCommon.MaxDistance, 0.001));
    const float spawnProb = saturate((0.08 + nearFactor * 0.42 + complexity * 0.55) * saturate((float)ComputeCommon.SpawnBudget / max((ComputeCommon.ScreenSize.x * ComputeCommon.ScreenSize.y) * 0.2, 1.0)));
    const uint pixelHash = HashU32((uint)pixel.x * 1973u ^ (uint)pixel.y * 9277u ^ (uint)ComputeCommon.FrameNumber * 26699u);
    if (((float)(pixelHash & 1023u) / 1023.0) > spawnProb)
        return;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const float cascade0CellSize = max(ComputeCommon.GridCellSize, 0.1);
    const uint cascadeIndex = GetCascadeIndexByDistance(length(viewPos));
    const float cellSize = cascade0CellSize * GetCascadeScale(cascadeIndex);
    const float radius = max(ComputeCommon.MinRadius, 0.001) * max(ComputeCommon.RadiusScale, 0.05) * GetCascadeRadiusScale(cascadeIndex);
    const int3 cellCoord = int3(floor(worldPos / cellSize));

    uint pageIndex = 0u;
    if (!TryGetOrCreatePageIndex(cellCoord, cascadeIndex, pageIndex))
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }

    const uint pageSize = GetPageSize(maxSurfels);
    const uint desiredSlots = min(GetDesiredSlotsPerCell(cascadeIndex), pageSize);
    const uint base = pageIndex * pageSize;
    if (base >= maxSurfels)
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }

    float overlapPenalty = 0.0;
    float minSeparationNorm = 1e9;
    uint activeNeighborCount = 0u;
    [loop] for (uint i = 0u; i < desiredSlots; ++i)
    {
        const uint idx = base + i;
        if (idx >= maxSurfels)
            break;
        const SurfelData s = SurfelPool[idx];
        if (s.Extra.y <= 0.5)
            continue;
        if ((uint)round(s.Extra.w) != cascadeIndex)
            continue;
        const float d = distance(worldPos, s.PositionRadius.xyz);
        const float pairRadius = radius + max(s.PositionRadius.w, 0.001);
        overlapPenalty += max(pairRadius - d, 0.0);
        minSeparationNorm = min(minSeparationNorm, d / max(pairRadius, 0.001));
        activeNeighborCount++;
    }

    const float centerPriority = ComputeCenterProximityScore(worldPos, cellCoord, cellSize, ComputeCommon.UseCenterSpawnBias);
    const float3 cellLocal = frac(worldPos / max(cellSize, 0.001));
    // Margin is a radius-relative band: radius * FaceMarginRadiusScale.
    const float faceMargin = radius * max(ComputeCommon.FaceMarginRadiusScale, 0.0);
    const float distToNegX = cellLocal.x * cellSize;
    const float distToPosX = (1.0 - cellLocal.x) * cellSize;
    const float distToNegY = cellLocal.y * cellSize;
    const float distToPosY = (1.0 - cellLocal.y) * cellSize;
    const float distToNegZ = cellLocal.z * cellSize;
    const float distToPosZ = (1.0 - cellLocal.z) * cellSize;
    uint facesInsideMargin = 0u;
    facesInsideMargin += (distToNegX < faceMargin) ? 1u : 0u;
    facesInsideMargin += (distToPosX < faceMargin) ? 1u : 0u;
    facesInsideMargin += (distToNegY < faceMargin) ? 1u : 0u;
    facesInsideMargin += (distToPosY < faceMargin) ? 1u : 0u;
    facesInsideMargin += (distToNegZ < faceMargin) ? 1u : 0u;
    facesInsideMargin += (distToPosZ < faceMargin) ? 1u : 0u;

    // Allow one placement face to violate the margin, but reject edge/corner cases
    // where candidate gets too close to multiple faces at once.
    if (facesInsideMargin > 1u)
    {
        uint prevRejected = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirRejectedCount, 1u, prevRejected);
        return;
    }
    const float separationPriority = ComputeNonOverlapScoreNeighbor27(activeNeighborCount, minSeparationNorm);
    const float overlapFaceScore = ComputeOverlapFaceCount(overlapPenalty, radius);

    const bool isFirstPlacement = (activeNeighborCount == 0u);
    float finalPriority = 0.0;
    if (isFirstPlacement)
    {
        // First placement path: prefer candidates with less than 2 overlapped faces, then center proximity.
        const float overlapFaceCount = (1.0 - overlapFaceScore) * 6.0;
        const bool exceedsFirstPlacementOverlapFaceLimit = (overlapFaceCount >= 2.0);
        if (exceedsFirstPlacementOverlapFaceLimit)
        {
            uint prevRejected = 0u;
            InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirRejectedCount, 1u, prevRejected);
            return;
        }
        finalPriority = centerPriority;
    }
    else
    {
        finalPriority = ComposeReservoirPriority(separationPriority, overlapFaceScore);
    }

    const uint priority = (uint)(finalPriority * 16777215.0);
    if (priority == 0u)
    {
        uint prevRejected = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirRejectedCount, 1u, prevRejected);
        return;
    }

    SurfelCandidate c;
    c.Surfel.PositionRadius = float4(worldPos, radius);
    c.Surfel.NormalSeenFrame = float4(worldNormal, (float)ComputeCommon.FrameNumber);
    c.Surfel.AlbedoWeight = float4(albedo, 1.0);
    c.Surfel.Extra = float4(0.0, 1.0, 0.0, (float)cascadeIndex);
    c.CellCascade = int4(cellCoord, (int)cascadeIndex);
    c.Priority = priority;
    c.Padding = uint3(0u, 0u, 0u);
    CandidateBuffer[candidateIndex] = c;

    uint prevWinnerScore = 0u;
    InterlockedMax(WinnerScoreBuffer[pageIndex], priority, prevWinnerScore);
    if (priority < prevWinnerScore)
        return;

    [loop] for (uint spin = 0u; spin < 256u; ++spin)
    {
        uint prevLock = 0u;
        InterlockedCompareExchange(WinnerLockBuffer[pageIndex], 0u, 1u, prevLock);
        if (prevLock != 0u)
            continue;

        if (priority >= WinnerScoreBuffer[pageIndex])
        {
            WinnerScoreBuffer[pageIndex] = priority;
            WinnerIndexBuffer[pageIndex] = candidateIndex;
        }
        WinnerLockBuffer[pageIndex] = 0u;
        break;
    }

    AttemptOutput[pixel] = float4(1.0, 1.0, 1.0, 1.0);
    DebugOutput[pixel] = float4(overlapFaceScore, centerPriority, saturate((float)priority / 16777215.0), 1.0);
}
