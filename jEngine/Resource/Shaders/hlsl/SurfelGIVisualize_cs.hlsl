#include "common.hlsl"

#ifndef SURFEL_GI_CASCADE_COUNT
    #define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)
#define SURFEL_GI_ENABLE_OVERFLOW 0

struct VisualizeUniformBuffer
{
    float4x4 InvP;
    float4x4 InvV;
    float2 ScreenSize;
    float BlendAlpha;
    float GridCellSize;
    float4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeStartDistancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    int MaxSurfels;
    int SurfelPageSize;
    int SurfelPageTableCapacity;
    int NeighborCellRadius;
    int BlendWithScene;
    int ShowStateDebug;
    float4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    float4 CascadeCellCountPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    int ShowCellDebug;
    int ShowUnderfilledCellDebug;
    int ShowCellGrid;
    int ShowSpawnAttemptDebug;
    int ShowIrradianceDebug;
    int IrradianceDebugMode;
    int2 Padding0;
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

RWTexture2D<float4> Result : register(u0, space0);
Texture2D DepthTexture : register(t1, space0);
SamplerState DepthTextureSampler : register(s1, space0);
Texture2D LinearDepthTexture : register(t2, space0);
StructuredBuffer<SurfelData> SurfelPool : register(t3, space0);
Texture2D SpawnAttemptTexture : register(t4, space0);
StructuredBuffer<uint> SurfelCellPageTable : register(t6, space0);
StructuredBuffer<SurfelIrradianceData> SurfelIrradianceBuffer : register(t7, space0);
StructuredBuffer<uint> WinnerScoreBuffer : register(t8, space0);
StructuredBuffer<uint> WinnerIndexBuffer : register(t9, space0);
Texture2D GBufferNormalTexture : register(t10, space0);

cbuffer VisualizeCommon : register(b5, space0)
{
    VisualizeUniformBuffer VisualizeCommon;
}

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

float3 CellDebugColor(int3 cellCoord, uint cascadeIndex)
{
    uint h = Hash3(cellCoord);
    h ^= HashU32(cascadeIndex * 0x9e3779b9u);
    h = HashU32(h);
    const float r = (float)(h & 255u) / 255.0;
    const float g = (float)((h >> 8u) & 255u) / 255.0;
    const float b = (float)((h >> 16u) & 255u) / 255.0;
    return saturate(float3(0.2, 0.2, 0.2) + float3(r, g, b) * 0.8);
}

float3 RadiusDebugColor(float radius)
{
    // Log-compress wide radius range, then map small->large as Blue->Cyan->Green->Yellow->Red.
    const float t = saturate((log2(max(radius, 0.0001) + 1.0) - 1.0) / 6.0);
    const float4 c0 = float4(0.12, 0.35, 1.00, 0.0);
    const float4 c1 = float4(0.12, 0.90, 1.00, 0.25);
    const float4 c2 = float4(0.15, 1.00, 0.30, 0.5);
    const float4 c3 = float4(1.00, 0.95, 0.15, 0.75);
    const float4 c4 = float4(1.00, 0.20, 0.15, 1.0);

    if (t < c1.w)
        return lerp(c0.xyz, c1.xyz, saturate(t / max(c1.w - c0.w, 0.0001)));
    if (t < c2.w)
        return lerp(c1.xyz, c2.xyz, saturate((t - c1.w) / max(c2.w - c1.w, 0.0001)));
    if (t < c3.w)
        return lerp(c2.xyz, c3.xyz, saturate((t - c2.w) / max(c3.w - c2.w, 0.0001)));
    return lerp(c3.xyz, c4.xyz, saturate((t - c3.w) / max(c4.w - c3.w, 0.0001)));
}

float3 IrradianceDebugColor(float3 irradiance, float historyWeight)
{
    if (historyWeight < 0.01)
        return float3(1.0, 0.0, 1.0);

    const float luminance = dot(irradiance, float3(0.2126, 0.7152, 0.0722));
    const float mappedLuma = saturate(log2(1.0 + luminance) / log2(1.0 + 16.0));
    const float3 chroma = (luminance > 1e-5) ? (irradiance / luminance) : float3(0.0, 0.0, 0.0);
    const float3 gammaCorrected = pow(saturate(chroma * mappedLuma), 1.0 / 2.2);
    const float confidence = saturate(0.3 + historyWeight / 12.0);
    return lerp(float3(0.06, 0.06, 0.06), gammaCorrected, confidence);
}

float3 MSMEScalarDebugColor(float value)
{
    const float t = saturate(value);
    return lerp(float3(0.05, 0.08, 0.20), float3(1.0, 0.22, 0.10), t);
}

float3 MSMEStateDebugColor(float count, float vbbr, float inconsistency)
{
    const float countVis = saturate(count / 32.0);
    const float inconsistencyVis = saturate(inconsistency / 2.0);
    return saturate(float3(countVis, vbbr, inconsistencyVis));
}

uint GetSlotsPerCell(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint maxSlotsPerCell = min(max((uint)VisualizeCommon.SurfelPageSize, 1u), 5u);
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
    const float4 packed = VisualizeCommon.SurfelsPerCellPacked[packIndex];
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
    const float4 packedX = VisualizeCommon.CascadeClipmapGridDimXPacked[packIndex];
    const float4 packedY = VisualizeCommon.CascadeClipmapGridDimYPacked[packIndex];
    const float4 packedZ = VisualizeCommon.CascadeClipmapGridDimZPacked[packIndex];
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
    const float cellSize = max(VisualizeCommon.GridCellSize, 0.1) * GetCascadeScale(cascadeIndex);
    const float3 cameraWorldPos = mul(VisualizeCommon.InvV, float4(0.0, 0.0, 0.0, 1.0)).xyz;
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

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)VisualizeCommon.SurfelPageSize, 1u), maxSurfels);
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

int GetPackedInt(float4 packedArray[SURFEL_GI_CASCADE_PACKED_COUNT], uint cascadeIndex)
{
    return (int)round(GetPackedFloat(packedArray, cascadeIndex));
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
    const int dimX = max(GetPackedInt(VisualizeCommon.CascadeClipmapGridDimXPacked, cascadeIndex), 1);
    const int dimY = max(GetPackedInt(VisualizeCommon.CascadeClipmapGridDimYPacked, cascadeIndex), 1);
    const int dimZ = max(GetPackedInt(VisualizeCommon.CascadeClipmapGridDimZPacked, cascadeIndex), 1);
    return int3(dimX, dimY, dimZ);
}

int3 GetCascadeOriginCell(uint cascadeIndex)
{
    return int3(
        GetPackedInt(VisualizeCommon.CascadeOriginCellXPacked, cascadeIndex),
        GetPackedInt(VisualizeCommon.CascadeOriginCellYPacked, cascadeIndex),
        GetPackedInt(VisualizeCommon.CascadeOriginCellZPacked, cascadeIndex));
}

int3 GetCascadeRingOffset(uint cascadeIndex)
{
    return int3(
        GetPackedInt(VisualizeCommon.CascadeRingOffsetXPacked, cascadeIndex),
        GetPackedInt(VisualizeCommon.CascadeRingOffsetYPacked, cascadeIndex),
        GetPackedInt(VisualizeCommon.CascadeRingOffsetZPacked, cascadeIndex));
}

uint GetCascadeCellBase(uint cascadeIndex)
{
    return GetPackedUint(VisualizeCommon.CascadeCellBasePacked, cascadeIndex);
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
    if (cellLinear >= max((uint)VisualizeCommon.SurfelPageTableCapacity, 1u))
        return false;

    const uint pageSize = GetPageSize(maxSurfels);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return false;
    const uint base = cellLinear * pageSize;
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
        const float4 packed = VisualizeCommon.CascadeCellScaleFromPrevPacked[packIndex];
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
        const float4 packed = VisualizeCommon.CascadeStartDistancePacked[packIndex];
        const float startDistance = (lane == 0u) ? packed.x : ((lane == 1u) ? packed.y : ((lane == 2u) ? packed.z : packed.w));
        if (cameraDistance >= max(startDistance, 0.0))
            cascade = i;
    }
    return cascade;
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const int2 pixel = int2(GlobalInvocationID.xy);
    const int2 screenSize = int2(VisualizeCommon.ScreenSize);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
    {
        Result[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, VisualizeCommon.InvP);
    const float3 worldPos = mul(VisualizeCommon.InvV, float4(viewPos, 1.0)).xyz;
    const float3 worldNormal = normalize(GBufferNormalTexture.SampleLevel(DepthTextureSampler, uv, 0).xyz * 2.0 - 1.0);
    const float cascade0CellSize = max(VisualizeCommon.GridCellSize, 0.1);
    const float cameraDistance = length(viewPos);
    const uint debugCascadeIndex = GetCascadeIndexByDistance(cameraDistance);
    const float debugCellSize = cascade0CellSize * GetCascadeScale(debugCascadeIndex);

    const uint maxSurfels = max((uint)VisualizeCommon.MaxSurfels, 1u);
    const int neighborRadius = clamp(VisualizeCommon.NeighborCellRadius, 0, 3);

    float bestDist2 = 1e38;
    float3 bestColor = float3(0.0, 0.0, 0.0);
    float surfelMask = 0.0;
    int bestCascadeIndex = -1;
    float bestGridDist2 = 1e38;
    int gridCascadeIndex = -1;

    [loop] for (uint cascadeIndex = 0u; cascadeIndex < (uint)SURFEL_GI_CASCADE_COUNT; ++cascadeIndex)
    {
        const float cascadeScale = GetCascadeScale(cascadeIndex);
        const float cellSize = cascade0CellSize * cascadeScale;
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
                        if (isDormant && VisualizeCommon.ShowStateDebug == 0)
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
                        const float r2 = surfelRadius * surfelRadius;

                        if (d2 < bestGridDist2)
                        {
                            bestGridDist2 = d2;
                            gridCascadeIndex = (int)cascadeIndex;
                        }

                        if (d2 <= r2 && d2 < bestDist2)
                        {
                            bestDist2 = d2;
                            surfelMask = 1.0;
                            bestCascadeIndex = (int)cascadeIndex;
                            if (VisualizeCommon.ShowIrradianceDebug != 0)
                            {
                                const SurfelIrradianceData irradianceData = SurfelIrradianceBuffer[surfelIndex];
                                const float3 meanIrradiance = max(irradianceData.IrradianceAndCount.xyz, 0.0);
                                const float3 shortMeanIrradiance = max(irradianceData.MSMEData0.xyz, 0.0);
                                const float3 variance = max(irradianceData.MSMEData1.xyz, 0.0);
                                const float count = irradianceData.IrradianceAndCount.w;
                                const float vbbr = irradianceData.MSMEData0.w;
                                const float inconsistency = irradianceData.MSMEData1.w;

                                if (VisualizeCommon.IrradianceDebugMode == 1)
                                {
                                    bestColor = IrradianceDebugColor(shortMeanIrradiance, count);
                                }
                                else if (VisualizeCommon.IrradianceDebugMode == 2)
                                {
                                    bestColor = IrradianceDebugColor(variance, 1.0);
                                }
                                else if (VisualizeCommon.IrradianceDebugMode == 3)
                                {
                                    bestColor = MSMEScalarDebugColor(saturate(inconsistency / 2.0));
                                }
                                else if (VisualizeCommon.IrradianceDebugMode == 4)
                                {
                                    bestColor = MSMEStateDebugColor(count, vbbr, inconsistency);
                                }
                                else
                                {
                                    bestColor = IrradianceDebugColor(meanIrradiance, count);
                                }
                            }
                            else if (VisualizeCommon.ShowCellDebug != 0)
                            {
                                bestColor = CellDebugColor(surfelCellCoord, surfelCascade);
                            }
                            else
                            {
                                bestColor = RadiusDebugColor(surfelRadius);
                            }

                            if (VisualizeCommon.ShowStateDebug != 0 && VisualizeCommon.ShowCellDebug == 0 && VisualizeCommon.ShowIrradianceDebug == 0)
                            {
                                const float state = s.Extra.x;
                                float3 stateTint = float3(1.0, 1.0, 1.0);
                                // 0:new spawn, 1:stable keep, 2:merged moved, 3:replaced far, 4:stolen far, 5:dormant, 6:revived dormant.
                                if (state < 0.5)
                                    stateTint = float3(1.0, 0.2, 0.2);
                                else if (state < 1.5)
                                    stateTint = float3(0.2, 1.0, 0.2);
                                else if (state < 2.5)
                                    stateTint = float3(1.0, 0.95, 0.2);
                                else if (state < 3.5)
                                    stateTint = float3(0.2, 1.0, 1.0);
                                else if (state < 4.5)
                                    stateTint = float3(1.0, 0.2, 1.0);
                                else if (state < 5.5)
                                    stateTint = float3(0.7, 0.7, 0.7);
                                else
                                    stateTint = float3(0.2, 0.6, 1.0);

                                bestColor = stateTint;
                            }
                        }
                    }

#if SURFEL_GI_ENABLE_OVERFLOW
                    const uint bucketIndex = min(GetCellBucketIndex(queryCellCoord, maxSurfels, desiredSlotsPerCell, cascadeIndex), maxSurfels - 1u);
                    int overflowNode = OverflowHeads[bucketIndex].Head;
                    [loop] for (uint iter = 0u; iter < 24u && overflowNode >= 0; ++iter)
                    {
                        const uint nodeIndex = (uint)overflowNode;
                        if (nodeIndex >= maxSurfels)
                            break;

                        const OverflowNode node = OverflowNodes[nodeIndex];
                        const SurfelData s = node.Surfel;
                        const bool isDormant = (s.Extra.y < 0.5) && (abs(s.Extra.x - 5.0) < 0.5);
                        if (s.Extra.y < 0.5 && !isDormant)
                        {
                            overflowNode = node.Next;
                            continue;
                        }
                        if (isDormant && VisualizeCommon.ShowStateDebug == 0)
                        {
                            overflowNode = node.Next;
                            continue;
                        }

                        const uint surfelCascade = (uint)round(s.Extra.w);
                        if (surfelCascade == cascadeIndex)
                        {
                            const float3 surfelPos = s.PositionRadius.xyz;
                            const int3 surfelCellCoord = int3(floor(surfelPos / cellSize));
                            if (all(surfelCellCoord == queryCellCoord))
                            {
                                const float surfelRadius = max(s.PositionRadius.w, 0.0001);
                                const float3 delta = worldPos - surfelPos;
                                const float d2 = dot(delta, delta);
                                const float r2 = surfelRadius * surfelRadius;

                                if (d2 < bestGridDist2)
                                {
                                    bestGridDist2 = d2;
                                    gridCascadeIndex = (int)cascadeIndex;
                                }

                                if (d2 <= r2 && d2 < bestDist2)
                                {
                                    bestDist2 = d2;
                                    surfelMask = 1.0;
                                    bestCascadeIndex = (int)cascadeIndex;
                                    if (VisualizeCommon.ShowCellDebug != 0)
                                    {
                                        bestColor = CellDebugColor(surfelCellCoord, surfelCascade);
                                    }
                                    else
                                    {
                                        bestColor = RadiusDebugColor(surfelRadius);
                                    }
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

    float3 visualizeColor;
    const bool hasSurfel = (bestDist2 < 1e37);
    if (hasSurfel)
    {
        visualizeColor = bestColor;
    }
    else
    {
        const float sceneDepth = LinearDepthTexture.Load(int3(pixel, 0)).x;
        const float v = saturate(sceneDepth * 0.02);
        visualizeColor = float3(v * 0.08, v * 0.08, v * 0.08);
    }

    if (VisualizeCommon.ShowUnderfilledCellDebug != 0 && VisualizeCommon.ShowIrradianceDebug == 0)
    {
        const float debugCascadeScale = GetCascadeScale(debugCascadeIndex);
        const float debugCellSizeForOcc = cascade0CellSize * debugCascadeScale;
        const int3 debugCellCoord = int3(floor(worldPos / debugCellSizeForOcc));
        const uint debugDesiredSlots = GetDesiredSlotsPerCell(debugCascadeIndex);
        const uint debugCascadeCapacity = GetCascadePartitionCapacity(maxSurfels, debugCascadeIndex);
        const uint debugSlotsPerCell = GetSlotsPerCell(debugCascadeCapacity, debugDesiredSlots);
        uint debugBaseIndex = 0u;
        const bool hasDebugCellPage = TryGetCellBaseIndex(debugCellCoord, maxSurfels, debugCascadeIndex, debugBaseIndex);

        uint aliveCount = 0u;
        bool hasNormalMismatch = false;
        [loop] for (uint slot = 0u; slot < debugSlotsPerCell && hasDebugCellPage; ++slot)
        {
            const uint surfelIndex = debugBaseIndex + slot;
            const SurfelData s = SurfelPool[surfelIndex];
            if (s.Extra.y <= 0.5)
                continue;

            const uint surfelCascade = (uint)round(s.Extra.w);
            if (surfelCascade != debugCascadeIndex)
                continue;

            const int3 surfelCellCoord = int3(floor(s.PositionRadius.xyz / debugCellSizeForOcc));
            if (any(surfelCellCoord != debugCellCoord))
            {
                continue;
            }

            const float3 surfelNormal = normalize(s.NormalSeenFrame.xyz);
            if (dot(surfelNormal, worldNormal) < 0.7)
                hasNormalMismatch = true;

            aliveCount++;
        }

        if (hasNormalMismatch)
        {
            // At least one surfel normal in this debug cell is significantly different from current world normal.
            const float3 normalMismatchTint = float3(1.0, 0.1, 0.8);
            visualizeColor = lerp(visualizeColor, normalMismatchTint, 0.6);
        }
        else if (aliveCount < debugDesiredSlots)
        {
            // Underfilled-cell reason hint:
            // blue  -> no winner for this cell (likely priority/rejection path),
            // yellow-> winner exists, but cell is still underfilled (placement/path mismatch).
            uint debugCellLinear = 0u;
            const bool hasDebugCellLinear = TryWorldCellToLinear(debugCellCoord, debugCascadeIndex, debugCellLinear);
            bool hasWinner = false;
            if (hasDebugCellLinear && debugCellLinear < max((uint)VisualizeCommon.SurfelPageTableCapacity, 1u))
            {
                const uint winnerScore = WinnerScoreBuffer[debugCellLinear];
                const uint winnerIndex = WinnerIndexBuffer[debugCellLinear];
                hasWinner = (winnerScore > 0u) && (winnerIndex != 0xffffffffu);
            }

            const float fillRatio = (float)aliveCount / max((float)debugDesiredSlots, 1.0);
            const float intensity = saturate((1.0 - fillRatio) * 0.85 + 0.15);
            const float3 underfilledTint = hasWinner ? float3(1.0, 0.9, 0.2) : float3(0.15, 0.6, 1.0);
            visualizeColor = lerp(visualizeColor, underfilledTint, intensity * 0.7);
        }
    }

    if (VisualizeCommon.ShowCellGrid != 0 && VisualizeCommon.ShowIrradianceDebug == 0)
    {
        const uint resolvedGridCascadeIndex = (gridCascadeIndex >= 0) ? (uint)gridCascadeIndex : debugCascadeIndex;
        const float gridCellSize = cascade0CellSize * GetCascadeScale(resolvedGridCascadeIndex);
        const float3 gridCoord = worldPos / max(gridCellSize, 0.001);
        const float3 fracCoord = frac(gridCoord);
        const float3 edgeDist = min(fracCoord, 1.0 - fracCoord);
        const float nearestEdge = min(edgeDist.x, min(edgeDist.y, edgeDist.z));
        const float lineWidth = 0.02;
        const float lineMask = 1.0 - smoothstep(lineWidth, lineWidth * 2.5, nearestEdge);
        visualizeColor = lerp(visualizeColor, float3(1.0, 1.0, 1.0), lineMask * 0.85);
    }

    if (VisualizeCommon.ShowSpawnAttemptDebug != 0)
    {
        float bestAttemptStrength = 0.0;
        float3 bestAttemptColor = float3(0.0, 0.0, 0.0);
        [unroll] for (int dy = -1; dy <= 1; ++dy)
        {
            [unroll] for (int dx = -1; dx <= 1; ++dx)
            {
                const int2 samplePixel = clamp(pixel + int2(dx, dy), int2(0, 0), screenSize - 1);
                const float3 sampleColor = SpawnAttemptTexture.Load(int3(samplePixel, 0)).xyz;
                const float luma = max(sampleColor.x, max(sampleColor.y, sampleColor.z));
                const float dist2 = (float)(dx * dx + dy * dy);
                const float falloff = (dist2 <= 0.0) ? 1.0 : ((dist2 <= 1.0) ? 0.6 : 0.3);
                const float strength = luma * falloff;
                if (strength > bestAttemptStrength)
                {
                    bestAttemptStrength = strength;
                    bestAttemptColor = sampleColor;
                }
            }
        }

        if (bestAttemptStrength > 0.001)
        {
            visualizeColor = lerp(visualizeColor, bestAttemptColor, saturate(bestAttemptStrength));
        }
    }

    Result[pixel] = float4(visualizeColor, surfelMask);
}
