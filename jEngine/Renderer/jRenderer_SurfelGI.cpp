#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "jSceneRenderTargets.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jRaytracingScene.h"
#include <cmath>
#include <limits>

namespace
{
struct jSurfelGPU
{
    Vector4 PositionRadius = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 NormalSeenFrame = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 AlbedoWeight = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 Extra = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct alignas(16) jSurfelIrradianceGPU
{
    Vector4 IrradianceAndWeight = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct alignas(16) jVisibleCellGPU
{
    int32 CellX = 0;
    int32 CellY = 0;
    int32 CellZ = 0;
    int32 Cascade = 0;
};

struct alignas(16) jVisibleCellCounterGPU
{
    uint32 Count = 0;
    uint32 Padding0 = 0;
    uint32 Padding1 = 0;
    uint32 Padding2 = 0;
};

struct alignas(16) jSurfelGIStatsGPU
{
    uint32 ActiveCount = 0;
    uint32 DormantCount = 0;
    uint32 MismatchCount = 0;
    uint32 TTLRetireCount = 0;
    uint32 PageGCCount = 0;
    uint32 PageEvictCount = 0;
    uint32 ReservoirOverflowCount = 0;
    uint32 ReservoirRejectedCount = 0;
};

struct alignas(16) jSurfelActiveCounterGPU
{
    uint32 Count = 0;
    uint32 Padding0 = 0;
    uint32 Padding1 = 0;
    uint32 Padding2 = 0;
};

struct alignas(16) jSurfelInlineRayDispatchArgsGPU
{
    uint32 GroupCountX = 1;
    uint32 GroupCountY = 1;
    uint32 GroupCountZ = 1;
    uint32 Padding0 = 0;
};

struct jSurfelClipmapCascadeRuntimeState
{
    bool Initialized = false;
    int32 DimX = 0;
    int32 DimY = 0;
    int32 DimZ = 0;
    int32 OriginX = 0;
    int32 OriginY = 0;
    int32 OriginZ = 0;
    int32 RingOffsetX = 0;
    int32 RingOffsetY = 0;
    int32 RingOffsetZ = 0;
};

std::shared_ptr<jBuffer> GSurfelPoolBuffer;
int32 GSurfelPoolMaxCount = 0;
std::shared_ptr<jBuffer> GSurfelIrradianceBuffer;
int32 GSurfelIrradianceCapacity = 0;
int32 GSurfelPageSize = 8;
std::shared_ptr<jBuffer> GVisibleCellWorklistBuffer;
std::shared_ptr<jBuffer> GVisibleCellCounterBuffer;
int32 GVisibleCellWorklistCapacity = 0;
std::shared_ptr<jBuffer> GSurfelCellPageTableBuffer;
int32 GSurfelCellPageTableCapacity = 0;
std::shared_ptr<jBuffer> GSurfelGICandidateBuffer;
int32 GSurfelGICandidateCapacity = 0;
std::shared_ptr<jBuffer> GSurfelGIWinnerScoreBuffer;
std::shared_ptr<jBuffer> GSurfelGIWinnerIndexBuffer;
std::shared_ptr<jBuffer> GSurfelGIWinnerLockBuffer;
int32 GSurfelGIWinnerCapacity = 0;
std::shared_ptr<jBuffer> GSurfelGIStatsBuffer;
std::shared_ptr<jBuffer> GSurfelGIActiveIndexBuffer;
int32 GSurfelGIActiveIndexCapacity = 0;
std::shared_ptr<jBuffer> GSurfelGIActiveCounterBuffer;
std::shared_ptr<jBuffer> GSurfelGIInlineRayDispatchArgsBuffer;
jSurfelClipmapCascadeRuntimeState GSurfelClipmapRuntimeStates[SURFEL_GI_CASCADE_COUNT];
bool GSurfelClipmapForceClearAll = true;
int32 GSurfelCascadeCellBase[SURFEL_GI_CASCADE_COUNT] = {};
int32 GSurfelCascadeCellCount[SURFEL_GI_CASCADE_COUNT] = {};
constexpr int32 SURFEL_GI_MAX_SURFELS_HARD_CAP = 2097152;
constexpr int32 SURFEL_GI_MAX_SLOTS_PER_CELL = 5;
constexpr int32 SURFEL_GI_VISIBLE_CELL_WORKLIST_MULTIPLIER = 2;

FORCEINLINE int32 PositiveModuloInt32(int32 value, int32 divisor)
{
    if (divisor <= 0)
        return 0;
    const int32 m = value % divisor;
    return (m < 0) ? (m + divisor) : m;
}

void EnsureSurfelGIResources(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
    int32 MaxSurfelSlotsPerCell = Clamp(gOptions.SurfelGIReservoirPerCellLimit, 1, SURFEL_GI_MAX_SLOTS_PER_CELL);
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        MaxSurfelSlotsPerCell = Max(MaxSurfelSlotsPerCell, Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL));
    }
    GSurfelPageSize = Max(1, MaxSurfelSlotsPerCell);

    uint64 TotalCellCount64 = 0;
    int64 RunningCellBase = 0;
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        const int32 DimX = Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
        const int32 DimY = Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
        const int32 DimZ = Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
        const uint64 CascadeCellCount64 = (uint64)DimX * (uint64)DimY * (uint64)DimZ;
        const int32 CascadeCellCount = (int32)Min<uint64>(CascadeCellCount64, (uint64)std::numeric_limits<int32>::max());
        GSurfelCascadeCellBase[cascade] = (int32)Min<int64>(RunningCellBase, (int64)std::numeric_limits<int32>::max());
        GSurfelCascadeCellCount[cascade] = Max(1, CascadeCellCount);
        RunningCellBase = Min<int64>(RunningCellBase + (int64)GSurfelCascadeCellCount[cascade], (int64)std::numeric_limits<int32>::max());
        TotalCellCount64 += CascadeCellCount64;
    }
    const int32 TotalCellCount = Max(1, (int32)Min<uint64>(TotalCellCount64, (uint64)std::numeric_limits<int32>::max()));
    const int32 RequestedMaxSurfels = Clamp(Max(1024, gOptions.SurfelGIMaxSurfels), 1024, SURFEL_GI_MAX_SURFELS_HARD_CAP);
    const int64 MinRequiredForTargetSlots64 = (int64)TotalCellCount * (int64)Max(1, GSurfelPageSize);
    const int32 MinRequiredForTargetSlots = (int32)Min<int64>(MinRequiredForTargetSlots64, (int64)SURFEL_GI_MAX_SURFELS_HARD_CAP);
    const int32 MaxSurfels = Max(RequestedMaxSurfels, MinRequiredForTargetSlots);
    gOptions.SurfelGIMaxSurfels = MaxSurfels;
    const int32 MaxSlotsPerCellByBudget = Max(1, MaxSurfels / Max(1, TotalCellCount));
    GSurfelPageSize = Clamp(GSurfelPageSize, 1, MaxSlotsPerCellByBudget);
    bool RecreatedPrimaryStorage = false;

    if (!GSurfelPoolBuffer || GSurfelPoolMaxCount != MaxSurfels)
    {
        GSurfelPoolMaxCount = MaxSurfels;
        std::vector<jSurfelGPU> InitialPool;
        InitialPool.resize(MaxSurfels);

        GSurfelPoolBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGPU) * (uint64)MaxSurfels,
            0,
            sizeof(jSurfelGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialPool.data(),
            sizeof(jSurfelGPU) * (uint64)MaxSurfels,
            jNameStatic("SurfelGI_Pool"));
        RecreatedPrimaryStorage = true;
    }

    if (!GSurfelIrradianceBuffer || GSurfelIrradianceCapacity != MaxSurfels)
    {
        GSurfelIrradianceCapacity = MaxSurfels;
        std::vector<jSurfelIrradianceGPU> InitialIrradiance;
        InitialIrradiance.resize(MaxSurfels);

        GSurfelIrradianceBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelIrradianceGPU) * (uint64)MaxSurfels,
            0,
            sizeof(jSurfelIrradianceGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialIrradiance.data(),
            sizeof(jSurfelIrradianceGPU) * (uint64)MaxSurfels,
            jNameStatic("SurfelGI_Irradiance"));
        RecreatedPrimaryStorage = true;
    }

    if (!GSurfelGIActiveIndexBuffer || GSurfelGIActiveIndexCapacity != MaxSurfels)
    {
        GSurfelGIActiveIndexCapacity = MaxSurfels;
        std::vector<uint32> InitialActiveIndices;
        InitialActiveIndices.resize((size_t)MaxSurfels, 0u);

        GSurfelGIActiveIndexBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * (uint64)MaxSurfels,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialActiveIndices.data(),
            sizeof(uint32) * (uint64)MaxSurfels,
            jNameStatic("SurfelGI_ActiveIndex"));
    }

    if (!GSurfelGIActiveCounterBuffer)
    {
        jSurfelActiveCounterGPU InitialActiveCounter = {};
        GSurfelGIActiveCounterBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelActiveCounterGPU),
            0,
            sizeof(jSurfelActiveCounterGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            &InitialActiveCounter,
            sizeof(jSurfelActiveCounterGPU),
            jNameStatic("SurfelGI_ActiveCounter"));
    }

    if (!GSurfelGIInlineRayDispatchArgsBuffer)
    {
        jSurfelInlineRayDispatchArgsGPU InitialDispatchArgs = {};
        GSurfelGIInlineRayDispatchArgsBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelInlineRayDispatchArgsGPU),
            0,
            sizeof(jSurfelInlineRayDispatchArgsGPU),
            EBufferCreateFlag::UAV | EBufferCreateFlag::IndirectCommand,
            EResourceLayout::GENERAL,
            &InitialDispatchArgs,
            sizeof(jSurfelInlineRayDispatchArgsGPU),
            jNameStatic("SurfelGI_InlineRayDispatchArgs"));
    }

    if (!GSurfelCellPageTableBuffer || GSurfelCellPageTableCapacity != TotalCellCount)
    {
        GSurfelCellPageTableCapacity = TotalCellCount;
        std::vector<uint32> InitialEntries;
        InitialEntries.resize((size_t)TotalCellCount, 0u);

        GSurfelCellPageTableBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * (uint64)TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialEntries.data(),
            sizeof(uint32) * (uint64)TotalCellCount,
            jNameStatic("SurfelGI_CellPageTable"));
        RecreatedPrimaryStorage = true;
    }

    const int32 Width = InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width;
    const int32 Height = InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height;
    if (!jSceneRenderTarget::SurfelGI_Debug_RT
        || jSceneRenderTarget::SurfelGI_Debug_RT->Info.Width != Width
        || jSceneRenderTarget::SurfelGI_Debug_RT->Info.Height != Height)
    {
        jRenderTargetInfo Info = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::R11G11B10F,
            .Width = Width,
            .Height = Height,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = EMSAASamples::COUNT_1,
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .TextureCreateFlag = ETextureCreateFlag::UAV,
            .ResourceName = jNameStatic("SurfelGI_Debug_RT")
        };
        jSceneRenderTarget::SurfelGI_Debug_RT = g_rhi->CreateRenderTarget(Info);
    }

    if (!jSceneRenderTarget::SurfelGI_Attempt_RT
        || jSceneRenderTarget::SurfelGI_Attempt_RT->Info.Width != Width
        || jSceneRenderTarget::SurfelGI_Attempt_RT->Info.Height != Height)
    {
        jRenderTargetInfo Info = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::R11G11B10F,
            .Width = Width,
            .Height = Height,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = EMSAASamples::COUNT_1,
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .TextureCreateFlag = ETextureCreateFlag::UAV,
            .ResourceName = jNameStatic("SurfelGI_Attempt_RT")
        };
        jSceneRenderTarget::SurfelGI_Attempt_RT = g_rhi->CreateRenderTarget(Info);
    }

    const int32 CandidateCapacity = Max(1, Width * Height);
    if (!GSurfelGICandidateBuffer || GSurfelGICandidateCapacity != CandidateCapacity)
    {
        GSurfelGICandidateCapacity = CandidateCapacity;
        struct alignas(16) jSurfelCandidateGPU
        {
            jSurfelGPU Surfel;
            int32 CellX = 0;
            int32 CellY = 0;
            int32 CellZ = 0;
            int32 Cascade = 0;
            uint32 Priority = 0;
            uint32 Padding0 = 0;
            uint32 Padding1 = 0;
            uint32 Padding2 = 0;
        };

        std::vector<jSurfelCandidateGPU> InitialCandidates;
        InitialCandidates.resize((size_t)CandidateCapacity);
        GSurfelGICandidateBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelCandidateGPU) * (uint64)CandidateCapacity,
            0,
            sizeof(jSurfelCandidateGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialCandidates.data(),
            sizeof(jSurfelCandidateGPU) * (uint64)CandidateCapacity,
            jNameStatic("SurfelGI_ReservoirCandidates"));
    }

    if (!GSurfelGIWinnerScoreBuffer || !GSurfelGIWinnerIndexBuffer || !GSurfelGIWinnerLockBuffer || GSurfelGIWinnerCapacity != TotalCellCount)
    {
        GSurfelGIWinnerCapacity = TotalCellCount;

        std::vector<uint32> InitialWinnerScore;
        InitialWinnerScore.resize((size_t)TotalCellCount, 0u);
        GSurfelGIWinnerScoreBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * (uint64)TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialWinnerScore.data(),
            sizeof(uint32) * (uint64)TotalCellCount,
            jNameStatic("SurfelGI_ReservoirWinnerScore"));

        std::vector<uint32> InitialWinnerIndex;
        InitialWinnerIndex.resize((size_t)TotalCellCount, 0xffffffffu);
        GSurfelGIWinnerIndexBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * (uint64)TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialWinnerIndex.data(),
            sizeof(uint32) * (uint64)TotalCellCount,
            jNameStatic("SurfelGI_ReservoirWinnerIndex"));

        std::vector<uint32> InitialWinnerLock;
        InitialWinnerLock.resize((size_t)TotalCellCount, 0u);
        GSurfelGIWinnerLockBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * (uint64)TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialWinnerLock.data(),
            sizeof(uint32) * (uint64)TotalCellCount,
            jNameStatic("SurfelGI_ReservoirWinnerLock"));
    }

    if (!GSurfelGIStatsBuffer)
    {
        jSurfelGIStatsGPU InitialStats = {};
        GSurfelGIStatsBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGIStatsGPU),
            0,
            sizeof(jSurfelGIStatsGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            &InitialStats,
            sizeof(jSurfelGIStatsGPU),
            jNameStatic("SurfelGI_ReservoirStats"));
    }

    const int64 MaxVisibleCells64 = (int64)Width * (int64)Height * (int64)SURFEL_GI_VISIBLE_CELL_WORKLIST_MULTIPLIER;
    const int32 MaxVisibleCells = Max(1, (int32)Min<int64>(MaxVisibleCells64, (int64)std::numeric_limits<int32>::max()));
    if (!GVisibleCellWorklistBuffer || GVisibleCellWorklistCapacity != MaxVisibleCells)
    {
        GVisibleCellWorklistCapacity = MaxVisibleCells;
        std::vector<jVisibleCellGPU> InitialWorklist;
        InitialWorklist.resize((size_t)MaxVisibleCells);
        GVisibleCellWorklistBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jVisibleCellGPU) * (uint64)MaxVisibleCells,
            0,
            sizeof(jVisibleCellGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialWorklist.data(),
            sizeof(jVisibleCellGPU) * (uint64)MaxVisibleCells,
            jNameStatic("SurfelGI_VisibleCellWorklist"));
    }

    if (!GVisibleCellCounterBuffer)
    {
        jVisibleCellCounterGPU CounterInit = {};
        GVisibleCellCounterBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jVisibleCellCounterGPU),
            0,
            sizeof(jVisibleCellCounterGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            &CounterInit,
            sizeof(jVisibleCellCounterGPU),
            jNameStatic("SurfelGI_VisibleCellCounter"));
    }

    if (RecreatedPrimaryStorage)
    {
        GSurfelClipmapForceClearAll = true;
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            GSurfelClipmapRuntimeStates[cascade].Initialized = false;
        }
    }

}
}

void jRenderer::SurfelGIPass()
{
    if (!gOptions.UseSurfelGI)
        return;

    SCOPE_CPU_PROFILE(SurfelGIPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGIPass);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGIPass", Vector4(0.25f, 0.85f, 0.4f, 1.0f));

    EnsureSurfelGIResources(RenderFrameContextPtr);
    if (!GSurfelPoolBuffer || !jSceneRenderTarget::SurfelGI_Debug_RT || !jSceneRenderTarget::SurfelGI_Attempt_RT
        || !GVisibleCellWorklistBuffer || !GVisibleCellCounterBuffer || !GSurfelCellPageTableBuffer
        || !GSurfelGICandidateBuffer || !GSurfelGIWinnerScoreBuffer || !GSurfelGIWinnerIndexBuffer || !GSurfelGIWinnerLockBuffer
        || !GSurfelGIStatsBuffer || !GSurfelIrradianceBuffer
        || !GSurfelGIActiveIndexBuffer || !GSurfelGIActiveCounterBuffer || !GSurfelGIInlineRayDispatchArgsBuffer)
        return;
    struct alignas(16) jFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };
    static_assert(sizeof(jFloat4) == 16, "jFloat4 must be 16 bytes");

    struct alignas(16) jSurfelGIUniformBuffer
    {
        Matrix InvP;
        Matrix V;
        Matrix InvV;
        Vector2 ScreenSize;
        float MergeDistanceScale;
        float NormalThreshold;
        float DepthEdgeScale;
        float NormalEdgeScale;
        int32 UseCenterSpawnBias;
        float NearKeepRadius;
        float NearSpawnBias;
        float FrustumInteriorScale;
        float FarNearFactorThreshold;
        float FarMaxDistanceMultiplier;
        float ReplaceNearDelta;
        float StaleAgeDivisor;
        float MinRadius;
        float MaxDistance;
        int32 FrameNumber;
        int32 TileSize;
        int32 MaxSurfels;
        int32 SurfelPageSize;
        int32 SurfelPageTableCapacity;
        int32 SpawnBudget;
        int32 TTLInFrames;
        float GridCellSize;
        jFloat4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeStartDistancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeRadiusScalePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        int32 SpawnHysteresisFrames;
        int32 DeleteHysteresisFrames;
        float RadiusScale;
        float FaceMarginRadiusScale;
        jFloat4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeCellCountPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeDeltaCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeDeltaCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeDeltaCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeClearAllPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
    };
    static_assert((sizeof(jSurfelGIUniformBuffer) % 16) == 0, "jSurfelGIUniformBuffer size must be 16-byte aligned");

    auto MainCamera = jCamera::GetMainCamera();
    if (!MainCamera)
        return;

    jSurfelGIUniformBuffer UniformData;
    float CascadeStartDistanceSanitized[SURFEL_GI_CASCADE_COUNT] = {};
    float PrevStartDistance = 0.0f;
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        if (cascade == 0)
        {
            CascadeStartDistanceSanitized[cascade] = 0.0f;
        }
        else
        {
            PrevStartDistance = Max(PrevStartDistance, Max(0.0f, gOptions.SurfelGICascadeStartDistance[cascade]));
            CascadeStartDistanceSanitized[cascade] = PrevStartDistance;
        }
    }
    UniformData.InvP = MainCamera->Projection.GetInverse();
    UniformData.V = MainCamera->View;
    UniformData.InvV = MainCamera->View.GetInverse();
    UniformData.ScreenSize = Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT);
    UniformData.MergeDistanceScale = gOptions.SurfelGIMergeDistanceScale;
    UniformData.NormalThreshold = gOptions.SurfelGINormalThreshold;
    UniformData.DepthEdgeScale = 0.75f;
    UniformData.NormalEdgeScale = 1.25f;
    UniformData.UseCenterSpawnBias = gOptions.UseSurfelGICenterSpawnBias ? 1 : 0;
    UniformData.NearKeepRadius = Max(0.0f, gOptions.SurfelGINearKeepRadius);
    UniformData.NearSpawnBias = Clamp(gOptions.SurfelGINearSpawnBias, 0.0f, 1.0f);
    UniformData.FrustumInteriorScale = Max(1.0f, gOptions.SurfelGIFrustumInteriorScale);
    UniformData.FarNearFactorThreshold = Clamp(gOptions.SurfelGIFarNearFactorThreshold, 0.0f, 1.0f);
    UniformData.FarMaxDistanceMultiplier = Max(1.0f, gOptions.SurfelGIFarMaxDistanceMultiplier);
    UniformData.ReplaceNearDelta = Clamp(gOptions.SurfelGIReplaceNearDelta, 0.0f, 1.0f);
    UniformData.StaleAgeDivisor = Max(1.0f, gOptions.SurfelGIStaleAgeDivisor);
    UniformData.MinRadius = 15.0f;
    UniformData.MaxDistance = gOptions.SSGIMaxDistance;
    UniformData.FrameNumber = (int32)g_rhi->GetCurrentFrameNumber();
    UniformData.TileSize = Max(1, gOptions.SurfelGITileSize);
    UniformData.MaxSurfels = GSurfelPoolMaxCount;
    UniformData.SurfelPageSize = Max(1, GSurfelPageSize);
    UniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
    UniformData.SpawnBudget = Max(1, gOptions.SurfelGISpawnBudgetPerFrame);
    UniformData.TTLInFrames = Max(1, gOptions.SurfelGITTLInFrames);
    UniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
    for (int32 pack = 0; pack < SURFEL_GI_CASCADE_PACKED_COUNT; ++pack)
    {
        UniformData.CascadeCellScaleFromPrevPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeStartDistancePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeRadiusScalePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeClipmapGridDimXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeClipmapGridDimYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeClipmapGridDimZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.SurfelsPerCellPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeOriginCellXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeOriginCellYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeOriginCellZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeRingOffsetXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeRingOffsetYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeRingOffsetZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeCellBasePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeCellCountPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeDeltaCellXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeDeltaCellYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeDeltaCellZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        UniformData.CascadeClearAllPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    UniformData.FaceMarginRadiusScale = Clamp(gOptions.SurfelGIFaceMarginRadiusScale, 0.0f, 2.0f);
    auto SetPackedCascadeValue = [](jFloat4* packedArray, int32 cascade, float value)
    {
        const int32 packIndex = cascade / 4;
        const int32 lane = cascade % 4;
        if (lane == 0) packedArray[packIndex].x = value;
        else if (lane == 1) packedArray[packIndex].y = value;
        else if (lane == 2) packedArray[packIndex].z = value;
        else packedArray[packIndex].w = value;
    };
    auto GetCascadeScale = [](int32 cascadeIndex)
    {
        float scale = 1.0f;
        for (int32 i = 1; i <= cascadeIndex && i < SURFEL_GI_CASCADE_COUNT; ++i)
        {
            scale *= Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[i]);
        }
        return scale;
    };
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        SetPackedCascadeValue(UniformData.CascadeCellScaleFromPrevPacked, cascade, (cascade == 0) ? 1.0f : Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[cascade]));
        SetPackedCascadeValue(UniformData.CascadeStartDistancePacked, cascade, CascadeStartDistanceSanitized[cascade]);
        SetPackedCascadeValue(UniformData.CascadeRadiusScalePacked, cascade, (cascade == 0) ? 1.0f : Max(0.05f, gOptions.SurfelGICascadeRadiusScale[cascade]));
    }
    UniformData.SpawnHysteresisFrames = Max(1, gOptions.SurfelGISpawnHysteresisFrames);
    UniformData.DeleteHysteresisFrames = Max(1, gOptions.SurfelGIDeleteHysteresisFrames);
    UniformData.RadiusScale = Max(0.05f, gOptions.SurfelGIRadiusScale);
    const bool ForceClearAllThisFrame = GSurfelClipmapForceClearAll;
    bool NeedClipmapCellClear = ForceClearAllThisFrame;
    int64 RunningCascadeCellBase = 0;
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        const int32 DimX = Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
        const int32 DimY = Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
        const int32 DimZ = Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
        const int32 SurfelPerCell = Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL);

        const uint64 CascadeCellCount64 = (uint64)DimX * (uint64)DimY * (uint64)DimZ;
        const int32 CascadeCellCount = Max(1, (int32)Min<uint64>(CascadeCellCount64, (uint64)std::numeric_limits<int32>::max()));
        GSurfelCascadeCellBase[cascade] = (int32)Min<int64>(RunningCascadeCellBase, (int64)std::numeric_limits<int32>::max());
        GSurfelCascadeCellCount[cascade] = CascadeCellCount;
        RunningCascadeCellBase = Min<int64>(RunningCascadeCellBase + (int64)CascadeCellCount, (int64)std::numeric_limits<int32>::max());

        const float CellSize = Max(0.1f, UniformData.GridCellSize) * GetCascadeScale(cascade);
        const int32 CameraCellX = (int32)std::floor(MainCamera->Pos.x / Max(CellSize, 0.001f));
        const int32 CameraCellY = (int32)std::floor(MainCamera->Pos.y / Max(CellSize, 0.001f));
        const int32 CameraCellZ = (int32)std::floor(MainCamera->Pos.z / Max(CellSize, 0.001f));
        const int32 DesiredOriginX = CameraCellX - (DimX / 2);
        const int32 DesiredOriginY = CameraCellY - (DimY / 2);
        const int32 DesiredOriginZ = CameraCellZ - (DimZ / 2);

        auto& RuntimeState = GSurfelClipmapRuntimeStates[cascade];
        int32 DeltaX = 0;
        int32 DeltaY = 0;
        int32 DeltaZ = 0;
        bool CascadeClearAll = ForceClearAllThisFrame || !RuntimeState.Initialized
            || RuntimeState.DimX != DimX || RuntimeState.DimY != DimY || RuntimeState.DimZ != DimZ;

        if (!CascadeClearAll)
        {
            DeltaX = DesiredOriginX - RuntimeState.OriginX;
            DeltaY = DesiredOriginY - RuntimeState.OriginY;
            DeltaZ = DesiredOriginZ - RuntimeState.OriginZ;
            if (abs(DeltaX) >= DimX || abs(DeltaY) >= DimY || abs(DeltaZ) >= DimZ)
            {
                CascadeClearAll = true;
                DeltaX = 0;
                DeltaY = 0;
                DeltaZ = 0;
            }
        }

        if (CascadeClearAll)
        {
            RuntimeState.RingOffsetX = 0;
            RuntimeState.RingOffsetY = 0;
            RuntimeState.RingOffsetZ = 0;
        }
        else
        {
            RuntimeState.RingOffsetX = PositiveModuloInt32(RuntimeState.RingOffsetX + DeltaX, DimX);
            RuntimeState.RingOffsetY = PositiveModuloInt32(RuntimeState.RingOffsetY + DeltaY, DimY);
            RuntimeState.RingOffsetZ = PositiveModuloInt32(RuntimeState.RingOffsetZ + DeltaZ, DimZ);
        }

        RuntimeState.OriginX = DesiredOriginX;
        RuntimeState.OriginY = DesiredOriginY;
        RuntimeState.OriginZ = DesiredOriginZ;
        RuntimeState.DimX = DimX;
        RuntimeState.DimY = DimY;
        RuntimeState.DimZ = DimZ;
        RuntimeState.Initialized = true;

        if (CascadeClearAll || DeltaX != 0 || DeltaY != 0 || DeltaZ != 0)
            NeedClipmapCellClear = true;

        SetPackedCascadeValue(UniformData.CascadeClipmapGridDimXPacked, cascade, (float)DimX);
        SetPackedCascadeValue(UniformData.CascadeClipmapGridDimYPacked, cascade, (float)DimY);
        SetPackedCascadeValue(UniformData.CascadeClipmapGridDimZPacked, cascade, (float)DimZ);
        SetPackedCascadeValue(UniformData.SurfelsPerCellPacked, cascade, (float)SurfelPerCell);
        SetPackedCascadeValue(UniformData.CascadeOriginCellXPacked, cascade, (float)RuntimeState.OriginX);
        SetPackedCascadeValue(UniformData.CascadeOriginCellYPacked, cascade, (float)RuntimeState.OriginY);
        SetPackedCascadeValue(UniformData.CascadeOriginCellZPacked, cascade, (float)RuntimeState.OriginZ);
        SetPackedCascadeValue(UniformData.CascadeRingOffsetXPacked, cascade, (float)RuntimeState.RingOffsetX);
        SetPackedCascadeValue(UniformData.CascadeRingOffsetYPacked, cascade, (float)RuntimeState.RingOffsetY);
        SetPackedCascadeValue(UniformData.CascadeRingOffsetZPacked, cascade, (float)RuntimeState.RingOffsetZ);
        SetPackedCascadeValue(UniformData.CascadeCellBasePacked, cascade, (float)GSurfelCascadeCellBase[cascade]);
        SetPackedCascadeValue(UniformData.CascadeCellCountPacked, cascade, (float)GSurfelCascadeCellCount[cascade]);
        SetPackedCascadeValue(UniformData.CascadeDeltaCellXPacked, cascade, (float)DeltaX);
        SetPackedCascadeValue(UniformData.CascadeDeltaCellYPacked, cascade, (float)DeltaY);
        SetPackedCascadeValue(UniformData.CascadeDeltaCellZPacked, cascade, (float)DeltaZ);
        SetPackedCascadeValue(UniformData.CascadeClearAllPacked, cascade, CascadeClearAll ? 1.0f : 0.0f);
    }
    GSurfelClipmapForceClearAll = false;

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
        g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformData)));
    OneFrameUniformBuffer->UpdateBufferData(&UniformData, sizeof(UniformData));

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellWorklistBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGICandidateBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerScoreBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerIndexBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerLockBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIStatsBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get(), EResourceLayout::UAV);

    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    const int32 Width = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width;
    const int32 Height = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height;
    const int32 GroupX = (Width + 7) / 8;
    const int32 GroupY = (Height + 7) / 8;

    if (NeedClipmapCellClear)
    {
        jShaderBindingArray ClipmapClearBindingArray;
        jShaderBindingResourceInlineAllocator ClipmapClearResourceAllocator;
        ClipmapClearBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            ClipmapClearResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
        ClipmapClearBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            ClipmapClearResourceAllocator.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));
        ClipmapClearBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            ClipmapClearResourceAllocator.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));
        ClipmapClearBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
            ClipmapClearResourceAllocator.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

        auto ClipmapClearBindingInstance = g_rhi->CreateShaderBindingInstance(ClipmapClearBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo ClipmapClearShaderInfo;
        ClipmapClearShaderInfo.SetName(jNameStatic("SurfelGIClearClipmapCells_CS"));
        ClipmapClearShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIClearClipmapCells_cs.hlsl"));
        ClipmapClearShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        ClipmapClearShaderInfo.SetEntryPoint(jNameStatic("main"));
        jShader* ClipmapClearShader = g_rhi->CreateShader(ClipmapClearShaderInfo);

        jShaderBindingLayoutArray ClipmapClearLayoutArray;
        ClipmapClearLayoutArray.Add(ClipmapClearBindingInstance->ShaderBindingsLayouts);
        jPipelineStateInfo* ClipmapClearPSO = g_rhi->CreateComputePipelineStateInfo(ClipmapClearShader, ClipmapClearLayoutArray, {});
        ClipmapClearPSO->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray ClipmapClearInstanceArray;
        ClipmapClearInstanceArray.Add(ClipmapClearBindingInstance.get());

        jShaderBindingInstanceCombiner ClipmapClearCombiner;
        ClipmapClearCombiner.ShaderBindingInstanceArray = &ClipmapClearInstanceArray;
        ClipmapClearCombiner.DescriptorSetHandles.Add(ClipmapClearBindingInstance->GetHandle());
        if (const std::vector<uint32>* DynamicOffsets = ClipmapClearBindingInstance->GetDynamicOffsets())
        {
            if (!DynamicOffsets->empty())
            {
                ClipmapClearCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
            }
        }

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ClipmapClearPSO, ClipmapClearCombiner, 0);
        const int32 ClipmapClearGroupX = (Max(1, GSurfelCellPageTableCapacity) + 63) / 64;
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Clear Clipmap Cells", Vector4(0.8f, 0.6f, 0.2f, 1.0f));
        g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, ClipmapClearGroupX), 1, 1);

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
    }

    {
        jShaderBindingArray ClearBindingArray;
        jShaderBindingResourceInlineAllocator ClearResourceAllocator;
        ClearBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            ClearResourceAllocator.Alloc<jBufferResource>(GVisibleCellCounterBuffer.get())));

        auto ClearBindingInstance = g_rhi->CreateShaderBindingInstance(ClearBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo ClearShaderInfo;
        ClearShaderInfo.SetName(jNameStatic("SurfelGIClearVisibleCellCounter_CS"));
        ClearShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIClearVisibleCellCounter_cs.hlsl"));
        ClearShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        ClearShaderInfo.SetEntryPoint(jNameStatic("main"));
        jShader* ClearShader = g_rhi->CreateShader(ClearShaderInfo);

        jShaderBindingLayoutArray ClearLayoutArray;
        ClearLayoutArray.Add(ClearBindingInstance->ShaderBindingsLayouts);
        jPipelineStateInfo* ClearPSO = g_rhi->CreateComputePipelineStateInfo(ClearShader, ClearLayoutArray, {});
        ClearPSO->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray ClearInstanceArray;
        ClearInstanceArray.Add(ClearBindingInstance.get());

        jShaderBindingInstanceCombiner ClearCombiner;
        ClearCombiner.ShaderBindingInstanceArray = &ClearInstanceArray;
        ClearCombiner.DescriptorSetHandles.Add(ClearBindingInstance->GetHandle());

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ClearPSO, ClearCombiner, 0);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch ClearVisibleCellCounter", Vector4(0.55f, 0.55f, 0.95f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchClearVisibleCellCounter);
        g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get());

    {
        jShaderBindingArray CollectBindingArray;
        jShaderBindingResourceInlineAllocator CollectResourceAllocator;
        int32 CollectBindingPoint = 0;

        CollectBindingArray.Add(jShaderBinding::Create(CollectBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
            CollectResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));
        CollectBindingArray.Add(jShaderBinding::Create(CollectBindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
            CollectResourceAllocator.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
        CollectBindingArray.Add(jShaderBinding::Create(CollectBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            CollectResourceAllocator.Alloc<jBufferResource>(GVisibleCellWorklistBuffer.get())));
        CollectBindingArray.Add(jShaderBinding::Create(CollectBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            CollectResourceAllocator.Alloc<jBufferResource>(GVisibleCellCounterBuffer.get())));

        auto CollectBindingInstance = g_rhi->CreateShaderBindingInstance(CollectBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo CollectShaderInfo;
        CollectShaderInfo.SetName(jNameStatic("SurfelGIVisibleCellCollect_CS"));
        CollectShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIVisibleCellCollect_cs.hlsl"));
        CollectShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        CollectShaderInfo.SetEntryPoint(jNameStatic("main"));
        jShader* CollectShader = g_rhi->CreateShader(CollectShaderInfo);

        jShaderBindingLayoutArray CollectLayoutArray;
        CollectLayoutArray.Add(CollectBindingInstance->ShaderBindingsLayouts);
        jPipelineStateInfo* CollectPSO = g_rhi->CreateComputePipelineStateInfo(CollectShader, CollectLayoutArray, {});
        CollectPSO->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray CollectInstanceArray;
        CollectInstanceArray.Add(CollectBindingInstance.get());

        jShaderBindingInstanceCombiner CollectCombiner;
        CollectCombiner.ShaderBindingInstanceArray = &CollectInstanceArray;
        CollectCombiner.DescriptorSetHandles.Add(CollectBindingInstance->GetHandle());
        if (const std::vector<uint32>* DynamicOffsets = CollectBindingInstance->GetDynamicOffsets())
        {
            if (!DynamicOffsets->empty())
            {
                CollectCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
            }
        }

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), CollectPSO, CollectCombiner, 0);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch CollectVisibleCells", Vector4(0.25f, 0.65f, 0.95f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchCollectVisibleCells);
        g_rhi->DispatchCompute(RenderFrameContextPtr, GroupX, GroupY, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellWorklistBuffer.get());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get());
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellWorklistBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

    {
        jShaderBindingArray RefreshBindingArray;
        jShaderBindingResourceInlineAllocator RefreshResourceAllocator;
        int32 RefreshBindingPoint = 0;

        RefreshBindingArray.Add(jShaderBinding::Create(RefreshBindingPoint++, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
            RefreshResourceAllocator.Alloc<jBufferResource>(GVisibleCellWorklistBuffer.get())));
        RefreshBindingArray.Add(jShaderBinding::Create(RefreshBindingPoint++, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
            RefreshResourceAllocator.Alloc<jBufferResource>(GVisibleCellCounterBuffer.get())));
        RefreshBindingArray.Add(jShaderBinding::Create(RefreshBindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
            RefreshResourceAllocator.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
        RefreshBindingArray.Add(jShaderBinding::Create(RefreshBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            RefreshResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
        RefreshBindingArray.Add(jShaderBinding::Create(RefreshBindingPoint++, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
            RefreshResourceAllocator.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));

        auto RefreshBindingInstance = g_rhi->CreateShaderBindingInstance(RefreshBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo RefreshShaderInfo;
        RefreshShaderInfo.SetName(jNameStatic("SurfelGIRefreshVisibleCellSurfels_CS"));
        RefreshShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIRefreshVisibleCellSurfels_cs.hlsl"));
        RefreshShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        RefreshShaderInfo.SetEntryPoint(jNameStatic("main"));
        jShader* RefreshShader = g_rhi->CreateShader(RefreshShaderInfo);

        jShaderBindingLayoutArray RefreshLayoutArray;
        RefreshLayoutArray.Add(RefreshBindingInstance->ShaderBindingsLayouts);
        jPipelineStateInfo* RefreshPSO = g_rhi->CreateComputePipelineStateInfo(RefreshShader, RefreshLayoutArray, {});
        RefreshPSO->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray RefreshInstanceArray;
        RefreshInstanceArray.Add(RefreshBindingInstance.get());

        jShaderBindingInstanceCombiner RefreshCombiner;
        RefreshCombiner.ShaderBindingInstanceArray = &RefreshInstanceArray;
        RefreshCombiner.DescriptorSetHandles.Add(RefreshBindingInstance->GetHandle());
        if (const std::vector<uint32>* DynamicOffsets = RefreshBindingInstance->GetDynamicOffsets())
        {
            if (!DynamicOffsets->empty())
            {
                RefreshCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
            }
        }

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), RefreshPSO, RefreshCombiner, 0);
        const int32 RefreshGroupX = (GVisibleCellWorklistCapacity + 63) / 64;
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch RefreshVisibleCellSurfels", Vector4(0.75f, 0.8f, 0.25f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchRefreshVisibleCellSurfels);
        g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, RefreshGroupX), 1, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellWorklistBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::UAV);

    {
        jShaderBindingArray CleanupBindingArray;
        jShaderBindingResourceInlineAllocator CleanupResourceAllocator;
        int32 CleanupBindingPoint = 0;

        CleanupBindingArray.Add(jShaderBinding::Create(CleanupBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            CleanupResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
        CleanupBindingArray.Add(jShaderBinding::Create(CleanupBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            CleanupResourceAllocator.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));
        CleanupBindingArray.Add(jShaderBinding::Create(CleanupBindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
            CleanupResourceAllocator.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

        auto CleanupBindingInstance = g_rhi->CreateShaderBindingInstance(CleanupBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo CleanupShaderInfo;
        CleanupShaderInfo.SetName(jNameStatic("SurfelGICleanup_CS"));
        CleanupShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGICleanup_cs.hlsl"));
        CleanupShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        CleanupShaderInfo.SetEntryPoint(jNameStatic("main"));
        jShader* CleanupShader = g_rhi->CreateShader(CleanupShaderInfo);

        jShaderBindingLayoutArray CleanupLayoutArray;
        CleanupLayoutArray.Add(CleanupBindingInstance->ShaderBindingsLayouts);
        jPipelineStateInfo* CleanupPSO = g_rhi->CreateComputePipelineStateInfo(CleanupShader, CleanupLayoutArray, {});
        CleanupPSO->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray CleanupInstanceArray;
        CleanupInstanceArray.Add(CleanupBindingInstance.get());

        jShaderBindingInstanceCombiner CleanupCombiner;
        CleanupCombiner.ShaderBindingInstanceArray = &CleanupInstanceArray;
        CleanupCombiner.DescriptorSetHandles.Add(CleanupBindingInstance->GetHandle());
        if (const std::vector<uint32>* DynamicOffsets = CleanupBindingInstance->GetDynamicOffsets())
        {
            if (!DynamicOffsets->empty())
            {
                CleanupCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
            }
        }

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), CleanupPSO, CleanupCombiner, 0);
        const int32 CleanupGroupX = (GSurfelPoolMaxCount + 63) / 64;
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch Cleanup", Vector4(0.8f, 0.35f, 0.35f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchCleanup);
        g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, CleanupGroupX), 1, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
    {
        struct alignas(16) jSurfelGIReservoirClearUniform
        {
            uint32 CandidateCapacity = 0;
            uint32 PageCapacity = 0;
            uint32 Padding0 = 0;
            uint32 Padding1 = 0;
        };
        static_assert(sizeof(jSurfelGIReservoirClearUniform) == 16, "jSurfelGIReservoirClearUniform must be 16-byte aligned");

        jSurfelGIReservoirClearUniform ClearUniformData;
        ClearUniformData.CandidateCapacity = (uint32)Max(1, GSurfelGICandidateCapacity);
        ClearUniformData.PageCapacity = (uint32)Max(1, GSurfelCellPageTableCapacity);

        auto ClearUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIReservoirClearUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ClearUniformData)));
        ClearUniformBuffer->UpdateBufferData(&ClearUniformData, sizeof(ClearUniformData));

        {
            jShaderBindingArray ReservoirClearBindingArray;
            jShaderBindingResourceInlineAllocator ReservoirClearResourceAllocator;
            ReservoirClearBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirClearResourceAllocator.Alloc<jBufferResource>(GSurfelGICandidateBuffer.get())));
            ReservoirClearBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirClearResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerScoreBuffer.get())));
            ReservoirClearBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirClearResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerIndexBuffer.get())));
            ReservoirClearBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirClearResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerLockBuffer.get())));
            ReservoirClearBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                ReservoirClearResourceAllocator.Alloc<jUniformBufferResource>(ClearUniformBuffer.get()), true));

            auto ReservoirClearBindingInstance = g_rhi->CreateShaderBindingInstance(ReservoirClearBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo ReservoirClearShaderInfo;
            ReservoirClearShaderInfo.SetName(jNameStatic("SurfelGIClearCandidates_CS"));
            ReservoirClearShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIClearCandidates_cs.hlsl"));
            ReservoirClearShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            ReservoirClearShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* ReservoirClearShader = g_rhi->CreateShader(ReservoirClearShaderInfo);

            jShaderBindingLayoutArray ReservoirClearLayoutArray;
            ReservoirClearLayoutArray.Add(ReservoirClearBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* ReservoirClearPSO = g_rhi->CreateComputePipelineStateInfo(ReservoirClearShader, ReservoirClearLayoutArray, {});
            ReservoirClearPSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray ReservoirClearInstanceArray;
            ReservoirClearInstanceArray.Add(ReservoirClearBindingInstance.get());

            jShaderBindingInstanceCombiner ReservoirClearCombiner;
            ReservoirClearCombiner.ShaderBindingInstanceArray = &ReservoirClearInstanceArray;
            ReservoirClearCombiner.DescriptorSetHandles.Add(ReservoirClearBindingInstance->GetHandle());
            if (const std::vector<uint32>* DynamicOffsets = ReservoirClearBindingInstance->GetDynamicOffsets())
            {
                if (!DynamicOffsets->empty())
                {
                    ReservoirClearCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
                }
            }

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ReservoirClearPSO, ReservoirClearCombiner, 0);
            const int32 ClearGroupX = (Max(GSurfelGICandidateCapacity, GSurfelCellPageTableCapacity) + 63) / 64;
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir ClearCandidates", Vector4(0.55f, 0.95f, 0.55f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirClearCandidates);
            g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, ClearGroupX), 1, 1);
        }

        {
            jShaderBindingArray ReservoirClearStatsBindingArray;
            jShaderBindingResourceInlineAllocator ReservoirClearStatsResourceAllocator;
            ReservoirClearStatsBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirClearStatsResourceAllocator.Alloc<jBufferResource>(GSurfelGIStatsBuffer.get())));

            auto ReservoirClearStatsBindingInstance = g_rhi->CreateShaderBindingInstance(ReservoirClearStatsBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo ReservoirClearStatsShaderInfo;
            ReservoirClearStatsShaderInfo.SetName(jNameStatic("SurfelGIClearStats_CS"));
            ReservoirClearStatsShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIClearStats_cs.hlsl"));
            ReservoirClearStatsShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            ReservoirClearStatsShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* ReservoirClearStatsShader = g_rhi->CreateShader(ReservoirClearStatsShaderInfo);

            jShaderBindingLayoutArray ReservoirClearStatsLayoutArray;
            ReservoirClearStatsLayoutArray.Add(ReservoirClearStatsBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* ReservoirClearStatsPSO = g_rhi->CreateComputePipelineStateInfo(ReservoirClearStatsShader, ReservoirClearStatsLayoutArray, {});
            ReservoirClearStatsPSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray ReservoirClearStatsInstanceArray;
            ReservoirClearStatsInstanceArray.Add(ReservoirClearStatsBindingInstance.get());

            jShaderBindingInstanceCombiner ReservoirClearStatsCombiner;
            ReservoirClearStatsCombiner.ShaderBindingInstanceArray = &ReservoirClearStatsInstanceArray;
            ReservoirClearStatsCombiner.DescriptorSetHandles.Add(ReservoirClearStatsBindingInstance->GetHandle());

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ReservoirClearStatsPSO, ReservoirClearStatsCombiner, 0);
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir ClearStats", Vector4(0.55f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirClearStats);
            g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGICandidateBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerScoreBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerIndexBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerLockBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIStatsBuffer.get());

        {
            jShaderBindingArray ReservoirGatherBindingArray;
            jShaderBindingResourceInlineAllocator ReservoirGatherResourceAllocator;
            int32 ReservoirGatherBindingPoint = 0;
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState)));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), SamplerState)));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), nullptr)));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), nullptr)));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), nullptr)));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGIStatsBuffer.get())));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGICandidateBuffer.get())));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerScoreBuffer.get())));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerIndexBuffer.get())));
            ReservoirGatherBindingArray.Add(jShaderBinding::Create(ReservoirGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerLockBuffer.get())));

            auto ReservoirGatherBindingInstance = g_rhi->CreateShaderBindingInstance(ReservoirGatherBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo ReservoirGatherShaderInfo;
            ReservoirGatherShaderInfo.SetName(jNameStatic("SurfelGIGatherCandidates_CS"));
            ReservoirGatherShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIGatherCandidates_cs.hlsl"));
            ReservoirGatherShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            ReservoirGatherShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* ReservoirGatherShader = g_rhi->CreateShader(ReservoirGatherShaderInfo);

            jShaderBindingLayoutArray ReservoirGatherLayoutArray;
            ReservoirGatherLayoutArray.Add(ReservoirGatherBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* ReservoirGatherPSO = g_rhi->CreateComputePipelineStateInfo(ReservoirGatherShader, ReservoirGatherLayoutArray, {});
            ReservoirGatherPSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray ReservoirGatherInstanceArray;
            ReservoirGatherInstanceArray.Add(ReservoirGatherBindingInstance.get());

            jShaderBindingInstanceCombiner ReservoirGatherCombiner;
            ReservoirGatherCombiner.ShaderBindingInstanceArray = &ReservoirGatherInstanceArray;
            ReservoirGatherCombiner.DescriptorSetHandles.Add(ReservoirGatherBindingInstance->GetHandle());
            if (const std::vector<uint32>* DynamicOffsets = ReservoirGatherBindingInstance->GetDynamicOffsets())
            {
                if (!DynamicOffsets->empty())
                {
                    ReservoirGatherCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
                }
            }

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ReservoirGatherPSO, ReservoirGatherCombiner, 0);
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir GatherCandidates", Vector4(0.25f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirGatherCandidates);
            g_rhi->DispatchCompute(RenderFrameContextPtr, GroupX, GroupY, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGICandidateBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerScoreBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerIndexBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerLockBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get());
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGICandidateBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerScoreBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerIndexBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

        {
            jShaderBindingArray ReservoirPlaceBindingArray;
            jShaderBindingResourceInlineAllocator ReservoirPlaceResourceAllocator;
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelGICandidateBuffer.get())));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerScoreBuffer.get())));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelGIWinnerIndexBuffer.get())));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelGIStatsBuffer.get())));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(6, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(7, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));

            auto ReservoirPlaceBindingInstance = g_rhi->CreateShaderBindingInstance(ReservoirPlaceBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo ReservoirPlaceShaderInfo;
            ReservoirPlaceShaderInfo.SetName(jNameStatic("SurfelGIPlaceCandidates_CS"));
            ReservoirPlaceShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIPlaceCandidates_cs.hlsl"));
            ReservoirPlaceShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            ReservoirPlaceShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* ReservoirPlaceShader = g_rhi->CreateShader(ReservoirPlaceShaderInfo);

            jShaderBindingLayoutArray ReservoirPlaceLayoutArray;
            ReservoirPlaceLayoutArray.Add(ReservoirPlaceBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* ReservoirPlacePSO = g_rhi->CreateComputePipelineStateInfo(ReservoirPlaceShader, ReservoirPlaceLayoutArray, {});
            ReservoirPlacePSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray ReservoirPlaceInstanceArray;
            ReservoirPlaceInstanceArray.Add(ReservoirPlaceBindingInstance.get());

            jShaderBindingInstanceCombiner ReservoirPlaceCombiner;
            ReservoirPlaceCombiner.ShaderBindingInstanceArray = &ReservoirPlaceInstanceArray;
            ReservoirPlaceCombiner.DescriptorSetHandles.Add(ReservoirPlaceBindingInstance->GetHandle());
            if (const std::vector<uint32>* DynamicOffsets = ReservoirPlaceBindingInstance->GetDynamicOffsets())
            {
                if (!DynamicOffsets->empty())
                {
                    ReservoirPlaceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
                }
            }

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ReservoirPlacePSO, ReservoirPlaceCombiner, 0);
            const int32 PlaceGroupX = (GSurfelCellPageTableCapacity + 63) / 64;
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir PlaceWinners", Vector4(0.2f, 0.9f, 0.35f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirPlaceCandidates);
            g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, PlaceGroupX), 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIStatsBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
    }

    const bool CanGatherIrradianceInlineRay = gOptions.SurfelGIInlineRayEnable
        && gOptions.UseRaytracing
        && RenderFrameContextPtr->RaytracingScene
        && RenderFrameContextPtr->RaytracingScene->IsValid()
        && RenderFrameContextPtr->RaytracingScene->TLASBufferPtr;
    if (CanGatherIrradianceInlineRay)
    {
        struct alignas(16) jSurfelGIActiveCompactUniformBuffer
        {
            uint32 MaxSurfels = 0;
            uint32 Padding0 = 0;
            uint32 Padding1 = 0;
            uint32 Padding2 = 0;
        };
        static_assert(sizeof(jSurfelGIActiveCompactUniformBuffer) == 16, "jSurfelGIActiveCompactUniformBuffer must be 16-byte aligned");

        jSurfelGIActiveCompactUniformBuffer ActiveCompactUniformData;
        ActiveCompactUniformData.MaxSurfels = (uint32)Max(1, GSurfelPoolMaxCount);
        auto ActiveCompactUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIActiveCompactUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ActiveCompactUniformData)));
        ActiveCompactUniformBuffer->UpdateBufferData(&ActiveCompactUniformData, sizeof(ActiveCompactUniformData));

        {
            jShaderBindingArray ClearInlineRayDispatchBindingArray;
            jShaderBindingResourceInlineAllocator ClearInlineRayDispatchResourceAllocator;
            ClearInlineRayDispatchBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ClearInlineRayDispatchResourceAllocator.Alloc<jBufferResource>(GSurfelGIActiveCounterBuffer.get())));
            ClearInlineRayDispatchBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ClearInlineRayDispatchResourceAllocator.Alloc<jBufferResource>(GSurfelGIInlineRayDispatchArgsBuffer.get())));

            auto ClearInlineRayDispatchBindingInstance = g_rhi->CreateShaderBindingInstance(ClearInlineRayDispatchBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo ClearInlineRayDispatchShaderInfo;
            ClearInlineRayDispatchShaderInfo.SetName(jNameStatic("SurfelGIClearInlineRayDispatch_CS"));
            ClearInlineRayDispatchShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIClearInlineRayDispatch_cs.hlsl"));
            ClearInlineRayDispatchShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            ClearInlineRayDispatchShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* ClearInlineRayDispatchShader = g_rhi->CreateShader(ClearInlineRayDispatchShaderInfo);

            jShaderBindingLayoutArray ClearInlineRayDispatchLayoutArray;
            ClearInlineRayDispatchLayoutArray.Add(ClearInlineRayDispatchBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* ClearInlineRayDispatchPSO = g_rhi->CreateComputePipelineStateInfo(ClearInlineRayDispatchShader, ClearInlineRayDispatchLayoutArray, {});
            ClearInlineRayDispatchPSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray ClearInlineRayDispatchInstanceArray;
            ClearInlineRayDispatchInstanceArray.Add(ClearInlineRayDispatchBindingInstance.get());

            jShaderBindingInstanceCombiner ClearInlineRayDispatchCombiner;
            ClearInlineRayDispatchCombiner.ShaderBindingInstanceArray = &ClearInlineRayDispatchInstanceArray;
            ClearInlineRayDispatchCombiner.DescriptorSetHandles.Add(ClearInlineRayDispatchBindingInstance->GetHandle());

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ClearInlineRayDispatchPSO, ClearInlineRayDispatchCombiner, 0);
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Clear InlineRay Dispatch", Vector4(0.15f, 0.55f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchClearInlineRayDispatch);
            g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get());
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get(), EResourceLayout::UAV);

        {
            jShaderBindingArray CompactActiveBindingArray;
            jShaderBindingResourceInlineAllocator CompactActiveResourceAllocator;
            CompactActiveBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                CompactActiveResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
            CompactActiveBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                CompactActiveResourceAllocator.Alloc<jBufferResource>(GSurfelGIActiveIndexBuffer.get())));
            CompactActiveBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                CompactActiveResourceAllocator.Alloc<jBufferResource>(GSurfelGIActiveCounterBuffer.get())));
            CompactActiveBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                CompactActiveResourceAllocator.Alloc<jUniformBufferResource>(ActiveCompactUniformBuffer.get()), true));

            auto CompactActiveBindingInstance = g_rhi->CreateShaderBindingInstance(CompactActiveBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo CompactActiveShaderInfo;
            CompactActiveShaderInfo.SetName(jNameStatic("SurfelGICompactActiveInlineRayIndices_CS"));
            CompactActiveShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGICompactActiveInlineRayIndices_cs.hlsl"));
            CompactActiveShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            CompactActiveShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* CompactActiveShader = g_rhi->CreateShader(CompactActiveShaderInfo);

            jShaderBindingLayoutArray CompactActiveLayoutArray;
            CompactActiveLayoutArray.Add(CompactActiveBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* CompactActivePSO = g_rhi->CreateComputePipelineStateInfo(CompactActiveShader, CompactActiveLayoutArray, {});
            CompactActivePSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray CompactActiveInstanceArray;
            CompactActiveInstanceArray.Add(CompactActiveBindingInstance.get());

            jShaderBindingInstanceCombiner CompactActiveCombiner;
            CompactActiveCombiner.ShaderBindingInstanceArray = &CompactActiveInstanceArray;
            CompactActiveCombiner.DescriptorSetHandles.Add(CompactActiveBindingInstance->GetHandle());
            if (const std::vector<uint32>* DynamicOffsets = CompactActiveBindingInstance->GetDynamicOffsets())
            {
                if (!DynamicOffsets->empty())
                {
                    CompactActiveCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
                }
            }

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), CompactActivePSO, CompactActiveCombiner, 0);
            const int32 CompactGroupX = (Max(1, GSurfelPoolMaxCount) + 63) / 64;
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Compact Active InlineRay Indices", Vector4(0.2f, 0.6f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchCompactActiveInlineRayIndices);
            g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, CompactGroupX), 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get());

        {
            jShaderBindingArray BuildDispatchArgsBindingArray;
            jShaderBindingResourceInlineAllocator BuildDispatchArgsResourceAllocator;
            BuildDispatchArgsBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                BuildDispatchArgsResourceAllocator.Alloc<jBufferResource>(GSurfelGIActiveCounterBuffer.get())));
            BuildDispatchArgsBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                BuildDispatchArgsResourceAllocator.Alloc<jBufferResource>(GSurfelGIInlineRayDispatchArgsBuffer.get())));

            auto BuildDispatchArgsBindingInstance = g_rhi->CreateShaderBindingInstance(BuildDispatchArgsBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo BuildDispatchArgsShaderInfo;
            BuildDispatchArgsShaderInfo.SetName(jNameStatic("SurfelGIBuildInlineRayDispatchArgs_CS"));
            BuildDispatchArgsShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIBuildInlineRayDispatchArgs_cs.hlsl"));
            BuildDispatchArgsShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            BuildDispatchArgsShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* BuildDispatchArgsShader = g_rhi->CreateShader(BuildDispatchArgsShaderInfo);

            jShaderBindingLayoutArray BuildDispatchArgsLayoutArray;
            BuildDispatchArgsLayoutArray.Add(BuildDispatchArgsBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* BuildDispatchArgsPSO = g_rhi->CreateComputePipelineStateInfo(BuildDispatchArgsShader, BuildDispatchArgsLayoutArray, {});
            BuildDispatchArgsPSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray BuildDispatchArgsInstanceArray;
            BuildDispatchArgsInstanceArray.Add(BuildDispatchArgsBindingInstance.get());

            jShaderBindingInstanceCombiner BuildDispatchArgsCombiner;
            BuildDispatchArgsCombiner.ShaderBindingInstanceArray = &BuildDispatchArgsInstanceArray;
            BuildDispatchArgsCombiner.DescriptorSetHandles.Add(BuildDispatchArgsBindingInstance->GetHandle());

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), BuildDispatchArgsPSO, BuildDispatchArgsCombiner, 0);
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Build InlineRay DispatchArgs", Vector4(0.25f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchBuildInlineRayDispatchArgs);
            g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get());

        struct alignas(16) jSurfelGIInlineRayGatherUniformBuffer
        {
            Matrix V;
            Matrix P;
            Matrix InvP;
            Matrix InvV;
            uint32 MaxSurfels = 0;
            uint32 RayCount = 0;
            float MaxRayDistance = 0.0f;
            float NormalBias = 0.0f;
            float HistoryBlend = 0.0f;
            float HitDepthThickness = 0.0f;
            int32 FrameNumber = 0;
            float Padding0 = 0.0f;
            jFloat4 SkyColor = { 0.0f, 0.0f, 0.0f, 0.0f };
            jFloat4 SunDirectionAndIntensity = { 0.0f, 0.0f, -1.0f, 0.0f };
            jFloat4 SunColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        };
        static_assert((sizeof(jSurfelGIInlineRayGatherUniformBuffer) % 16) == 0, "jSurfelGIInlineRayGatherUniformBuffer size must be 16-byte aligned");

        jSurfelGIInlineRayGatherUniformBuffer GatherUniformData;
        GatherUniformData.V = MainCamera->View;
        GatherUniformData.P = MainCamera->Projection;
        GatherUniformData.InvP = MainCamera->Projection.GetInverse();
        GatherUniformData.InvV = MainCamera->View.GetInverse();
        GatherUniformData.MaxSurfels = (uint32)Max(1, GSurfelPoolMaxCount);
        GatherUniformData.RayCount = (uint32)Clamp(gOptions.SurfelGIInlineRayCount, 1, 16);
        GatherUniformData.MaxRayDistance = Max(10.0f, gOptions.SurfelGIInlineRayMaxDistance);
        GatherUniformData.NormalBias = Max(0.001f, gOptions.SurfelGIInlineRayNormalBias);
        GatherUniformData.HistoryBlend = Clamp(gOptions.SurfelGIInlineRayHistoryBlend, 0.0f, 0.99f);
        GatherUniformData.HitDepthThickness = 25.0f;
        GatherUniformData.FrameNumber = UniformData.FrameNumber;
        const Vector SkyColor = Vector(0.02f, 0.03f, 0.05f) + gOptions.DirectionalLightColor * 0.08f;
        const Vector SunDirection = gOptions.DefaultSunDir.GetNormalize();
        GatherUniformData.SkyColor = { Max(0.0f, SkyColor.x), Max(0.0f, SkyColor.y), Max(0.0f, SkyColor.z), 1.0f };
        GatherUniformData.SunDirectionAndIntensity = {
            SunDirection.x,
            SunDirection.y,
            SunDirection.z,
            Max(0.0f, gOptions.DirectionalLightIntensity * 0.05f)
        };
        GatherUniformData.SunColor = {
            Max(0.0f, gOptions.DirectionalLightColor.x),
            Max(0.0f, gOptions.DirectionalLightColor.y),
            Max(0.0f, gOptions.DirectionalLightColor.z),
            1.0f
        };

        auto GatherUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIInlineRayGatherUniformBuffer"), jLifeTimeType::OneFrame, sizeof(GatherUniformData)));
        GatherUniformBuffer->UpdateBufferData(&GatherUniformData, sizeof(GatherUniformData));

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::UAV);

        jShaderBindingArray IrradianceGatherBindingArray;
        jShaderBindingResourceInlineAllocator IrradianceGatherResourceAllocator;
        int32 IrradianceGatherBindingPoint = 0;
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jBufferResource>(RenderFrameContextPtr->RaytracingScene->TLASBufferPtr.get()), true));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGIActiveIndexBuffer.get())));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jBufferResource>(GSurfelGIActiveCounterBuffer.get())));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState)));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), SamplerState)));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));
        IrradianceGatherBindingArray.Add(jShaderBinding::Create(IrradianceGatherBindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
            IrradianceGatherResourceAllocator.Alloc<jUniformBufferResource>(GatherUniformBuffer.get()), true));

        auto IrradianceGatherBindingInstance = g_rhi->CreateShaderBindingInstance(IrradianceGatherBindingArray, jShaderBindingInstanceType::SingleFrame);

        jShaderInfo IrradianceGatherShaderInfo;
        IrradianceGatherShaderInfo.SetName(jNameStatic("SurfelGIGatherIrradianceInlineRay_CS"));
        IrradianceGatherShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIGatherIrradianceInlineRay_cs.hlsl"));
        IrradianceGatherShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        IrradianceGatherShaderInfo.SetEntryPoint(jNameStatic("main"));
        jShader* IrradianceGatherShader = g_rhi->CreateShader(IrradianceGatherShaderInfo);

        jShaderBindingLayoutArray IrradianceGatherLayoutArray;
        IrradianceGatherLayoutArray.Add(IrradianceGatherBindingInstance->ShaderBindingsLayouts);
        jPipelineStateInfo* IrradianceGatherPSO = g_rhi->CreateComputePipelineStateInfo(IrradianceGatherShader, IrradianceGatherLayoutArray, {});
        IrradianceGatherPSO->Bind(RenderFrameContextPtr);

        jShaderBindingInstanceArray IrradianceGatherInstanceArray;
        IrradianceGatherInstanceArray.Add(IrradianceGatherBindingInstance.get());

        jShaderBindingInstanceCombiner IrradianceGatherCombiner;
        IrradianceGatherCombiner.ShaderBindingInstanceArray = &IrradianceGatherInstanceArray;
        IrradianceGatherCombiner.DescriptorSetHandles.Add(IrradianceGatherBindingInstance->GetHandle());
        if (const std::vector<uint32>* DynamicOffsets = IrradianceGatherBindingInstance->GetDynamicOffsets())
        {
            if (!DynamicOffsets->empty())
            {
                IrradianceGatherCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
            }
        }

        g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceGatherPSO, IrradianceGatherCombiner, 0);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI GatherIrradiance InlineRay", Vector4(0.2f, 0.65f, 0.95f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchGatherIrradianceInlineRay);
        const jName RHIName = g_rhi->GetRHIName();
        const bool SupportsComputeIndirectDispatch = (RHIName == jNameStatic("Vulkan")) || (RHIName == jNameStatic("DirectX12"));
        if (SupportsComputeIndirectDispatch)
        {
            g_rhi->DispatchComputeIndirect(RenderFrameContextPtr, GSurfelGIInlineRayDispatchArgsBuffer.get(), 0);
        }
        else
        {
            const int32 FallbackGatherGroupX = (Max(1, GSurfelGIActiveIndexCapacity) + 63) / 64;
            g_rhi->DispatchCompute(RenderFrameContextPtr, Max(1, FallbackGatherGroupX), 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture());

    if (gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug || gOptions.ShowSurfelGIIrradianceDebug)
    {
        struct alignas(16) jSurfelGIVisualizeUniformBuffer
        {
            Matrix InvP;
            Matrix InvV;
            Vector2 ScreenSize;
            float BlendAlpha;
            float GridCellSize;
            jFloat4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeStartDistancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            int32 MaxSurfels;
            int32 SurfelPageSize;
            int32 SurfelPageTableCapacity;
            int32 NeighborCellRadius;
            int32 BlendWithScene;
            int32 ShowStateDebug;
            jFloat4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            jFloat4 CascadeCellCountPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
            int32 ShowCellDebug;
            int32 ShowUnderfilledCellDebug;
            int32 ShowCellGrid;
            int32 ShowSpawnAttemptDebug;
            int32 ShowIrradianceDebug;
            int32 Padding0;
            int32 Padding1;
            int32 Padding2;
        };
        static_assert((sizeof(jSurfelGIVisualizeUniformBuffer) % 16) == 0, "jSurfelGIVisualizeUniformBuffer size must be 16-byte aligned");

        jSurfelGIVisualizeUniformBuffer VisualizeUniformData;
        VisualizeUniformData.InvP = MainCamera->Projection.GetInverse();
        VisualizeUniformData.InvV = MainCamera->View.GetInverse();
        VisualizeUniformData.ScreenSize = Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT);
        VisualizeUniformData.BlendAlpha = Clamp(gOptions.SurfelGIVisualizeBlendAlpha, 0.0f, 1.0f);
        VisualizeUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
        for (int32 pack = 0; pack < SURFEL_GI_CASCADE_PACKED_COUNT; ++pack)
        {
            VisualizeUniformData.CascadeCellScaleFromPrevPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeStartDistancePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeClipmapGridDimXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeClipmapGridDimYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeClipmapGridDimZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.SurfelsPerCellPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeOriginCellXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeOriginCellYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeOriginCellZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeRingOffsetXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeRingOffsetYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeRingOffsetZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeCellBasePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            VisualizeUniformData.CascadeCellCountPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
        auto SetPackedVisualizeValue = [](jFloat4* packedArray, int32 cascade, float value)
        {
            const int32 packIndex = cascade / 4;
            const int32 lane = cascade % 4;
            if (lane == 0) packedArray[packIndex].x = value;
            else if (lane == 1) packedArray[packIndex].y = value;
            else if (lane == 2) packedArray[packIndex].z = value;
            else packedArray[packIndex].w = value;
        };
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            SetPackedVisualizeValue(VisualizeUniformData.CascadeCellScaleFromPrevPacked, cascade, (cascade == 0) ? 1.0f : Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[cascade]));
            SetPackedVisualizeValue(VisualizeUniformData.CascadeStartDistancePacked, cascade, CascadeStartDistanceSanitized[cascade]);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeClipmapGridDimXPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512));
            SetPackedVisualizeValue(VisualizeUniformData.CascadeClipmapGridDimYPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512));
            SetPackedVisualizeValue(VisualizeUniformData.CascadeClipmapGridDimZPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512));
            SetPackedVisualizeValue(VisualizeUniformData.CascadeOriginCellXPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginX);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeOriginCellYPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginY);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeOriginCellZPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginZ);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeRingOffsetXPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeRingOffsetYPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeRingOffsetZPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeCellBasePacked, cascade, (float)GSurfelCascadeCellBase[cascade]);
            SetPackedVisualizeValue(VisualizeUniformData.CascadeCellCountPacked, cascade, (float)GSurfelCascadeCellCount[cascade]);
        }
        VisualizeUniformData.MaxSurfels = GSurfelPoolMaxCount;
        VisualizeUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
        VisualizeUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
        VisualizeUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
        VisualizeUniformData.BlendWithScene = gOptions.SurfelGIVisualizeBlendWithScene ? 1 : 0;
        VisualizeUniformData.ShowStateDebug = gOptions.ShowSurfelGIStateDebug ? 1 : 0;
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            SetPackedVisualizeValue(VisualizeUniformData.SurfelsPerCellPacked, cascade, (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL));
        }
        VisualizeUniformData.ShowCellDebug = gOptions.ShowSurfelGICellDebug ? 1 : 0;
        VisualizeUniformData.ShowUnderfilledCellDebug = gOptions.ShowSurfelGIUnderfilledCellDebug ? 1 : 0;
        VisualizeUniformData.ShowCellGrid = gOptions.ShowSurfelGICellGrid ? 1 : 0;
        VisualizeUniformData.ShowSpawnAttemptDebug = gOptions.ShowSurfelGISpawnAttemptDebug ? 1 : 0;
        VisualizeUniformData.ShowIrradianceDebug = gOptions.ShowSurfelGIIrradianceDebug ? 1 : 0;
        VisualizeUniformData.Padding0 = 0;
        VisualizeUniformData.Padding1 = 0;
        VisualizeUniformData.Padding2 = 0;

        auto VisualizeUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIVisualizeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(VisualizeUniformData)));
        VisualizeUniformBuffer->UpdateBufferData(&VisualizeUniformData, sizeof(VisualizeUniformData));

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch Visualize", Vector4(0.15f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchVisualize);
            jRHIUtil::DispatchCompute(RenderFrameContextPtr, jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(),
                [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                    InOutShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), nullptr)));

                    InOutShaderBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(VisualizeUniformBuffer.get()), true));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(6, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(7, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));
                },
                [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo ShaderInfo;
                    ShaderInfo.SetName(jNameStatic("SurfelGIVisualize_CS"));
                    ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIVisualize_cs.hlsl"));
                    ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                    ShaderInfo.SetEntryPoint(jNameStatic("main"));
                    return g_rhi->CreateShader(ShaderInfo);
                });
        }

        if (gOptions.SurfelGIVisualizeBlendWithScene)
        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI BlendToScene", Vector4(0.8f, 0.45f, 0.2f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_BlendToScene);

            auto TempColorRT = jRenderTargetPool::GetRenderTargetForOneFrame(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info);
            jRHIUtil::DrawQuad(RenderFrameContextPtr, TempColorRT, { 0, 0, SCR_WIDTH, SCR_HEIGHT },
                [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    jTexture* InTexture = InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture();
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                    InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture, SamplerState)));
                },
                [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo shaderInfo;
                    shaderInfo.SetName(jNameStatic("CopyPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    return g_rhi->CreateShader(shaderInfo);
                }
            );

            struct jApplySurfelVisualizeUniformBuffer
            {
                float BlendAlpha;
                int32 SceneWidth;
                int32 SceneHeight;
                int32 Padding0;
            };

            jApplySurfelVisualizeUniformBuffer ApplyUniformData;
            ApplyUniformData.BlendAlpha = Clamp(gOptions.SurfelGIVisualizeBlendAlpha, 0.0f, 1.0f);
            ApplyUniformData.SceneWidth = SCR_WIDTH;
            ApplyUniformData.SceneHeight = SCR_HEIGHT;
            ApplyUniformData.Padding0 = 0;

            auto ApplyUniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("ApplySurfelGIVisualizeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ApplyUniformData));
            ApplyUniformBuffer->UpdateBufferData(&ApplyUniformData, sizeof(ApplyUniformData));

            jRHIUtil::DispatchCompute(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(),
                [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), TempColorRT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

                    InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(TempColorRT->GetTexture(), nullptr)));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), nullptr)));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::COMPUTE
                        , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(ApplyUniformBuffer.get())));
                },
                [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo shaderInfo;
                    shaderInfo.SetName(jNameStatic("ApplySurfelGIVisualize_CS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/ApplySurfelGIVisualize_cs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                    return g_rhi->CreateShader(shaderInfo);
                }
            );
        }
    }

    if (gOptions.ShowSurfelGIDebug
        || ((gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug || gOptions.ShowSurfelGIIrradianceDebug) && !gOptions.SurfelGIVisualizeBlendWithScene))
    {
        DebugRTs.push_back(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexturePtr());
    }
}
