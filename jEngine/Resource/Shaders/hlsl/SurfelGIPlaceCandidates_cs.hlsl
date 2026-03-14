#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#define SURFEL_GI_GUIDE_DIM 4
#define SURFEL_GI_GUIDE_LOBE_COUNT (SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM)
#define SURFEL_GI_GUIDE_TOTAL_FLOATS (SURFEL_GI_GUIDE_LOBE_COUNT + SURFEL_GI_GUIDE_DIM)
// Temp toggle: allow replacing active surfel when candidate normal is very different.
#define SURFEL_GI_ENABLE_NORMAL_MISMATCH_REPLACE 0
// Replace only when normals are almost opposite (full flip): dot < -0.9.
#define SURFEL_GI_NORMAL_MISMATCH_REPLACE_DOT_THRESHOLD -0.9

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

float3 SafeNormalize3(float3 v, float3 fallback)
{
    const float lenSq = dot(v, v);
    return (lenSq > 1e-6) ? (v * rsqrt(lenSq)) : fallback;
}

float ComputeIrradianceSeedWeight(jSurfelGPU candidateSurfel, jSurfelGPU neighborSurfel, jSurfelIrradianceGPU neighborIrradiance)
{
    const float3 candidateNormal = SafeNormalize3(candidateSurfel.NormalSeenFrame.xyz, float3(0.0, 1.0, 0.0));
    const float3 neighborNormal = SafeNormalize3(neighborSurfel.NormalSeenFrame.xyz, candidateNormal);
    const float normalWeight = saturate(dot(candidateNormal, neighborNormal));
    const float3 delta = candidateSurfel.PositionRadius.xyz - neighborSurfel.PositionRadius.xyz;
    const float dist = length(delta);
    const float combinedRadius = max(candidateSurfel.PositionRadius.w + neighborSurfel.PositionRadius.w, 0.001);
    const float distanceWeight = saturate(1.0 - dist / max(combinedRadius * 2.0, 0.001));
    const float confidenceWeight = saturate(neighborIrradiance.IrradianceAndCount.w / 8.0);
    return normalWeight * distanceWeight * confidenceWeight;
}

bool TrySeedIrradianceFromActiveCellSurfels(uint base, uint desiredSlots, uint maxSurfels, uint pageCascade, uint ignoreIndex, jSurfelGPU candidateSurfel, out jSurfelIrradianceGPU outSeed)
{
    float3 weightedIrradiance = 0.0;
    float weightSum = 0.0;

    [loop] for (uint i = 0u; i < desiredSlots; ++i)
    {
        const uint idx = base + i;
        if (idx >= maxSurfels)
            break;
        if (idx == ignoreIndex)
            continue;

        const jSurfelGPU neighborSurfel = SurfelPool[idx];
        if (neighborSurfel.Extra.y <= 0.5)
            continue;
        if ((uint)round(neighborSurfel.Extra.w) != pageCascade)
            continue;

        const jSurfelIrradianceGPU neighborIrradiance = SurfelIrradianceBuffer[idx];
        if (neighborIrradiance.IrradianceAndCount.w <= 0.01)
            continue;

        const float weight = ComputeIrradianceSeedWeight(candidateSurfel, neighborSurfel, neighborIrradiance);
        if (weight <= 1e-5)
            continue;

        weightedIrradiance += max(neighborIrradiance.IrradianceAndCount.xyz, 0.0) * weight;
        weightSum += weight;
    }

    if (weightSum <= 1e-5)
    {
        outSeed.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
        outSeed.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
        outSeed.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
        return false;
    }

    const float3 seededIrradiance = weightedIrradiance / weightSum;
    outSeed.IrradianceAndCount = float4(seededIrradiance, 1.0);
    outSeed.MSMEData0 = float4(seededIrradiance, 1.0);
    outSeed.MSMEData1 = float4(0.0, 0.0, 0.0, 1.0);
    return true;
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

    const jSurfelCandidateGPU c = CandidateBuffer[winnerIndex];
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
    bool foundNormalMismatchReplace = false;
#if SURFEL_GI_ENABLE_NORMAL_MISMATCH_REPLACE
    const float3 candidateNormal = normalize(c.Surfel.NormalSeenFrame.xyz);
    const float normalMismatchDotThreshold = clamp(SURFEL_GI_NORMAL_MISMATCH_REPLACE_DOT_THRESHOLD, -1.0, 0.999);
#endif

    [loop] for (uint i = 0u; i < desiredSlots; ++i)
    {
        const uint idx = base + i;
        if (idx >= maxSurfels)
            break;
        const jSurfelGPU s = SurfelPool[idx];
        if (s.Extra.y <= 0.5)
        {
            writeIndex = idx;
            foundInactive = true;
            break;
        }

#if SURFEL_GI_ENABLE_NORMAL_MISMATCH_REPLACE
        if (foundNormalMismatchReplace)
            continue;

        const uint surfelCascade = (uint)round(s.Extra.w);
        if (surfelCascade != pageCascade)
            continue;

        const float3 existingNormal = normalize(s.NormalSeenFrame.xyz);
        if (dot(existingNormal, candidateNormal) < normalMismatchDotThreshold)
        {
            writeIndex = idx;
            foundNormalMismatchReplace = true;
        }
#endif
    }

    // Prefer inactive slot placement. If none exists, allow replacement when normal mismatch is large.
    if (!foundInactive && !foundNormalMismatchReplace)
        return;

    const jSurfelGPU existing = SurfelPool[writeIndex];
    const bool isDormantReuse = (existing.Extra.y <= 0.5) && (abs(existing.Extra.x - 5.0) < 0.5);
    const bool isNormalMismatchReplace = (!foundInactive && foundNormalMismatchReplace);
    jSurfelGPU outSurfel;
    if (isDormantReuse)
    {
        // Re-activate dormant surfel in-place using the winner candidate's fresh attributes.
        outSurfel = existing;
        outSurfel.PositionRadius = c.Surfel.PositionRadius;
        outSurfel.NormalSeenFrame.xyz = c.Surfel.NormalSeenFrame.xyz;
        outSurfel.NormalSeenFrame.w = (float)ComputeCommon.FrameNumber;
        outSurfel.AlbedoWeight = c.Surfel.AlbedoWeight;
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
        if (isNormalMismatchReplace)
            outSurfel.Extra.x = 7.0;      // "normal mismatch replaced" for debug.
    }
    SurfelPool[writeIndex] = outSurfel;

    jSurfelIrradianceGPU outIrradiance;
    if (!TrySeedIrradianceFromActiveCellSurfels(base, desiredSlots, maxSurfels, pageCascade, writeIndex, outSurfel, outIrradiance))
    {
        outIrradiance.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
        outIrradiance.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
        outIrradiance.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
    }
    SurfelIrradianceBuffer[writeIndex] = outIrradiance;
    const uint guidingBaseIndex = writeIndex * SURFEL_GI_GUIDE_TOTAL_FLOATS;
    [unroll] for (uint guideIndex = 0u; guideIndex < SURFEL_GI_GUIDE_TOTAL_FLOATS; ++guideIndex)
    {
        SurfelGuidingBuffer[guidingBaseIndex + guideIndex] = 0.0;
    }

    if (isNormalMismatchReplace)
    {
        uint oldMismatch = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].MismatchCount, 1u, oldMismatch);
    }
    else
    {
        uint oldValue = 0u;
        InterlockedAdd(SurfelGIStatsBuffer[0].ActiveCount, 1u, oldValue);
    }
}
