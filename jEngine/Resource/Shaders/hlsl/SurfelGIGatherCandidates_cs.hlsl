#include "common.hlsl"
#include "SurfelGIClipmapLookup.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_BOUNDARY_BAND_SCALE 1.0
// Temp debug switch: 0 keeps candidate placement on primary cascade only.
#define SURFEL_GI_ENABLE_BOUNDARY_OVERLAP 0
#ifndef SURFEL_GI_ENABLE_STATE1_RETRY
    // 1: handle State==1 (allocating) with short spin/retry to reduce duplicate page allocations.
    // 0: keep legacy behavior (skip allocating slots and continue probing).
    #define SURFEL_GI_ENABLE_STATE1_RETRY 1
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

bool TryGetBoundaryCascadePair(float cameraDistance, out uint outLowCascade, out uint outHighCascade)
{
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const float startDistance = SurfelGIGetCascadeStartDistance(ComputeCommon.CascadeStartDistancePacked, i);
        const float lowerCellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, i - 1u);
        const float upperCellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, i);
        const float boundaryBand = max(max(lowerCellSize, upperCellSize) * SURFEL_GI_BOUNDARY_BAND_SCALE, 1.0);
        if (abs(cameraDistance - startDistance) <= boundaryBand)
        {
            outLowCascade = i - 1u;
            outHighCascade = i;
            return true;
        }
    }

    outLowCascade = 0u;
    outHighCascade = 0u;
    return false;
}

bool TryComputeCellCoordForCascade(float3 worldPos, uint cascadeIndex, out int3 outCellCoord)
{
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, cascadeIndex);
    const int3 cellCoord = int3(floor(worldPos / cellSize));
    const int3 dim = SurfelGIGetCascadeDim(
        ComputeCommon.CascadeClipmapGridDimXPacked,
        ComputeCommon.CascadeClipmapGridDimYPacked,
        ComputeCommon.CascadeClipmapGridDimZPacked,
        cascadeIndex);
    const int3 local = cellCoord - SurfelGIGetCascadeOriginCell(
        ComputeCommon.CascadeOriginCellXPacked,
        ComputeCommon.CascadeOriginCellYPacked,
        ComputeCommon.CascadeOriginCellZPacked,
        cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    outCellCoord = cellCoord;
    return true;
}

bool TrySelectCascadeForWorldPos(float3 worldPos, float cameraDistance, out uint outCascadeIndex, out int3 outCellCoord)
{
    const uint desiredCascade = SurfelGIGetCascadeIndexByDistance(ComputeCommon.CascadeStartDistancePacked, cameraDistance);
    if (TryComputeCellCoordForCascade(worldPos, desiredCascade, outCellCoord))
    {
        outCascadeIndex = desiredCascade;
        return true;
    }

    [loop] for (uint offset = 1u; offset < (uint)SURFEL_GI_CASCADE_COUNT; ++offset)
    {
        const uint highCascade = desiredCascade + offset;
        if (highCascade < (uint)SURFEL_GI_CASCADE_COUNT && TryComputeCellCoordForCascade(worldPos, highCascade, outCellCoord))
        {
            outCascadeIndex = highCascade;
            return true;
        }

        if (desiredCascade >= offset)
        {
            const uint lowCascade = desiredCascade - offset;
            if (TryComputeCellCoordForCascade(worldPos, lowCascade, outCellCoord))
            {
                outCascadeIndex = lowCascade;
                return true;
            }
        }
    }

    outCascadeIndex = desiredCascade;
    outCellCoord = int3(0, 0, 0);
    return false;
}

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
}

bool TryWorldCellToLinear(int3 worldCell, uint cascadeIndex, out uint outCellLinear)
{
    return SurfelGITryWorldCellToLinear(
        worldCell,
        cascadeIndex,
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
        outCellLinear);
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

float ComputeCenterProximityScore(float3 worldPos, int3 cellCoord, float cellSize, int preferCellCenterForFirstPlacement)
{
    const float3 cellCenter = (float3(cellCoord) + 0.5) * cellSize;
    const float centerDistance = distance(worldPos, cellCenter) / max(cellSize * 0.8660254, 0.001);
    return (preferCellCenterForFirstPlacement != 0) ? (1.0 - saturate(centerDistance)) : 1.0;
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

    const int tileSize = max(ComputeCommon.TileSize, 1);
    const int2 dispatchSize = max((screenSize + (tileSize - 1)) / tileSize, int2(1, 1));
    const int2 tileCoord = pixel / tileSize;
    const int2 samplePixel = min(tileCoord * tileSize + int2(tileSize / 2, tileSize / 2), screenSize - 1);

    AttemptOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
    DebugOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);

    if (any(pixel != samplePixel))
        return;

    const uint candidateIndex = (uint)(tileCoord.y * dispatchSize.x + tileCoord.x);

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
    const float sampleCount = max((float)(dispatchSize.x * dispatchSize.y), 1.0);
    const float spawnProb = saturate((0.08 + nearFactor * 0.42 + complexity * 0.55) * saturate((float)ComputeCommon.SpawnBudget / max(sampleCount * 0.2, 1.0)));
    const uint pixelHash = HashU32((uint)pixel.x * 1973u ^ (uint)pixel.y * 9277u ^ ComputeCommon.FrameNumber * 26699u);
    if (((float)(pixelHash & 1023u) / 1023.0) > spawnProb)
        return;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const float cascade0CellSize = max(ComputeCommon.GridCellSize, 0.1);
    const float cameraDistance = length(viewPos);
    uint primaryCascadeIndex = 0u;
    int3 primaryCellCoord = int3(0, 0, 0);
    if (!TrySelectCascadeForWorldPos(worldPos, cameraDistance, primaryCascadeIndex, primaryCellCoord))
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }

    uint cascadeIndex = primaryCascadeIndex;
    int3 cellCoord = primaryCellCoord;
#if SURFEL_GI_ENABLE_BOUNDARY_OVERLAP
    uint boundaryLowCascade = 0u;
    uint boundaryHighCascade = 0u;
    if (TryGetBoundaryCascadePair(cameraDistance, boundaryLowCascade, boundaryHighCascade))
    {
        const uint secondaryCascadeIndex = (primaryCascadeIndex == boundaryLowCascade) ? boundaryHighCascade : boundaryLowCascade;
        int3 secondaryCellCoord = int3(0, 0, 0);
        if (secondaryCascadeIndex != primaryCascadeIndex && TryComputeCellCoordForCascade(worldPos, secondaryCascadeIndex, secondaryCellCoord))
        {
            // Boundary band: distribute spawn between both cascades instead of hard-switching to one.
            const uint boundarySelector = HashU32(pixelHash ^ (ComputeCommon.FrameNumber * 1597334677u));
            if ((boundarySelector & 1u) != 0u)
            {
                cascadeIndex = secondaryCascadeIndex;
                cellCoord = secondaryCellCoord;
            }
        }
    }
#endif
    const float cellSize = cascade0CellSize * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, cascadeIndex);
    const float radius = max(ComputeCommon.MinRadius, 0.001) * max(ComputeCommon.RadiusScale, 0.05) * SurfelGIGetCascadeRadiusScale(ComputeCommon.CascadeRadiusScalePacked, cascadeIndex);

    uint cellLinear = 0u;
    if (!TryWorldCellToLinear(cellCoord, cascadeIndex, cellLinear))
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }
    if (cellLinear >= max((uint)ComputeCommon.SurfelPageTableCapacity, 1u))
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }

    const uint pageSize = GetPageSize(maxSurfels);
    const uint desiredSlots = min(SurfelGIGetDesiredSlotsPerCell(ComputeCommon.SurfelsPerCellPacked, cascadeIndex), pageSize);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }
    const uint base = cellLinear * pageSize;
    if (base >= maxSurfels)
    {
        uint prevOverflow = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirOverflowCount, 1u, prevOverflow);
        return;
    }

    float overlapPenalty = 0.0;
    float minSeparationNorm = 1e9;
    uint activeNeighborCount = 0u;
    float3 neighborNormalSum = float3(0.0, 0.0, 0.0);
    [loop] for (uint i = 0u; i < desiredSlots; ++i)
    {
        const uint idx = base + i;
        if (idx >= maxSurfels)
            break;
        const jSurfelGPU s = SurfelPool[idx];
        if (s.IsActive == 0u)
            continue;
        if (s.CascadeIndex != cascadeIndex)
            continue;
        const float d = distance(worldPos, s.PositionRadius.xyz);
        const float pairRadius = radius + max(s.PositionRadius.w, 0.001);
        overlapPenalty += max(pairRadius - d, 0.0);
        minSeparationNorm = min(minSeparationNorm, d / max(pairRadius, 0.001));
        activeNeighborCount++;
        neighborNormalSum += normalize(s.Normal);
    }

    float cellNormalConsistencyScore = 1.0;
    if (activeNeighborCount > 0u)
    {
        const float n2 = dot(neighborNormalSum, neighborNormalSum);
        if (n2 > 1e-6)
        {
            const float3 cellDominantNormal = neighborNormalSum * rsqrt(n2);
            const float normalDot = dot(cellDominantNormal, worldNormal);
            const float normalThreshold = clamp(ComputeCommon.NormalThreshold, -1.0, 0.999);
            cellNormalConsistencyScore = max(0.1, saturate((normalDot - normalThreshold) / max(1.0 - normalThreshold, 1e-4)));
        }
    }

    const float centerPriority = ComputeCenterProximityScore(worldPos, cellCoord, cellSize, ComputeCommon.PreferCellCenterForFirstPlacement);
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
    // Bias winner selection toward candidates aligned with already placed surfel normals in this cell.
    finalPriority *= cellNormalConsistencyScore;

    const uint priority = (uint)(finalPriority * 16777215.0);
    if (priority == 0u)
    {
        uint prevRejected = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ReservoirRejectedCount, 1u, prevRejected);
        return;
    }

    jSurfelCandidateGPU c;
    c.Surfel.PositionRadius = float4(worldPos, radius);
    c.Surfel.Normal = worldNormal;
    c.Surfel.LastSeenFrame = ComputeCommon.FrameNumber;
    c.Surfel.AlbedoWeight = float4(albedo, 1.0);
    c.Surfel.State = SURFEL_GI_SURFEL_STATE_NEW;
    c.Surfel.IsActive = 1u;
    c.Surfel.OwnerCellHash = 0u;
    c.Surfel.CascadeIndex = cascadeIndex;
    c.Priority = priority;
    c.Padding0 = 0u;
    c.Padding1 = 0u;
    c.Padding2 = 0u;
    CandidateBuffer[candidateIndex] = c;

    uint prevWinnerScore = 0u;
    InterlockedMax(WinnerScoreBuffer[cellLinear], priority, prevWinnerScore);
    if (priority < prevWinnerScore)
        return;

    [loop] for (uint spin = 0u; spin < 256u; ++spin)
    {
        uint prevLock = 0u;
        InterlockedCompareExchange(WinnerLockBuffer[cellLinear], 0u, 1u, prevLock);
        if (prevLock != 0u)
            continue;

        if (priority >= WinnerScoreBuffer[cellLinear])
        {
            WinnerScoreBuffer[cellLinear] = priority;
            WinnerIndexBuffer[cellLinear] = candidateIndex;
        }
        WinnerLockBuffer[cellLinear] = 0u;
        break;
    }

    AttemptOutput[pixel] = float4(1.0, 1.0, 1.0, 1.0);
    DebugOutput[pixel] = float4(overlapFaceScore, centerPriority, saturate((float)priority / 16777215.0), 1.0);
}
