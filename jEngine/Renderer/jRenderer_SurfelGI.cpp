#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Material/jMaterial.h"
#include "FileLoader/jImageFileLoader.h"
#include "jSceneRenderTargets.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jRaytracingScene.h"
#include "Shader/jShaderParameterSet.h"
#include <cmath>
#include <array>
#include <unordered_map>
#include <cstddef>

namespace
{
BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jApplySurfelGIVisualizeUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(float, BlendAlpha)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SceneWidth)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SceneHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jApplySurfelGIVisualizeCSParameters)
    SHADER_RW_TEXTURE2D(OutSceneColor)
    SHADER_TEXTURE2D(SceneColorInput)
    SHADER_TEXTURE2D(SurfelVisualizeInput)
    SHADER_UNIFORM_BUFFER(jApplySurfelGIVisualizeUniformBuffer, ApplyCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jApplySurfelGIUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(float, SurfelGIIntensity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SceneWidth)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SceneHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIActiveCompactUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, MaxSurfels)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, Padding1)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, Padding2)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jApplySurfelGICSParameters)
    SHADER_RW_TEXTURE2D(OutColorTexture)
    SHADER_TEXTURE2D(SceneColorTexture)
    SHADER_TEXTURE2D(SurfelGITexture)
    SHADER_TEXTURE2D(AlbedoTexture)
    SHADER_UNIFORM_BUFFER(jApplySurfelGIUniformBuffer, ApplySurfelGIUniformBuffer)
END_SHADER_PARAMETER_SET()

template <typename TShaderParameters>
void DispatchShaderParameterComputePass(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , jName InShaderName, jName InShaderFilePath, const TShaderParameters& InParameters
    , uint32 NumGroupsX, uint32 NumGroupsY, uint32 NumGroupsZ)
{
    auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
        InParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

    jShaderInfo ShaderInfo;
    ShaderInfo.SetName(InShaderName);
    ShaderInfo.SetShaderFilepath(InShaderFilePath);
    ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    jShaderParameterSet::AppendToShaderInfo<TShaderParameters>(ShaderInfo, 0);
    jShader* Shader = g_rhi->CreateShader(ShaderInfo);

    jShaderBindingInstanceGroup ShaderBindingGroup;
    ShaderBindingGroup.Add(CurrentBindingInstance);
    jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingGroup.GetLayoutArray(), {});
    ComputePipelineStateInfo->Bind(InRenderFrameContextPtr);

    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingGroup.GetCombiner(), 0);
    g_rhi->DispatchCompute(InRenderFrameContextPtr, NumGroupsX, NumGroupsY, NumGroupsZ);
}

/*
 * CPU-side overview of the SurfelGI pipeline implemented in this file.
 *
 * 1. Build and maintain a clipmapped surfel pool in world space.
 *    - surfels are partitioned by cascade and stored in paged cell slots
 *    - only visible / recently relevant cells are refreshed every frame
 *
 * 2. Gather incoming radiance for active surfels.
 *    - each active surfel shoots a small number of inline rays
 *    - the result is accumulated into a temporal history buffer
 *    - the current history model is MSME (mean / short mean / variance / inconsistency / vbbr)
 *    - optional guiding biases future rays toward bright directions discovered in previous frames
 *
 * 3. Resolve surfel-space lighting back to pixels.
 *    - every screen pixel looks up nearby surfels in the clipmap
 *    - their irradiance is blended with geometric and confidence weights
 *    - the result is written into a screen-space irradiance texture
 *
 * 4. Apply the resolved irradiance to scene color.
 *    - final contribution is diffuse indirect lighting: irradiance * albedo / PI
 *
 * 5. Provide debug views.
 *    - surfel placement / occupancy / irradiance state visualization
 *    - hovered-surface ray debug that shows the actual rays fired this frame
 */
constexpr int32 SURFEL_GI_GUIDE_DIM = 4;
constexpr int32 SURFEL_GI_GUIDE_TOTAL_FLOATS = SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM + SURFEL_GI_GUIDE_DIM;
constexpr int32 SURFEL_GI_HOVER_DEBUG_MAX_RAYS = 16;

// Packed GPU representation of one surfel stored in the global surfel pool.
// The HLSL side reads the same layout directly, so field order matters.
BEGIN_SHADER_STRUCT(jSurfelGPU)
    SHADER_STRUCT_MEMBER(Vector4, PositionRadius)
    SHADER_STRUCT_MEMBER(Vector, Normal)
    SHADER_STRUCT_MEMBER(uint32, LastSeenFrame)
    SHADER_STRUCT_MEMBER(Vector4, AlbedoWeight)
    SHADER_STRUCT_MEMBER(int32, State)
    SHADER_STRUCT_MEMBER(uint32, IsActive)
    SHADER_STRUCT_MEMBER(uint32, OwnerCellHash)
    SHADER_STRUCT_MEMBER(uint32, CascadeIndex)
END_SHADER_STRUCT()
static_assert(sizeof(jSurfelGPU) == 64, "jSurfelGPU layout must match HLSL SurfelData");

// Temporal lighting state stored per surfel.
// IrradianceAndCount.xyz is the long-term irradiance used by resolve/apply.
// MSMEData0 / MSMEData1 store the extra state needed by the MSME update.
BEGIN_SHADER_STRUCT(jSurfelIrradianceGPU)
    SHADER_STRUCT_MEMBER(Vector4, IrradianceAndCount)
    SHADER_STRUCT_MEMBER(Vector4, MSMEData0)
    SHADER_STRUCT_MEMBER(Vector4, MSMEData1)
END_SHADER_STRUCT()

BEGIN_SHADER_STRUCT(jSurfelCandidateGPU)
    SHADER_STRUCT_MEMBER(jSurfelGPU, Surfel)
    SHADER_STRUCT_MEMBER(uint32, Priority)
    SHADER_STRUCT_MEMBER(uint32, Padding0)
    SHADER_STRUCT_MEMBER(uint32, Padding1)
    SHADER_STRUCT_MEMBER(uint32, Padding2)
END_SHADER_STRUCT()

BEGIN_SHADER_STRUCT(jSurfelGIStatsGPU)
    SHADER_STRUCT_MEMBER(uint32, ActiveCount)
    SHADER_STRUCT_MEMBER(uint32, MismatchCount)
    SHADER_STRUCT_MEMBER(uint32, ReservoirOverflowCount)
    SHADER_STRUCT_MEMBER(uint32, ReservoirRejectedCount)
END_SHADER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIClearStatsCSParameters)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGIStatsGPU, StatsBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIReservoirClearUniform)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, CandidateCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, PageCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, Padding1)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, V)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvV)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ScreenSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, NormalThreshold)
    SHADER_UNIFORM_BUFFER_MEMBER(float, DepthEdgeScale)
    SHADER_UNIFORM_BUFFER_MEMBER(float, NormalEdgeScale)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, PreferCellCenterForFirstPlacement)
    SHADER_UNIFORM_BUFFER_MEMBER(float, MinRadius)
    SHADER_UNIFORM_BUFFER_MEMBER(float, MaxDistance)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, TileSize)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MaxSurfels)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, CascadePartitionCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageSize)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageTableCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SpawnBudget)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, TTLInFrames)
    SHADER_UNIFORM_BUFFER_MEMBER(float, GridCellSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellScalePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeStartDistancePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRadiusScalePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, OutOfViewKeepFrames)
    SHADER_UNIFORM_BUFFER_MEMBER(float, RadiusScale)
    SHADER_UNIFORM_BUFFER_MEMBER(float, FaceMarginRadiusScale)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SurfelsPerCellPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellBasePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellCountPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeDeltaCellXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeDeltaCellYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeDeltaCellZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClearAllPacked)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIClearVisibleCellCounterCSParameters)
    SHADER_RW_STRUCTURED_BUFFER(uint32, VisibleCellCounterBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIClearCandidatesCSParameters)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelCandidateGPU, CandidateBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, WinnerScoreBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, WinnerIndexBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, WinnerLockBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIReservoirClearUniform, ClearParam)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIClearClipmapCellsCSParameters)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_RW_STRUCTURED_BUFFER(uint32, CellSurfelCount)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelIrradianceGPU, SurfelIrradianceBuffer)
    SHADER_RW_STRUCTURED_BUFFER(float, SurfelGuidingBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIVisibleCellCollectCSParameters)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
    SHADER_RW_STRUCTURED_BUFFER(uint32, VisibleCellCounterBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIRefreshVisibleCellSurfelsCSParameters)
    SHADER_STRUCTURED_BUFFER(uint32, VisibleCellCounterBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelCellPageTable)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGICleanupCSParameters)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelIrradianceGPU, SurfelIrradianceBuffer)
    SHADER_RW_STRUCTURED_BUFFER(float, SurfelGuidingBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIGatherCandidatesCSParameters)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_TEXTURE2D(GBuffer0)
    SHADER_TEXTURE2D(GBuffer1)
    SHADER_TEXTURE2D_SRV(LinearDepthTexture)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_RW_TEXTURE2D(DebugOutput)
    SHADER_RW_TEXTURE2D(AttemptOutput)
    SHADER_RW_STRUCTURED_BUFFER(uint32, SurfelCellPageTable)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGIStatsGPU, SurfelGIStatsBuffer)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelCandidateGPU, CandidateBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, WinnerScoreBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, WinnerIndexBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, WinnerLockBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIPlaceCandidatesCSParameters)
    SHADER_STRUCTURED_BUFFER(jSurfelCandidateGPU, CandidateBuffer)
    SHADER_STRUCTURED_BUFFER(uint32, WinnerScoreBuffer)
    SHADER_STRUCTURED_BUFFER(uint32, WinnerIndexBuffer)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelCellPageTable)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGIStatsGPU, SurfelGIStatsBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIUniformBuffer, ComputeCommon)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelIrradianceGPU, SurfelIrradianceBuffer)
    SHADER_RW_STRUCTURED_BUFFER(float, SurfelGuidingBuffer)
END_SHADER_PARAMETER_SET()

namespace
{
    struct alignas(16) jSurfelGILegacyCandidateGPU
    {
        jSurfelGPU Surfel;
        uint32 Priority = 0;
        uint32 Padding0 = 0;
        uint32 Padding1 = 0;
        uint32 Padding2 = 0;
    };

    struct alignas(16) jSurfelGILegacyUniformBuffer
    {
        Matrix InvP;
        Matrix V;
        Matrix InvV;
        Vector2 ScreenSize;
        float NormalThreshold;
        float DepthEdgeScale;
        float NormalEdgeScale;
        int32 PreferCellCenterForFirstPlacement;
        float MinRadius;
        float MaxDistance;
        uint32 FrameNumber;
        int32 TileSize;
        int32 MaxSurfels;
        int32 CascadePartitionCapacity;
        int32 SurfelPageSize;
        int32 SurfelPageTableCapacity;
        int32 SpawnBudget;
        int32 TTLInFrames;
        float GridCellSize;
        Vector4 CascadeCellScalePacked;
        Vector4 CascadeStartDistancePacked;
        Vector4 CascadeRadiusScalePacked;
        int32 OutOfViewKeepFrames;
        float RadiusScale;
        float FaceMarginRadiusScale;
        Vector4 CascadeClipmapGridDimXPacked;
        Vector4 CascadeClipmapGridDimYPacked;
        Vector4 CascadeClipmapGridDimZPacked;
        Vector4 SurfelsPerCellPacked;
        Vector4 CascadeOriginCellXPacked;
        Vector4 CascadeOriginCellYPacked;
        Vector4 CascadeOriginCellZPacked;
        Vector4 CascadeRingOffsetXPacked;
        Vector4 CascadeRingOffsetYPacked;
        Vector4 CascadeRingOffsetZPacked;
        Vector4 CascadeCellBasePacked;
        Vector4 CascadeCellCountPacked;
        Vector4 CascadeDeltaCellXPacked;
        Vector4 CascadeDeltaCellYPacked;
        Vector4 CascadeDeltaCellZPacked;
        Vector4 CascadeClearAllPacked;
    };

    static_assert(sizeof(jSurfelCandidateGPU) == sizeof(jSurfelGILegacyCandidateGPU), "jSurfelCandidateGPU size mismatch");
    static_assert(alignof(jSurfelCandidateGPU) == alignof(jSurfelGILegacyCandidateGPU), "jSurfelCandidateGPU alignment mismatch");
    static_assert(offsetof(jSurfelCandidateGPU, Surfel) == offsetof(jSurfelGILegacyCandidateGPU, Surfel), "jSurfelCandidateGPU Surfel offset mismatch");
    static_assert(offsetof(jSurfelCandidateGPU, Priority) == offsetof(jSurfelGILegacyCandidateGPU, Priority), "jSurfelCandidateGPU Priority offset mismatch");

    static_assert(sizeof(jSurfelGIUniformBuffer) == sizeof(jSurfelGILegacyUniformBuffer), "jSurfelGIUniformBuffer size mismatch");
    static_assert(alignof(jSurfelGIUniformBuffer) == alignof(jSurfelGILegacyUniformBuffer), "jSurfelGIUniformBuffer alignment mismatch");
    static_assert(offsetof(jSurfelGIUniformBuffer, ScreenSize) == offsetof(jSurfelGILegacyUniformBuffer, ScreenSize), "jSurfelGIUniformBuffer ScreenSize offset mismatch");
    static_assert(offsetof(jSurfelGIUniformBuffer, NormalEdgeScale) == offsetof(jSurfelGILegacyUniformBuffer, NormalEdgeScale), "jSurfelGIUniformBuffer NormalEdgeScale offset mismatch");
    static_assert(offsetof(jSurfelGIUniformBuffer, GridCellSize) == offsetof(jSurfelGILegacyUniformBuffer, GridCellSize), "jSurfelGIUniformBuffer GridCellSize offset mismatch");
    static_assert(offsetof(jSurfelGIUniformBuffer, CascadeCellScalePacked) == offsetof(jSurfelGILegacyUniformBuffer, CascadeCellScalePacked), "jSurfelGIUniformBuffer CascadeCellScalePacked offset mismatch");
    static_assert(offsetof(jSurfelGIUniformBuffer, CascadeClipmapGridDimXPacked) == offsetof(jSurfelGILegacyUniformBuffer, CascadeClipmapGridDimXPacked), "jSurfelGIUniformBuffer CascadeClipmapGridDimXPacked offset mismatch");
    static_assert(offsetof(jSurfelGIUniformBuffer, CascadeClearAllPacked) == offsetof(jSurfelGILegacyUniformBuffer, CascadeClearAllPacked), "jSurfelGIUniformBuffer CascadeClearAllPacked offset mismatch");
}

BEGIN_SHADER_STRUCT(jSurfelInlineRayDispatchArgsGPU)
    SHADER_STRUCT_MEMBER(uint32, GroupCountX)
    SHADER_STRUCT_MEMBER(uint32, GroupCountY)
    SHADER_STRUCT_MEMBER(uint32, GroupCountZ)
    SHADER_STRUCT_MEMBER(uint32, Padding0)
END_SHADER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIClearInlineRayDispatchCSParameters)
    SHADER_RW_STRUCTURED_BUFFER(uint32, ActiveCounterBuffer)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelInlineRayDispatchArgsGPU, DispatchArgsBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIBuildInlineRayDispatchArgsCSParameters)
    SHADER_STRUCTURED_BUFFER(uint32, ActiveCounterBuffer)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelInlineRayDispatchArgsGPU, DispatchArgsBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGICompactActiveInlineRayIndicesCSParameters)
    SHADER_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_RW_STRUCTURED_BUFFER(uint32, ActiveIndexBuffer)
    SHADER_RW_STRUCTURED_BUFFER(uint32, ActiveCounterBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIActiveCompactUniformBuffer, ActiveCompactUniformBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_STRUCT(jSurfelGIHoverSelectionGPU)
    SHADER_STRUCT_MEMBER(uint32, SurfelIndex)
    SHADER_STRUCT_MEMBER(uint32, Valid)
END_SHADER_STRUCT()

BEGIN_SHADER_STRUCT(jSurfelGIHoverRayDebugGPU)
    SHADER_STRUCT_MEMBER(Vector4, OriginAndCount)
    SHADER_STRUCT_MEMBER_ARRAY(Vector4, RayDirAndType, SURFEL_GI_HOVER_DEBUG_MAX_RAYS)
    SHADER_STRUCT_MEMBER_ARRAY(Vector4, RayColor, SURFEL_GI_HOVER_DEBUG_MAX_RAYS)
END_SHADER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIInlineRayGatherUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, MaxSurfels)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, RayCount)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, BootstrapRayCount)
    SHADER_UNIFORM_BUFFER_MEMBER(float, MaxRayDistance)
    SHADER_UNIFORM_BUFFER_MEMBER(float, RadianceScale)
    SHADER_UNIFORM_BUFFER_MEMBER(float, NormalBias)
    SHADER_UNIFORM_BUFFER_MEMBER(float, HistoryBlend)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, UseGuiding)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, SurfelPageSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, GridCellSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellScalePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeStartDistancePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellBasePacked)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIVisualizeUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvV)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, ViewProj)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ScreenSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, BlendAlpha)
    SHADER_UNIFORM_BUFFER_MEMBER(float, GridCellSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellScalePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeStartDistancePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MaxSurfels)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, CascadePartitionCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageSize)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageTableCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, NeighborCellRadius)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, BlendWithScene)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowStateDebug)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SurfelsPerCellPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellBasePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellCountPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowCellDebug)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowUnderfilledCellDebug)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowCellGrid)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowSpawnAttemptDebug)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowIrradianceDebug)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, IrradianceDebugMode)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowHoverRayDebug)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, ShowHoverRayRadianceColor)
    SHADER_UNIFORM_BUFFER_MEMBER(float, HoverRayLength)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIVisualizeCSParameters)
    SHADER_RW_TEXTURE2D(Result)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_TEXTURE2D(LinearDepthTexture)
    SHADER_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_TEXTURE2D(SpawnAttemptTexture)
    SHADER_UNIFORM_BUFFER(jSurfelGIVisualizeUniformBuffer, VisualizeCommon)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelCellPageTable)
    SHADER_STRUCTURED_BUFFER(jSurfelIrradianceGPU, SurfelIrradianceBuffer)
    SHADER_STRUCTURED_BUFFER(uint32, WinnerScoreBuffer)
    SHADER_STRUCTURED_BUFFER(uint32, WinnerIndexBuffer)
    SHADER_TEXTURE2D(GBufferNormalTexture)
    SHADER_STRUCTURED_BUFFER(jSurfelGIHoverRayDebugGPU, HoverRayDebugBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIResolveUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvV)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ScreenSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, GridCellSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellScalePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SurfelsPerCellPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellBasePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MaxSurfels)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, CascadePartitionCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageSize)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageTableCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, NeighborCellRadius)
    SHADER_UNIFORM_BUFFER_MEMBER(float, ResolveSoftness)
    SHADER_UNIFORM_BUFFER_MEMBER(float, ResolveIrradianceWarmupUpdates)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding1)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding2)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIResolveCSParameters)
    SHADER_RW_TEXTURE2D(Result)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_TEXTURE2D(GBufferNormalTexture)
    SHADER_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelCellPageTable)
    SHADER_STRUCTURED_BUFFER(jSurfelIrradianceGPU, SurfelIrradianceBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIResolveUniformBuffer, ResolveCommon)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIHoverSelectUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvV)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ScreenSize)
    SHADER_UNIFORM_BUFFER_MEMBER(float, GridCellSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellScalePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeClipmapGridDimZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, SurfelsPerCellPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeOriginCellZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetXPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetYPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeRingOffsetZPacked)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, CascadeCellBasePacked)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MaxSurfels)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, CascadePartitionCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageSize)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, SurfelPageTableCapacity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, NeighborCellRadius)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MousePixelX)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MousePixelY)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, MouseValid)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, Padding0)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGISelectHoveredSurfelCSParameters)
    SHADER_TEXTURE2D(DepthTexture)
    SHADER_STRUCTURED_BUFFER(jSurfelGPU, SurfelPool)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelCellPageTable)
    SHADER_UNIFORM_BUFFER(jSurfelGIHoverSelectUniformBuffer, HoverSelectCommon)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGIHoverSelectionGPU, HoverSelectionBuffer)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGIHoverRayDebugGPU, HoverRayDebugBuffer)
END_SHADER_PARAMETER_SET()

// Runtime state that maps a world-space clipmap coordinate to the physical ring-buffer
// location used by the current frame. This is updated on the CPU and packed into several
// uniform buffers so that every shader can perform the same cell lookup deterministically.
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

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jSurfelGIHWRTDISceneConstantBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, ProjectionToWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, CameraPosition)
    SHADER_UNIFORM_BUFFER_MEMBER(float, NormalBias)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, NumLights)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, DebugViewMode)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, ForceMipLevel0)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, RenderWidth)
    SHADER_UNIFORM_BUFFER_MEMBER(float, DebugLineWidth)
    SHADER_UNIFORM_BUFFER_MEMBER(float, DebugUVScale)
    SHADER_UNIFORM_BUFFER_MEMBER(float, DebugPrimitiveIDScale)
    SHADER_UNIFORM_BUFFER_MEMBER(float, ShadowRayStartOffset)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, RenderHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding0)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding1)
    SHADER_UNIFORM_BUFFER_MEMBER(float, Padding2)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_STRUCT(HWRTDILightData)
    SHADER_STRUCT_MEMBER(Vector4, ColorAndType)
    SHADER_STRUCT_MEMBER(Vector4, PositionAndMaxDistance)
    SHADER_STRUCT_MEMBER(Vector4, DirectionAndPenumbra)
    SHADER_STRUCT_MEMBER(Vector4, UmbraAndPadding)
END_SHADER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIHWRTDIGlobalParameters)
    SHADER_ACCELERATION_STRUCTURE(Scene)
    SHADER_RW_TEXTURE2D(RenderTarget)
    SHADER_UNIFORM_BUFFER(jSurfelGIHWRTDISceneConstantBuffer, g_sceneCB)
    SHADER_SAMPLER(DefaultSamplerState)
    SHADER_TEXTURECUBE_SRV(EnvTexture)
    SHADER_STRUCTURED_BUFFER(HWRTDILightData, LightBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSurfelGIHWRTDIGatherParameters)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelGIActiveSurfelIndexBuffer)
    SHADER_STRUCTURED_BUFFER(uint32, SurfelGIActiveSurfelCounterBuffer)
    SHADER_STRUCTURED_BUFFER(jSurfelGPU, SurfelGIPool)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelIrradianceGPU, SurfelGIIrradianceBuffer)
    SHADER_RW_STRUCTURED_BUFFER(float, SurfelGIGuidingBuffer)
    SHADER_UNIFORM_BUFFER(jSurfelGIInlineRayGatherUniformBuffer, g_surfelGatherCB)
    SHADER_STRUCTURED_BUFFER(jSurfelGIHoverSelectionGPU, SurfelGIHoverSelectionBuffer)
    SHADER_RW_STRUCTURED_BUFFER(jSurfelGIHoverRayDebugGPU, SurfelGIHoverRayDebugBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_STRUCT(MaterialInstanceUniform)
    SHADER_STRUCT_MEMBER(uint32, MaterialFlags)
    SHADER_STRUCT_MEMBER(uint32, AlbedoSamplerIndex)
    SHADER_STRUCT_MEMBER(uint32, NormalSamplerIndex)
    SHADER_STRUCT_MEMBER(uint32, RMSamplerIndex)
    SHADER_STRUCT_MEMBER(float, AlphaCutoff)
    SHADER_STRUCT_MEMBER(float, Padding0)
    SHADER_STRUCT_MEMBER(float, Padding1)
    SHADER_STRUCT_MEMBER(float, Padding2)
END_SHADER_STRUCT()

struct jSurfelGIHWRTDIBindlessUInt2
{
};

template <>
struct TShaderParameterHLSLTypeInfo<jSurfelGIHWRTDIBindlessUInt2>
{
    static constexpr const char* GetTypeName() { return "uint2"; }
    static void AppendTypeDeclaration(std::string&) {}
};

BEGIN_SHADER_BINDLESS_SET(jSurfelGIHWRTDIBindlessParameters)
    // Bindless tables are assigned consecutive spaces after the fixed SurfelGI sets.
    SHADER_BINDLESS_STRUCTURED_BUFFER(jSurfelGIHWRTDIBindlessUInt2, VertexIndexOffsetArray)
    SHADER_BINDLESS_BUFFER(uint32, IndexBindlessArray)
    SHADER_BINDLESS_STRUCTURED_BUFFER(RenderObjectUniformBuffer, RenderObjParamArray)
    SHADER_BINDLESS_BYTEADDRESS_BUFFER(VerticesBindlessArray)
    SHADER_BINDLESS_UNIFORM_BUFFER(MaterialInstanceUniform, MaterialInstanceArray)
    SHADER_BINDLESS_TEXTURE2D(AlbedoTextureArray)
    SHADER_BINDLESS_TEXTURE2D(NormalTextureArray)
    SHADER_BINDLESS_TEXTURE2D(RMTextureArray)
    SHADER_BINDLESS_SAMPLER(AlbedoSamplerArray)
    SHADER_BINDLESS_SAMPLER(NormalSamplerArray)
    SHADER_BINDLESS_SAMPLER(RMSamplerArray)
END_SHADER_BINDLESS_SET()

struct jShaderSurfelGIGatherIrradianceHWRTCS : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jSurfelGIHWRTDIGlobalParameters,
        jSurfelGIHWRTDIGatherParameters)

    DECLARE_DEFINE(USE_SURFEL_GI, 0, 1);
    DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

    using ShaderPermutation = jPermutation<USE_SURFEL_GI, USE_BINDLESS_RESOURCE>;
    ShaderPermutation Permutation;

    static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
    {
        if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
            InOutBinder.AddBindless<jSurfelGIHWRTDIBindlessParameters>();
    }

    DECLARE_SHADER_WITH_PERMUTATION(jShaderSurfelGIGatherIrradianceHWRTCS, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderSurfelGIGatherIrradianceHWRTCS
    , "SurfelGIGatherIrradianceHWRTDI_CS"
    , "Resource/Shaders/hlsl/HWRT_DI.hlsl"
    , ""
    , "SurfelGIGatherIrradianceHWRT_CS"
    , EShaderAccessStageFlag::COMPUTE)

enum : uint32
{
    SurfelGI_HWRTDI_MaterialFlag_HasAlbedoTexture = 1u << 0,
    SurfelGI_HWRTDI_MaterialFlag_HasNormalTexture = 1u << 1,
    SurfelGI_HWRTDI_MaterialFlag_HasRMTexture = 1u << 2,
    SurfelGI_HWRTDI_MaterialFlag_UseSRGBAlbedoTexture = 1u << 3,
    SurfelGI_HWRTDI_MaterialFlag_IsSkyMaterial = 1u << 4,
    SurfelGI_HWRTDI_MaterialFlag_UseAlphaCutout = 1u << 5,
    SurfelGI_HWRTDI_MaterialFlag_NonOpaqueGeometry = 1u << 6
};

// Global GPU resources owned by the SurfelGI system.
// The names reflect the major pipeline phases:
// - pool / page table: persistent world-space surfel storage
// - irradiance / guiding: temporal lighting state per surfel
// - worklists / candidates / winners: transient placement and maintenance data
// - active / dispatch args: compact list of surfels that need inline-ray gathering
// - hover buffers: optional debug path for "show me the rays of the surfel under the mouse"
std::shared_ptr<jBuffer> GSurfelPoolBuffer;
int32 GSurfelPoolMaxCount = 0;
std::shared_ptr<jBuffer> GSurfelIrradianceBuffer;
int32 GSurfelIrradianceCapacity = 0;
std::shared_ptr<jBuffer> GSurfelGuidingBuffer;
int32 GSurfelGuidingCapacity = 0;
int32 GSurfelPageSize = 8;
std::shared_ptr<jBuffer> GVisibleCellCounterBuffer;
int32 GVisibleCellCounterCapacity = 0;
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
std::shared_ptr<jBuffer> GSurfelGIHoverSelectionBuffer;
std::shared_ptr<jBuffer> GSurfelGIHoverRayDebugBuffer;
jSurfelClipmapCascadeRuntimeState GSurfelClipmapRuntimeStates[SURFEL_GI_CASCADE_COUNT];
bool GSurfelClipmapForceClearAll = true;
int32 GSurfelCascadeCellBase[SURFEL_GI_CASCADE_COUNT] = {};
int32 GSurfelCascadeCellCount[SURFEL_GI_CASCADE_COUNT] = {};
constexpr int32 SURFEL_GI_MAX_SURFELS_HARD_CAP = 2097152;
constexpr int32 SURFEL_GI_MAX_SLOTS_PER_CELL = 5;

int32 AlignSurfelGIMaxSurfelsToCascadeCount(int32 InMaxSurfels)
{
    constexpr int32 CascadeCount = (SURFEL_GI_CASCADE_COUNT > 0) ? SURFEL_GI_CASCADE_COUNT : 1;
    const int32 ClampedMaxSurfels = Clamp(InMaxSurfels, CascadeCount, SURFEL_GI_MAX_SURFELS_HARD_CAP);
    const int32 AlignedUp = ((ClampedMaxSurfels + CascadeCount - 1) / CascadeCount) * CascadeCount;
    if (AlignedUp <= SURFEL_GI_MAX_SURFELS_HARD_CAP)
        return AlignedUp;

    return Max(CascadeCount, (SURFEL_GI_MAX_SURFELS_HARD_CAP / CascadeCount) * CascadeCount);
}

int32 GetSurfelGICascadePartitionCapacity()
{
    return Max(1, GSurfelPoolMaxCount / Max(1, SURFEL_GI_CASCADE_COUNT));
}

float GetSurfelGICascadeCellScale(int32 CascadeIndex)
{
    float Scale = 1.0f;
    for (int32 i = 1; i <= CascadeIndex && i < SURFEL_GI_CASCADE_COUNT; ++i)
    {
        Scale *= Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[i]);
    }
    return Scale;
}

int32 GetSurfelGIEffectiveTileSize()
{
    return gOptions.UseSurfelGITileBasedSampling ? Max(1, gOptions.SurfelGITileSize) : 1;
}

int32 GetSurfelGISampleDispatchDim(int32 InExtent)
{
    const int32 TileSize = GetSurfelGIEffectiveTileSize();
    return Max(1, (InExtent + TileSize - 1) / TileSize);
}

void ResetSurfelGIRuntimeState()
{
    GSurfelPoolMaxCount = 0;
    GSurfelIrradianceCapacity = 0;
    GSurfelGuidingCapacity = 0;
    GSurfelPageSize = 8;
    GVisibleCellCounterCapacity = 0;
    GSurfelCellPageTableCapacity = 0;
    GSurfelGICandidateCapacity = 0;
    GSurfelGIWinnerCapacity = 0;
    GSurfelGIActiveIndexCapacity = 0;
    GSurfelClipmapForceClearAll = true;

    for (jSurfelClipmapCascadeRuntimeState& RuntimeState : GSurfelClipmapRuntimeStates)
        RuntimeState = {};
    for (int32& CellBase : GSurfelCascadeCellBase)
        CellBase = 0;
    for (int32& CellCount : GSurfelCascadeCellCount)
        CellCount = 0;
}

FORCEINLINE int32 PositiveModuloInt32(int32 value, int32 divisor)
{
    if (divisor <= 0)
        return 0;
    const int32 m = value % divisor;
    return (m < 0) ? (m + divisor) : m;
}

bool TryGetClientMousePosition(int32& OutMouseX, int32& OutMouseY)
{
#if defined(_WIN32)
    // Hover-ray debug is expressed in client-space pixels, so this helper converts the
    // OS cursor position into the render window's local coordinates and rejects positions
    // outside the current client rect.
    HWND WindowHandle = (HWND)g_rhi->GetWindow();
    if (!WindowHandle)
        return false;

    POINT CursorPos = {};
    if (!GetCursorPos(&CursorPos))
        return false;
    if (!ScreenToClient(WindowHandle, &CursorPos))
        return false;

    RECT ClientRect = {};
    if (!GetClientRect(WindowHandle, &ClientRect))
        return false;

    const int32 Width = (int32)std::max<LONG>(0, ClientRect.right - ClientRect.left);
    const int32 Height = (int32)std::max<LONG>(0, ClientRect.bottom - ClientRect.top);
    if (CursorPos.x < 0 || CursorPos.y < 0 || CursorPos.x >= Width || CursorPos.y >= Height)
        return false;

    OutMouseX = CursorPos.x;
    OutMouseY = CursorPos.y;
    return true;
#else
    OutMouseX = 0;
    OutMouseY = 0;
    return false;
#endif
}

bool DispatchSurfelGIHWRTDIGather(
    const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr,
    jCamera* InMainCamera,
    const std::shared_ptr<IUniformBufferBlock>& InGatherUniformBuffer)
{
    if (!InRenderFrameContextPtr || !InMainCamera || !InGatherUniformBuffer)
        return false;

    auto* RaytracingScene = InRenderFrameContextPtr->RaytracingScene;
    if (!RaytracingScene || !RaytracingScene->TLASBufferPtr || RaytracingScene->InstanceList.empty())
        return false;

    const EShaderAccessStageFlag BindingShaderStageFlag = EShaderAccessStageFlag::COMPUTE;

    std::vector<const jBuffer*> VertexAndIndexOffsetBuffers;
    std::vector<const jBuffer*> IndexBuffers;
    std::vector<const jBuffer*> RenderObjectBuffers;
    std::vector<const jBuffer*> VertexBuffers;
    std::vector<const IUniformBufferBlock*> MaterialInstanceBuffers;
    std::vector<jTextureResourceBindless::jTextureBindData> AlbedoTextures;
    std::vector<jTextureResourceBindless::jTextureBindData> NormalTextures;
    std::vector<jTextureResourceBindless::jTextureBindData> RMTextures;
    std::vector<const jSamplerStateInfo*> AlbedoSamplerStates;
    std::vector<const jSamplerStateInfo*> NormalSamplerStates;
    std::vector<const jSamplerStateInfo*> RMSamplerStates;
    std::vector<jHWRTDIPackedLight> PackedLights;
    std::vector<std::shared_ptr<IUniformBufferBlock>> RefCountMaintainer;
    std::shared_ptr<jBuffer> PackedLightBuffer;

    VertexAndIndexOffsetBuffers.reserve(RaytracingScene->InstanceList.size());
    IndexBuffers.reserve(RaytracingScene->InstanceList.size());
    RenderObjectBuffers.reserve(RaytracingScene->InstanceList.size());
    VertexBuffers.reserve(RaytracingScene->InstanceList.size());
    MaterialInstanceBuffers.reserve(RaytracingScene->InstanceList.size());
    AlbedoTextures.reserve(RaytracingScene->InstanceList.size());
    NormalTextures.reserve(RaytracingScene->InstanceList.size());
    RMTextures.reserve(RaytracingScene->InstanceList.size());
    AlbedoSamplerStates.reserve(RaytracingScene->InstanceList.size());
    NormalSamplerStates.reserve(RaytracingScene->InstanceList.size());
    RMSamplerStates.reserve(RaytracingScene->InstanceList.size());
    PackedLights.reserve(jLight::GetLights().size());

    const auto CreateOneFrameUniformBuffer = [&](jName Name, const void* Data, uint32 Size)
    {
        auto UniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(Name, jLifeTimeType::OneFrame, Size));
        UniformBuffer->UpdateBufferData(Data, Size);
        RefCountMaintainer.push_back(UniformBuffer);
        return UniformBuffer;
    };

    std::unordered_map<const jSamplerStateInfo*, uint32> AlbedoSamplerIndexMap;
    std::unordered_map<const jSamplerStateInfo*, uint32> NormalSamplerIndexMap;
    std::unordered_map<const jSamplerStateInfo*, uint32> RMSamplerIndexMap;
    AlbedoSamplerIndexMap.reserve(RaytracingScene->InstanceList.size());
    NormalSamplerIndexMap.reserve(RaytracingScene->InstanceList.size());
    RMSamplerIndexMap.reserve(RaytracingScene->InstanceList.size());

    const auto GetOrAddSamplerIndex = [](std::vector<const jSamplerStateInfo*>& InSamplerStates
        , std::unordered_map<const jSamplerStateInfo*, uint32>& InSamplerIndexMap
        , const jSamplerStateInfo* InSamplerState) -> uint32
    {
        check(InSamplerState);

        auto It = InSamplerIndexMap.find(InSamplerState);
        if (It != InSamplerIndexMap.end())
            return It->second;

        const uint32 NewIndex = (uint32)InSamplerStates.size();
        InSamplerStates.push_back(InSamplerState);
        InSamplerIndexMap.emplace(InSamplerState, NewIndex);
        return NewIndex;
    };

    for (jRenderObject* RenderObject : RaytracingScene->InstanceList)
    {
        if (!RenderObject || !RenderObject->GeometryDataPtr || !RenderObject->IsSupportRaytracing())
            return false;

        RenderObject->CreateShaderBindingInstance();

        VertexAndIndexOffsetBuffers.push_back(RenderObject->VertexAndIndexOffsetBuffer.get());
        IndexBuffers.push_back(RenderObject->GeometryDataPtr->IndexBufferPtr->GetBuffer());
        RenderObjectBuffers.push_back(RenderObject->TestUniformBuffer.get());
        VertexBuffers.push_back(RenderObject->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

        const jMaterial* Material = RenderObject->MaterialPtr ? RenderObject->MaterialPtr.get() : GDefaultMaterial.get();
        if (!Material)
            return false;

        MaterialInstanceUniform MaterialUniform;
        if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Albedo].Texture)
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_HasAlbedoTexture;
        if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Normal].Texture)
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_HasNormalTexture;
        if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Metallic].Texture)
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_HasRMTexture;
        if (Material->IsUseSRGBAlbedoTexture())
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_UseSRGBAlbedoTexture;

        const bool IsSkyMaterial = Material->IsUseSphericalMap();
        if (IsSkyMaterial)
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_IsSkyMaterial;

        const bool UseAlphaCutout = !IsSkyMaterial
            && Material->HasAlbedoTexture()
            && Material->IsRaytracingAlphaTestEnabled();
        if (UseAlphaCutout)
        {
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_UseAlphaCutout;
            MaterialUniform.MaterialFlags |= SurfelGI_HWRTDI_MaterialFlag_NonOpaqueGeometry;
        }
        MaterialUniform.AlphaCutoff = Clamp(Material->RaytracingAlphaCutoff, 0.0f, 1.0f);

        const jSamplerStateInfo* AlbedoSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Albedo);
        const jSamplerStateInfo* NormalSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Normal);
        const jSamplerStateInfo* RMSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Metallic);
        MaterialUniform.AlbedoSamplerIndex = GetOrAddSamplerIndex(AlbedoSamplerStates, AlbedoSamplerIndexMap, AlbedoSamplerState);
        MaterialUniform.NormalSamplerIndex = GetOrAddSamplerIndex(NormalSamplerStates, NormalSamplerIndexMap, NormalSamplerState);
        MaterialUniform.RMSamplerIndex = GetOrAddSamplerIndex(RMSamplerStates, RMSamplerIndexMap, RMSamplerState);

        auto MaterialUniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("SurfelGI_HWRTDI_MaterialInstance"), &MaterialUniform, sizeof(MaterialUniform));
        MaterialInstanceBuffers.push_back(MaterialUniformBuffer.get());

        AlbedoTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Albedo), nullptr, 0 });
        NormalTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Normal), nullptr, 0 });
        RMTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Metallic), nullptr, 0 });
    }

    for (jLight* Light : jLight::GetLights())
    {
        if (!Light)
            continue;

        switch (Light->Type)
        {
        case ELightType::DIRECTIONAL:
        {
            const auto* DirectionalLight = static_cast<jDirectionalLight*>(Light);
            const jDirectionalLightUniformBufferData& LightData = DirectionalLight->GetLightData();
            jHWRTDIPackedLight PackedLight;
            PackedLight.ColorAndType = Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, (float)static_cast<uint32>(ELightType::DIRECTIONAL));
            PackedLight.DirectionAndPenumbra = Vector4(LightData.Direction.x, LightData.Direction.y, LightData.Direction.z, 0.0f);
            PackedLights.push_back(PackedLight);
            break;
        }
        case ELightType::POINT:
        {
            const auto* PointLight = static_cast<jPointLight*>(Light);
            const jPointLightUniformBufferData& LightData = PointLight->GetLightData();
            jHWRTDIPackedLight PackedLight;
            PackedLight.ColorAndType = Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, (float)static_cast<uint32>(ELightType::POINT));
            PackedLight.PositionAndMaxDistance = Vector4(LightData.Position.x, LightData.Position.y, LightData.Position.z, LightData.MaxDistance);
            PackedLights.push_back(PackedLight);
            break;
        }
        case ELightType::SPOT:
        {
            const auto* SpotLight = static_cast<jSpotLight*>(Light);
            const jSpotLightUniformBufferData& LightData = SpotLight->GetLightData();
            jHWRTDIPackedLight PackedLight;
            PackedLight.ColorAndType = Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, (float)static_cast<uint32>(ELightType::SPOT));
            PackedLight.PositionAndMaxDistance = Vector4(LightData.Position.x, LightData.Position.y, LightData.Position.z, LightData.MaxDistance);
            PackedLight.DirectionAndPenumbra = Vector4(LightData.Direction.x, LightData.Direction.y, LightData.Direction.z, LightData.PenumbraRadian);
            PackedLight.UmbraAndPadding = Vector4(LightData.UmbraRadian, 0.0f, 0.0f, 0.0f);
            PackedLights.push_back(PackedLight);
            break;
        }
        default:
            break;
        }
    }
    const uint32 NumPackedLights = (uint32)PackedLights.size();
    if (PackedLights.empty())
    {
        PackedLights.push_back(jHWRTDIPackedLight());
    }
    const uint32 PackedLightCount = (uint32)PackedLights.size();
    const size_t PackedLightBufferSize = sizeof(jHWRTDIPackedLight) * PackedLightCount;
    PackedLightBuffer = g_rhi->CreateStructuredBuffer(PackedLightBufferSize, 0, sizeof(jHWRTDIPackedLight), EBufferCreateFlag::UAV
        , EResourceLayout::GENERAL, PackedLights.data(), PackedLightBufferSize, jNameStatic("SurfelGI_HWRTDI_PackedLightBuffer"));
    check(PackedLightBuffer);

    jSurfelGIHWRTDISceneConstantBuffer SceneCB;
    SceneCB.ProjectionToWorld = InMainCamera->GetInverseViewProjectionMatrix();
    SceneCB.CameraPosition = InMainCamera->Pos;
    SceneCB.NormalBias = (gOptions.HWRTNormalBias > 0.0f) ? gOptions.HWRTNormalBias : 0.0f;
    SceneCB.ShadowRayStartOffset = (gOptions.HWRTShadowRayStartOffset > 0.0f) ? gOptions.HWRTShadowRayStartOffset : 0.0f;
    SceneCB.RenderWidth = (uint32)InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Width;
    SceneCB.RenderHeight = (uint32)InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Height;
    SceneCB.DebugViewMode = 0u;
    SceneCB.DebugLineWidth = Max(gOptions.HWRTDebugLineWidth, 0.0005f);
    SceneCB.DebugUVScale = Max(gOptions.HWRTDebugUVScale, 1.0f);
    SceneCB.DebugPrimitiveIDScale = Max(gOptions.HWRTDebugPrimitiveIDScale, 0.1f);
    SceneCB.ForceMipLevel0 = gOptions.HWRTForceMipLevel0 ? 1u : 0u;
    SceneCB.NumLights = NumPackedLights;

    auto SceneUniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("SurfelGI_HWRTDI_SceneData"), &SceneCB, sizeof(SceneCB));

    jShaderBindingArray GlobalShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllocator;
    GlobalShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jBufferResource>(RaytracingScene->TLASBufferPtr.get()), true));
    GlobalShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_UAV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), nullptr)));
    GlobalShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::UNIFORMBUFFER, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jUniformBufferResource>(SceneUniformBuffer.get()), true));

    const jSamplerStateInfo* HWRTDISamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();
    GlobalShaderBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::SAMPLER, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jSamplerResource>(HWRTDISamplerState)));

    jTexture* EnvTexture = jSceneRenderTarget::CubeEnvMap2 ? jSceneRenderTarget::CubeEnvMap2 : GWhiteCubeTexture.get();
    GlobalShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::TEXTURE_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jTextureResource>(EnvTexture, nullptr)));
    GlobalShaderBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::BUFFER_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jBufferResource>(PackedLightBuffer.get())));
    auto GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(GlobalShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

    jSurfelGIHWRTDIBindlessParameters BindlessParameters;
    BindlessParameters.VertexIndexOffsetArray.Buffers = VertexAndIndexOffsetBuffers;
    BindlessParameters.IndexBindlessArray.Buffers = IndexBuffers;
    BindlessParameters.RenderObjParamArray.Buffers = RenderObjectBuffers;
    BindlessParameters.VerticesBindlessArray.Buffers = VertexBuffers;
    BindlessParameters.MaterialInstanceArray.Buffers = MaterialInstanceBuffers;
    BindlessParameters.AlbedoTextureArray.Textures = AlbedoTextures;
    BindlessParameters.NormalTextureArray.Textures = NormalTextures;
    BindlessParameters.RMTextureArray.Textures = RMTextures;
    BindlessParameters.AlbedoSamplerArray.SamplerStates = AlbedoSamplerStates;
    BindlessParameters.NormalSamplerArray.SamplerStates = NormalSamplerStates;
    BindlessParameters.RMSamplerArray.SamplerStates = RMSamplerStates;
    std::vector<std::shared_ptr<jShaderBindingInstance>> BindlessShaderBindingInstances =
        jShaderBindlessSet::CreateShaderBindingInstances(BindlessParameters, BindingShaderStageFlag, jShaderBindingInstanceType::SingleFrame);

    jSurfelGIHWRTDIGatherParameters SurfelGatherParameters;
    SurfelGatherParameters.SurfelGIActiveSurfelIndexBuffer.Buffer = GSurfelGIActiveIndexBuffer.get();
    SurfelGatherParameters.SurfelGIActiveSurfelCounterBuffer.Buffer = GSurfelGIActiveCounterBuffer.get();
    SurfelGatherParameters.SurfelGIPool.Buffer = GSurfelPoolBuffer.get();
    SurfelGatherParameters.SurfelGIIrradianceBuffer.Buffer = GSurfelIrradianceBuffer.get();
    SurfelGatherParameters.SurfelGIGuidingBuffer.Buffer = GSurfelGuidingBuffer.get();
    SurfelGatherParameters.g_surfelGatherCB.Buffer = std::shared_ptr<IUniformBufferBlock>(InGatherUniformBuffer);
    SurfelGatherParameters.SurfelGIHoverSelectionBuffer.Buffer = GSurfelGIHoverSelectionBuffer.get();
    SurfelGatherParameters.SurfelGIHoverRayDebugBuffer.Buffer = GSurfelGIHoverRayDebugBuffer.get();
    auto SurfelGatherBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
        SurfelGatherParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

    jShaderBindingInstanceGroup ShaderBindingGroup;
    ShaderBindingGroup.Add(GlobalShaderBindingInstance);
    ShaderBindingGroup.Add(SurfelGatherBindingInstance);
    ShaderBindingGroup.Add(BindlessShaderBindingInstances);

    jShaderSurfelGIGatherIrradianceHWRTCS::ShaderPermutation GatherPermutation;
    GatherPermutation.SetIndex<jShaderSurfelGIGatherIrradianceHWRTCS::USE_SURFEL_GI>(1);
    GatherPermutation.SetIndex<jShaderSurfelGIGatherIrradianceHWRTCS::USE_BINDLESS_RESOURCE>(1);
    jShader* IrradianceGatherShader = jShaderSurfelGIGatherIrradianceHWRTCS::CreateShader(GatherPermutation);
    jPipelineStateInfo* IrradianceGatherPSO = g_rhi->CreateComputePipelineStateInfo(IrradianceGatherShader, ShaderBindingGroup.GetLayoutArray(), {});
    IrradianceGatherPSO->Bind(InRenderFrameContextPtr);

    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), PackedLightBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceGatherPSO, ShaderBindingGroup.GetCombiner(), 0);
    const jName RHIName = g_rhi->GetRHIName();
    const bool SupportsComputeIndirectDispatch = (RHIName == jNameStatic("Vulkan")) || (RHIName == jNameStatic("DirectX12"));
    if (SupportsComputeIndirectDispatch)
    {
        g_rhi->DispatchComputeIndirect(InRenderFrameContextPtr, GSurfelGIInlineRayDispatchArgsBuffer.get(), 0);
    }
    else
    {
        const int32 FallbackGatherGroupX = (Max(1, GSurfelGIActiveIndexCapacity) + 63) / 64;
        g_rhi->DispatchCompute(InRenderFrameContextPtr, Max(1, FallbackGatherGroupX), 1, 1);
    }

    return true;
}

void EnsureSurfelGIResources(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
    int32 MaxSurfelSlotsPerCell = Clamp(gOptions.SurfelGIReservoirPerCellLimit, 1, SURFEL_GI_MAX_SLOTS_PER_CELL);
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        MaxSurfelSlotsPerCell = Max(MaxSurfelSlotsPerCell, Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL));
    }
    GSurfelPageSize = Max(1, MaxSurfelSlotsPerCell);

    int32 TotalCellCount = 0;
    int32 RunningCellBase = 0;
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        const int32 DimX = Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
        const int32 DimY = Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
        const int32 DimZ = Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
        const int32 CascadeCellCount = DimX * DimY * DimZ;
        GSurfelCascadeCellBase[cascade] = RunningCellBase;
        GSurfelCascadeCellCount[cascade] = Max(1, CascadeCellCount);
        RunningCellBase += GSurfelCascadeCellCount[cascade];
        TotalCellCount += CascadeCellCount;
    }
    TotalCellCount = Max(1, TotalCellCount);
    const int32 RequestedMaxSurfels = Clamp(Max(1024, gOptions.SurfelGIMaxSurfels), 1024, SURFEL_GI_MAX_SURFELS_HARD_CAP);
    const int32 MinRequiredForTargetSlots = Min(TotalCellCount * Max(1, GSurfelPageSize), SURFEL_GI_MAX_SURFELS_HARD_CAP);
    const int32 MaxSurfels = AlignSurfelGIMaxSurfelsToCascadeCount(Max(RequestedMaxSurfels, MinRequiredForTargetSlots));
    const int32 MaxSlotsPerCellByBudget = Max(1, MaxSurfels / Max(1, TotalCellCount));
    GSurfelPageSize = Clamp(GSurfelPageSize, 1, MaxSlotsPerCellByBudget);
    bool RecreatedPrimaryStorage = false;

    // Surfel pool and associated buffers. The pool is a big structured buffer of jSurfelGPU, which is the GPU-friendly representation of surfels.
    if (!GSurfelPoolBuffer || GSurfelPoolMaxCount != MaxSurfels)
    {
        GSurfelPoolMaxCount = MaxSurfels;
        std::vector<jSurfelGPU> InitialPool;
        InitialPool.resize(MaxSurfels);

        GSurfelPoolBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGPU) * MaxSurfels,
            0,
            sizeof(jSurfelGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialPool.data(),
            sizeof(jSurfelGPU) * MaxSurfels,
            jNameStatic("SurfelGI_Pool"));
        RecreatedPrimaryStorage = true;
    }

    // The irradiance buffer is a structured buffer of jSurfelIrradianceGPU, which holds the current lighting state of each surfel.
    // This is read and updated by the inline ray tracing pass, and read by the shading pass.
    if (!GSurfelIrradianceBuffer || GSurfelIrradianceCapacity != MaxSurfels)
    {
        GSurfelIrradianceCapacity = MaxSurfels;
        std::vector<jSurfelIrradianceGPU> InitialIrradiance;
        InitialIrradiance.resize(MaxSurfels);

        GSurfelIrradianceBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelIrradianceGPU) * MaxSurfels,
            0,
            sizeof(jSurfelIrradianceGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialIrradiance.data(),
            sizeof(jSurfelIrradianceGPU) * MaxSurfels,
            jNameStatic("SurfelGI_Irradiance"));
        RecreatedPrimaryStorage = true;
    }

    // The guiding buffer is a flat float buffer storing one compact guiding distribution per surfel.
    // It is updated by the inline ray gather pass and read by later gathers to bias ray generation toward useful directions.
    if (!GSurfelGuidingBuffer || GSurfelGuidingCapacity != MaxSurfels)
    {
        GSurfelGuidingCapacity = MaxSurfels;
        const int32 GuidingFloatCount = MaxSurfels * SURFEL_GI_GUIDE_TOTAL_FLOATS;

        GSurfelGuidingBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(float) * GuidingFloatCount,
            0,
            sizeof(float),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            nullptr,
            0,
            jNameStatic("SurfelGI_Guiding"));
        RecreatedPrimaryStorage = true;
    }

    // The active index buffer is a structured buffer of uint32, which holds the indices of currently active surfels in the pool.
    // The active-compaction pass writes this buffer, and the inline ray gather pass reads it.
    if (!GSurfelGIActiveIndexBuffer || GSurfelGIActiveIndexCapacity != MaxSurfels)
    {
        GSurfelGIActiveIndexCapacity = MaxSurfels;
        std::vector<uint32> InitialActiveIndices;
        InitialActiveIndices.resize((size_t)MaxSurfels, 0u);

        GSurfelGIActiveIndexBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * MaxSurfels,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialActiveIndices.data(),
            sizeof(uint32) * MaxSurfels,
            jNameStatic("SurfelGI_ActiveIndex"));
    }

    // The active counter buffer is a structured buffer of uint32, which holds the count of currently active surfels.
    // This is used for indirect dispatch and to avoid dispatching more threads than necessary.
    if (!GSurfelGIActiveCounterBuffer)
    {
        uint32 InitialActiveCounter = 0;
        GSurfelGIActiveCounterBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32),
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            &InitialActiveCounter,
            sizeof(uint32),
            jNameStatic("SurfelGI_ActiveCounter"));
    }

    // The inline ray dispatch args buffer is a structured buffer of jSurfelInlineRayDispatchArgsGPU
    // , which holds the arguments for indirect dispatch of the inline ray tracing pass.
    if (!GSurfelGIInlineRayDispatchArgsBuffer)
    {
        jSurfelInlineRayDispatchArgsGPU InitialDispatchArgs = {};
        GSurfelGIInlineRayDispatchArgsBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelInlineRayDispatchArgsGPU),
            0,
            sizeof(jSurfelInlineRayDispatchArgsGPU),
            EBufferCreateFlag::UAV | EBufferCreateFlag::IndirectCommand,
            EResourceLayout::UAV,
            &InitialDispatchArgs,
            sizeof(jSurfelInlineRayDispatchArgsGPU),
            jNameStatic("SurfelGI_InlineRayDispatchArgs"));
    }

    // The hover selection buffer is a structured buffer of jSurfelGIHoverSelectionGPU
    // , which holds the result of the hover-ray intersection for debugging purposes.
    if (!GSurfelGIHoverSelectionBuffer)
    {
        jSurfelGIHoverSelectionGPU InitialSelection = {};
        GSurfelGIHoverSelectionBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGIHoverSelectionGPU),
            0,
            sizeof(jSurfelGIHoverSelectionGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            &InitialSelection,
            sizeof(jSurfelGIHoverSelectionGPU),
            jNameStatic("SurfelGI_HoverSelection"));
    }

    // The hover ray debug buffer is a structured buffer of jSurfelGIHoverRayDebugGPU
    // , which holds the rays and intersection results for the hover-ray debug visualization.
    if (!GSurfelGIHoverRayDebugBuffer)
    {
        jSurfelGIHoverRayDebugGPU InitialHoverDebug = {};
        GSurfelGIHoverRayDebugBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGIHoverRayDebugGPU),
            0,
            sizeof(jSurfelGIHoverRayDebugGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            &InitialHoverDebug,
            sizeof(jSurfelGIHoverRayDebugGPU),
            jNameStatic("SurfelGI_HoverRayDebug"));
    }

    // The cell page table buffer is a structured buffer of uint32, which holds the mapping from cell indices to surfel pool pages.
    if (!GSurfelCellPageTableBuffer || GSurfelCellPageTableCapacity != TotalCellCount)
    {
        GSurfelCellPageTableCapacity = TotalCellCount;
        std::vector<uint32> InitialEntries;
        InitialEntries.resize((size_t)TotalCellCount, 0u);

        GSurfelCellPageTableBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialEntries.data(),
            sizeof(uint32) * TotalCellCount,
            jNameStatic("SurfelGI_CellPageTable"));
        RecreatedPrimaryStorage = true;
    }

    // The debug render targets for surfel GI. These are used to visualize the surfel distribution and other intermediate data for debugging purposes.
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

    // The attempt render target is a debug mask written by GatherCandidates to show pixels that attempted surfel spawning.
    // It is sampled only by the SurfelGI visualization pass when spawn-attempt debug is enabled.
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

    // CandidateBuffer stores surfel placement candidates generated by the GatherCandidates pass.
    // A later winner-selection pass keeps at most one highest-priority candidate per cell.
    const int32 SampleDispatchWidth = GetSurfelGISampleDispatchDim(Width);
    const int32 SampleDispatchHeight = GetSurfelGISampleDispatchDim(Height);
    const int32 CandidateCapacity = Max(1, SampleDispatchWidth * SampleDispatchHeight);
    if (!GSurfelGICandidateBuffer || GSurfelGICandidateCapacity != CandidateCapacity)
    {
        GSurfelGICandidateCapacity = CandidateCapacity;
        std::vector<jSurfelCandidateGPU> InitialCandidates;
        InitialCandidates.resize((size_t)CandidateCapacity);
        GSurfelGICandidateBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelCandidateGPU) * CandidateCapacity,
            0,
            sizeof(jSurfelCandidateGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialCandidates.data(),
            sizeof(jSurfelCandidateGPU) * CandidateCapacity,
            jNameStatic("SurfelGI_ReservoirCandidates"));
    }

    // The winner buffers store the highest-priority placement candidate per cell.
    // The lock buffer protects the score/index pair from concurrent shader updates.
    if (!GSurfelGIWinnerScoreBuffer || !GSurfelGIWinnerIndexBuffer || !GSurfelGIWinnerLockBuffer || GSurfelGIWinnerCapacity != TotalCellCount)
    {
        GSurfelGIWinnerCapacity = TotalCellCount;

        // Initialize winner score buffer with 0, so that any candidate with a positive score can become the winner.
        std::vector<uint32> InitialWinnerScore;
        InitialWinnerScore.resize((size_t)TotalCellCount, 0u);
        GSurfelGIWinnerScoreBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialWinnerScore.data(),
            sizeof(uint32) * TotalCellCount,
            jNameStatic("SurfelGI_ReservoirWinnerScore"));

        // Initialize winner index buffer with 0xffffffff, which is an invalid index indicating no winner yet.
        std::vector<uint32> InitialWinnerIndex;
        InitialWinnerIndex.resize((size_t)TotalCellCount, 0xffffffffu);
        GSurfelGIWinnerIndexBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialWinnerIndex.data(),
            sizeof(uint32) * TotalCellCount,
            jNameStatic("SurfelGI_ReservoirWinnerIndex"));

        // Initialize winner lock buffer with 0, which means unlocked.
        // Atomic operations acquire one lock per cell while updating the winner score/index pair.
        std::vector<uint32> InitialWinnerLock;
        InitialWinnerLock.resize((size_t)TotalCellCount, 0u);
        GSurfelGIWinnerLockBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * TotalCellCount,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialWinnerLock.data(),
            sizeof(uint32) * TotalCellCount,
            jNameStatic("SurfelGI_ReservoirWinnerLock"));
    }

    // The stats buffer is used to collect various statistics about the SurfelGI process for debugging and analysis purposes.
    if (!GSurfelGIStatsBuffer)
    {
        jSurfelGIStatsGPU InitialStats = {};
        GSurfelGIStatsBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGIStatsGPU),
            0,
            sizeof(jSurfelGIStatsGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            &InitialStats,
            sizeof(jSurfelGIStatsGPU),
            jNameStatic("SurfelGI_ReservoirStats"));
    }

    const int32 VisibleCellCapacity = Max(1, TotalCellCount);
    // The visible cell counter buffer is indexed by clipmap cell linear index.
    // Each element records how many sampled screen tiles hit the cell this frame.
    if (!GVisibleCellCounterBuffer || GVisibleCellCounterCapacity != VisibleCellCapacity)
    {
        GVisibleCellCounterCapacity = VisibleCellCapacity;
        std::vector<uint32> InitialCounters;
        InitialCounters.resize((size_t)VisibleCellCapacity, 0u);
        GVisibleCellCounterBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(uint32) * VisibleCellCapacity,
            0,
            sizeof(uint32),
            EBufferCreateFlag::UAV,
            EResourceLayout::UAV,
            InitialCounters.data(),
            sizeof(uint32) * VisibleCellCapacity,
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
} // namespace

void ReleaseSurfelGIResources()
{
    GSurfelPoolBuffer.reset();
    GSurfelIrradianceBuffer.reset();
    GSurfelGuidingBuffer.reset();
    GVisibleCellCounterBuffer.reset();
    GSurfelCellPageTableBuffer.reset();
    GSurfelGICandidateBuffer.reset();
    GSurfelGIWinnerScoreBuffer.reset();
    GSurfelGIWinnerIndexBuffer.reset();
    GSurfelGIWinnerLockBuffer.reset();
    GSurfelGIStatsBuffer.reset();
    GSurfelGIActiveIndexBuffer.reset();
    GSurfelGIActiveCounterBuffer.reset();
    GSurfelGIInlineRayDispatchArgsBuffer.reset();
    GSurfelGIHoverSelectionBuffer.reset();
    GSurfelGIHoverRayDebugBuffer.reset();
    ResetSurfelGIRuntimeState();
}

void jRenderer::SurfelGIPass()
{
    /*
     * Main SurfelGI simulation / update pass.
     *
     * High-level order inside this function:
     * - make sure all persistent resources exist and match the current settings
     * - pack the clipmap state into uniform buffers
     * - clear / collect / refresh visible cells
     * - clean up stale surfels and build placement candidates
     * - place winning candidates into the surfel pool
     * - build the active surfel list for inline-ray gathering
     * - optionally select the hovered surfel for debug visualization
     * - gather irradiance with inline rays and update MSME history
     * - run the debug visualization pass
     *
     * This is the "surfel-space" half of the system. It stops at producing surfel irradiance.
     * The later resolve/apply passes turn that surfel-space data into a screen-space result.
     */
    if (!gOptions.UseSurfelGI)
        return;

    SCOPE_CPU_PROFILE(SurfelGIPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGIPass);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGIPass", Vector4(0.25f, 0.85f, 0.4f, 1.0f));

    EnsureSurfelGIResources(RenderFrameContextPtr);
    if (!GSurfelPoolBuffer || !jSceneRenderTarget::SurfelGI_Debug_RT || !jSceneRenderTarget::SurfelGI_Attempt_RT
        || !GVisibleCellCounterBuffer || !GSurfelCellPageTableBuffer
        || !GSurfelGICandidateBuffer || !GSurfelGIWinnerScoreBuffer || !GSurfelGIWinnerIndexBuffer || !GSurfelGIWinnerLockBuffer
        || !GSurfelGIStatsBuffer || !GSurfelIrradianceBuffer || !GSurfelGuidingBuffer
        || !GSurfelGIActiveIndexBuffer || !GSurfelGIActiveCounterBuffer || !GSurfelGIInlineRayDispatchArgsBuffer)
        return;
    auto MainCamera = jCamera::GetMainCamera();
    if (!MainCamera)
        return;
    const int32 Width = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width;
    const int32 Height = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height;

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
    UniformData.ScreenSize = Vector2((float)Width, (float)Height);
    UniformData.NormalThreshold = gOptions.SurfelGINormalThreshold;
    UniformData.DepthEdgeScale = 0.75f;
    UniformData.NormalEdgeScale = 1.25f;
    UniformData.PreferCellCenterForFirstPlacement = gOptions.UseSurfelGIPreferCellCenterForFirstPlacement ? 1 : 0;
    UniformData.MinRadius = 15.0f;
    UniformData.MaxDistance = gOptions.SSGIMaxDistance;
    UniformData.FrameNumber = g_rhi->GetCurrentFrameNumber();
    UniformData.TileSize = GetSurfelGIEffectiveTileSize();
    UniformData.MaxSurfels = GSurfelPoolMaxCount;
    UniformData.CascadePartitionCapacity = GetSurfelGICascadePartitionCapacity();
    UniformData.SurfelPageSize = Max(1, GSurfelPageSize);
    UniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
    UniformData.SpawnBudget = Max(1, gOptions.SurfelGISpawnBudgetPerFrame);
    UniformData.TTLInFrames = Max(1, gOptions.SurfelGITTLInFrames);
    UniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
    UniformData.CascadeCellScalePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeStartDistancePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeRadiusScalePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeClipmapGridDimXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeClipmapGridDimYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeClipmapGridDimZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.SurfelsPerCellPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeOriginCellXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeOriginCellYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeOriginCellZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeRingOffsetXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeRingOffsetYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeRingOffsetZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeCellBasePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeCellCountPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeDeltaCellXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeDeltaCellYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeDeltaCellZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.CascadeClearAllPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    UniformData.FaceMarginRadiusScale = Clamp(gOptions.SurfelGIFaceMarginRadiusScale, 0.0f, 2.0f);
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        UniformData.CascadeCellScalePacked.v[cascade] = GetSurfelGICascadeCellScale(cascade);
        UniformData.CascadeStartDistancePacked.v[cascade] = CascadeStartDistanceSanitized[cascade];
        UniformData.CascadeRadiusScalePacked.v[cascade] = (cascade == 0) ? 1.0f : Max(0.05f, gOptions.SurfelGICascadeRadiusScale[cascade]);
    }
    UniformData.OutOfViewKeepFrames = Max(1, gOptions.SurfelGIOutOfViewKeepFrames);
    UniformData.RadiusScale = Max(0.05f, gOptions.SurfelGIRadiusScale);
    const bool ForceClearAllThisFrame = GSurfelClipmapForceClearAll;
    bool NeedClipmapCellClear = ForceClearAllThisFrame;
    int32 RunningCascadeCellBase = 0;
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        const int32 DimX = Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
        const int32 DimY = Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
        const int32 DimZ = Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
        const int32 SurfelPerCell = Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL);

        const int32 CascadeCellCount = Max(1, DimX * DimY * DimZ);
        GSurfelCascadeCellBase[cascade] = RunningCascadeCellBase;
        GSurfelCascadeCellCount[cascade] = CascadeCellCount;
        RunningCascadeCellBase += CascadeCellCount;

        const float CellSize = Max(0.1f, UniformData.GridCellSize) * GetSurfelGICascadeCellScale(cascade);
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

        UniformData.CascadeClipmapGridDimXPacked.v[cascade] = (float)DimX;
        UniformData.CascadeClipmapGridDimYPacked.v[cascade] = (float)DimY;
        UniformData.CascadeClipmapGridDimZPacked.v[cascade] = (float)DimZ;
        UniformData.SurfelsPerCellPacked.v[cascade] = (float)SurfelPerCell;
        UniformData.CascadeOriginCellXPacked.v[cascade] = (float)RuntimeState.OriginX;
        UniformData.CascadeOriginCellYPacked.v[cascade] = (float)RuntimeState.OriginY;
        UniformData.CascadeOriginCellZPacked.v[cascade] = (float)RuntimeState.OriginZ;
        UniformData.CascadeRingOffsetXPacked.v[cascade] = (float)RuntimeState.RingOffsetX;
        UniformData.CascadeRingOffsetYPacked.v[cascade] = (float)RuntimeState.RingOffsetY;
        UniformData.CascadeRingOffsetZPacked.v[cascade] = (float)RuntimeState.RingOffsetZ;
        UniformData.CascadeCellBasePacked.v[cascade] = (float)GSurfelCascadeCellBase[cascade];
        UniformData.CascadeCellCountPacked.v[cascade] = (float)GSurfelCascadeCellCount[cascade];
        UniformData.CascadeDeltaCellXPacked.v[cascade] = (float)DeltaX;
        UniformData.CascadeDeltaCellYPacked.v[cascade] = (float)DeltaY;
        UniformData.CascadeDeltaCellZPacked.v[cascade] = (float)DeltaZ;
        UniformData.CascadeClearAllPacked.v[cascade] = CascadeClearAll ? 1.0f : 0.0f;
    }
    GSurfelClipmapForceClearAll = false;

    auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
        g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformData)));
    OneFrameUniformBuffer->UpdateBufferData(&UniformData, sizeof(UniformData));

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get(), EResourceLayout::UAV);
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

    const int32 RTSize_GroupX = (Width + 7) / 8;
    const int32 RTSize_GroupY = (Height + 7) / 8;
    const int32 TileDispatchWidth = GetSurfelGISampleDispatchDim(Width);
    const int32 TileDispatchHeight = GetSurfelGISampleDispatchDim(Height);
    const int32 TileDispatchGroupX = (TileDispatchWidth + 7) / 8;
    const int32 TileDispatchGroupY = (TileDispatchHeight + 7) / 8;

    if (NeedClipmapCellClear)
    {
        const int32 ClipmapClearGroupX = (Max(1, GSurfelCellPageTableCapacity) + 63) / 64;

        jSurfelGIClearClipmapCellsCSParameters ClipmapClearParameters;
        ClipmapClearParameters.SurfelPool.Buffer = GSurfelPoolBuffer.get();
        ClipmapClearParameters.CellSurfelCount.Buffer = GSurfelCellPageTableBuffer.get();
        ClipmapClearParameters.SurfelIrradianceBuffer.Buffer = GSurfelIrradianceBuffer.get();
        ClipmapClearParameters.SurfelGuidingBuffer.Buffer = GSurfelGuidingBuffer.get();
        ClipmapClearParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Clear Clipmap Cells", Vector4(0.8f, 0.6f, 0.2f, 1.0f));
        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("SurfelGIClearClipmapCells_CS")
            , jNameStatic("Resource/Shaders/hlsl/SurfelGIClearClipmapCells_cs.hlsl")
            , ClipmapClearParameters
            , Max(1, ClipmapClearGroupX), 1, 1);

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get());
    }

    {
        const int32 ClearVisibleCellCounterGroupX = (Max(1, GVisibleCellCounterCapacity) + 63) / 64;

        jSurfelGIClearVisibleCellCounterCSParameters ClearParameters;
        ClearParameters.VisibleCellCounterBuffer.Buffer = GVisibleCellCounterBuffer.get();
        ClearParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch ClearVisibleCellCounter", Vector4(0.55f, 0.55f, 0.95f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchClearVisibleCellCounter);
        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("SurfelGIClearVisibleCellCounter_CS")
            , jNameStatic("Resource/Shaders/hlsl/SurfelGIClearVisibleCellCounter_cs.hlsl")
            , ClearParameters
            , Max(1, ClearVisibleCellCounterGroupX), 1, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get());

    {
        jSurfelGIVisibleCellCollectCSParameters CollectParameters;
        CollectParameters.DepthTexture.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture();
        CollectParameters.DepthTexture.SamplerState = SamplerState;
        CollectParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
        CollectParameters.VisibleCellCounterBuffer.Buffer = GVisibleCellCounterBuffer.get();

        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch CollectVisibleCells", Vector4(0.25f, 0.65f, 0.95f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchCollectVisibleCells);
        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("SurfelGIVisibleCellCollect_CS")
            , jNameStatic("Resource/Shaders/hlsl/SurfelGIVisibleCellCollect_cs.hlsl")
            , CollectParameters
            , TileDispatchGroupX, TileDispatchGroupY, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get());
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

    {
        const int32 RefreshGroupX = (GVisibleCellCounterCapacity + 63) / 64;

        jSurfelGIRefreshVisibleCellSurfelsCSParameters RefreshParameters;
        RefreshParameters.VisibleCellCounterBuffer.Buffer = GVisibleCellCounterBuffer.get();
        RefreshParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
        RefreshParameters.SurfelPool.Buffer = GSurfelPoolBuffer.get();
        RefreshParameters.SurfelCellPageTable.Buffer = GSurfelCellPageTableBuffer.get();

        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch RefreshVisibleCellSurfels", Vector4(0.75f, 0.8f, 0.25f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchRefreshVisibleCellSurfels);
        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("SurfelGIRefreshVisibleCellSurfels_CS")
            , jNameStatic("Resource/Shaders/hlsl/SurfelGIRefreshVisibleCellSurfels_cs.hlsl")
            , RefreshParameters
            , Max(1, RefreshGroupX), 1, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GVisibleCellCounterBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::UAV);

    {
        const int32 CleanupGroupX = (GSurfelPoolMaxCount + 63) / 64;

        jSurfelGICleanupCSParameters CleanupParameters;
        CleanupParameters.SurfelPool.Buffer = GSurfelPoolBuffer.get();
        CleanupParameters.SurfelIrradianceBuffer.Buffer = GSurfelIrradianceBuffer.get();
        CleanupParameters.SurfelGuidingBuffer.Buffer = GSurfelGuidingBuffer.get();
        CleanupParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;

        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch Cleanup", Vector4(0.8f, 0.35f, 0.35f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchCleanup);
        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("SurfelGICleanup_CS")
            , jNameStatic("Resource/Shaders/hlsl/SurfelGICleanup_cs.hlsl")
            , CleanupParameters
            , Max(1, CleanupGroupX), 1, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get());
    {
        jSurfelGIReservoirClearUniform ClearUniformData;
        ClearUniformData.CandidateCapacity = (uint32)Max(1, GSurfelGICandidateCapacity);
        ClearUniformData.PageCapacity = (uint32)Max(1, GSurfelCellPageTableCapacity);

        {
            auto ClearUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
                g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIReservoirClearUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ClearUniformData)));
            ClearUniformBuffer->UpdateBufferData(&ClearUniformData, sizeof(ClearUniformData));

            jSurfelGIClearCandidatesCSParameters ReservoirClearParameters;
            ReservoirClearParameters.CandidateBuffer.Buffer = GSurfelGICandidateBuffer.get();
            ReservoirClearParameters.WinnerScoreBuffer.Buffer = GSurfelGIWinnerScoreBuffer.get();
            ReservoirClearParameters.WinnerIndexBuffer.Buffer = GSurfelGIWinnerIndexBuffer.get();
            ReservoirClearParameters.WinnerLockBuffer.Buffer = GSurfelGIWinnerLockBuffer.get();
            ReservoirClearParameters.ClearParam.Buffer = ClearUniformBuffer;

            const int32 ClearGroupX = (Max(GSurfelGICandidateCapacity, GSurfelCellPageTableCapacity) + 63) / 64;
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir ClearCandidates", Vector4(0.55f, 0.95f, 0.55f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirClearCandidates);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIClearCandidates_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIClearCandidates_cs.hlsl")
                , ReservoirClearParameters
                , Max(1, ClearGroupX), 1, 1);
        }

        {
            jSurfelGIClearStatsCSParameters ReservoirClearStatsParameters;
            ReservoirClearStatsParameters.StatsBuffer.Buffer = GSurfelGIStatsBuffer.get();

            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir ClearStats", Vector4(0.55f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirClearStats);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIClearStats_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIClearStats_cs.hlsl")
                , ReservoirClearStatsParameters
                , 1, 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGICandidateBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerScoreBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerIndexBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIWinnerLockBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIStatsBuffer.get());

        {
            jSurfelGIGatherCandidatesCSParameters ReservoirGatherParameters;
            ReservoirGatherParameters.DepthTexture.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture();
            ReservoirGatherParameters.DepthTexture.SamplerState = SamplerState;
            ReservoirGatherParameters.GBuffer0.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture();
            ReservoirGatherParameters.GBuffer0.SamplerState = SamplerState;
            ReservoirGatherParameters.GBuffer1.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture();
            ReservoirGatherParameters.GBuffer1.SamplerState = SamplerState;
            ReservoirGatherParameters.LinearDepthTexture.Texture = RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture();
            ReservoirGatherParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
            ReservoirGatherParameters.SurfelPool.Buffer = GSurfelPoolBuffer.get();
            ReservoirGatherParameters.DebugOutput.Texture = jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture();
            ReservoirGatherParameters.AttemptOutput.Texture = jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture();
            ReservoirGatherParameters.SurfelCellPageTable.Buffer = GSurfelCellPageTableBuffer.get();
            ReservoirGatherParameters.SurfelGIStatsBuffer.Buffer = GSurfelGIStatsBuffer.get();
            ReservoirGatherParameters.CandidateBuffer.Buffer = GSurfelGICandidateBuffer.get();
            ReservoirGatherParameters.WinnerScoreBuffer.Buffer = GSurfelGIWinnerScoreBuffer.get();
            ReservoirGatherParameters.WinnerIndexBuffer.Buffer = GSurfelGIWinnerIndexBuffer.get();
            ReservoirGatherParameters.WinnerLockBuffer.Buffer = GSurfelGIWinnerLockBuffer.get();

            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir GatherCandidates", Vector4(0.25f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirGatherCandidates);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIGatherCandidates_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIGatherCandidates_cs.hlsl")
                , ReservoirGatherParameters
                , RTSize_GroupX, RTSize_GroupY, 1);
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
            jSurfelGIPlaceCandidatesCSParameters ReservoirPlaceParameters;
            ReservoirPlaceParameters.CandidateBuffer.Buffer = GSurfelGICandidateBuffer.get();
            ReservoirPlaceParameters.WinnerScoreBuffer.Buffer = GSurfelGIWinnerScoreBuffer.get();
            ReservoirPlaceParameters.WinnerIndexBuffer.Buffer = GSurfelGIWinnerIndexBuffer.get();
            ReservoirPlaceParameters.SurfelCellPageTable.Buffer = GSurfelCellPageTableBuffer.get();
            ReservoirPlaceParameters.SurfelPool.Buffer = GSurfelPoolBuffer.get();
            ReservoirPlaceParameters.SurfelGIStatsBuffer.Buffer = GSurfelGIStatsBuffer.get();
            ReservoirPlaceParameters.ComputeCommon.Buffer = OneFrameUniformBuffer;
            ReservoirPlaceParameters.SurfelIrradianceBuffer.Buffer = GSurfelIrradianceBuffer.get();
            ReservoirPlaceParameters.SurfelGuidingBuffer.Buffer = GSurfelGuidingBuffer.get();

            const int32 PlaceGroupX = (GSurfelCellPageTableCapacity + 63) / 64;
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Reservoir PlaceWinners", Vector4(0.2f, 0.9f, 0.35f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchReservoirPlaceCandidates);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIPlaceCandidates_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIPlaceCandidates_cs.hlsl")
                , ReservoirPlaceParameters
                , Max(1, PlaceGroupX), 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIStatsBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get());
    }

    const bool CanGatherIrradianceInlineRay = gOptions.SurfelGIInlineRayEnable
        && gOptions.UseRaytracing
        && GSupportInlineRaytracing
        && RenderFrameContextPtr->RaytracingScene
        && RenderFrameContextPtr->RaytracingScene->IsValid()
        && RenderFrameContextPtr->RaytracingScene->TLASBufferPtr;
    if (CanGatherIrradianceInlineRay)
    {
        static_assert(sizeof(jSurfelGIActiveCompactUniformBuffer) == 16, "jSurfelGIActiveCompactUniformBuffer must be 16-byte aligned");

        jSurfelGIActiveCompactUniformBuffer ActiveCompactUniformData;
        ActiveCompactUniformData.MaxSurfels = (uint32)Max(1, GSurfelPoolMaxCount);
        auto ActiveCompactUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIActiveCompactUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ActiveCompactUniformData)));
        ActiveCompactUniformBuffer->UpdateBufferData(&ActiveCompactUniformData, sizeof(ActiveCompactUniformData));

        {
            jSurfelGIClearInlineRayDispatchCSParameters ClearInlineRayDispatchParameters;
            ClearInlineRayDispatchParameters.ActiveCounterBuffer.Buffer = GSurfelGIActiveCounterBuffer.get();
            ClearInlineRayDispatchParameters.DispatchArgsBuffer.Buffer = GSurfelGIInlineRayDispatchArgsBuffer.get();

            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Clear InlineRay Dispatch", Vector4(0.15f, 0.55f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchClearInlineRayDispatch);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIClearInlineRayDispatch_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIClearInlineRayDispatch_cs.hlsl")
                , ClearInlineRayDispatchParameters
                , 1, 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get());
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get(), EResourceLayout::UAV);

        {
            jSurfelGICompactActiveInlineRayIndicesCSParameters CompactActiveParameters;
            CompactActiveParameters.SurfelPool.Buffer = GSurfelPoolBuffer.get();
            CompactActiveParameters.ActiveIndexBuffer.Buffer = GSurfelGIActiveIndexBuffer.get();
            CompactActiveParameters.ActiveCounterBuffer.Buffer = GSurfelGIActiveCounterBuffer.get();
            CompactActiveParameters.ActiveCompactUniformBuffer.Buffer = ActiveCompactUniformBuffer;

            const int32 CompactGroupX = (Max(1, GSurfelPoolMaxCount) + 63) / 64;
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Compact Active InlineRay Indices", Vector4(0.2f, 0.6f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchCompactActiveInlineRayIndices);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGICompactActiveInlineRayIndices_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGICompactActiveInlineRayIndices_cs.hlsl")
                , CompactActiveParameters
                , Max(1, CompactGroupX), 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get());

        {
            jSurfelGIBuildInlineRayDispatchArgsCSParameters BuildDispatchArgsParameters;
            BuildDispatchArgsParameters.ActiveCounterBuffer.Buffer = GSurfelGIActiveCounterBuffer.get();
            BuildDispatchArgsParameters.DispatchArgsBuffer.Buffer = GSurfelGIInlineRayDispatchArgsBuffer.get();

            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Build InlineRay DispatchArgs", Vector4(0.25f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchBuildInlineRayDispatchArgs);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIBuildInlineRayDispatchArgs_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIBuildInlineRayDispatchArgs_cs.hlsl")
                , BuildDispatchArgsParameters
                , 1, 1, 1);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIInlineRayDispatchArgsBuffer.get());

        if (gOptions.ShowSurfelGIHoverRayDebug)
        {
            int32 HoverMouseX = 0;
            int32 HoverMouseY = 0;
            const bool HasMouseInClient = TryGetClientMousePosition(HoverMouseX, HoverMouseY);
            if (HasMouseInClient)
            {
                PickMouseX = HoverMouseX;
                PickMouseY = HoverMouseY;
            }

            jSurfelGIHoverSelectUniformBuffer HoverSelectUniformData;
            HoverSelectUniformData.InvP = MainCamera->Projection.GetInverse();
            HoverSelectUniformData.InvV = MainCamera->View.GetInverse();
            HoverSelectUniformData.ScreenSize = Vector2((float)Width, (float)Height);
            HoverSelectUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
            HoverSelectUniformData.CascadeCellScalePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeClipmapGridDimXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeClipmapGridDimYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeClipmapGridDimZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.SurfelsPerCellPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeOriginCellXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeOriginCellYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeOriginCellZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeRingOffsetXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeRingOffsetYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeRingOffsetZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            HoverSelectUniformData.CascadeCellBasePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
            {
                HoverSelectUniformData.CascadeCellScalePacked.v[cascade] = GetSurfelGICascadeCellScale(cascade);
                HoverSelectUniformData.CascadeClipmapGridDimXPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
                HoverSelectUniformData.CascadeClipmapGridDimYPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
                HoverSelectUniformData.CascadeClipmapGridDimZPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
                HoverSelectUniformData.SurfelsPerCellPacked.v[cascade] = (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL);
                HoverSelectUniformData.CascadeOriginCellXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginX;
                HoverSelectUniformData.CascadeOriginCellYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginY;
                HoverSelectUniformData.CascadeOriginCellZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginZ;
                HoverSelectUniformData.CascadeRingOffsetXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX;
                HoverSelectUniformData.CascadeRingOffsetYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY;
                HoverSelectUniformData.CascadeRingOffsetZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ;
                HoverSelectUniformData.CascadeCellBasePacked.v[cascade] = (float)GSurfelCascadeCellBase[cascade];
            }
            HoverSelectUniformData.MaxSurfels = Max(1, GSurfelPoolMaxCount);
            HoverSelectUniformData.CascadePartitionCapacity = GetSurfelGICascadePartitionCapacity();
            HoverSelectUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
            HoverSelectUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
            HoverSelectUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
            HoverSelectUniformData.MousePixelX = HasMouseInClient ? Clamp(PickMouseX, 0, Max(0, Width - 1)) : -1;
            HoverSelectUniformData.MousePixelY = HasMouseInClient ? Clamp(PickMouseY, 0, Max(0, Height - 1)) : -1;
            HoverSelectUniformData.MouseValid = HasMouseInClient ? 1 : 0;

            auto HoverSelectUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
                g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIHoverSelectUniformBuffer"), jLifeTimeType::OneFrame, sizeof(HoverSelectUniformData)));
            HoverSelectUniformBuffer->UpdateBufferData(&HoverSelectUniformData, sizeof(HoverSelectUniformData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverSelectionBuffer.get(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get(), EResourceLayout::UAV);

            const jSamplerStateInfo* DepthSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Select Hovered Surfel", Vector4(0.85f, 0.35f, 0.2f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchSelectHoveredSurfel);

            jSurfelGISelectHoveredSurfelCSParameters Parameters;
            Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), DepthSamplerState };
            Parameters.SurfelPool = { GSurfelPoolBuffer.get() };
            Parameters.SurfelCellPageTable = { GSurfelCellPageTableBuffer.get() };
            Parameters.HoverSelectCommon.Buffer = HoverSelectUniformBuffer;
            Parameters.HoverSelectionBuffer = { GSurfelGIHoverSelectionBuffer.get() };
            Parameters.HoverRayDebugBuffer = { GSurfelGIHoverRayDebugBuffer.get() };

            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGISelectHoveredSurfel_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGISelectHoveredSurfel_cs.hlsl")
                , Parameters
                , 1, 1, 1);

            g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverSelectionBuffer.get());
            g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get());
        }

        jSurfelGIInlineRayGatherUniformBuffer GatherUniformData;
        GatherUniformData.MaxSurfels = (uint32)Max(1, GSurfelPoolMaxCount);
        GatherUniformData.RayCount = (uint32)Clamp(gOptions.SurfelGIInlineRayCount, 1, 16);
        GatherUniformData.BootstrapRayCount = (uint32)Clamp(gOptions.SurfelGINewSurfelBootstrapRayCount, 1, 32);
        GatherUniformData.MaxRayDistance = Max(10.0f, gOptions.SurfelGIInlineRayMaxDistance);
        GatherUniformData.RadianceScale = Max(0.0f, gOptions.SurfelGIRadianceScale);
        GatherUniformData.NormalBias = Max(0.001f, gOptions.SurfelGIInlineRayNormalBias);
        GatherUniformData.HistoryBlend = Clamp(gOptions.SurfelGIInlineRayHistoryBlend, 0.0f, 0.99f);
        GatherUniformData.UseGuiding = gOptions.SurfelGIInlineRayGuideEnable ? 1u : 0u;
        GatherUniformData.FrameNumber = UniformData.FrameNumber;
        GatherUniformData.SurfelPageSize = (uint32)Max(1, GSurfelPageSize);
        GatherUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
        GatherUniformData.CascadeCellScalePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeStartDistancePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeClipmapGridDimXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeClipmapGridDimYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeClipmapGridDimZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeOriginCellXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeOriginCellYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeOriginCellZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeRingOffsetXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeRingOffsetYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeRingOffsetZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        GatherUniformData.CascadeCellBasePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            GatherUniformData.CascadeCellScalePacked.v[cascade] = GetSurfelGICascadeCellScale(cascade);
            GatherUniformData.CascadeStartDistancePacked.v[cascade] = CascadeStartDistanceSanitized[cascade];
            GatherUniformData.CascadeClipmapGridDimXPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
            GatherUniformData.CascadeClipmapGridDimYPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
            GatherUniformData.CascadeClipmapGridDimZPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
            GatherUniformData.CascadeOriginCellXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginX;
            GatherUniformData.CascadeOriginCellYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginY;
            GatherUniformData.CascadeOriginCellZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginZ;
            GatherUniformData.CascadeRingOffsetXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX;
            GatherUniformData.CascadeRingOffsetYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY;
            GatherUniformData.CascadeRingOffsetZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ;
            GatherUniformData.CascadeCellBasePacked.v[cascade] = (float)GSurfelCascadeCellBase[cascade];
        }

        auto GatherUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIInlineRayGatherUniformBuffer"), jLifeTimeType::OneFrame, sizeof(GatherUniformData)));
        GatherUniformBuffer->UpdateBufferData(&GatherUniformData, sizeof(GatherUniformData));

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveIndexBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIActiveCounterBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get(), EResourceLayout::UAV);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverSelectionBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get(), EResourceLayout::UAV);

        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI GatherIrradiance HWRTDI", Vector4(0.2f, 0.65f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchGatherIrradianceInlineRay);
            DispatchSurfelGIHWRTDIGather(RenderFrameContextPtr, MainCamera, GatherUniformBuffer);
        }

        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get());
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get());
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture());

    if (gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug || gOptions.ShowSurfelGIIrradianceDebug || gOptions.ShowSurfelGIHoverRayDebug)
    {
        jSurfelGIVisualizeUniformBuffer VisualizeUniformData;
        VisualizeUniformData.InvP = MainCamera->Projection.GetInverse();
        VisualizeUniformData.InvV = MainCamera->View.GetInverse();
        VisualizeUniformData.ViewProj = MainCamera->ViewProjection;
        VisualizeUniformData.ScreenSize = Vector2((float)Width, (float)Height);
        VisualizeUniformData.BlendAlpha = Clamp(gOptions.SurfelGIVisualizeBlendAlpha, 0.0f, 1.0f);
        VisualizeUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
        VisualizeUniformData.CascadeCellScalePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeStartDistancePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeClipmapGridDimXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeClipmapGridDimYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeClipmapGridDimZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.SurfelsPerCellPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeOriginCellXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeOriginCellYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeOriginCellZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeRingOffsetXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeRingOffsetYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeRingOffsetZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeCellBasePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        VisualizeUniformData.CascadeCellCountPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            VisualizeUniformData.CascadeCellScalePacked.v[cascade] = GetSurfelGICascadeCellScale(cascade);
            VisualizeUniformData.CascadeStartDistancePacked.v[cascade] = CascadeStartDistanceSanitized[cascade];
            VisualizeUniformData.CascadeClipmapGridDimXPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
            VisualizeUniformData.CascadeClipmapGridDimYPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
            VisualizeUniformData.CascadeClipmapGridDimZPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
            VisualizeUniformData.CascadeOriginCellXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginX;
            VisualizeUniformData.CascadeOriginCellYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginY;
            VisualizeUniformData.CascadeOriginCellZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginZ;
            VisualizeUniformData.CascadeRingOffsetXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX;
            VisualizeUniformData.CascadeRingOffsetYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY;
            VisualizeUniformData.CascadeRingOffsetZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ;
            VisualizeUniformData.CascadeCellBasePacked.v[cascade] = (float)GSurfelCascadeCellBase[cascade];
            VisualizeUniformData.CascadeCellCountPacked.v[cascade] = (float)GSurfelCascadeCellCount[cascade];
        }
        VisualizeUniformData.MaxSurfels = GSurfelPoolMaxCount;
        VisualizeUniformData.CascadePartitionCapacity = GetSurfelGICascadePartitionCapacity();
        VisualizeUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
        VisualizeUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
        VisualizeUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
        VisualizeUniformData.BlendWithScene = gOptions.SurfelGIVisualizeBlendWithScene ? 1 : 0;
        VisualizeUniformData.ShowStateDebug = gOptions.ShowSurfelGIStateDebug ? 1 : 0;
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            VisualizeUniformData.SurfelsPerCellPacked.v[cascade] = (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL);
        }
        VisualizeUniformData.ShowCellDebug = gOptions.ShowSurfelGICellDebug ? 1 : 0;
        VisualizeUniformData.ShowUnderfilledCellDebug = gOptions.ShowSurfelGIUnderfilledCellDebug ? 1 : 0;
        VisualizeUniformData.ShowCellGrid = gOptions.ShowSurfelGICellGrid ? 1 : 0;
        VisualizeUniformData.ShowSpawnAttemptDebug = gOptions.ShowSurfelGISpawnAttemptDebug ? 1 : 0;
        VisualizeUniformData.ShowIrradianceDebug = gOptions.ShowSurfelGIIrradianceDebug ? 1 : 0;
        VisualizeUniformData.IrradianceDebugMode = Clamp(gOptions.SurfelGIIrradianceDebugMode, 0, 4);
        VisualizeUniformData.ShowHoverRayDebug = gOptions.ShowSurfelGIHoverRayDebug ? 1 : 0;
        VisualizeUniformData.ShowHoverRayRadianceColor = gOptions.ShowSurfelGIHoverRayHitRadianceColor ? 1 : 0;
        VisualizeUniformData.HoverRayLength = Max(gOptions.SurfelGIWorldGridCellSize * 1.25f, 10.0f);
        VisualizeUniformData.Padding0 = 0;

        auto VisualizeUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIVisualizeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(VisualizeUniformData)));
        VisualizeUniformBuffer->UpdateBufferData(&VisualizeUniformData, sizeof(VisualizeUniformData));

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch Visualize", Vector4(0.15f, 0.75f, 0.95f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchVisualize);
            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            jSurfelGIVisualizeCSParameters Parameters;
            Parameters.Result = { jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture() };
            Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
            Parameters.LinearDepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), nullptr };
            Parameters.SurfelPool = { GSurfelPoolBuffer.get() };
            Parameters.SpawnAttemptTexture = { jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), nullptr };
            Parameters.VisualizeCommon.Buffer = VisualizeUniformBuffer;
            Parameters.SurfelCellPageTable = { GSurfelCellPageTableBuffer.get() };
            Parameters.SurfelIrradianceBuffer = { GSurfelIrradianceBuffer.get() };
            Parameters.WinnerScoreBuffer = { GSurfelGIWinnerScoreBuffer.get() };
            Parameters.WinnerIndexBuffer = { GSurfelGIWinnerIndexBuffer.get() };
            Parameters.GBufferNormalTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState };
            Parameters.HoverRayDebugBuffer = { GSurfelGIHoverRayDebugBuffer.get() };

            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("SurfelGIVisualize_CS")
                , jNameStatic("Resource/Shaders/hlsl/SurfelGIVisualize_cs.hlsl")
                , Parameters
                , jSceneRenderTarget::SurfelGI_Debug_RT->Info.Width / 8 + ((jSceneRenderTarget::SurfelGI_Debug_RT->Info.Width % 8) ? 1 : 0)
                , jSceneRenderTarget::SurfelGI_Debug_RT->Info.Height / 8 + ((jSceneRenderTarget::SurfelGI_Debug_RT->Info.Height % 8) ? 1 : 0)
                , 1);
        }

        if (gOptions.SurfelGIVisualizeBlendWithScene)
        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI BlendToScene", Vector4(0.8f, 0.45f, 0.2f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_BlendToScene);

            const int32 ColorWidth = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Width;
            const int32 ColorHeight = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Height;
            auto TempColorRT = jRenderTargetPool::GetRenderTargetForOneFrame(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info);
            jRHIUtil::DrawQuad(RenderFrameContextPtr, TempColorRT, { 0, 0, ColorWidth, ColorHeight },
                [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    jTexture* InTexture = InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture();
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                    jRHIUtil::BuildSingleTextureFragmentBindings(InTexture, SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
                },
                [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo shaderInfo;
                    shaderInfo.SetName(jNameStatic("CopyPS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                    jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
                    return g_rhi->CreateShader(shaderInfo);
                }
            );

            jApplySurfelGIVisualizeUniformBuffer ApplyUniformData;
            ApplyUniformData.BlendAlpha = Clamp(gOptions.SurfelGIVisualizeBlendAlpha, 0.0f, 1.0f);
            ApplyUniformData.SceneWidth = ColorWidth;
            ApplyUniformData.SceneHeight = ColorHeight;
            ApplyUniformData.Padding0 = 0;

            auto ApplyUniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("ApplySurfelGIVisualizeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ApplyUniformData));
            ApplyUniformBuffer->UpdateBufferData(&ApplyUniformData, sizeof(ApplyUniformData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), TempColorRT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            jApplySurfelGIVisualizeCSParameters Parameters;
            Parameters.OutSceneColor = { RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture() };
            Parameters.SceneColorInput = { TempColorRT->GetTexture(), nullptr };
            Parameters.SurfelVisualizeInput = { jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), nullptr };
            Parameters.ApplyCommon.Buffer = std::shared_ptr<IUniformBufferBlock>(ApplyUniformBuffer);

            const uint32 NumGroupsX = ColorWidth / 8 + ((ColorWidth % 8) ? 1 : 0);
            const uint32 NumGroupsY = ColorHeight / 8 + ((ColorHeight % 8) ? 1 : 0);
            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("ApplySurfelGIVisualize_CS")
                , jNameStatic("Resource/Shaders/hlsl/ApplySurfelGIVisualize_cs.hlsl")
                , Parameters, NumGroupsX, NumGroupsY, 1);
        }
    }

    if (gOptions.ShowSurfelGIDebug
        || ((gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug || gOptions.ShowSurfelGIIrradianceDebug || gOptions.ShowSurfelGIHoverRayDebug) && !gOptions.SurfelGIVisualizeBlendWithScene))
    {
        DebugRTs.push_back(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexturePtr());
    }
}

void jRenderer::SurfelGIResolvePass()
{
    /*
     * Convert world-space surfel lighting into a screen-space irradiance buffer.
     *
     * Every pixel reconstructs its world position/normal, searches nearby surfel cells across
     * cascades, and blends the surfels that appear to describe the same local surface.
     *
     * Important distinction:
     * - SurfelGIPass() computes lighting "on surfels"
     * - SurfelGIResolvePass() answers "which surfels should influence this pixel?"
     */
    if (!gOptions.UseSurfelGI || !GSurfelPoolBuffer || !GSurfelIrradianceBuffer || !GSurfelCellPageTableBuffer)
        return;
    if (!RenderFrameContextPtr || !RenderFrameContextPtr->SceneRenderTargetPtr)
        return;
    auto MainCamera = jCamera::GetMainCamera();
    if (!MainCamera)
        return;
    const int32 Width = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width;
    const int32 Height = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height;

    auto ResolveRT = jRenderTargetPool::GetRenderTargetForOneFrame({
        .Type = ETextureType::TEXTURE_2D,
        .Format = ETextureFormat::R11G11B10F,
        .Width = Width,
        .Height = Height,
        .LayerCount = 1,
        .IsGenerateMipmap = false,
        .SampleCount = EMSAASamples::COUNT_1,
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("SurfelGI_Resolve_RT")
        });
    jSceneRenderTarget::SurfelGI_Resolve_RT = ResolveRT;

    jSurfelGIResolveUniformBuffer ResolveUniformData;
    ResolveUniformData.InvP = MainCamera->Projection.GetInverse();
    ResolveUniformData.InvV = MainCamera->View.GetInverse();
    ResolveUniformData.ScreenSize = Vector2((float)Width, (float)Height);
    ResolveUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
    ResolveUniformData.CascadeCellScalePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeClipmapGridDimXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeClipmapGridDimYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeClipmapGridDimZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.SurfelsPerCellPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeOriginCellXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeOriginCellYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeOriginCellZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeRingOffsetXPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeRingOffsetYPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeRingOffsetZPacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    ResolveUniformData.CascadeCellBasePacked = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        ResolveUniformData.CascadeCellScalePacked.v[cascade] = GetSurfelGICascadeCellScale(cascade);
        ResolveUniformData.CascadeClipmapGridDimXPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512);
        ResolveUniformData.CascadeClipmapGridDimYPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512);
        ResolveUniformData.CascadeClipmapGridDimZPacked.v[cascade] = (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512);
        ResolveUniformData.SurfelsPerCellPacked.v[cascade] = (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL);
        ResolveUniformData.CascadeOriginCellXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginX;
        ResolveUniformData.CascadeOriginCellYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginY;
        ResolveUniformData.CascadeOriginCellZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].OriginZ;
        ResolveUniformData.CascadeRingOffsetXPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX;
        ResolveUniformData.CascadeRingOffsetYPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY;
        ResolveUniformData.CascadeRingOffsetZPacked.v[cascade] = (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ;
        ResolveUniformData.CascadeCellBasePacked.v[cascade] = (float)GSurfelCascadeCellBase[cascade];
    }
    ResolveUniformData.MaxSurfels = Max(1, GSurfelPoolMaxCount);
    ResolveUniformData.CascadePartitionCapacity = GetSurfelGICascadePartitionCapacity();
    ResolveUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
    ResolveUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
    ResolveUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
    ResolveUniformData.ResolveSoftness = Max(0.1f, gOptions.SurfelGIResolveSoftness);
    ResolveUniformData.ResolveIrradianceWarmupUpdates = Max(0.0f, gOptions.SurfelGIResolveIrradianceWarmupUpdates);

    auto ResolveUniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIResolveUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ResolveUniformData));
    ResolveUniformBuffer->UpdateBufferData(&ResolveUniformData, sizeof(ResolveUniformData));

    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Resolve", Vector4(0.85f, 0.6f, 0.2f, 1.0f));
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_Resolve);
    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ResolveRT->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

    jSurfelGIResolveCSParameters Parameters;
    Parameters.Result = { ResolveRT->GetTexture() };
    Parameters.DepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState };
    Parameters.GBufferNormalTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState };
    Parameters.SurfelPool = { GSurfelPoolBuffer.get() };
    Parameters.SurfelCellPageTable = { GSurfelCellPageTableBuffer.get() };
    Parameters.SurfelIrradianceBuffer = { GSurfelIrradianceBuffer.get() };
    Parameters.ResolveCommon.Buffer = ResolveUniformBuffer;

    DispatchShaderParameterComputePass(RenderFrameContextPtr
        , jNameStatic("SurfelGIResolve_CS")
        , jNameStatic("Resource/Shaders/hlsl/SurfelGIResolve_cs.hlsl")
        , Parameters
        , ResolveRT->Info.Width / 8 + ((ResolveRT->Info.Width % 8) ? 1 : 0)
        , ResolveRT->Info.Height / 8 + ((ResolveRT->Info.Height % 8) ? 1 : 0)
        , 1);
}

void jRenderer::ApplySurfelGI()
{
    /*
     * Final shading step for SurfelGI.
     *
     * SurfelGI_Resolve_RT stores irradiance, not reflected radiance. To turn irradiance into
     * diffuse lighting, the compute shader multiplies it by albedo / PI and scales it by the
     * user-controlled intensity value before adding it to scene color.
     *
     * We copy the current color target first to avoid read/write hazards while dispatching the
     * compute shader into ColorPtr.
     */
    if (!gOptions.UseSurfelGI || !jSceneRenderTarget::SurfelGI_Resolve_RT)
        return;
    if (!RenderFrameContextPtr || !RenderFrameContextPtr->SceneRenderTargetPtr)
        return;

    const int32 ColorWidth = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Width;
    const int32 ColorHeight = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Height;
    auto TempColorRT = jRenderTargetPool::GetRenderTargetForOneFrame(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info);
    jRHIUtil::DrawQuad(RenderFrameContextPtr, TempColorRT, { 0, 0, ColorWidth, ColorHeight },
        [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
        {
            jTexture* InTexture = InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture();
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            jRHIUtil::BuildSingleTextureFragmentBindings(InTexture, SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
        },
        [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("CopyPS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
            return g_rhi->CreateShader(shaderInfo);
        });

    jApplySurfelGIUniformBuffer ApplyUniformData;
    ApplyUniformData.SurfelGIIntensity = Max(0.0f, gOptions.SurfelGIIntensity);
    ApplyUniformData.SceneWidth = ColorWidth;
    ApplyUniformData.SceneHeight = ColorHeight;

    auto ApplyUniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("ApplySurfelGIUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ApplyUniformData));
    ApplyUniformBuffer->UpdateBufferData(&ApplyUniformData, sizeof(ApplyUniformData));

    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "ApplySurfelGI", Vector4(0.95f, 0.55f, 0.2f, 1.0f));
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, Apply_SurfelGI);
    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), TempColorRT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Resolve_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

    jApplySurfelGICSParameters Parameters;
    Parameters.OutColorTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture() };
    Parameters.SceneColorTexture = { TempColorRT->GetTexture(), nullptr };
    Parameters.SurfelGITexture = { jSceneRenderTarget::SurfelGI_Resolve_RT->GetTexture(), SamplerState };
    Parameters.AlbedoTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), SamplerState };
    Parameters.ApplySurfelGIUniformBuffer.Buffer = std::shared_ptr<IUniformBufferBlock>(ApplyUniformBuffer);

    const uint32 NumGroupsX = ColorWidth / 8 + ((ColorWidth % 8) ? 1 : 0);
    const uint32 NumGroupsY = ColorHeight / 8 + ((ColorHeight % 8) ? 1 : 0);
    DispatchShaderParameterComputePass(RenderFrameContextPtr
        , jNameStatic("ApplySurfelGI_CS")
        , jNameStatic("Resource/Shaders/hlsl/ApplySurfelGI_cs.hlsl")
        , Parameters, NumGroupsX, NumGroupsY, 1);
}
