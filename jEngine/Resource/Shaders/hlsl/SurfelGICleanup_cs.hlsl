#include "common.hlsl"

#ifndef SURFEL_GI_AGE_CONSUME_SCALE
    #define SURFEL_GI_AGE_CONSUME_SCALE 2.0
#endif
#ifndef SURFEL_GI_BOUNDARY_CLEANUP_BAND_SCALE
    #define SURFEL_GI_BOUNDARY_CLEANUP_BAND_SCALE 1.0
#endif

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

struct SurfelIrradianceData
{
    float4 IrradianceAndCount;
    float4 MSMEData0;
    float4 MSMEData1;
};

cbuffer ComputeCommon : register(b2, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

RWStructuredBuffer<SurfelData> SurfelPool : register(u0, space0);
RWStructuredBuffer<SurfelIrradianceData> SurfelIrradianceBuffer : register(u1, space0);

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

uint GetConsumedAge(float lastSeenFrame)
{
    const float rawAge = abs((float)ComputeCommon.FrameNumber - lastSeenFrame);
    return (uint)(rawAge * SURFEL_GI_AGE_CONSUME_SCALE);
}

void MarkDormantSurfel(inout SurfelData s, int3 cellCoord, uint cascadeIndex)
{
    s.Extra.x = 5.0;
    s.Extra.y = 0.0;
    s.Extra.z = asfloat(HashCellWithCascade(cellCoord, cascadeIndex));
    s.Extra.w = (float)cascadeIndex;
}

void ResetSurfelHard(inout SurfelData s)
{
    s.PositionRadius = float4(0.0, 0.0, 0.0, 0.0);
    s.NormalSeenFrame = float4(0.0, 0.0, 0.0, 0.0);
    s.AlbedoWeight = float4(0.0, 0.0, 0.0, 0.0);
    s.Extra = float4(0.0, 0.0, 0.0, 0.0);
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint surfelIndex = GlobalInvocationID.x;
    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    if (surfelIndex >= maxSurfels)
        return;

    SurfelData s = SurfelPool[surfelIndex];
    const uint ttl = max((uint)ComputeCommon.TTLInFrames, 1u);
    const uint deleteHysteresis = max((uint)ComputeCommon.DeleteHysteresisFrames, 1u);
    const float cascade0CellSize = max(ComputeCommon.GridCellSize, 0.1);
    const float cascadeBoundaryBand = max(cascade0CellSize * SURFEL_GI_BOUNDARY_CLEANUP_BAND_SCALE, 1.0);

    if (s.Extra.y <= 0.5)
    {
        const bool isDormant = (abs(s.Extra.x - 5.0) < 0.5);
        if (isDormant)
        {
            // Dormant slots are still valid reuse candidates.
            // Do not hard-purge them in cleanup to avoid popping while visible.
            return;
        }

        // Purge very old inactive/dormant slots to keep allocation state clean.
        const uint inactiveAgeFrames = GetConsumedAge(s.NormalSeenFrame.w);
        const uint inactivePurgeFrames = max(ttl * 4u, deleteHysteresis * 2u);
        if (inactiveAgeFrames > inactivePurgeFrames)
        {
            ResetSurfelHard(s);
            SurfelPool[surfelIndex] = s;

            SurfelIrradianceData ir;
            ir.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
            ir.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
            ir.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
            SurfelIrradianceBuffer[surfelIndex] = ir;
        }
        return;
    }

    const uint surfelCascade = min((uint)round(s.Extra.w), (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const float3 surfelPos = s.PositionRadius.xyz;
    const float3 surfelViewPos = mul(ComputeCommon.V, float4(surfelPos, 1.0)).xyz;
    const float surfelCameraDistance = length(surfelViewPos);
    const uint expectedCascade = GetCascadeIndexByDistance(surfelCameraDistance);

    uint ageFrames = GetConsumedAge(s.NormalSeenFrame.w);
    if (surfelCascade != expectedCascade)
        ageFrames *= 2u;

    const bool isNearKeepSurfel = surfelCameraDistance <= max(ComputeCommon.NearKeepRadius, 0.0);
    const float nearFactor = saturate(1.0 - abs(surfelViewPos.z) / max(ComputeCommon.MaxDistance, 0.001));
    const uint effectiveTTLBase = isNearKeepSurfel ? ttl : max(1u, (uint)round((float)ttl * lerp(0.25, 1.0, nearFactor)));
    const uint effectiveTTL = max(effectiveTTLBase, deleteHysteresis);

    if (ageFrames > effectiveTTL && !IsBoundarySurfel(surfelPos, cascadeBoundaryBand))
    {
        const float cellSize = cascade0CellSize * GetCascadeScale(surfelCascade);
        const int3 cellCoord = int3(floor(surfelPos / cellSize));
        MarkDormantSurfel(s, cellCoord, surfelCascade);
        SurfelPool[surfelIndex] = s;

        SurfelIrradianceData ir;
        ir.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
        ir.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
        ir.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
        SurfelIrradianceBuffer[surfelIndex] = ir;
    }
}
