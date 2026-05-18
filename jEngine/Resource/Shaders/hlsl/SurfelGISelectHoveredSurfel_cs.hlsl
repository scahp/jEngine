#include "common.hlsl"
#include "SurfelGIClipmapLookup.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif

// This pass selects the surfel that best corresponds to the mouse cursor's current surface point.
// The gather pass uses the result to capture only one surfel's rays, which keeps the debug path
// cheap and easy to understand.

bool TryWorldCellToLinear(int3 worldCell, uint cascadeIndex, out uint outCellLinear)
{
    return SurfelGITryWorldCellToLinear(
        worldCell,
        cascadeIndex,
        HoverSelectCommon.CascadeClipmapGridDimXPacked,
        HoverSelectCommon.CascadeClipmapGridDimYPacked,
        HoverSelectCommon.CascadeClipmapGridDimZPacked,
        HoverSelectCommon.CascadeOriginCellXPacked,
        HoverSelectCommon.CascadeOriginCellYPacked,
        HoverSelectCommon.CascadeOriginCellZPacked,
        HoverSelectCommon.CascadeRingOffsetXPacked,
        HoverSelectCommon.CascadeRingOffsetYPacked,
        HoverSelectCommon.CascadeRingOffsetZPacked,
        HoverSelectCommon.CascadeCellBasePacked,
        outCellLinear);
}

bool TryGetCellBaseIndex(int3 cellCoord, uint maxSurfels, uint cascadeIndex, out uint outCellBaseIndex)
{
    return SurfelGITryGetCellBaseIndex(
        cellCoord,
        maxSurfels,
        min(max((uint)HoverSelectCommon.SurfelPageSize, 1u), maxSurfels),
        (uint)HoverSelectCommon.SurfelPageTableCapacity,
        cascadeIndex,
        HoverSelectCommon.CascadeClipmapGridDimXPacked,
        HoverSelectCommon.CascadeClipmapGridDimYPacked,
        HoverSelectCommon.CascadeClipmapGridDimZPacked,
        HoverSelectCommon.CascadeOriginCellXPacked,
        HoverSelectCommon.CascadeOriginCellYPacked,
        HoverSelectCommon.CascadeOriginCellZPacked,
        HoverSelectCommon.CascadeRingOffsetXPacked,
        HoverSelectCommon.CascadeRingOffsetYPacked,
        HoverSelectCommon.CascadeRingOffsetZPacked,
        HoverSelectCommon.CascadeCellBasePacked,
        outCellBaseIndex);
}

[numthreads(1, 1, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    // Reset both outputs on the GPU every frame. This avoids relying on CPU-side buffer uploads
    // and guarantees that "no hovered surfel" is represented explicitly.
    jSurfelGIHoverSelectionGPU Result = (jSurfelGIHoverSelectionGPU)0;
    jSurfelGIHoverRayDebugGPU HoverRayDebug = (jSurfelGIHoverRayDebugGPU)0;
    Result.SurfelIndex = 0xffffffffu;
    Result.Valid = 0u;
    HoverRayDebugBuffer[0] = HoverRayDebug;

    if (HoverSelectCommon.MouseValid == 0)
    {
        HoverSelectionBuffer[0] = Result;
        return;
    }

    const int2 pixel = int2(HoverSelectCommon.MousePixelX, HoverSelectCommon.MousePixelY);
    const int2 screenSize = int2(HoverSelectCommon.ScreenSize);
    if (any(pixel < 0) || pixel.x >= screenSize.x || pixel.y >= screenSize.y)
    {
        HoverSelectionBuffer[0] = Result;
        return;
    }

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
    {
        HoverSelectionBuffer[0] = Result;
        return;
    }

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, HoverSelectCommon.InvP);
    const float3 worldPos = mul(HoverSelectCommon.InvV, float4(viewPos, 1.0)).xyz;
    const float cascade0CellSize = max(HoverSelectCommon.GridCellSize, 0.1);
    const uint maxSurfels = max((uint)HoverSelectCommon.MaxSurfels, 1u);
    const int neighborRadius = clamp(HoverSelectCommon.NeighborCellRadius, 0, 3);

    float bestDist2 = 1e38;
    uint bestSurfelIndex = 0xffffffffu;

    // Search the same neighborhood shape used by resolve/visualize so the picked surfel matches
    // what the user is looking at in other debug views.
    [loop] for (uint cascadeIndex = 0u; cascadeIndex < (uint)SURFEL_GI_CASCADE_COUNT; ++cascadeIndex)
    {
        const float cellSize = cascade0CellSize * SurfelGIGetCascadeScale(HoverSelectCommon.CascadeCellScalePacked, cascadeIndex);
        const int3 baseCellCoord = int3(floor(worldPos / cellSize));
        const uint desiredSlotsPerCell = SurfelGIGetDesiredSlotsPerCell(HoverSelectCommon.SurfelsPerCellPacked, cascadeIndex);

        [loop] for (int z = -neighborRadius; z <= neighborRadius; ++z)
        {
            [loop] for (int y = -neighborRadius; y <= neighborRadius; ++y)
            {
                [loop] for (int x = -neighborRadius; x <= neighborRadius; ++x)
                {
                    const int3 queryCellCoord = baseCellCoord + int3(x, y, z);
                    uint baseIndex = 0u;
                    if (!TryGetCellBaseIndex(queryCellCoord, maxSurfels, cascadeIndex, baseIndex))
                        continue;

                    [loop] for (uint slot = 0u; slot < desiredSlotsPerCell; ++slot)
                    {
                        const uint surfelIndex = baseIndex + slot;
                        const jSurfelGPU s = SurfelPool[surfelIndex];
                        const bool isDormant = (s.IsActive == 0u) && (s.State == SURFEL_GI_SURFEL_STATE_DORMANT);
                        if (s.IsActive == 0u && !isDormant)
                            continue;

                        const uint surfelCascade = s.CascadeIndex;
                        if (surfelCascade != cascadeIndex)
                            continue;

                        const float3 surfelPos = s.PositionRadius.xyz;
                        const int3 surfelCellCoord = int3(floor(surfelPos / cellSize));
                        if (any(surfelCellCoord != queryCellCoord))
                            continue;

                        const float surfelRadius = max(s.PositionRadius.w, 0.0001);
                        const float3 delta = worldPos - surfelPos;
                        const float d2 = dot(delta, delta);
                        if (d2 <= surfelRadius * surfelRadius && d2 < bestDist2)
                        {
                            bestDist2 = d2;
                            bestSurfelIndex = surfelIndex;
                        }
                    }
                }
            }
        }
    }

    Result.SurfelIndex = bestSurfelIndex;
    Result.Valid = (bestSurfelIndex != 0xffffffffu) ? 1u : 0u;
    HoverSelectionBuffer[0] = Result;
}
