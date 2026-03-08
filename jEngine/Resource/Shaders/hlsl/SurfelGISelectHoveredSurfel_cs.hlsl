#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)

struct HoverSelectUniformBuffer
{
    float4x4 InvP;
    float4x4 InvV;
    float2 ScreenSize;
    float GridCellSize;
    float4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
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
    int MaxSurfels;
    int SurfelPageSize;
    int SurfelPageTableCapacity;
    int NeighborCellRadius;
    int MousePixelX;
    int MousePixelY;
    int MouseValid;
    int Padding0;
};

struct SurfelData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

struct HoverSelectionData
{
    uint SurfelIndex;
    uint Valid;
    uint MousePixelX;
    uint MousePixelY;
};

struct HoverRayDebugData
{
    float4 OriginAndCount;
    float4 RayDirAndType[16];
};

Texture2D DepthTexture : register(t0, space0);
SamplerState DepthTextureSampler : register(s0, space0);
StructuredBuffer<SurfelData> SurfelPool : register(t1, space0);
StructuredBuffer<uint> SurfelCellPageTable : register(t2, space0);
RWStructuredBuffer<HoverSelectionData> HoverSelectionBuffer : register(u4, space0);
RWStructuredBuffer<HoverRayDebugData> HoverRayDebugBuffer : register(u5, space0);

cbuffer HoverSelectCommon : register(b3, space0)
{
    HoverSelectUniformBuffer HoverSelectCommon;
}

float GetPackedFloat(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    const uint c = min(cascadeIndex, (uint)(SURFEL_GI_CASCADE_COUNT - 1));
    const uint packIndex = c >> 2u;
    const uint lane = c & 3u;
    const float4 packed = packedArray[packIndex];
    return (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
}

int GetPackedInt(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return (int)round(GetPackedFloat(packedArray, cascadeIndex));
}

uint GetDesiredSlotsPerCell(uint cascadeIndex)
{
    const float value = GetPackedFloat(HoverSelectCommon.SurfelsPerCellPacked, cascadeIndex);
    return max((uint)round(value), 1u);
}

uint GetSlotsPerCell(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint maxSlotsPerCell = min(max((uint)HoverSelectCommon.SurfelPageSize, 1u), 5u);
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

float GetCascadeScale(uint cascadeIndex)
{
    float scale = 1.0;
    [loop] for (uint i = 1u; i <= cascadeIndex && i < (uint)SURFEL_GI_CASCADE_COUNT; ++i)
    {
        scale *= max(GetPackedFloat(HoverSelectCommon.CascadeCellScaleFromPrevPacked, i), 1.0);
    }
    return scale;
}

int3 ModWrap3(int3 v, int3 dim)
{
    int3 r = v % dim;
    if (r.x < 0) r.x += dim.x;
    if (r.y < 0) r.y += dim.y;
    if (r.z < 0) r.z += dim.z;
    return r;
}

int3 GetCascadeDimDirect(uint cascadeIndex)
{
    return int3(
        max(GetPackedInt(HoverSelectCommon.CascadeClipmapGridDimXPacked, cascadeIndex), 1),
        max(GetPackedInt(HoverSelectCommon.CascadeClipmapGridDimYPacked, cascadeIndex), 1),
        max(GetPackedInt(HoverSelectCommon.CascadeClipmapGridDimZPacked, cascadeIndex), 1));
}

int3 GetCascadeOriginCell(uint cascadeIndex)
{
    return int3(
        GetPackedInt(HoverSelectCommon.CascadeOriginCellXPacked, cascadeIndex),
        GetPackedInt(HoverSelectCommon.CascadeOriginCellYPacked, cascadeIndex),
        GetPackedInt(HoverSelectCommon.CascadeOriginCellZPacked, cascadeIndex));
}

int3 GetCascadeRingOffset(uint cascadeIndex)
{
    return int3(
        GetPackedInt(HoverSelectCommon.CascadeRingOffsetXPacked, cascadeIndex),
        GetPackedInt(HoverSelectCommon.CascadeRingOffsetYPacked, cascadeIndex),
        GetPackedInt(HoverSelectCommon.CascadeRingOffsetZPacked, cascadeIndex));
}

uint GetCascadeCellBase(uint cascadeIndex)
{
    return (uint)round(GetPackedFloat(HoverSelectCommon.CascadeCellBasePacked, cascadeIndex));
}

bool TryWorldCellToLinear(int3 worldCell, uint cascadeIndex, out uint outCellLinear)
{
    const int3 dim = GetCascadeDimDirect(cascadeIndex);
    const int3 local = worldCell - GetCascadeOriginCell(cascadeIndex);
    if (any(local < 0) || any(local >= dim))
        return false;

    const int3 phys = ModWrap3(local + GetCascadeRingOffset(cascadeIndex), dim);
    const uint localLinear = (uint)(phys.x + dim.x * (phys.y + dim.y * phys.z));
    outCellLinear = GetCascadeCellBase(cascadeIndex) + localLinear;
    return true;
}

bool TryGetCellBaseIndex(int3 cellCoord, uint maxSurfels, uint cascadeIndex, out uint outCellBaseIndex)
{
    uint cellLinear = 0u;
    if (!TryWorldCellToLinear(cellCoord, cascadeIndex, cellLinear))
        return false;
    if (cellLinear >= max((uint)HoverSelectCommon.SurfelPageTableCapacity, 1u))
        return false;

    const uint pageSize = min(max((uint)HoverSelectCommon.SurfelPageSize, 1u), maxSurfels);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return false;

    const uint base = cellLinear * pageSize;
    if (base >= maxSurfels)
        return false;

    outCellBaseIndex = base;
    return true;
}

[numthreads(1, 1, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    HoverSelectionData Result = (HoverSelectionData)0;
    HoverRayDebugData HoverRayDebug = (HoverRayDebugData)0;
    Result.SurfelIndex = 0xffffffffu;
    Result.Valid = 0u;
    Result.MousePixelX = (HoverSelectCommon.MousePixelX >= 0) ? (uint)HoverSelectCommon.MousePixelX : 0u;
    Result.MousePixelY = (HoverSelectCommon.MousePixelY >= 0) ? (uint)HoverSelectCommon.MousePixelY : 0u;
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

    [loop] for (uint cascadeIndex = 0u; cascadeIndex < (uint)SURFEL_GI_CASCADE_COUNT; ++cascadeIndex)
    {
        const float cellSize = cascade0CellSize * GetCascadeScale(cascadeIndex);
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
                    if (!TryGetCellBaseIndex(queryCellCoord, maxSurfels, cascadeIndex, baseIndex))
                        continue;

                    [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
                    {
                        const uint surfelIndex = baseIndex + slot;
                        const SurfelData s = SurfelPool[surfelIndex];
                        const bool isDormant = (s.Extra.y < 0.5) && (abs(s.Extra.x - 5.0) < 0.5);
                        if (s.Extra.y < 0.5 && !isDormant)
                            continue;

                        const uint surfelCascade = (uint)round(s.Extra.w);
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
