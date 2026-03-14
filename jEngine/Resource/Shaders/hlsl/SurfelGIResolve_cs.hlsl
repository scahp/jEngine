#include "common.hlsl"
#include "SurfelGIClipmapLookup.hlsl"

// This shader is the bridge between surfel-space lighting and pixel-space shading.
// Earlier passes store irradiance on surfels; this pass asks which surfels should influence
// the current screen pixel and combines their irradiance into a screen-space texture.

uint GetDesiredSlotsPerCell(uint cascadeIndex)
{
    const float value = SurfelGIGetPackedFloat(ResolveCommon.SurfelsPerCellPacked, cascadeIndex);
    return max((uint)round(value), 1u);
}

uint GetSlotsPerCell(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint maxSlotsPerCell = min(max((uint)ResolveCommon.SurfelPageSize, 1u), 5u);
    const uint clampedDesired = clamp(desiredSlotsPerCell, 1u, maxSlotsPerCell);
    return min(max(1u, maxSurfels), clampedDesired);
}

uint GetCellCount(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    return max(1u, maxSurfels / slotsPerCell);
}

uint GetCascadePartitionCapacity(uint maxSurfels, uint cascadeIndex)
{
    const uint cascadeCount = (uint)SURFEL_GI_CASCADE_COUNT;
    const uint c = min(cascadeIndex, cascadeCount - 1u);
    const uint base = maxSurfels / cascadeCount;
    const uint rem = maxSurfels % cascadeCount;
    return max(1u, base + ((c < rem) ? 1u : 0u));
}

float ComputeSurfelWeight(float3 pixelWorldPos, float3 pixelWorldNormal, jSurfelGPU surfel, jSurfelIrradianceGPU irradiance)
{
    // Resolve is intentionally conservative: a surfel should only influence pixels that look like
    // they lie on the same local surface patch. We therefore combine:
    // - plane distance: reject pixels far away from the surfel's tangent plane
    // - radial distance: reject pixels outside the surfel's footprint
    // - normal alignment: avoid mixing unrelated surface orientations
    // - receiver facing: avoid using the back side of the surfel too strongly
    // - confidence: favor surfels whose temporal history has matured
    const float3 surfelNormal = normalize(surfel.NormalSeenFrame.xyz);
    const float surfelRadius = max(surfel.PositionRadius.w, 0.001);
    const float resolveSoftness = max(ResolveCommon.ResolveSoftness, 0.1);
    const float planeExtent = surfelRadius * (1.25 * resolveSoftness);
    const float radialExtent = surfelRadius * (2.0 * resolveSoftness);
    const float3 delta = pixelWorldPos - surfel.PositionRadius.xyz;
    const float planeDistance = abs(dot(delta, surfelNormal));
    const float3 tangentOffset = delta - surfelNormal * dot(delta, surfelNormal);
    const float radialDistance = length(tangentOffset);
    const float normalAlignment = saturate(dot(pixelWorldNormal, surfelNormal));
    const float receiverFacing = saturate(dot(surfelNormal, normalize(pixelWorldPos - surfel.PositionRadius.xyz)) * 0.5 + 0.5);
    const float planeWeight = saturate(1.0 - planeDistance / max(planeExtent, 0.001));
    const float radialWeight = saturate(1.0 - radialDistance / max(radialExtent, 0.001));
    const float confidenceWeight = saturate(irradiance.IrradianceAndCount.w / 16.0);
    return planeWeight * radialWeight * normalAlignment * receiverFacing * confidenceWeight;
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    // Reconstruct the pixel's world-space surface point first. Every later resolve decision is
    // made relative to this point.
    const int2 pixel = int2(GlobalInvocationID.xy);
    const int2 screenSize = int2(ResolveCommon.ScreenSize);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
    {
        Result[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, ResolveCommon.InvP);
    const float3 worldPos = mul(ResolveCommon.InvV, float4(viewPos, 1.0)).xyz;
    float3 worldNormal = GBufferNormalTexture.SampleLevel(GBufferNormalTextureSampler, uv, 0).xyz * 2.0 - 1.0;
    const float normalLenSq = dot(worldNormal, worldNormal);
    if (normalLenSq <= 1e-6)
    {
        Result[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    worldNormal *= rsqrt(normalLenSq);

    const uint maxSurfels = max((uint)ResolveCommon.MaxSurfels, 1u);
    const float cascade0CellSize = max(ResolveCommon.GridCellSize, 0.1);
    const int neighborRadius = clamp(ResolveCommon.NeighborCellRadius, 0, 3);

    float3 irradianceSum = 0.0;
    float weightSum = 0.0;

    // Search nearby cells in every cascade. This is effectively a sparse neighborhood query over
    // the surfel clipmap. The same cell-lookup logic is shared with visualization / hover select.
    [loop] for (uint cascadeIndex = 0u; cascadeIndex < (uint)SURFEL_GI_CASCADE_COUNT; ++cascadeIndex)
    {
        const float cellSize = cascade0CellSize * SurfelGIGetCascadeScale(ResolveCommon.CascadeCellScaleFromPrevPacked, cascadeIndex);
        const int3 baseCellCoord = int3(floor(worldPos / cellSize));
        const uint desiredSlotsPerCell = GetDesiredSlotsPerCell(cascadeIndex);
        const uint cascadeCapacity = GetCascadePartitionCapacity(maxSurfels, cascadeIndex);
        const uint slotsPerCell = GetSlotsPerCell(cascadeCapacity, desiredSlotsPerCell);

        [loop] for (int z = -neighborRadius; z <= neighborRadius; ++z)
        {
            [loop] for (int y = -neighborRadius; y <= neighborRadius; ++y)
            {
                [loop] for (int x = -neighborRadius; x <= neighborRadius; ++x)
                {
                    const int3 queryCellCoord = baseCellCoord + int3(x, y, z);
                    uint baseIndex = 0u;
                    if (!SurfelGITryGetCellBaseIndex(
                        queryCellCoord,
                        maxSurfels,
                        (uint)ResolveCommon.SurfelPageSize,
                        (uint)ResolveCommon.SurfelPageTableCapacity,
                        cascadeIndex,
                        ResolveCommon.CascadeClipmapGridDimXPacked,
                        ResolveCommon.CascadeClipmapGridDimYPacked,
                        ResolveCommon.CascadeClipmapGridDimZPacked,
                        ResolveCommon.CascadeOriginCellXPacked,
                        ResolveCommon.CascadeOriginCellYPacked,
                        ResolveCommon.CascadeOriginCellZPacked,
                        ResolveCommon.CascadeRingOffsetXPacked,
                        ResolveCommon.CascadeRingOffsetYPacked,
                        ResolveCommon.CascadeRingOffsetZPacked,
                        ResolveCommon.CascadeCellBasePacked,
                        baseIndex))
                    {
                        continue;
                    }

                    [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
                    {
                        const uint surfelIndex = baseIndex + slot;
                        const jSurfelGPU surfel = SurfelPool[surfelIndex];
                        if (surfel.Extra.y < 0.5)
                            continue;
                        if ((uint)round(surfel.Extra.w) != cascadeIndex)
                            continue;

                        const float3 surfelPos = surfel.PositionRadius.xyz;
                        const int3 surfelCellCoord = int3(floor(surfelPos / cellSize));
                        if (any(surfelCellCoord != queryCellCoord))
                            continue;

                        const jSurfelIrradianceGPU irradiance = SurfelIrradianceBuffer[surfelIndex];
                        // IrradianceAndCount.xyz stores the MSME long-term mean. That is the stable
                        // lighting value meant for downstream shading.
                        const float irradianceWarmupUpdates = max(ResolveCommon.ResolveIrradianceWarmupUpdates, 0.0);
                        const float maturity = (irradianceWarmupUpdates > 0.0) ? saturate(irradiance.IrradianceAndCount.w / irradianceWarmupUpdates) : 1.0;
                        const float3 surfelIrradiance = max(irradiance.IrradianceAndCount.xyz, 0.0) * maturity;
                        const float weight = ComputeSurfelWeight(worldPos, worldNormal, surfel, irradiance);
                        if (weight <= 1e-5)
                            continue;

                        irradianceSum += surfelIrradiance * weight;
                        weightSum += weight;
                    }
                }
            }
        }
    }

    // The resolve output is irradiance per pixel. ApplySurfelGI_cs.hlsl converts this into
    // reflected diffuse light by multiplying it with albedo / PI.
    const float3 resolvedIrradiance = (weightSum > 1e-5) ? (irradianceSum / weightSum) : float3(0.0, 0.0, 0.0);
    Result[pixel] = float4(resolvedIrradiance, 1.0);
}
