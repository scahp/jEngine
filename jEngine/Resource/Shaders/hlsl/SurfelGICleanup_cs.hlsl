#include "common.hlsl"
#include "SurfelGIClipmapLookup.hlsl"

#ifndef SURFEL_GI_AGE_CONSUME_SCALE
    #define SURFEL_GI_AGE_CONSUME_SCALE 2.0
#endif
#ifndef SURFEL_GI_BOUNDARY_CLEANUP_BAND_SCALE
    #define SURFEL_GI_BOUNDARY_CLEANUP_BAND_SCALE 1.0
#endif

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_GUIDE_DIM 4
#define SURFEL_GI_GUIDE_LOBE_COUNT (SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM)
#define SURFEL_GI_GUIDE_TOTAL_FLOATS (SURFEL_GI_GUIDE_LOBE_COUNT + SURFEL_GI_GUIDE_DIM)

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

bool IsBoundarySurfel(float3 surfelPos, float boundaryBand)
{
    const float3 surfelViewPos = mul(ComputeCommon.V, float4(surfelPos, 1.0)).xyz;
    const float cameraDistance = length(surfelViewPos);
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const float startDistance = SurfelGIGetCascadeStartDistance(ComputeCommon.CascadeStartDistancePacked, i);
        if (abs(cameraDistance - startDistance) <= boundaryBand)
            return true;
    }
    return false;
}

uint GetConsumedAge(uint lastSeenFrame)
{
    const uint rawAge = ComputeCommon.FrameNumber - min(lastSeenFrame, ComputeCommon.FrameNumber);
    return (uint)((float)rawAge * SURFEL_GI_AGE_CONSUME_SCALE);
}

void MarkDormantSurfel(inout jSurfelGPU s, int3 cellCoord, uint cascadeIndex)
{
    s.State = SURFEL_GI_SURFEL_STATE_DORMANT;
    s.IsActive = 0u;
    s.OwnerCellHash = HashCellWithCascade(cellCoord, cascadeIndex);
    s.CascadeIndex = cascadeIndex;
}

void ResetSurfelHard(inout jSurfelGPU s)
{
    s.PositionRadius = float4(0.0, 0.0, 0.0, 0.0);
    s.Normal = float3(0.0, 0.0, 0.0);
    s.LastSeenFrame = 0u;
    s.AlbedoWeight = float4(0.0, 0.0, 0.0, 0.0);
    s.State = SURFEL_GI_SURFEL_STATE_NEW;
    s.IsActive = 0u;
    s.OwnerCellHash = 0u;
    s.CascadeIndex = 0u;
}

void ResetSurfelGuiding(uint surfelIndex)
{
    const uint guidingBaseIndex = surfelIndex * SURFEL_GI_GUIDE_TOTAL_FLOATS;
    [unroll] for (uint guideIndex = 0u; guideIndex < SURFEL_GI_GUIDE_TOTAL_FLOATS; ++guideIndex)
    {
        SurfelGuidingBuffer[guidingBaseIndex + guideIndex] = 0.0;
    }
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint surfelIndex = GlobalInvocationID.x;
    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    if (surfelIndex >= maxSurfels)
        return;

    jSurfelGPU s = SurfelPool[surfelIndex];
    const uint ttl = max((uint)ComputeCommon.TTLInFrames, 1u);
    const uint outOfViewKeepFrames = max((uint)ComputeCommon.OutOfViewKeepFrames, 1u);
    const float cascade0CellSize = max(ComputeCommon.GridCellSize, 0.1);
    const float cascadeBoundaryBand = max(cascade0CellSize * SURFEL_GI_BOUNDARY_CLEANUP_BAND_SCALE, 1.0);

    if (s.IsActive == 0u)
    {
        const bool isDormant = (s.State == SURFEL_GI_SURFEL_STATE_DORMANT);
        if (isDormant)
        {
            // Dormant slots are still valid reuse candidates.
            // Do not hard-purge them in cleanup to avoid popping while visible.
            return;
        }

        // Purge very old inactive/dormant slots to keep allocation state clean.
        const uint inactiveAgeFrames = GetConsumedAge(s.LastSeenFrame);
        const uint inactivePurgeFrames = max(ttl * 4u, outOfViewKeepFrames * 2u);
        if (inactiveAgeFrames > inactivePurgeFrames)
        {
            ResetSurfelHard(s);
            SurfelPool[surfelIndex] = s;

            jSurfelIrradianceGPU ir;
            ir.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
            ir.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
            ir.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
            SurfelIrradianceBuffer[surfelIndex] = ir;
            ResetSurfelGuiding(surfelIndex);
        }
        return;
    }

    const uint surfelCascade = min(s.CascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const float3 surfelPos = s.PositionRadius.xyz;
    const float3 surfelViewPos = mul(ComputeCommon.V, float4(surfelPos, 1.0)).xyz;
    const float surfelCameraDistance = length(surfelViewPos);
    const uint expectedCascade = SurfelGIGetCascadeIndexByDistance(ComputeCommon.CascadeStartDistancePacked, surfelCameraDistance);

    uint ageFrames = GetConsumedAge(s.LastSeenFrame);
    if (surfelCascade != expectedCascade)
        ageFrames *= 2u;

    const float nearFactor = saturate(1.0 - abs(surfelViewPos.z) / max(ComputeCommon.MaxDistance, 0.001));
    const uint effectiveTTLBase = max(1u, (uint)round((float)ttl * lerp(0.25, 1.0, nearFactor)));
    const uint effectiveTTL = max(effectiveTTLBase, outOfViewKeepFrames);

    if (ageFrames > effectiveTTL && !IsBoundarySurfel(surfelPos, cascadeBoundaryBand))
    {
        const float cellSize = cascade0CellSize * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, surfelCascade);
        const int3 cellCoord = int3(floor(surfelPos / cellSize));
        MarkDormantSurfel(s, cellCoord, surfelCascade);
        SurfelPool[surfelIndex] = s;

        jSurfelIrradianceGPU ir;
        ir.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
        ir.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
        ir.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
        SurfelIrradianceBuffer[surfelIndex] = ir;
        ResetSurfelGuiding(surfelIndex);
    }
}
