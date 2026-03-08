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

RWStructuredBuffer<SurfelData> SurfelPool : register(u0, space0);
RWStructuredBuffer<uint> CellSurfelCount : register(u1, space0);
RWStructuredBuffer<SurfelIrradianceData> SurfelIrradianceBuffer : register(u2, space0);

cbuffer ComputeCommon : register(b3, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
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

int3 GetCascadeDim(uint cascadeIndex)
{
    const int x = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimXPacked, cascadeIndex), 1);
    const int y = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimYPacked, cascadeIndex), 1);
    const int z = max(GetPackedInt(ComputeCommon.CascadeClipmapGridDimZPacked, cascadeIndex), 1);
    return int3(x, y, z);
}

int3 GetCascadeRing(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeRingOffsetXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeRingOffsetYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeRingOffsetZPacked, cascadeIndex));
}

int3 GetCascadeDelta(uint cascadeIndex)
{
    return int3(
        GetPackedInt(ComputeCommon.CascadeDeltaCellXPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeDeltaCellYPacked, cascadeIndex),
        GetPackedInt(ComputeCommon.CascadeDeltaCellZPacked, cascadeIndex));
}

bool GetCascadeClearAll(uint cascadeIndex)
{
    return GetPackedFloat(ComputeCommon.CascadeClearAllPacked, cascadeIndex) > 0.5;
}

uint GetCascadeCellBase(uint cascadeIndex)
{
    return GetPackedUint(ComputeCommon.CascadeCellBasePacked, cascadeIndex);
}

uint GetCascadeCellCount(uint cascadeIndex)
{
    return max(GetPackedUint(ComputeCommon.CascadeCellCountPacked, cascadeIndex), 1u);
}

uint GetPageSize(uint maxSurfels)
{
    return min(max((uint)ComputeCommon.SurfelPageSize, 1u), maxSurfels);
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

bool ShouldClearCell(int3 localCell, int3 dim, int3 delta, bool clearAll)
{
    if (clearAll)
        return true;

    bool shouldClear = false;
    if (delta.x > 0) shouldClear = shouldClear || (localCell.x >= (dim.x - delta.x));
    else if (delta.x < 0) shouldClear = shouldClear || (localCell.x < -delta.x);

    if (delta.y > 0) shouldClear = shouldClear || (localCell.y >= (dim.y - delta.y));
    else if (delta.y < 0) shouldClear = shouldClear || (localCell.y < -delta.y);

    if (delta.z > 0) shouldClear = shouldClear || (localCell.z >= (dim.z - delta.z));
    else if (delta.z < 0) shouldClear = shouldClear || (localCell.z < -delta.z);

    return shouldClear;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint cellLinear = DTid.x;
    const uint cellCapacity = max((uint)ComputeCommon.SurfelPageTableCapacity, 1u);
    if (cellLinear >= cellCapacity)
        return;

    const uint cascadeIndex = GetCascadeIndexFromCellLinear(cellLinear);
    const uint cascadeBase = GetCascadeCellBase(cascadeIndex);
    const uint localLinear = cellLinear - cascadeBase;
    const int3 dim = GetCascadeDim(cascadeIndex);
    const uint dimX = (uint)max(dim.x, 1);
    const uint dimY = (uint)max(dim.y, 1);
    const uint dimZ = (uint)max(dim.z, 1);
    const uint cascadeCellCount = dimX * dimY * dimZ;
    if (localLinear >= cascadeCellCount)
        return;

    uint tmp = localLinear;
    int3 phys;
    phys.x = (int)(tmp % dimX);
    tmp /= dimX;
    phys.y = (int)(tmp % dimY);
    phys.z = (int)(tmp / dimY);

    const int3 ring = GetCascadeRing(cascadeIndex);
    const int3 localCell = ModWrap3(phys - ring, dim);
    const int3 delta = GetCascadeDelta(cascadeIndex);
    const bool clearAll = GetCascadeClearAll(cascadeIndex);
    if (!ShouldClearCell(localCell, dim, delta, clearAll))
        return;

    CellSurfelCount[cellLinear] = 0u;

    const uint maxSurfels = max((uint)ComputeCommon.MaxSurfels, 1u);
    const uint pageSize = GetPageSize(maxSurfels);
    if (cellLinear > ((maxSurfels - 1u) / max(pageSize, 1u)))
        return;
    const uint baseSurfel = cellLinear * pageSize;
    if (baseSurfel >= maxSurfels)
        return;

    [loop] for (uint slot = 0u; slot < pageSize; ++slot)
    {
        const uint surfelIndex = baseSurfel + slot;
        if (surfelIndex >= maxSurfels)
            break;

        SurfelData s;
        s.PositionRadius = float4(0.0, 0.0, 0.0, 0.0);
        s.NormalSeenFrame = float4(0.0, 0.0, 0.0, 0.0);
        s.AlbedoWeight = float4(0.0, 0.0, 0.0, 0.0);
        s.Extra = float4(0.0, 0.0, 0.0, 0.0);
        SurfelPool[surfelIndex] = s;

        SurfelIrradianceData ir;
        ir.IrradianceAndCount = float4(0.0, 0.0, 0.0, 0.0);
        ir.MSMEData0 = float4(0.0, 0.0, 0.0, 0.0);
        ir.MSMEData1 = float4(0.0, 0.0, 0.0, 0.0);
        SurfelIrradianceBuffer[surfelIndex] = ir;
    }
}
