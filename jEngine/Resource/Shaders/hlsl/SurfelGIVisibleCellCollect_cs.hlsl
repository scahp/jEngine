#include "common.hlsl"
#include "SurfelGIClipmapLookup.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_BOUNDARY_BAND_SCALE 1.0
// Temp debug switch: 0 keeps a single cascade assignment (no boundary dual emit).
#define SURFEL_GI_ENABLE_BOUNDARY_OVERLAP 0

int3 GetCascadeDim(uint cascadeIndex)
{
    return int3(
        max((int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeClipmapGridDimXPacked, cascadeIndex)), 1),
        max((int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeClipmapGridDimYPacked, cascadeIndex)), 1),
        max((int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeClipmapGridDimZPacked, cascadeIndex)), 1));
}

int3 GetCascadeOriginCell(uint cascadeIndex)
{
    return int3(
        (int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeOriginCellXPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeOriginCellYPacked, cascadeIndex)),
        (int)round(SurfelGIGetPackedFloat(ComputeCommon.CascadeOriginCellZPacked, cascadeIndex)));
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

bool TryComputeCellCoordForCascade(float3 worldPos, uint cascadeIndex, out int3 outCellCoord)
{
    const float cellSize = max(ComputeCommon.GridCellSize, 0.1) * SurfelGIGetCascadeScale(ComputeCommon.CascadeCellScalePacked, cascadeIndex);
    const int3 cellCoord = int3(floor(worldPos / cellSize));
    const int3 local = cellCoord - GetCascadeOriginCell(cascadeIndex);
    const int3 dim = GetCascadeDim(cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    outCellCoord = cellCoord;
    return true;
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

void EmitVisibleCell(int3 cellCoord, uint cascadeIndex)
{
    uint cellLinear = 0u;
    if (!TryWorldCellToLinear(cellCoord, cascadeIndex, cellLinear))
        return;
    if (cellLinear >= max((uint)ComputeCommon.SurfelPageTableCapacity, 1u))
        return;

    uint previousCount = 0u;
    InterlockedAdd(VisibleCellCounterBuffer[cellLinear], 1u, previousCount);
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const int2 screenSize = int2(ComputeCommon.ScreenSize);
    const int tileSize = max(ComputeCommon.TileSize, 1);
    const int2 dispatchSize = max((screenSize + (tileSize - 1)) / tileSize, int2(1, 1));
    const int2 tileCoord = int2(GlobalInvocationID.xy);
    if (tileCoord.x >= dispatchSize.x || tileCoord.y >= dispatchSize.y)
        return;

    const int2 samplePixel = min(tileCoord * tileSize + int2(tileSize / 2, tileSize / 2), screenSize - 1);
    const float2 uv = (float2(samplePixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
        return;

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, ComputeCommon.InvP);
    const float3 worldPos = mul(ComputeCommon.InvV, float4(viewPos, 1.0)).xyz;
    const float cameraDistance = length(viewPos);
    
    uint primaryCascadeIndex = 0u;
    int3 primaryCellCoord = int3(0, 0, 0);
    if (!TrySelectCascadeForWorldPos(worldPos, cameraDistance, primaryCascadeIndex, primaryCellCoord))
        return;

    EmitVisibleCell(primaryCellCoord, primaryCascadeIndex);

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
                EmitVisibleCell(secondaryCellCoord, secondaryCascadeIndex);
        }
    }
#endif
}
