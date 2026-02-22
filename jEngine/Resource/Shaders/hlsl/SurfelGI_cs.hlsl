#include "common.hlsl"

#ifndef SURFEL_GI_AGE_CONSUME_SCALE
    // Compile-time knob: 2.0 means surfel ages are consumed twice as fast.
    #define SURFEL_GI_AGE_CONSUME_SCALE 2.0
#endif

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#define SURFEL_GI_ENABLE_OVERFLOW 0
#define SURFEL_GI_ENABLE_REPLACE_FAR 0
#define SURFEL_GI_ENABLE_STEAL_FAR 0

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

// High-contrast palette for SpawnAttempt visualization.
static const float3 ATTEMPT_COLOR_GATE_PASS                 = float3(1.00, 0.00, 1.00); // magenta
static const float3 ATTEMPT_COLOR_CASCADE_MISMATCH          = float3(0.00, 1.00, 0.00); // lime
static const float3 ATTEMPT_COLOR_MERGED                    = float3(0.00, 0.00, 1.00); // pure blue
static const float3 ATTEMPT_COLOR_DORMANT_REUSED            = float3(1.00, 0.50, 0.00); // orange
static const float3 ATTEMPT_COLOR_HISTORICAL_REUSED         = float3(0.00, 1.00, 0.35); // spring green
static const float3 ATTEMPT_COLOR_HYSTERESIS_WAIT           = float3(0.00, 1.00, 1.00); // cyan
static const float3 ATTEMPT_COLOR_REJECTED_MIN_SEPARATION   = float3(1.00, 1.00, 0.00); // yellow
static const float3 ATTEMPT_COLOR_REJECTED_NO_CREATE_PATH   = float3(1.00, 0.00, 0.00); // red
static const float3 ATTEMPT_COLOR_REJECTED_OCCUPANCY_GATE   = float3(0.00, 0.00, 0.00); // black
static const float3 ATTEMPT_COLOR_REJECTED_NO_WRITABLE_SLOT = float3(0.30, 1.00, 0.00); // neon green
static const float3 ATTEMPT_COLOR_SPAWN_NEW                 = float3(1.00, 1.00, 1.00); // white
static const float3 ATTEMPT_COLOR_REPLACED_FAR              = float3(0.35, 0.35, 0.35); // gray
static const float3 ATTEMPT_COLOR_STEAL_FAR                 = float3(0.00, 0.50, 0.00); // dark green


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
    const uint maxSlotsPerCell = min(max((uint)ComputeCommon.SurfelPageSize, 1u), 10u);
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

bool TryGetOrCreatePageIndex(int3 cellCoord, uint cascadeIndex, bool allowCreate, out uint outPageIndex)
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

        if (state == 0u)
        {
            if (!allowCreate)
                return false;

            uint claimedPrev = 0u;
            InterlockedCompareExchange(SurfelCellPageTable[pageIndex].State, 0u, 1u, claimedPrev);
            if (claimedPrev == 0u)
            {
                SurfelCellPageTable[pageIndex].CellCascade = int4(cellCoord, (int)cascadeIndex);
                SurfelCellPageTable[pageIndex].State = 2u;
                outPageIndex = pageIndex;
                return true;
            }

            if (PageEntryMatches(pageIndex, cellCoord, cascadeIndex))
            {
                outPageIndex = pageIndex;
                return true;
            }
        }
    }

    return false;
}

bool TryGetCellBaseIndex(int3 cellCoord, uint maxSurfels, uint cascadeIndex, bool allowCreate, out uint outCellBaseIndex)
{
    uint pageIndex = 0u;
    if (!TryGetOrCreatePageIndex(cellCoord, cascadeIndex, allowCreate, pageIndex))
        return false;

    const uint pageSize = GetPageSize(maxSurfels);
    const uint base = pageIndex * pageSize;
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

uint GetConsumedAge(float lastSeenFrame)
{
    const float rawAge = abs((float)ComputeCommon.FrameNumber - lastSeenFrame);
    return (uint)(rawAge * SURFEL_GI_AGE_CONSUME_SCALE);
}

uint GetConsumedAgeWithCascadeBias(float lastSeenFrame, uint surfelCascade, uint targetCascade)
{
    const uint baseAge = GetConsumedAge(lastSeenFrame);
    return (surfelCascade == targetCascade) ? baseAge : (baseAge * 2u);
}

bool IsRadiusCompatible(float existingRadius, float candidateRadius)
{
    const float a = max(existingRadius, 0.001);
    const float b = max(candidateRadius, 0.001);
    const float ratio = max(a, b) / min(a, b);
    // Avoid reusing surfels whose radius diverged too much.
    return ratio <= 1.5;
}

bool IsBoundarySurfel(float3 surfelPos, float boundaryBand)
{
    const float3 surfelViewPos = mul(ComputeCommon.V, float4(surfelPos, 1.0)).xyz;
    const float cameraDistance = length(surfelViewPos);
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const uint packIndex = i >> 2u;
        const uint lane = i & 3u;
        const float4 packed = ComputeCommon.CascadeStartDistancePacked[packIndex];
        const float startDistance = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
        if (abs(cameraDistance - startDistance) <= boundaryBand)
            return true;
    }
    return false;
}

bool IsDormantSurfel(SurfelData s)
{
    // Extra.x == 5.0 means "recycle waiting (dormant)".
    return (s.Extra.y <= 0.5) && (abs(s.Extra.x - 5.0) < 0.5);
}

void MarkDormantSurfel(inout SurfelData s, int3 cellCoord, uint cascadeIndex, uint frameNumber)
{
    (void)frameNumber;
    s.Extra.x = 5.0;
    s.Extra.y = 0.0;
    s.Extra.z = asfloat(HashCellWithCascade(cellCoord, cascadeIndex));   // previous cell hash
    s.Extra.w = (float)cascadeIndex;
}

bool IsPositionInsideCellBounds(float3 position, float radius, int3 cellCoord, float cellSize)
{
    const float3 cellMin = float3(cellCoord) * cellSize;
    const float3 cellMax = cellMin + cellSize;
    const float r = max(radius, 0.001);
    return all((position - r) >= cellMin) && all((position + r) <= cellMax);
}

float ComputeHardOverlapPenaltyAtPosition(float3 candidatePos, float candidateRadius, uint ignorePoolIndex, int3 cellCoord, float cellSize, uint maxSurfels, uint desiredSlotsPerCell, uint slotsPerCell, uint cascadeIndex)
{
    float overlapPenalty = 0.0;
    [loop] for (int nz = -1; nz <= 1; ++nz)
    {
        [loop] for (int ny = -1; ny <= 1; ++ny)
        {
            [loop] for (int nx = -1; nx <= 1; ++nx)
            {
                const int3 neighborCell = cellCoord + int3(nx, ny, nz);
                uint neighborBaseIndex = 0u;
                if (!TryGetCellBaseIndex(neighborCell, maxSurfels, cascadeIndex, false, neighborBaseIndex))
                    continue;
                [loop] for (uint neighborSlot = 0u; neighborSlot < slotsPerCell; ++neighborSlot)
                {
                    const uint neighborIndex = neighborBaseIndex + neighborSlot;
                    if (neighborIndex == ignorePoolIndex)
                        continue;

                    const SurfelData neighborSurfel = SurfelPool[neighborIndex];
                    if (neighborSurfel.Extra.y <= 0.5)
                        continue;
                    if ((uint)round(neighborSurfel.Extra.w) != cascadeIndex)
                        continue;

                    const int3 neighborCellCoord = int3(floor(neighborSurfel.PositionRadius.xyz / cellSize));
                    if (any(neighborCellCoord != neighborCell))
                        continue;

                    const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                    const float noOverlapDistance = neighborRadius + max(candidateRadius, 0.001);
                    const float d = distance(candidatePos, neighborSurfel.PositionRadius.xyz);
                    overlapPenalty += max(noOverlapDistance - d, 0.0);
                }

#if SURFEL_GI_ENABLE_OVERFLOW
                const uint neighborBucketIndex = min(GetCellBucketIndex(neighborCell, maxSurfels, desiredSlotsPerCell, cascadeIndex), maxSurfels - 1u);
                int overflowNode = OverflowHeads[neighborBucketIndex].Head;
                [loop] for (uint iter = 0u; iter < 24u && overflowNode >= 0; ++iter)
                {
                    const uint nodeIndex = (uint)overflowNode;
                    if (nodeIndex >= maxSurfels)
                        break;
                    const OverflowNode node = OverflowNodes[nodeIndex];
                    const SurfelData neighborSurfel = node.Surfel;
                    overflowNode = node.Next;

                    if (neighborSurfel.Extra.y <= 0.5)
                        continue;
                    if ((uint)round(neighborSurfel.Extra.w) != cascadeIndex)
                        continue;

                    const int3 neighborCellCoord = int3(floor(neighborSurfel.PositionRadius.xyz / cellSize));
                    if (any(neighborCellCoord != neighborCell))
                        continue;

                    const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                    const float noOverlapDistance = neighborRadius + max(candidateRadius, 0.001);
                    const float d = distance(candidatePos, neighborSurfel.PositionRadius.xyz);
                    overlapPenalty += max(noOverlapDistance - d, 0.0);
                }
#endif
            }
        }
    }
    return overlapPenalty;
}

float SampleLinearDepthClamped(int2 pixel, int2 screenSize)
{
    int2 clampedPixel = clamp(pixel, int2(0, 0), screenSize - 1);
    return LinearDepthTexture.Load(int3(clampedPixel, 0)).x;
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const int2 pixel = int2(GlobalInvocationID.xy);
    const int2 screenSize = int2(ComputeCommon.ScreenSize);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);

    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
    {
        DebugOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        AttemptOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    AttemptOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);

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
    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint deleteHysteresis = max((uint)ComputeCommon.DeleteHysteresisFrames, 1u);
    const uint ttl = max((uint)ComputeCommon.TTLInFrames, 1u);
    // TEMP debug switch (false = normal TTL behavior enabled)
    const bool disableTTLDeactivation = false;
    const float cascade0CellSize = max(ComputeCommon.GridCellSize, 0.1);
    const float cascadeBoundaryBand = max(cascade0CellSize * 0.5, 1.0);

    const uint tileSize = max((uint)ComputeCommon.TileSize, 1u);
    const uint2 tileCoord = uint2(pixel) / tileSize;
    const uint tileHash = HashU32(tileCoord.x * 73856093u ^ tileCoord.y * 19349663u ^ (uint)ComputeCommon.FrameNumber);
    const float tileJitter = (float)(tileHash & 1023u) / 1023.0;
    const float screenPixelCount = max(ComputeCommon.ScreenSize.x * ComputeCommon.ScreenSize.y, 1.0);
    const float budgetNorm = saturate((float)ComputeCommon.SpawnBudget / (screenPixelCount * 0.2));

    // Frustum interior factor: center area gets higher score, screen-edge gets lower score.
    const float2 uvDistToEdge = min(uv, 1.0 - uv);
    const float centerFactor = saturate(min(uvDistToEdge.x, uvDistToEdge.y) * max(ComputeCommon.FrustumInteriorScale, 1.0));
    const float frustumInterior = (ComputeCommon.UseCenterSpawnBias != 0) ? centerFactor : 1.0;
    const float nearThreshold = saturate(ComputeCommon.NearSpawnBias);
    const float nearCameraBoost = saturate((nearFactor - nearThreshold) / max(1.0 - nearThreshold, 0.001));
    const float cameraFrustumBoost = nearCameraBoost * frustumInterior;

    const float spawnProbBase = (0.08 + nearFactor * 0.42 + complexity * 0.55 + tileJitter * 0.1) * budgetNorm;
    const float spawnProbBoost = cameraFrustumBoost * (0.18 + complexity * 0.22);
    const float spawnProb = saturate(spawnProbBase + spawnProbBoost);

    const uint pixelHash = HashU32((uint)pixel.x * 1973u ^ (uint)pixel.y * 9277u ^ (uint)ComputeCommon.FrameNumber * 26699u);
    const float stochasticPick = (float)(pixelHash & 1023u) / 1023.0;
    const bool passesSpawnGate = (stochasticPick <= spawnProb);
    if (passesSpawnGate)
        AttemptOutput[pixel] = float4(ATTEMPT_COLOR_GATE_PASS, 1.0);
    if (stochasticPick > spawnProb)
    {
        DebugOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float cameraDistance = length(viewPos);
    const uint cascadeIndex = GetCascadeIndexByDistance(cameraDistance);
    const float cascadeScale = GetCascadeScale(cascadeIndex);
    const float cellSize = cascade0CellSize * cascadeScale;
    const float cascadeRadiusScale = GetCascadeRadiusScale(cascadeIndex);
    const float separationScale = 1.0;
    float radius = max(ComputeCommon.MinRadius, 0.001) * max(ComputeCommon.RadiusScale, 0.05) * cascadeRadiusScale;
    const uint desiredSlotsPerCell = GetDesiredSlotsPerCell(cascadeIndex);
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    const uint cascadePoolOffset = GetCascadePartitionOffset(maxSurfels, cascadeIndex);
    const uint cascadePoolCount = GetCascadePartitionCapacity(maxSurfels, cascadeIndex);
    const int3 cellCoord = int3(floor(worldPos / cellSize));
    const uint currentCellHash = HashCellWithCascade(cellCoord, cascadeIndex);
    uint cellBaseIndex = 0u;
    if (!TryGetCellBaseIndex(cellCoord, maxSurfels, cascadeIndex, true, cellBaseIndex))
    {
        AttemptOutput[pixel] = float4(ATTEMPT_COLOR_REJECTED_NO_WRITABLE_SLOT, 1.0);
        DebugOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    uint index = cellBaseIndex;
    float3 spawnPosition = worldPos;

    // Reuse policy:
    // only consider inactive(dormant) surfels that belong to the current cell.
    // Active neighbor reuse is intentionally disabled.
    {
        float bestDormantDistance = 1e20;
        uint bestDormantIndex = index;
        bool foundDormantReuse = false;

        [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
        {
            const uint queryIndex = cellBaseIndex + slot;
            SurfelData candidate = SurfelPool[queryIndex];
            if (candidate.Extra.y > 0.5)
                continue;

            if (!IsDormantSurfel(candidate))
                continue;

            const uint dormantCascade = (uint)round(candidate.Extra.w);
            if (dormantCascade != cascadeIndex)
                continue;

            const int3 dormantCellCoord = int3(floor(candidate.PositionRadius.xyz / cellSize));
            if (any(dormantCellCoord != cellCoord))
                continue;

            const float3 dormantNormal = normalize(candidate.NormalSeenFrame.xyz);
            if (dot(dormantNormal, worldNormal) < ComputeCommon.NormalThreshold)
                continue;

            const float dormantRadius = max(candidate.PositionRadius.w, 0.001);
            if (!IsRadiusCompatible(dormantRadius, radius))
                continue;

            const float dormantThreshold = max(dormantRadius, radius) * max(ComputeCommon.MergeDistanceScale, 0.001) * 1.35;
            const float dormantDistance = distance(candidate.PositionRadius.xyz, worldPos);
            if (dormantDistance < dormantThreshold && dormantDistance < bestDormantDistance)
            {
                bestDormantDistance = dormantDistance;
                bestDormantIndex = queryIndex;
                foundDormantReuse = true;
            }
        }

        if (foundDormantReuse)
        {
            index = bestDormantIndex;
        }
        else
        {
            float bestPlacementScore = 1e20;
            uint bestPlacementIndex = cellBaseIndex;
            bool hasLocalVacancy = false;

            [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
            {
                const uint slotIndex = cellBaseIndex + slot;
                SurfelData slotSurfel = SurfelPool[slotIndex];
                if (slotSurfel.Extra.y <= 0.5)
                {
                    hasLocalVacancy = true;
                    bestPlacementIndex = slotIndex;
                    break;
                }

                const uint slotCascade = (uint)round(slotSurfel.Extra.w);
                if (slotCascade != cascadeIndex)
                {
                    continue;
                }

                const uint slotAge = GetConsumedAge(slotSurfel.NormalSeenFrame.w);
                if (!disableTTLDeactivation && slotAge > max(ttl, deleteHysteresis) && !IsBoundarySurfel(slotSurfel.PositionRadius.xyz, cascadeBoundaryBand))
                {
                    const int3 slotCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
                    MarkDormantSurfel(slotSurfel, slotCellCoord, cascadeIndex, (uint)ComputeCommon.FrameNumber);
                    SurfelPool[slotIndex] = slotSurfel;
                    hasLocalVacancy = true;
                    bestPlacementIndex = slotIndex;
                    break;
                }

                const float3 slotPos = slotSurfel.PositionRadius.xyz;
                const int3 slotCellCoord = int3(floor(slotPos / cellSize));
                if (any(slotCellCoord != cellCoord))
                {
                    bestPlacementIndex = slotIndex;
                    break;
                }

                const float slotRadius = max(slotSurfel.PositionRadius.w, 0.001);
                float overlapPenalty = 0.0;

                [loop] for (int nz = -1; nz <= 1; ++nz)
                {
                    [loop] for (int ny = -1; ny <= 1; ++ny)
                    {
                        [loop] for (int nx = -1; nx <= 1; ++nx)
                        {
                            const int3 neighborCell = cellCoord + int3(nx, ny, nz);
                            uint neighborBaseIndex = 0u;
                            if (!TryGetCellBaseIndex(neighborCell, maxSurfels, cascadeIndex, false, neighborBaseIndex))
                                continue;

                            [loop] for (uint neighborSlot = 0u; neighborSlot < slotsPerCell; ++neighborSlot)
                            {
                                const uint neighborIndex = neighborBaseIndex + neighborSlot;
                                if (neighborIndex == slotIndex)
                                    continue;

                                SurfelData neighborSurfel = SurfelPool[neighborIndex];
                                if (neighborSurfel.Extra.y <= 0.5)
                                    continue;

                                const uint neighborCascade = (uint)round(neighborSurfel.Extra.w);
                                if (neighborCascade != cascadeIndex)
                                {
                                    continue;
                                }

                                const uint neighborAge = GetConsumedAge(neighborSurfel.NormalSeenFrame.w);
                                if (!disableTTLDeactivation && neighborAge > ttl && !IsBoundarySurfel(neighborSurfel.PositionRadius.xyz, cascadeBoundaryBand))
                                {
                                    const int3 staleNeighborCellCoord = int3(floor(neighborSurfel.PositionRadius.xyz / cellSize));
                                    MarkDormantSurfel(neighborSurfel, staleNeighborCellCoord, cascadeIndex, (uint)ComputeCommon.FrameNumber);
                                    SurfelPool[neighborIndex] = neighborSurfel;
                                    continue;
                                }

                                const float3 neighborPos = neighborSurfel.PositionRadius.xyz;
                                const int3 neighborCellCoord = int3(floor(neighborPos / cellSize));
                                if (any(neighborCellCoord != neighborCell))
                                    continue;

                                const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                                const float minSeparation = (slotRadius + neighborRadius) * separationScale;
                                const float centerDistance = distance(slotPos, neighborPos);
                                overlapPenalty += max(minSeparation - centerDistance, 0.0);
                            }
                        }
                    }
                }

                const float3 slotViewPos = mul(ComputeCommon.V, float4(slotPos, 1.0)).xyz;
                const float slotNearFactor = saturate(1.0 - abs(slotViewPos.z) / max(ComputeCommon.MaxDistance, 0.001));
                const float slotFarPenalty = (1.0 - slotNearFactor) * 0.15;
                const float slotAgePenalty = saturate((float)slotAge / max((float)ttl, 1.0)) * 0.35;
                const float placementScore = overlapPenalty + slotFarPenalty + slotAgePenalty;

                if (placementScore < bestPlacementScore)
                {
                    bestPlacementScore = placementScore;
                    bestPlacementIndex = slotIndex;
                }
            }

            index = bestPlacementIndex;

            // No explicit freelist exists; emulate "reuse queue tail" by probing dormant entries
            // from back of the pool only when local slots are saturated.
            if (!hasLocalVacancy)
            {
                float bestTailDormantScore = -1.0;
                uint tailDormantIndex = index;
                bool foundTailDormant = false;

                [unroll] for (uint probe = 0; probe < 10; ++probe)
                {
                    const uint probeHash = HashU32((uint)pixel.x * 19463u ^ (uint)pixel.y * 9137u ^ (uint)ComputeCommon.FrameNumber * 11279u ^ probe * 3121u);
                    const uint probeIndex = cascadePoolOffset + (probeHash % cascadePoolCount);
                    SurfelData probeSurfel = SurfelPool[probeIndex];
                    if (!IsDormantSurfel(probeSurfel))
                        continue;

                    const uint dormantCellHash = asuint(probeSurfel.Extra.z);

                    const float3 probeNormal = normalize(probeSurfel.NormalSeenFrame.xyz);
                    const float normalSimilarity = dot(probeNormal, worldNormal);
                    if (normalSimilarity < ComputeCommon.NormalThreshold)
                        continue;

                    const float3 probePos = probeSurfel.PositionRadius.xyz;
                    const float probeRadius = max(probeSurfel.PositionRadius.w, 0.001);
                    if (!IsRadiusCompatible(probeRadius, radius))
                        continue;
                    const float probeThreshold = max(probeRadius, radius) * max(ComputeCommon.MergeDistanceScale, 0.001)
                        * ((dormantCellHash == currentCellHash) ? 1.45 : 0.9);
                    const float probeDistance = distance(probePos, worldPos);
                    if (probeDistance > probeThreshold)
                        continue;

                    const float distScore = 1.0 - saturate(probeDistance / max(probeThreshold, 0.001));
                    const float dormantScore = normalSimilarity * 0.6 + distScore * 0.4;
                    if (dormantScore > bestTailDormantScore)
                    {
                        bestTailDormantScore = dormantScore;
                        tailDormantIndex = probeIndex;
                        foundTailDormant = true;
                    }
                }

                if (foundTailDormant)
                {
                    index = tailDormantIndex;
                }
            }
        }
    }

    SurfelData current = SurfelPool[index];
    if (current.Extra.y > 0.5)
    {
        const uint currentCascade = (uint)round(current.Extra.w);
        if (currentCascade != cascadeIndex)
        {
            const uint mismatchAge = GetConsumedAgeWithCascadeBias(current.NormalSeenFrame.w, currentCascade, cascadeIndex);
            if (!disableTTLDeactivation && mismatchAge > max(ttl, deleteHysteresis))
            {
                const float mismatchCellSize = cascade0CellSize * GetCascadeScale(currentCascade);
                const int3 mismatchCellCoord = int3(floor(current.PositionRadius.xyz / mismatchCellSize));
                MarkDormantSurfel(current, mismatchCellCoord, currentCascade, (uint)ComputeCommon.FrameNumber);
                SurfelPool[index] = current;
            }
            AttemptOutput[pixel] = float4(ATTEMPT_COLOR_CASCADE_MISMATCH, 1.0);
            DebugOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    const float3 currentPos = current.PositionRadius.xyz;
    const float3 currentViewPos = mul(ComputeCommon.V, float4(currentPos, 1.0)).xyz;
    const float currentCameraDistance = length(currentViewPos);

    const float lastSeenFrame = current.NormalSeenFrame.w;
    const uint ageFrames = GetConsumedAge(lastSeenFrame);
    const bool isNearKeepSurfel = currentCameraDistance <= max(ComputeCommon.NearKeepRadius, 0.0);
    const float currentNearFactor = saturate(1.0 - abs(currentViewPos.z) / max(ComputeCommon.MaxDistance, 0.001));
    // Prefer to retire far surfels earlier, but keep a minimum unseen hysteresis window.
    const uint effectiveTTLBase = isNearKeepSurfel ? ttl : max(1u, (uint)round((float)ttl * lerp(0.25, 1.0, currentNearFactor)));
    const uint effectiveTTL = max(effectiveTTLBase, deleteHysteresis);
    const bool alive = (current.Extra.y > 0.5) && (disableTTLDeactivation || (ageFrames <= effectiveTTL));
    const bool currentIsBoundary = IsBoundarySurfel(currentPos, cascadeBoundaryBand);
    if (!disableTTLDeactivation && (current.Extra.y > 0.5) && !alive && !currentIsBoundary)
    {
        const int3 retiredCellCoord = int3(floor(currentPos / cellSize));
        MarkDormantSurfel(current, retiredCellCoord, cascadeIndex, (uint)ComputeCommon.FrameNumber);
        SurfelPool[index] = current;
    }

    const float currentRadius = current.PositionRadius.w;
    const float3 currentNormal = normalize(current.NormalSeenFrame.xyz);

    const float distanceThreshold = max(currentRadius, radius) * max(ComputeCommon.MergeDistanceScale, 0.001);
    const bool canMerge = alive
        && IsRadiusCompatible(currentRadius, radius)
        && (distance(currentPos, worldPos) < distanceThreshold)
        && (dot(currentNormal, worldNormal) >= ComputeCommon.NormalThreshold);

    if (canMerge)
    {
        // Light merge mode:
        // keep existing surfel attributes stable and only refresh visibility timestamp.
        current.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
        current.Extra.y = 1.0;
        current.Extra.z = 0.0;
        current.Extra.w = (float)cascadeIndex;
        SurfelPool[index] = current;

        AttemptOutput[pixel] = float4(ATTEMPT_COLOR_MERGED, 1.0);
        DebugOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
        return;
    }

    const bool dormantReusable = IsDormantSurfel(current)
        && IsRadiusCompatible(currentRadius, radius)
        && (dot(currentNormal, worldNormal) >= ComputeCommon.NormalThreshold)
        && (distance(currentPos, worldPos) < distanceThreshold * 1.25);
    if (dormantReusable)
    {
        const float oldWeight = max(current.AlbedoWeight.w, 1.0);
        const float newWeight = min(oldWeight + 1.0, 64.0);
        const float blend = 1.0 / newWeight;
        current.NormalSeenFrame.xyz = normalize(lerp(current.NormalSeenFrame.xyz, worldNormal, blend));
        current.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
        current.AlbedoWeight.xyz = lerp(current.AlbedoWeight.xyz, albedo, blend);
        current.AlbedoWeight.w = newWeight;
        current.Extra.x = 6.0;
        current.Extra.y = 1.0;
        current.Extra.z = 0.0;
        current.Extra.w = (float)cascadeIndex;
        SurfelPool[index] = current;

        AttemptOutput[pixel] = float4(ATTEMPT_COLOR_DORMANT_REUSED, 1.0);
        DebugOutput[pixel] = float4(0.2, 0.6, 1.0, 1.0);
        return;
    }

    // Overflow-chain merge path: disabled in clipmap direct-mapping mode.
#if SURFEL_GI_ENABLE_OVERFLOW
    {
        int overflowNode = OverflowHeads[bucketIndex].Head;
        bool foundOverflowMerge = false;
        uint bestOverflowNodeIndex = 0u;
        float bestOverflowDistance = 1e20;

        [loop] for (uint iter = 0u; iter < 32u && overflowNode >= 0; ++iter)
        {
            const uint nodeIndex = (uint)overflowNode;
            OverflowNode node = OverflowNodes[nodeIndex];
            const SurfelData overflowSurfel = node.Surfel;

            if (overflowSurfel.Extra.y > 0.5)
            {
                const uint overflowCascade = (uint)round(overflowSurfel.Extra.w);
                if (overflowCascade == cascadeIndex)
                {
                    const uint overflowAge = GetConsumedAge(overflowSurfel.NormalSeenFrame.w);
                    if (!disableTTLDeactivation && overflowAge > max(ttl, deleteHysteresis) && !IsBoundarySurfel(overflowSurfel.PositionRadius.xyz, cascadeBoundaryBand))
                    {
                        const int3 staleCellCoord = int3(floor(overflowSurfel.PositionRadius.xyz / cellSize));
                        SurfelData staleNodeSurfel = overflowSurfel;
                        MarkDormantSurfel(staleNodeSurfel, staleCellCoord, cascadeIndex, (uint)ComputeCommon.FrameNumber);
                        node.Surfel = staleNodeSurfel;
                        OverflowNodes[nodeIndex] = node;
                        overflowNode = node.Next;
                        continue;
                    }

                    const float3 overflowPos = overflowSurfel.PositionRadius.xyz;
                    const float overflowRadius = overflowSurfel.PositionRadius.w;
                    const float3 overflowNormal = normalize(overflowSurfel.NormalSeenFrame.xyz);
                    const float overflowThreshold = max(overflowRadius, radius) * max(ComputeCommon.MergeDistanceScale, 0.001);
                    const float overflowDistance = distance(overflowPos, worldPos);
                    const bool overflowCanMerge = IsRadiusCompatible(overflowRadius, radius)
                        && (dot(overflowNormal, worldNormal) >= ComputeCommon.NormalThreshold)
                        && (overflowDistance < overflowThreshold);
                    if (overflowCanMerge && overflowDistance < bestOverflowDistance)
                    {
                        bestOverflowDistance = overflowDistance;
                        bestOverflowNodeIndex = nodeIndex;
                        foundOverflowMerge = true;
                    }
                }
            }

            overflowNode = node.Next;
        }

        if (foundOverflowMerge)
        {
            OverflowNode node = OverflowNodes[bestOverflowNodeIndex];
            SurfelData s = node.Surfel;
            s.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
            s.Extra.y = 1.0;
            s.Extra.z = 0.0;
            s.Extra.w = (float)cascadeIndex;
            node.Surfel = s;
            OverflowNodes[bestOverflowNodeIndex] = node;

            AttemptOutput[pixel] = float4(ATTEMPT_COLOR_MERGED, 1.0);
            DebugOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
            return;
        }
    }
#endif

    // Quick local occupancy probe.
    uint localAliveCountFast = 0u;
    bool localHasVacancyFast = false;
    bool localHasDormantFast = false;
    [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
    {
        const uint slotIndex = cellBaseIndex + slot;
        const SurfelData slotSurfel = SurfelPool[slotIndex];
        if (slotSurfel.Extra.y > 0.5)
        {
            const uint slotCascade = (uint)round(slotSurfel.Extra.w);
            if (slotCascade != cascadeIndex)
            {
                localHasVacancyFast = true;
                continue;
            }

            const int3 slotCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
            if (any(slotCellCoord != cellCoord))
            {
                localHasVacancyFast = true;
                continue;
            }
            localAliveCountFast++;
            continue;
        }

        localHasVacancyFast = true;
        if (IsDormantSurfel(slotSurfel))
        {
            const uint dormantCascade = (uint)round(slotSurfel.Extra.w);
            const int3 dormantCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
            if (dormantCascade == cascadeIndex && all(dormantCellCoord == cellCoord))
                localHasDormantFast = true;
        }
    }
    {
#if SURFEL_GI_ENABLE_OVERFLOW
        int overflowNode = OverflowHeads[bucketIndex].Head;
        [loop] for (uint iter = 0u; iter < 32u && overflowNode >= 0; ++iter)
        {
            const uint nodeIndex = (uint)overflowNode;
            if (nodeIndex >= maxSurfels)
                break;
            const OverflowNode node = OverflowNodes[nodeIndex];
            const SurfelData s = node.Surfel;
            if (s.Extra.y > 0.5)
            {
                const uint nodeCascade = (uint)round(s.Extra.w);
                if (nodeCascade == cascadeIndex)
                {
                    const int3 nodeCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
                    if (all(nodeCellCoord == cellCoord))
                        localAliveCountFast++;
                }
            }
            else if (IsDormantSurfel(s))
            {
                const uint dormantCascade = (uint)round(s.Extra.w);
                const int3 dormantCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
                if (dormantCascade == cascadeIndex && all(dormantCellCoord == cellCoord))
                    localHasDormantFast = true;
            }
            overflowNode = node.Next;
        }
#endif
    }
    const uint desiredLocalCountFast = min(desiredSlotsPerCell, slotsPerCell);
    const bool isLocalUnderfilledFast = (localAliveCountFast < desiredLocalCountFast);
    const bool allowSpawnOnNoOverlapOnly = isLocalUnderfilledFast && localHasVacancyFast && !localHasDormantFast;
    const bool canSpawn = isLocalUnderfilledFast;

    const bool currentVeryFar = (currentNearFactor < ComputeCommon.FarNearFactorThreshold)
        || (abs(currentViewPos.z) > ComputeCommon.MaxDistance * max(ComputeCommon.FarMaxDistanceMultiplier, 1.0));
    const uint staleDivisor = max(1u, (uint)round(max(ComputeCommon.StaleAgeDivisor, 1.0)));
    const bool staleEnough = ageFrames > max(2u, ttl / staleDivisor);
    // When a near candidate collides with an occupied slot, favor replacing far/stale surfels.
    bool replaceFarSurfel = (SURFEL_GI_ENABLE_REPLACE_FAR != 0) && alive && !canMerge
        && (nearFactor > currentNearFactor + ComputeCommon.ReplaceNearDelta)
        && (currentVeryFar || staleEnough)
        && !isNearKeepSurfel
        && !currentIsBoundary;

    // If near area is starving, steal a far/old surfel slot from sparse random probes.
    bool stealFarSurfel = false;
    uint stealIndex = index;
    if ((SURFEL_GI_ENABLE_STEAL_FAR != 0) && !canSpawn && !replaceFarSurfel && nearFactor > 0.55)
    {
        float bestEvictScore = -1.0;

        [unroll] for (uint probe = 0; probe < 6; ++probe)
        {
            const uint probeHash = HashU32((uint)pixel.x * 2243u ^ (uint)pixel.y * 1013u ^ (uint)ComputeCommon.FrameNumber * 7477u ^ probe * 31337u);
            const uint probeIndex = cascadePoolOffset + (probeHash % cascadePoolCount);
            SurfelData probeSurfel = SurfelPool[probeIndex];
            if (probeSurfel.Extra.y <= 0.5)
                continue;

            const uint probeCascade = (uint)round(probeSurfel.Extra.w);
            if (probeCascade != cascadeIndex)
            {
                continue;
            }

            const uint probeAge = GetConsumedAge(probeSurfel.NormalSeenFrame.w);
            const float3 probeViewPos = mul(ComputeCommon.V, float4(probeSurfel.PositionRadius.xyz, 1.0)).xyz;
            const float probeDist = length(probeViewPos);
            const bool probeNearKeep = probeDist <= max(ComputeCommon.NearKeepRadius, 0.0);
            if (probeNearKeep)
                continue;
            if (IsBoundarySurfel(probeSurfel.PositionRadius.xyz, cascadeBoundaryBand))
                continue;

            const float probeNearFactor = saturate(1.0 - abs(probeViewPos.z) / max(ComputeCommon.MaxDistance, 0.001));
            const float farScore = 1.0 - probeNearFactor;
            const float oldScore = saturate((float)probeAge / max((float)ttl, 1.0));
            const float evictScore = farScore * 0.75 + oldScore * 0.25;

            if (evictScore > bestEvictScore && (probeNearFactor < 0.2 || probeAge > ttl / 3u))
            {
                bestEvictScore = evictScore;
                stealIndex = probeIndex;
                stealFarSurfel = true;
            }
        }
    }

    // Hard minimum-separation gate for new placement/replacement.
    // If any alive neighbor surfel is too close, skip creating/replacing this frame.
    bool violatesMinSeparation = false;
    if (!canMerge)
    {
        [loop] for (int nz = -1; nz <= 1 && !violatesMinSeparation; ++nz)
        {
            [loop] for (int ny = -1; ny <= 1 && !violatesMinSeparation; ++ny)
            {
                [loop] for (int nx = -1; nx <= 1 && !violatesMinSeparation; ++nx)
                {
                    const int3 neighborCell = cellCoord + int3(nx, ny, nz);
                    uint neighborBaseIndex = 0u;
                    if (!TryGetCellBaseIndex(neighborCell, maxSurfels, cascadeIndex, false, neighborBaseIndex))
                        continue;
                    [loop] for (uint neighborSlot = 0u; neighborSlot < slotsPerCell; ++neighborSlot)
                    {
                        const uint neighborIndex = neighborBaseIndex + neighborSlot;
                        SurfelData neighborSurfel = SurfelPool[neighborIndex];
                        if (neighborSurfel.Extra.y <= 0.5)
                            continue;

                        const uint neighborCascade = (uint)round(neighborSurfel.Extra.w);
                        if (neighborCascade != cascadeIndex)
                            continue;

                        const uint neighborAge = GetConsumedAge(neighborSurfel.NormalSeenFrame.w);
                        if (!disableTTLDeactivation && neighborAge > ttl && !IsBoundarySurfel(neighborSurfel.PositionRadius.xyz, cascadeBoundaryBand))
                        {
                            const int3 staleNeighborCellCoord = int3(floor(neighborSurfel.PositionRadius.xyz / cellSize));
                            MarkDormantSurfel(neighborSurfel, staleNeighborCellCoord, cascadeIndex, (uint)ComputeCommon.FrameNumber);
                            SurfelPool[neighborIndex] = neighborSurfel;
                            continue;
                        }

                        const float3 neighborPos = neighborSurfel.PositionRadius.xyz;
                        const int3 neighborCellCoord = int3(floor(neighborPos / cellSize));
                        if (any(neighborCellCoord != neighborCell))
                            continue;

                        const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                        const float minSeparation = (neighborRadius + radius) * separationScale;
                        const float candidateDistance = distance(spawnPosition, neighborPos);
                        if (candidateDistance < minSeparation)
                        {
                            violatesMinSeparation = true;
                            break;
                        }
                    }
                    if (!violatesMinSeparation)
                    {
#if SURFEL_GI_ENABLE_OVERFLOW
                        const uint neighborBucketIndex = min(GetCellBucketIndex(neighborCell, maxSurfels, desiredSlotsPerCell, cascadeIndex), maxSurfels - 1u);
                        int overflowNode = OverflowHeads[neighborBucketIndex].Head;
                        [loop] for (uint iter = 0u; iter < 24u && overflowNode >= 0 && !violatesMinSeparation; ++iter)
                        {
                            const uint nodeIndex = (uint)overflowNode;
                            if (nodeIndex >= maxSurfels)
                                break;
                            OverflowNode node = OverflowNodes[nodeIndex];
                            SurfelData neighborSurfel = node.Surfel;
                            if (neighborSurfel.Extra.y > 0.5)
                            {
                                const uint neighborCascade = (uint)round(neighborSurfel.Extra.w);
                                if (neighborCascade == cascadeIndex)
                                {
                                    const uint neighborAge = GetConsumedAge(neighborSurfel.NormalSeenFrame.w);
                                    if (!disableTTLDeactivation && neighborAge > ttl && !IsBoundarySurfel(neighborSurfel.PositionRadius.xyz, cascadeBoundaryBand))
                                    {
                                        const int3 staleNeighborCellCoord = int3(floor(neighborSurfel.PositionRadius.xyz / cellSize));
                                        MarkDormantSurfel(neighborSurfel, staleNeighborCellCoord, cascadeIndex, (uint)ComputeCommon.FrameNumber);
                                        node.Surfel = neighborSurfel;
                                        OverflowNodes[nodeIndex] = node;
                                        overflowNode = node.Next;
                                        continue;
                                    }

                                    const float3 neighborPos = neighborSurfel.PositionRadius.xyz;
                                    const int3 neighborCellCoord = int3(floor(neighborPos / cellSize));
                                    if (all(neighborCellCoord == neighborCell))
                                    {
                                        const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                                        const float minSeparation = (neighborRadius + radius) * separationScale;
                                        const float candidateDistance = distance(spawnPosition, neighborPos);
                                        if (candidateDistance < minSeparation)
                                            violatesMinSeparation = true;
                                    }
                                }
                            }
                            overflowNode = node.Next;
                        }
#endif
                    }
                }
            }
        }
    }

    // In underfilled cells with vacancy and no local dormant, allow spawn if there is no real overlap.
    // This ignores soft minSeparation and only rejects true intersection.
    if (allowSpawnOnNoOverlapOnly)
    {
        bool overlapsAny = false;
        [loop] for (int nz = -1; nz <= 1 && !overlapsAny; ++nz)
        {
            [loop] for (int ny = -1; ny <= 1 && !overlapsAny; ++ny)
            {
                [loop] for (int nx = -1; nx <= 1 && !overlapsAny; ++nx)
                {
                    const int3 neighborCell = cellCoord + int3(nx, ny, nz);
                    uint neighborBaseIndex = 0u;
                    if (!TryGetCellBaseIndex(neighborCell, maxSurfels, cascadeIndex, false, neighborBaseIndex))
                        continue;
                    [loop] for (uint neighborSlot = 0u; neighborSlot < slotsPerCell; ++neighborSlot)
                    {
                        const uint neighborIndex = neighborBaseIndex + neighborSlot;
                        SurfelData neighborSurfel = SurfelPool[neighborIndex];
                        if (neighborSurfel.Extra.y <= 0.5)
                            continue;
                        const uint neighborCascade = (uint)round(neighborSurfel.Extra.w);
                        if (neighborCascade != cascadeIndex)
                            continue;
                        const float3 neighborPos = neighborSurfel.PositionRadius.xyz;
                        const int3 neighborCellCoord = int3(floor(neighborPos / cellSize));
                        if (any(neighborCellCoord != neighborCell))
                            continue;

                        const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                        const float noOverlapDistance = (neighborRadius + radius);
                        if (distance(spawnPosition, neighborPos) < noOverlapDistance)
                        {
                            overlapsAny = true;
                            break;
                        }
                    }
                    if (!overlapsAny)
                    {
#if SURFEL_GI_ENABLE_OVERFLOW
                        const uint neighborBucketIndex = min(GetCellBucketIndex(neighborCell, maxSurfels, desiredSlotsPerCell, cascadeIndex), maxSurfels - 1u);
                        int overflowNode = OverflowHeads[neighborBucketIndex].Head;
                        [loop] for (uint iter = 0u; iter < 24u && overflowNode >= 0 && !overlapsAny; ++iter)
                        {
                            const uint nodeIndex = (uint)overflowNode;
                            if (nodeIndex >= maxSurfels)
                                break;
                            const OverflowNode node = OverflowNodes[nodeIndex];
                            const SurfelData neighborSurfel = node.Surfel;
                            if (neighborSurfel.Extra.y <= 0.5)
                            {
                                overflowNode = node.Next;
                                continue;
                            }

                            const uint neighborCascade = (uint)round(neighborSurfel.Extra.w);
                            if (neighborCascade != cascadeIndex)
                            {
                                overflowNode = node.Next;
                                continue;
                            }
                            const float3 neighborPos = neighborSurfel.PositionRadius.xyz;
                            const int3 neighborCellCoord = int3(floor(neighborPos / cellSize));
                            if (any(neighborCellCoord != neighborCell))
                            {
                                overflowNode = node.Next;
                                continue;
                            }

                            const float neighborRadius = max(neighborSurfel.PositionRadius.w, 0.001);
                            const float noOverlapDistance = (neighborRadius + radius);
                            if (distance(spawnPosition, neighborPos) < noOverlapDistance)
                                overlapsAny = true;
                            overflowNode = node.Next;
                        }
#endif
                    }
                }
            }
        }
        violatesMinSeparation = overlapsAny;
    }

    const bool shouldCreateOrReplace = (canSpawn || replaceFarSurfel || stealFarSurfel)
        && !violatesMinSeparation;
    if (shouldCreateOrReplace)
    {
        // Cell-aware policy:
        // 1) If cell is underfilled and local dormant exists -> revive dormant first.
        // 2) If cell is underfilled and no local dormant but has local vacancy -> skip historical reuse and spawn directly.
        uint localAliveCount = 0u;
        bool localHasVacancy = false;
        bool foundLocalDormant = false;
        uint bestLocalDormantIndex = index;
        float bestLocalDormantDistance = 1e20;
        [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
        {
            const uint slotIndex = cellBaseIndex + slot;
            const SurfelData slotSurfel = SurfelPool[slotIndex];

            if (slotSurfel.Extra.y > 0.5)
            {
                const uint slotCascade = (uint)round(slotSurfel.Extra.w);
                if (slotCascade != cascadeIndex)
                {
                    localHasVacancy = true;
                    continue;
                }

                const int3 slotCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
                if (any(slotCellCoord != cellCoord))
                {
                    localHasVacancy = true;
                    continue;
                }

                localAliveCount++;
                continue;
            }

            localHasVacancy = true;
            if (!IsDormantSurfel(slotSurfel))
                continue;

            const uint dormantCascade = (uint)round(slotSurfel.Extra.w);
            if (dormantCascade != cascadeIndex)
                continue;

            const int3 dormantCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
            if (any(dormantCellCoord != cellCoord))
                continue;

            const float3 dormantNormal = normalize(slotSurfel.NormalSeenFrame.xyz);
            if (dot(dormantNormal, worldNormal) < ComputeCommon.NormalThreshold)
                continue;

            const float dormantRadius = max(slotSurfel.PositionRadius.w, 0.001);
            if (!IsRadiusCompatible(dormantRadius, radius))
                continue;

            const float dormantDistance = distance(slotSurfel.PositionRadius.xyz, worldPos);
            if (dormantDistance < bestLocalDormantDistance)
            {
                bestLocalDormantDistance = dormantDistance;
                bestLocalDormantIndex = slotIndex;
                foundLocalDormant = true;
            }
        }

        const uint desiredLocalCount = min(desiredSlotsPerCell, slotsPerCell);
        const bool isCellUnderfilled = (localAliveCount < desiredLocalCount);

        if (isCellUnderfilled && foundLocalDormant)
        {
            SurfelData historical = SurfelPool[bestLocalDormantIndex];
            const float oldWeight = max(historical.AlbedoWeight.w, 1.0);
            const float newWeight = min(oldWeight + 1.0, 64.0);
            const float blend = 1.0 / newWeight;
            historical.NormalSeenFrame.xyz = normalize(lerp(historical.NormalSeenFrame.xyz, worldNormal, blend));
            historical.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
            historical.AlbedoWeight.xyz = lerp(historical.AlbedoWeight.xyz, albedo, blend);
            historical.AlbedoWeight.w = newWeight;
            historical.Extra.x = 6.0;
            historical.Extra.y = 1.0;
            historical.Extra.z = 0.0;
            historical.Extra.w = (float)cascadeIndex;
            SurfelPool[bestLocalDormantIndex] = historical;

            AttemptOutput[pixel] = float4(ATTEMPT_COLOR_DORMANT_REUSED, 1.0);
            DebugOutput[pixel] = float4(0.2, 0.6, 1.0, 1.0);
            return;
        }

        const bool preferDirectSpawnInUnderfilledCell = isCellUnderfilled && !foundLocalDormant && localHasVacancy;

        // Before creating a new red surfel, try reactivating a previously used local surfel.
        // Skip this path when cell is underfilled and can accept direct spawn.
        bool foundHistorical = false;
        uint bestHistoricalIndex = index;
        float bestHistoricalDistance = 1e20;
        if (!preferDirectSpawnInUnderfilledCell)
        {
            [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
            {
                const uint slotIndex = cellBaseIndex + slot;
                SurfelData historical = SurfelPool[slotIndex];
                if (historical.Extra.y <= 0.5 && !IsDormantSurfel(historical))
                    continue;

                const uint historicalCascade = (uint)round(historical.Extra.w);
                if (historicalCascade != cascadeIndex)
                    continue;

                const float3 historicalPos = historical.PositionRadius.xyz;
                const int3 historicalCellCoord = int3(floor(historicalPos / cellSize));
                if (any(historicalCellCoord != cellCoord))
                    continue;

                const float3 historicalNormal = normalize(historical.NormalSeenFrame.xyz);
                const float normalSimilarity = dot(historicalNormal, worldNormal);
                if (normalSimilarity < ComputeCommon.NormalThreshold)
                    continue;

                const float historicalRadius = max(historical.PositionRadius.w, 0.001);
                if (!IsRadiusCompatible(historicalRadius, radius))
                    continue;
                const float historicalThreshold = max(historicalRadius, radius) * max(ComputeCommon.MergeDistanceScale, 0.001) * 1.8;
                const float historicalDistance = distance(historicalPos, worldPos);
                if (historicalDistance > historicalThreshold)
                    continue;

                if (historicalDistance < bestHistoricalDistance)
                {
                    bestHistoricalDistance = historicalDistance;
                    bestHistoricalIndex = slotIndex;
                    foundHistorical = true;
                }
            }

            if (foundHistorical)
            {
                SurfelData historical = SurfelPool[bestHistoricalIndex];
                const float oldWeight = max(historical.AlbedoWeight.w, 1.0);
                const float newWeight = min(oldWeight + 1.0, 64.0);
                const float blend = 1.0 / newWeight;
                historical.NormalSeenFrame.xyz = normalize(lerp(historical.NormalSeenFrame.xyz, worldNormal, blend));
                historical.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
                historical.AlbedoWeight.xyz = lerp(historical.AlbedoWeight.xyz, albedo, blend);
                historical.AlbedoWeight.w = newWeight;
                historical.Extra.x = 6.0;
                historical.Extra.y = 1.0;
                historical.Extra.z = 0.0;
                historical.Extra.w = (float)cascadeIndex;
                SurfelPool[bestHistoricalIndex] = historical;

                AttemptOutput[pixel] = float4(ATTEMPT_COLOR_HISTORICAL_REUSED, 1.0);
                DebugOutput[pixel] = float4(0.2, 0.6, 1.0, 1.0);
                return;
            }
        }

        const uint spawnHysteresis = max((uint)ComputeCommon.SpawnHysteresisFrames, 1u);
        const bool currentDormant = IsDormantSurfel(current);
        const uint pendingSpawnStreak = currentDormant ? spawnHysteresis : min((uint)current.Extra.z + 1u, 255u);
        if (!preferDirectSpawnInUnderfilledCell && pendingSpawnStreak < spawnHysteresis)
        {
            current.Extra.z = (float)pendingSpawnStreak;
            SurfelPool[index] = current;
            AttemptOutput[pixel] = float4(ATTEMPT_COLOR_HYSTERESIS_WAIT, 1.0);
            DebugOutput[pixel] = float4(0.0, 0.0, 0.5, 1.0);
            return;
        }

        if (stealFarSurfel)
        {
            index = stealIndex;
        }

        // Write gating based on current cell occupancy (not single slot alive state).
        uint localAliveCountWrite = 0u;
        [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
        {
            const uint slotIndex = cellBaseIndex + slot;
            const SurfelData slotSurfel = SurfelPool[slotIndex];
            if (slotSurfel.Extra.y <= 0.5)
                continue;

            const uint slotCascade = (uint)round(slotSurfel.Extra.w);
            if (slotCascade != cascadeIndex)
                continue;

            const int3 slotCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
            if (any(slotCellCoord != cellCoord))
                continue;

            localAliveCountWrite++;
        }
        {
#if SURFEL_GI_ENABLE_OVERFLOW
            int overflowNode = OverflowHeads[bucketIndex].Head;
            [loop] for (uint iter = 0u; iter < 32u && overflowNode >= 0; ++iter)
            {
                const uint nodeIndex = (uint)overflowNode;
                if (nodeIndex >= maxSurfels)
                    break;
                const OverflowNode node = OverflowNodes[nodeIndex];
                const SurfelData s = node.Surfel;
                if (s.Extra.y > 0.5)
                {
                    const uint nodeCascade = (uint)round(s.Extra.w);
                    if (nodeCascade == cascadeIndex)
                    {
                        const int3 nodeCellCoord = int3(floor(s.PositionRadius.xyz / cellSize));
                        if (all(nodeCellCoord == cellCoord))
                            localAliveCountWrite++;
                    }
                }
                overflowNode = node.Next;
            }
#endif
        }
        const uint desiredLocalCountWrite = min(desiredSlotsPerCell, slotsPerCell);
        const bool canWriteByCellOccupancy = (localAliveCountWrite < desiredLocalCountWrite);
        if (!canWriteByCellOccupancy)
        {
            AttemptOutput[pixel] = float4(ATTEMPT_COLOR_REJECTED_OCCUPANCY_GATE, 1.0);
            DebugOutput[pixel] = float4(0.0, 0.0, 1.0, 1.0);
            return;
        }

        // Prefer inactive slots; if none exists, recycle foreign slot in this bucket.
        bool foundWritableSlot = false;
        uint writeIndex = index;
        [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
        {
            const uint slotIndex = cellBaseIndex + slot;
            const SurfelData slotSurfel = SurfelPool[slotIndex];
            if (slotSurfel.Extra.y <= 0.5)
            {
                writeIndex = slotIndex;
                foundWritableSlot = true;
                break;
            }
        }
        if (!foundWritableSlot)
        {
            const bool severeUnderfilled = (localAliveCountWrite == 0u);
            // Deterministic cadence to avoid aggressive per-frame overwrite flicker.
            const bool fillCadencePass = (((uint)ComputeCommon.FrameNumber + (currentCellHash & 7u)) & 7u) == 0u;

            [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
            {
                const uint slotIndex = cellBaseIndex + slot;
                const SurfelData slotSurfel = SurfelPool[slotIndex];
                if (slotSurfel.Extra.y <= 0.5)
                    continue;

                const uint slotCascade = (uint)round(slotSurfel.Extra.w);
                const int3 slotCellCoord = int3(floor(slotSurfel.PositionRadius.xyz / cellSize));
                const bool isForeignSlot = (slotCascade != cascadeIndex) || any(slotCellCoord != cellCoord);
                const uint slotAge = GetConsumedAgeWithCascadeBias(slotSurfel.NormalSeenFrame.w, slotCascade, cascadeIndex);
                const float3 slotViewPos = mul(ComputeCommon.V, float4(slotSurfel.PositionRadius.xyz, 1.0)).xyz;
                const float slotDistance = length(slotViewPos);
                const bool slotNearKeep = slotDistance <= max(ComputeCommon.NearKeepRadius, 0.0);
                const bool slotBoundary = IsBoundarySurfel(slotSurfel.PositionRadius.xyz, cascadeBoundaryBand);
                const bool recyclableForeign = !slotNearKeep && !slotBoundary
                    && (slotAge > max(ttl / 2u, deleteHysteresis));
                const bool cadenceForeignFill = severeUnderfilled && fillCadencePass && !slotNearKeep && !slotBoundary;
                if (isForeignSlot && (recyclableForeign || cadenceForeignFill))
                {
                    writeIndex = slotIndex;
                    foundWritableSlot = true;
                    break;
                }
            }
        }
        if (!foundWritableSlot)
        {
            AttemptOutput[pixel] = float4(ATTEMPT_COLOR_REJECTED_NO_WRITABLE_SLOT, 1.0);
            DebugOutput[pixel] = float4(0.0, 0.0, 1.0, 1.0);
            return;
        }
        index = writeIndex;

        SurfelData s;
        s.PositionRadius = float4(spawnPosition, radius);
        s.NormalSeenFrame = float4(worldNormal, (float)ComputeCommon.FrameNumber);
        s.AlbedoWeight = float4(albedo, 1.0);
        s.Extra = float4(stealFarSurfel ? 4.0 : (replaceFarSurfel ? 3.0 : 0.0), 1.0, 0.0, (float)cascadeIndex);
        SurfelPool[index] = s;
        AttemptOutput[pixel] = float4(stealFarSurfel ? ATTEMPT_COLOR_STEAL_FAR
            : (replaceFarSurfel ? ATTEMPT_COLOR_REPLACED_FAR
            : ATTEMPT_COLOR_SPAWN_NEW), 1.0);

        DebugOutput[pixel] = stealFarSurfel ? float4(1.0, 0.0, 1.0, 1.0)
            : (replaceFarSurfel ? float4(0.0, 1.0, 1.0, 1.0)
            : (alive ? float4(1.0, 0.5, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0)));
    }
    else
    {
        float3 noCreateColor = ATTEMPT_COLOR_REJECTED_NO_CREATE_PATH;

        AttemptOutput[pixel] = float4(violatesMinSeparation ? ATTEMPT_COLOR_REJECTED_MIN_SEPARATION : noCreateColor, 1.0);
        DebugOutput[pixel] = violatesMinSeparation ? float4(1.0, 1.0, 0.0, 1.0) : float4(noCreateColor, 1.0);
    }
}
