#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#define SURFEL_GI_VISIBLE_CELL_WORKLIST_MULTIPLIER 2u
#define SURFEL_GI_BOUNDARY_BAND_SCALE 1.0
// Temp debug switch: 0 keeps a single cascade assignment (no boundary dual emit).
#define SURFEL_GI_ENABLE_BOUNDARY_OVERLAP 0

struct CommonComputeUniformBuffer
{
    float4x4 InvP;
    float4x4 V;
    float4x4 InvV;
    float2 ScreenSize;
    float NormalThreshold;
    float DepthEdgeScale;
    float NormalEdgeScale;
    int PreferCellCenterForFirstPlacement;
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
    int OutOfViewKeepFrames;
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

struct VisibleCellEntry
{
    int4 CellCascade;
};

struct VisibleCellCounter
{
    uint Count;
    uint3 Padding;
};

Texture2D DepthTexture : register(t0, space0);
SamplerState DepthTextureSampler : register(s0, space0);

cbuffer ComputeCommon : register(b1, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

RWStructuredBuffer<VisibleCellEntry> VisibleCellWorklist : register(u2, space0);
RWStructuredBuffer<VisibleCellCounter> VisibleCellCounterBuffer : register(u3, space0);

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

int GetPackedInt(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packed = packedArray[packIndex];
    const float value = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
    return (int)round(value);
}

int3 GetCascadeDim(uint cascadeIndex)
{
    return int3(
        max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimXPacked, cascadeIndex), 1),
        max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimYPacked, cascadeIndex), 1),
        max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimZPacked, cascadeIndex), 1));
}

int3 GetCascadeOriginCell(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeOriginCellXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeOriginCellYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeOriginCellZPacked, cascadeIndex));
}

bool TryComputeCellCoordForCascade(float3 worldPos, uint cascadeIndex, out int3 outCellCoord)
{
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * GetCascadeScale(cascadeIndex);
    const int3 cellCoord = int3(floor(worldPos / cellSize));
    const int3 local = cellCoord - GetCascadeOriginCell(cascadeIndex);
    const int3 dim = GetCascadeDim(cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    outCellCoord = cellCoord;
    return true;
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

float GetCascadeStartDistance(uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packed = ComputeCommon.CascadeStartDistancePacked[packIndex];
    const float startDistance = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
    return max(startDistance, 0.0);
}

bool TryGetBoundaryCascadePair(float cameraDistance, out uint outLowCascade, out uint outHighCascade)
{
    [loop] for (uint i = 1u; i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        const float startDistance = GetCascadeStartDistance(i);
        const float lowerCellSize = max(ComputeCommon.GridCellSize, 0.1) * GetCascadeScale(i - 1u);
        const float upperCellSize = max(ComputeCommon.GridCellSize, 0.1) * GetCascadeScale(i);
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

bool TrySelectCascadeForWorldPos(float3 worldPos, float cameraDistance, out uint outCascadeIndex, out int3 outCellCoord)
{
    const uint desiredCascade = GetCascadeIndexByDistance(cameraDistance);
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

void EmitVisibleCell(int3 cellCoord, uint cascadeIndex, uint maxVisibleCells)
{
    uint writeIndex = 0u;
    InterlockedAdd(VisibleCellCounterBuffer[0].Count, 1u, writeIndex);

    if (writeIndex < maxVisibleCells)
    {
        VisibleCellEntry entry;
        entry.CellCascade = int4(cellCoord, (int)cascadeIndex);
        VisibleCellWorklist[writeIndex] = entry;
    }
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
    if (any(pixel != samplePixel))
        return;

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
        return;

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, ComputeCommon.InvP);
    const float3 worldPos = mul(ComputeCommon.InvV, float4(viewPos, 1.0)).xyz;
    const float cameraDistance = length(viewPos);
    const uint maxVisibleCells = (uint)max(dispatchSize.x * dispatchSize.y * (int)SURFEL_GI_VISIBLE_CELL_WORKLIST_MULTIPLIER, 1);
    uint primaryCascadeIndex = 0u;
    int3 primaryCellCoord = int3(0, 0, 0);
    if (!TrySelectCascadeForWorldPos(worldPos, cameraDistance, primaryCascadeIndex, primaryCellCoord))
        return;

    EmitVisibleCell(primaryCellCoord, primaryCascadeIndex, maxVisibleCells);

#if SURFEL_GI_ENABLE_BOUNDARY_OVERLAP
    uint boundaryLowCascade = 0u;
    uint boundaryHighCascade = 0u;
    if (TryGetBoundaryCascadePair(cameraDistance, boundaryLowCascade, boundaryHighCascade))
    {
        const uint secondaryCascadeIndex = (primaryCascadeIndex == boundaryLowCascade) ? boundaryHighCascade : boundaryLowCascade;
        if (secondaryCascadeIndex != primaryCascadeIndex)
        {
            int3 secondaryCellCoord = int3(0, 0, 0);
            if (TryComputeCellCoordForCascade(worldPos, secondaryCascadeIndex, secondaryCellCoord))
                EmitVisibleCell(secondaryCellCoord, secondaryCascadeIndex, maxVisibleCells);
        }
    }
#endif
}
