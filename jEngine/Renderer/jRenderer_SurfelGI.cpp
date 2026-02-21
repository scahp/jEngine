#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "jSceneRenderTargets.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jRHIUtil.h"

namespace
{
struct jSurfelGPU
{
    Vector4 PositionRadius = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 NormalSeenFrame = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 AlbedoWeight = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 Extra = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
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

struct alignas(16) jSurfelCellPageEntryGPU
{
    int32 CellX = 0;
    int32 CellY = 0;
    int32 CellZ = 0;
    int32 Cascade = 0;
    uint32 State = 0;
    uint32 Padding0 = 0;
    uint32 Padding1 = 0;
    uint32 Padding2 = 0;
};

std::shared_ptr<jBuffer> GSurfelPoolBuffer;
int32 GSurfelPoolMaxCount = 0;
int32 GSurfelPageSize = 8;
std::shared_ptr<jBuffer> GVisibleCellWorklistBuffer;
std::shared_ptr<jBuffer> GVisibleCellCounterBuffer;
int32 GVisibleCellWorklistCapacity = 0;
std::shared_ptr<jBuffer> GSurfelCellPageTableBuffer;
int32 GSurfelCellPageTableCapacity = 0;

void EnsureSurfelGIResources(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
    const int32 MaxSurfels = Max(1024, gOptions.SurfelGIMaxSurfels);

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
    }

    const int32 PageCount = Max(1, GSurfelPoolMaxCount / Max(1, GSurfelPageSize));
    if (!GSurfelCellPageTableBuffer || GSurfelCellPageTableCapacity != PageCount)
    {
        GSurfelCellPageTableCapacity = PageCount;
        std::vector<jSurfelCellPageEntryGPU> InitialEntries;
        InitialEntries.resize((size_t)PageCount);

        GSurfelCellPageTableBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelCellPageEntryGPU) * (uint64)PageCount,
            0,
            sizeof(jSurfelCellPageEntryGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            InitialEntries.data(),
            sizeof(jSurfelCellPageEntryGPU) * (uint64)PageCount,
            jNameStatic("SurfelGI_CellPageTable"));
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

    const int32 MaxVisibleCells = Max(1, Width * Height);
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
        || !GVisibleCellWorklistBuffer || !GVisibleCellCounterBuffer || !GSurfelCellPageTableBuffer)
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
        float PaddingAfterRadiusScale;
        jFloat4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        jFloat4 OverlapAllowancePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
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
        UniformData.OverlapAllowancePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    UniformData.PaddingAfterRadiusScale = 0.0f;
    auto SetPackedCascadeValue = [](jFloat4* packedArray, int32 cascade, float value)
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
        SetPackedCascadeValue(UniformData.CascadeCellScaleFromPrevPacked, cascade, (cascade == 0) ? 1.0f : Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[cascade]));
        SetPackedCascadeValue(UniformData.CascadeStartDistancePacked, cascade, CascadeStartDistanceSanitized[cascade]);
        SetPackedCascadeValue(UniformData.CascadeRadiusScalePacked, cascade, (cascade == 0) ? 1.0f : Max(0.05f, gOptions.SurfelGICascadeRadiusScale[cascade]));
    }
    UniformData.SpawnHysteresisFrames = Max(1, gOptions.SurfelGISpawnHysteresisFrames);
    UniformData.DeleteHysteresisFrames = Max(1, gOptions.SurfelGIDeleteHysteresisFrames);
    UniformData.RadiusScale = Max(0.05f, gOptions.SurfelGIRadiusScale);
    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
    {
        SetPackedCascadeValue(UniformData.CascadeClipmapGridDimXPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512));
        SetPackedCascadeValue(UniformData.CascadeClipmapGridDimYPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512));
        SetPackedCascadeValue(UniformData.CascadeClipmapGridDimZPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512));
        SetPackedCascadeValue(UniformData.SurfelsPerCellPacked, cascade, (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, 8));
        SetPackedCascadeValue(UniformData.OverlapAllowancePacked, cascade, Clamp(gOptions.SurfelGIOverlapAllowance[cascade], 0.0f, 0.95f));
    }

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
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), EResourceLayout::UAV);
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), EResourceLayout::UAV);

    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState)));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), SamplerState)));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), nullptr)));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(), nullptr)));

    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture(), nullptr)));
    ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllactor.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));

    auto CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

    jShaderInfo ShaderInfo;
    ShaderInfo.SetName(jNameStatic("SurfelGI_CS"));
    ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGI_cs.hlsl"));
    ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    ShaderInfo.SetEntryPoint(jNameStatic("main"));
    jShader* Shader = g_rhi->CreateShader(ShaderInfo);

    jShaderBindingLayoutArray ShaderBindingLayoutArray;
    ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);
    jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

    ComputePipelineStateInfo->Bind(RenderFrameContextPtr);

    jShaderBindingInstanceArray ShaderBindingInstanceArray;
    ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

    jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
    ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;
    ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(CurrentBindingInstance->GetHandle());
    if (const std::vector<uint32>* DynamicOffsets = CurrentBindingInstance->GetDynamicOffsets())
    {
        if (!DynamicOffsets->empty())
        {
            ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
        }
    }

    g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingInstanceCombiner, 0);

    const int32 Width = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Width;
    const int32 Height = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->Info.Height;
    const int32 GroupX = (Width + 7) / 8;
    const int32 GroupY = (Height + 7) / 8;

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

    ComputePipelineStateInfo->Bind(RenderFrameContextPtr);
    g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingInstanceCombiner, 0);

    {
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Dispatch PlaceUpdate", Vector4(0.2f, 0.9f, 0.35f, 1.0f));
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchPlaceUpdate);
        g_rhi->DispatchCompute(RenderFrameContextPtr, GroupX, GroupY, 1);
    }

    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture());
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Attempt_RT->GetTexture());

    if (gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug)
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
            int32 ShowCellDebug;
            int32 ShowUnderfilledCellDebug;
            int32 ShowCellGrid;
            int32 ShowSpawnAttemptDebug;
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
        }
        VisualizeUniformData.MaxSurfels = GSurfelPoolMaxCount;
        VisualizeUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
        VisualizeUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
        VisualizeUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
        VisualizeUniformData.BlendWithScene = gOptions.SurfelGIVisualizeBlendWithScene ? 1 : 0;
        VisualizeUniformData.ShowStateDebug = gOptions.ShowSurfelGIStateDebug ? 1 : 0;
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            SetPackedVisualizeValue(VisualizeUniformData.SurfelsPerCellPacked, cascade, (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, 8));
        }
        VisualizeUniformData.ShowCellDebug = gOptions.ShowSurfelGICellDebug ? 1 : 0;
        VisualizeUniformData.ShowUnderfilledCellDebug = gOptions.ShowSurfelGIUnderfilledCellDebug ? 1 : 0;
        VisualizeUniformData.ShowCellGrid = gOptions.ShowSurfelGICellGrid ? 1 : 0;
        VisualizeUniformData.ShowSpawnAttemptDebug = gOptions.ShowSurfelGISpawnAttemptDebug ? 1 : 0;

        auto VisualizeUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIVisualizeUniformBuffer"), jLifeTimeType::OneFrame, sizeof(VisualizeUniformData)));
        VisualizeUniformBuffer->UpdateBufferData(&VisualizeUniformData, sizeof(VisualizeUniformData));

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
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
        || ((gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug) && !gOptions.SurfelGIVisualizeBlendWithScene))
    {
        DebugRTs.push_back(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexturePtr());
    }
}
