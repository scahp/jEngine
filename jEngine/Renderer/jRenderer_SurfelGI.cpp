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
#include <cmath>
#include <limits>
#include <array>
#include <unordered_map>

namespace
{
constexpr int32 SURFEL_GI_GUIDE_DIM = 4;
constexpr int32 SURFEL_GI_GUIDE_TOTAL_FLOATS = SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM + SURFEL_GI_GUIDE_DIM;
constexpr int32 SURFEL_GI_HOVER_DEBUG_MAX_RAYS = 16;

struct jSurfelGPU
{
    Vector4 PositionRadius = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 NormalSeenFrame = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 AlbedoWeight = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 Extra = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct alignas(16) jSurfelIrradianceGPU
{
    Vector4 IrradianceAndCount = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 MSMEData0 = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 MSMEData1 = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
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

struct alignas(16) jSurfelGIHoverSelectionGPU
{
    uint32 SurfelIndex = 0xffffffffu;
    uint32 Valid = 0;
    uint32 MousePixelX = 0;
    uint32 MousePixelY = 0;
};

struct alignas(16) jSurfelGIHoverRayDebugGPU
{
    Vector4 OriginAndCount = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 RayDirAndType[SURFEL_GI_HOVER_DEBUG_MAX_RAYS] = {};
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

struct alignas(16) jSurfelGIHWRTDISceneConstantBuffer
{
    Matrix ProjectionToWorld;
    Vector CameraPosition;
    float NormalBias = 0.1f;
    uint32 NumLights = 0;
    uint32 DebugViewMode = 0;
    uint32 ForceMipLevel0 = 0;
    uint32 RenderWidth = 0;
    float DebugLineWidth = 0.02f;
    float DebugUVScale = 16.0f;
    float DebugPrimitiveIDScale = 1.0f;
    float ShadowRayStartOffset = 0.001f;
    uint32 RenderHeight = 0;
    float Padding0 = 0.0f;
    float Padding1 = 0.0f;
    float Padding2 = 0.0f;
};
static_assert((sizeof(jSurfelGIHWRTDISceneConstantBuffer) % 16) == 0, "jSurfelGIHWRTDISceneConstantBuffer size must be 16-byte aligned");

struct alignas(16) jSurfelGIHWRTDIMaterialInstanceUniform
{
    uint32 MaterialFlags = 0;
    uint32 AlbedoSamplerIndex = 0;
    uint32 NormalSamplerIndex = 0;
    uint32 RMSamplerIndex = 0;
    float AlphaCutoff = 0.5f;
    float Padding0 = 0.0f;
    float Padding1 = 0.0f;
    float Padding2 = 0.0f;
};
static_assert((sizeof(jSurfelGIHWRTDIMaterialInstanceUniform) % 16) == 0, "jSurfelGIHWRTDIMaterialInstanceUniform size must be 16-byte aligned");

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

std::shared_ptr<jBuffer> GSurfelPoolBuffer;
int32 GSurfelPoolMaxCount = 0;
std::shared_ptr<jBuffer> GSurfelIrradianceBuffer;
int32 GSurfelIrradianceCapacity = 0;
std::shared_ptr<jBuffer> GSurfelGuidingBuffer;
int32 GSurfelGuidingCapacity = 0;
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
std::shared_ptr<jBuffer> GSurfelGIHoverSelectionBuffer;
std::shared_ptr<jBuffer> GSurfelGIHoverRayDebugBuffer;
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

bool TryGetClientMousePosition(int32& OutMouseX, int32& OutMouseY)
{
#if defined(_WIN32)
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

        jSurfelGIHWRTDIMaterialInstanceUniform MaterialUniform;
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
    const uint64 PackedLightBufferSize = (uint64)sizeof(jHWRTDIPackedLight) * (uint64)PackedLightCount;
    PackedLightBuffer = g_rhi->CreateStructuredBuffer(PackedLightBufferSize, 0, sizeof(jHWRTDIPackedLight), EBufferCreateFlag::UAV
        , EResourceLayout::GENERAL, PackedLights.data(), PackedLightBufferSize, jNameStatic("SurfelGI_HWRTDI_PackedLightBuffer"));
    check(PackedLightBuffer);

    jSurfelGIHWRTDISceneConstantBuffer SceneCB;
    SceneCB.ProjectionToWorld = InMainCamera->GetInverseViewProjectionMatrix();
    SceneCB.CameraPosition = InMainCamera->Pos;
    SceneCB.NormalBias = (gOptions.HWRTNormalBias > 0.0f) ? gOptions.HWRTNormalBias : 0.0f;
    SceneCB.ShadowRayStartOffset = (gOptions.HWRTShadowRayStartOffset > 0.0f) ? gOptions.HWRTShadowRayStartOffset : 0.0f;
    SceneCB.RenderWidth = (uint32)SCR_WIDTH;
    SceneCB.RenderHeight = (uint32)SCR_HEIGHT;
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

    jShaderBindingArray BindlessShaderBindingArray[11];
    BindlessShaderBindingArray[0].Add(jShaderBinding::CreateBindless(0, (uint32)VertexAndIndexOffsetBuffers.size(), EShaderBindingType::BUFFER_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(VertexAndIndexOffsetBuffers), false));
    BindlessShaderBindingArray[1].Add(jShaderBinding::CreateBindless(0, (uint32)IndexBuffers.size(), EShaderBindingType::BUFFER_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(IndexBuffers), false));
    BindlessShaderBindingArray[2].Add(jShaderBinding::CreateBindless(0, (uint32)RenderObjectBuffers.size(), EShaderBindingType::BUFFER_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(RenderObjectBuffers), false));
    BindlessShaderBindingArray[3].Add(jShaderBinding::CreateBindless(0, (uint32)VertexBuffers.size(), EShaderBindingType::BUFFER_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(VertexBuffers), false));
    BindlessShaderBindingArray[4].Add(jShaderBinding::CreateBindless(0, (uint32)MaterialInstanceBuffers.size(), EShaderBindingType::UNIFORMBUFFER, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jUniformBufferResourceBindless>(MaterialInstanceBuffers)));
    BindlessShaderBindingArray[5].Add(jShaderBinding::CreateBindless(0, (uint32)AlbedoTextures.size(), EShaderBindingType::TEXTURE_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jTextureResourceBindless>(AlbedoTextures)));
    BindlessShaderBindingArray[6].Add(jShaderBinding::CreateBindless(0, (uint32)NormalTextures.size(), EShaderBindingType::TEXTURE_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jTextureResourceBindless>(NormalTextures)));
    BindlessShaderBindingArray[7].Add(jShaderBinding::CreateBindless(0, (uint32)RMTextures.size(), EShaderBindingType::TEXTURE_SRV, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jTextureResourceBindless>(RMTextures)));
    BindlessShaderBindingArray[8].Add(jShaderBinding::CreateBindless(0, (uint32)AlbedoSamplerStates.size(), EShaderBindingType::SAMPLER, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jSamplerResourceBindless>(AlbedoSamplerStates)));
    BindlessShaderBindingArray[9].Add(jShaderBinding::CreateBindless(0, (uint32)NormalSamplerStates.size(), EShaderBindingType::SAMPLER, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jSamplerResourceBindless>(NormalSamplerStates)));
    BindlessShaderBindingArray[10].Add(jShaderBinding::CreateBindless(0, (uint32)RMSamplerStates.size(), EShaderBindingType::SAMPLER, BindingShaderStageFlag
        , ResourceInlineAllocator.Alloc<jSamplerResourceBindless>(RMSamplerStates)));
    std::array<std::shared_ptr<jShaderBindingInstance>, 11> BindlessShaderBindingInstances;
    for (int32 i = 0; i < (int32)BindlessShaderBindingInstances.size(); ++i)
    {
        BindlessShaderBindingInstances[(size_t)i] = g_rhi->CreateShaderBindingInstance(BindlessShaderBindingArray[i], jShaderBindingInstanceType::SingleFrame);
    }

    jShaderBindingArray SurfelGatherBindingArray;
    SurfelGatherBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelGIActiveIndexBuffer.get())));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelGIActiveCounterBuffer.get())));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelGuidingBuffer.get())));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jUniformBufferResource>(InGatherUniformBuffer.get()), true));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(6, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelGIHoverSelectionBuffer.get())));
    SurfelGatherBindingArray.Add(jShaderBinding::Create(7, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
        ResourceInlineAllocator.Alloc<jBufferResource>(GSurfelGIHoverRayDebugBuffer.get())));
    auto SurfelGatherBindingInstance = g_rhi->CreateShaderBindingInstance(SurfelGatherBindingArray, jShaderBindingInstanceType::SingleFrame);

    jShaderBindingLayoutArray LayoutArray;
    LayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);
    for (int32 i = 0; i < (int32)BindlessShaderBindingInstances.size(); ++i)
    {
        LayoutArray.Add(BindlessShaderBindingInstances[(size_t)i]->ShaderBindingsLayouts);
    }
    LayoutArray.Add(SurfelGatherBindingInstance->ShaderBindingsLayouts);

    jShaderInfo IrradianceGatherShaderInfo;
    IrradianceGatherShaderInfo.SetName(jNameStatic("SurfelGIGatherIrradianceHWRTDI_CS"));
    IrradianceGatherShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/HWRT_DI.hlsl"));
    IrradianceGatherShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    IrradianceGatherShaderInfo.SetEntryPoint(jNameStatic("SurfelGIGatherIrradianceHWRT_CS"));
    jShader* IrradianceGatherShader = g_rhi->CreateShader(IrradianceGatherShaderInfo);
    jPipelineStateInfo* IrradianceGatherPSO = g_rhi->CreateComputePipelineStateInfo(IrradianceGatherShader, LayoutArray, {});
    IrradianceGatherPSO->Bind(InRenderFrameContextPtr);

    jShaderBindingInstanceArray InstanceArray;
    InstanceArray.Add(GlobalShaderBindingInstance.get());
    for (int32 i = 0; i < (int32)BindlessShaderBindingInstances.size(); ++i)
    {
        InstanceArray.Add(BindlessShaderBindingInstances[(size_t)i].get());
    }
    InstanceArray.Add(SurfelGatherBindingInstance.get());

    jShaderBindingInstanceCombiner ShaderBindingCombiner;
    ShaderBindingCombiner.ShaderBindingInstanceArray = &InstanceArray;
    for (int32 i = 0; i < InstanceArray.NumOfData; ++i)
    {
        ShaderBindingCombiner.DescriptorSetHandles.Add(InstanceArray[i]->GetHandle());
        if (const std::vector<uint32>* DynamicOffsets = InstanceArray[i]->GetDynamicOffsets())
        {
            if (!DynamicOffsets->empty())
            {
                ShaderBindingCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
            }
        }
    }

    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), PackedLightBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), IrradianceGatherPSO, ShaderBindingCombiner, 0);
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

    if (!GSurfelGuidingBuffer || GSurfelGuidingCapacity != MaxSurfels)
    {
        GSurfelGuidingCapacity = MaxSurfels;
        const uint64 GuidingFloatCount = (uint64)MaxSurfels * (uint64)SURFEL_GI_GUIDE_TOTAL_FLOATS;

        GSurfelGuidingBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(float) * GuidingFloatCount,
            0,
            sizeof(float),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            nullptr,
            0,
            jNameStatic("SurfelGI_Guiding"));
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

    if (!GSurfelGIHoverSelectionBuffer)
    {
        jSurfelGIHoverSelectionGPU InitialSelection = {};
        GSurfelGIHoverSelectionBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGIHoverSelectionGPU),
            0,
            sizeof(jSurfelGIHoverSelectionGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            &InitialSelection,
            sizeof(jSurfelGIHoverSelectionGPU),
            jNameStatic("SurfelGI_HoverSelection"));
    }

    if (!GSurfelGIHoverRayDebugBuffer)
    {
        jSurfelGIHoverRayDebugGPU InitialHoverDebug = {};
        GSurfelGIHoverRayDebugBuffer = g_rhi->CreateStructuredBuffer(
            sizeof(jSurfelGIHoverRayDebugGPU),
            0,
            sizeof(jSurfelGIHoverRayDebugGPU),
            EBufferCreateFlag::UAV,
            EResourceLayout::GENERAL,
            &InitialHoverDebug,
            sizeof(jSurfelGIHoverRayDebugGPU),
            jNameStatic("SurfelGI_HoverRayDebug"));
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
        || !GSurfelGIStatsBuffer || !GSurfelIrradianceBuffer || !GSurfelGuidingBuffer
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
        ClipmapClearBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            ClipmapClearResourceAllocator.Alloc<jBufferResource>(GSurfelGuidingBuffer.get())));
        ClipmapClearBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
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
        g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get());
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
        CleanupBindingArray.Add(jShaderBinding::Create(CleanupBindingPoint++, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
            CleanupResourceAllocator.Alloc<jBufferResource>(GSurfelGuidingBuffer.get())));
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
    g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGuidingBuffer.get());
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
            ReservoirPlaceBindingArray.Add(jShaderBinding::Create(8, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                ReservoirPlaceResourceAllocator.Alloc<jBufferResource>(GSurfelGuidingBuffer.get())));

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

            struct alignas(16) jSurfelGIHoverSelectUniformBuffer
            {
                Matrix InvP;
                Matrix InvV;
                Vector2 ScreenSize;
                float GridCellSize;
                jFloat4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
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
                int32 MaxSurfels = 0;
                int32 SurfelPageSize = 0;
                int32 SurfelPageTableCapacity = 0;
                int32 NeighborCellRadius = 0;
                int32 MousePixelX = -1;
                int32 MousePixelY = -1;
                int32 MouseValid = 0;
                int32 Padding0 = 0;
            };
            static_assert((sizeof(jSurfelGIHoverSelectUniformBuffer) % 16) == 0, "jSurfelGIHoverSelectUniformBuffer size must be 16-byte aligned");

            jSurfelGIHoverSelectUniformBuffer HoverSelectUniformData;
            HoverSelectUniformData.InvP = MainCamera->Projection.GetInverse();
            HoverSelectUniformData.InvV = MainCamera->View.GetInverse();
            HoverSelectUniformData.ScreenSize = Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT);
            HoverSelectUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
            auto SetPackedHoverSelectValue = [](jFloat4* packedArray, int32 cascade, float value)
            {
                const int32 packIndex = cascade / 4;
                const int32 lane = cascade % 4;
                if (lane == 0) packedArray[packIndex].x = value;
                else if (lane == 1) packedArray[packIndex].y = value;
                else if (lane == 2) packedArray[packIndex].z = value;
                else packedArray[packIndex].w = value;
            };
            for (int32 pack = 0; pack < SURFEL_GI_CASCADE_PACKED_COUNT; ++pack)
            {
                HoverSelectUniformData.CascadeCellScaleFromPrevPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeClipmapGridDimXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeClipmapGridDimYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeClipmapGridDimZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.SurfelsPerCellPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeOriginCellXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeOriginCellYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeOriginCellZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeRingOffsetXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeRingOffsetYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeRingOffsetZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
                HoverSelectUniformData.CascadeCellBasePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
            }
            for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
            {
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeCellScaleFromPrevPacked, cascade, (cascade == 0) ? 1.0f : Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[cascade]));
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeClipmapGridDimXPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512));
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeClipmapGridDimYPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512));
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeClipmapGridDimZPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512));
                SetPackedHoverSelectValue(HoverSelectUniformData.SurfelsPerCellPacked, cascade, (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL));
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeOriginCellXPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginX);
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeOriginCellYPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginY);
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeOriginCellZPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginZ);
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeRingOffsetXPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX);
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeRingOffsetYPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY);
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeRingOffsetZPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ);
                SetPackedHoverSelectValue(HoverSelectUniformData.CascadeCellBasePacked, cascade, (float)GSurfelCascadeCellBase[cascade]);
            }
            HoverSelectUniformData.MaxSurfels = Max(1, GSurfelPoolMaxCount);
            HoverSelectUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
            HoverSelectUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
            HoverSelectUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
            HoverSelectUniformData.MousePixelX = HasMouseInClient ? Clamp(PickMouseX, 0, Max(0, SCR_WIDTH - 1)) : -1;
            HoverSelectUniformData.MousePixelY = HasMouseInClient ? Clamp(PickMouseY, 0, Max(0, SCR_HEIGHT - 1)) : -1;
            HoverSelectUniformData.MouseValid = HasMouseInClient ? 1 : 0;

            auto HoverSelectUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
                g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIHoverSelectUniformBuffer"), jLifeTimeType::OneFrame, sizeof(HoverSelectUniformData)));
            HoverSelectUniformBuffer->UpdateBufferData(&HoverSelectUniformData, sizeof(HoverSelectUniformData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverSelectionBuffer.get(), EResourceLayout::UAV);
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get(), EResourceLayout::UAV);

            jShaderBindingArray HoverSelectBindingArray;
            jShaderBindingResourceInlineAllocator HoverSelectResourceAllocator;
            const jSamplerStateInfo* DepthSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();
            HoverSelectBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                HoverSelectResourceAllocator.Alloc<jTextureResource>(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), DepthSamplerState)));
            HoverSelectBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                HoverSelectResourceAllocator.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
            HoverSelectBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                HoverSelectResourceAllocator.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));
            HoverSelectBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                HoverSelectResourceAllocator.Alloc<jUniformBufferResource>(HoverSelectUniformBuffer.get()), true));
            HoverSelectBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                HoverSelectResourceAllocator.Alloc<jBufferResource>(GSurfelGIHoverSelectionBuffer.get())));
            HoverSelectBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE,
                HoverSelectResourceAllocator.Alloc<jBufferResource>(GSurfelGIHoverRayDebugBuffer.get())));

            auto HoverSelectBindingInstance = g_rhi->CreateShaderBindingInstance(HoverSelectBindingArray, jShaderBindingInstanceType::SingleFrame);

            jShaderInfo HoverSelectShaderInfo;
            HoverSelectShaderInfo.SetName(jNameStatic("SurfelGISelectHoveredSurfel_CS"));
            HoverSelectShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGISelectHoveredSurfel_cs.hlsl"));
            HoverSelectShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            HoverSelectShaderInfo.SetEntryPoint(jNameStatic("main"));
            jShader* HoverSelectShader = g_rhi->CreateShader(HoverSelectShaderInfo);

            jShaderBindingLayoutArray HoverSelectLayoutArray;
            HoverSelectLayoutArray.Add(HoverSelectBindingInstance->ShaderBindingsLayouts);
            jPipelineStateInfo* HoverSelectPSO = g_rhi->CreateComputePipelineStateInfo(HoverSelectShader, HoverSelectLayoutArray, {});
            HoverSelectPSO->Bind(RenderFrameContextPtr);

            jShaderBindingInstanceArray HoverSelectInstanceArray;
            HoverSelectInstanceArray.Add(HoverSelectBindingInstance.get());

            jShaderBindingInstanceCombiner HoverSelectCombiner;
            HoverSelectCombiner.ShaderBindingInstanceArray = &HoverSelectInstanceArray;
            HoverSelectCombiner.DescriptorSetHandles.Add(HoverSelectBindingInstance->GetHandle());
            if (const std::vector<uint32>* DynamicOffsets = HoverSelectBindingInstance->GetDynamicOffsets())
            {
                if (!DynamicOffsets->empty())
                {
                    HoverSelectCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
                }
            }

            g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), HoverSelectPSO, HoverSelectCombiner, 0);
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Select Hovered Surfel", Vector4(0.85f, 0.35f, 0.2f, 1.0f));
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_DispatchSelectHoveredSurfel);
            g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
            g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverSelectionBuffer.get());
            g_rhi->UAVBarrier(RenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelGIHoverRayDebugBuffer.get());
        }

        struct alignas(16) jSurfelGIInlineRayGatherUniformBuffer
        {
            uint32 MaxSurfels = 0;
            uint32 RayCount = 0;
            float MaxRayDistance = 0.0f;
            float NormalBias = 0.0f;
            float HistoryBlend = 0.0f;
            uint32 UseGuiding = 0;
            int32 FrameNumber = 0;
            uint32 Padding1 = 0;
        };
        static_assert((sizeof(jSurfelGIInlineRayGatherUniformBuffer) % 16) == 0, "jSurfelGIInlineRayGatherUniformBuffer size must be 16-byte aligned");

        jSurfelGIInlineRayGatherUniformBuffer GatherUniformData;
        GatherUniformData.MaxSurfels = (uint32)Max(1, GSurfelPoolMaxCount);
        GatherUniformData.RayCount = (uint32)Clamp(gOptions.SurfelGIInlineRayCount, 1, 16);
        GatherUniformData.MaxRayDistance = Max(10.0f, gOptions.SurfelGIInlineRayMaxDistance);
        GatherUniformData.NormalBias = Max(0.001f, gOptions.SurfelGIInlineRayNormalBias);
        GatherUniformData.HistoryBlend = Clamp(gOptions.SurfelGIInlineRayHistoryBlend, 0.0f, 0.99f);
        GatherUniformData.UseGuiding = gOptions.SurfelGIInlineRayGuideEnable ? 1u : 0u;
        GatherUniformData.FrameNumber = UniformData.FrameNumber;

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
        struct alignas(16) jSurfelGIVisualizeUniformBuffer
        {
            Matrix InvP;
            Matrix InvV;
            Matrix ViewProj;
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
            int32 IrradianceDebugMode;
            int32 ShowHoverRayDebug;
            float HoverRayLength;
            int32 Padding0;
            int32 Padding1;
        };
        static_assert((sizeof(jSurfelGIVisualizeUniformBuffer) % 16) == 0, "jSurfelGIVisualizeUniformBuffer size must be 16-byte aligned");

        jSurfelGIVisualizeUniformBuffer VisualizeUniformData;
        VisualizeUniformData.InvP = MainCamera->Projection.GetInverse();
        VisualizeUniformData.InvV = MainCamera->View.GetInverse();
        VisualizeUniformData.ViewProj = MainCamera->ViewProjection;
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
        VisualizeUniformData.IrradianceDebugMode = Clamp(gOptions.SurfelGIIrradianceDebugMode, 0, 4);
        VisualizeUniformData.ShowHoverRayDebug = gOptions.ShowSurfelGIHoverRayDebug ? 1 : 0;
        VisualizeUniformData.HoverRayLength = Max(gOptions.SurfelGIWorldGridCellSize * 1.25f, 10.0f);
        VisualizeUniformData.Padding0 = 0;
        VisualizeUniformData.Padding1 = 0;

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
            jRHIUtil::DispatchCompute(RenderFrameContextPtr, jSceneRenderTarget::SurfelGI_Debug_RT->GetTexture(),
                [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
                {
                    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                        , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
                    g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

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
                    InOutShaderBindingArray.Add(jShaderBinding::Create(8, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelGIWinnerScoreBuffer.get())));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(9, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelGIWinnerIndexBuffer.get())));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(10, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState)));
                    InOutShaderBindingArray.Add(jShaderBinding::Create(11, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                        InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelGIHoverRayDebugBuffer.get())));
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
        || ((gOptions.ShowSurfelGIPlacedSurfels || gOptions.ShowSurfelGISpawnAttemptDebug || gOptions.ShowSurfelGIIrradianceDebug || gOptions.ShowSurfelGIHoverRayDebug) && !gOptions.SurfelGIVisualizeBlendWithScene))
    {
        DebugRTs.push_back(jSceneRenderTarget::SurfelGI_Debug_RT->GetTexturePtr());
    }
}

void jRenderer::SurfelGIResolvePass()
{
    if (!gOptions.UseSurfelGI || !GSurfelPoolBuffer || !GSurfelIrradianceBuffer || !GSurfelCellPageTableBuffer)
        return;
    if (!RenderFrameContextPtr || !RenderFrameContextPtr->SceneRenderTargetPtr)
        return;

    auto ResolveRT = jRenderTargetPool::GetRenderTargetForOneFrame({
        .Type = ETextureType::TEXTURE_2D,
        .Format = ETextureFormat::R11G11B10F,
        .Width = SCR_WIDTH,
        .Height = SCR_HEIGHT,
        .LayerCount = 1,
        .IsGenerateMipmap = false,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("SurfelGI_Resolve_RT")
        });
    jSceneRenderTarget::SurfelGI_Resolve_RT = ResolveRT;

    struct alignas(16) jSurfelGIResolveUniformBuffer
    {
        Matrix InvP;
        Matrix InvV;
        Vector2 ScreenSize;
        float GridCellSize;
        Vector4 CascadeCellScaleFromPrevPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeClipmapGridDimXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeClipmapGridDimYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeClipmapGridDimZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 SurfelsPerCellPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeOriginCellXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeOriginCellYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeOriginCellZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeRingOffsetXPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeRingOffsetYPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeRingOffsetZPacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        Vector4 CascadeCellBasePacked[SURFEL_GI_CASCADE_PACKED_COUNT];
        int32 MaxSurfels;
        int32 SurfelPageSize;
        int32 SurfelPageTableCapacity;
        int32 NeighborCellRadius;
    };
    static_assert((sizeof(jSurfelGIResolveUniformBuffer) % 16) == 0, "jSurfelGIResolveUniformBuffer size must be 16-byte aligned");

    jSurfelGIResolveUniformBuffer ResolveUniformData;
    ResolveUniformData.InvP = jCamera::GetMainCamera()->Projection.GetInverse();
    ResolveUniformData.InvV = jCamera::GetMainCamera()->View.GetInverse();
    ResolveUniformData.ScreenSize = Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT);
    ResolveUniformData.GridCellSize = Max(0.1f, gOptions.SurfelGIWorldGridCellSize);
    for (int32 pack = 0; pack < SURFEL_GI_CASCADE_PACKED_COUNT; ++pack)
    {
        ResolveUniformData.CascadeCellScaleFromPrevPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeClipmapGridDimXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeClipmapGridDimYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeClipmapGridDimZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.SurfelsPerCellPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeOriginCellXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeOriginCellYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeOriginCellZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeRingOffsetXPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeRingOffsetYPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeRingOffsetZPacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ResolveUniformData.CascadeCellBasePacked[pack] = { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    auto SetPackedResolveValue = [](Vector4* packedArray, int32 cascade, float value)
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
        SetPackedResolveValue(ResolveUniformData.CascadeCellScaleFromPrevPacked, cascade, (cascade == 0) ? 1.0f : Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[cascade]));
        SetPackedResolveValue(ResolveUniformData.CascadeClipmapGridDimXPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimX[cascade], 4, 512));
        SetPackedResolveValue(ResolveUniformData.CascadeClipmapGridDimYPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimY[cascade], 4, 512));
        SetPackedResolveValue(ResolveUniformData.CascadeClipmapGridDimZPacked, cascade, (float)Clamp(gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 512));
        SetPackedResolveValue(ResolveUniformData.SurfelsPerCellPacked, cascade, (float)Clamp(gOptions.SurfelGISurfelsPerCell[cascade], 1, SURFEL_GI_MAX_SLOTS_PER_CELL));
        SetPackedResolveValue(ResolveUniformData.CascadeOriginCellXPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginX);
        SetPackedResolveValue(ResolveUniformData.CascadeOriginCellYPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginY);
        SetPackedResolveValue(ResolveUniformData.CascadeOriginCellZPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].OriginZ);
        SetPackedResolveValue(ResolveUniformData.CascadeRingOffsetXPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetX);
        SetPackedResolveValue(ResolveUniformData.CascadeRingOffsetYPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetY);
        SetPackedResolveValue(ResolveUniformData.CascadeRingOffsetZPacked, cascade, (float)GSurfelClipmapRuntimeStates[cascade].RingOffsetZ);
        SetPackedResolveValue(ResolveUniformData.CascadeCellBasePacked, cascade, (float)GSurfelCascadeCellBase[cascade]);
    }
    ResolveUniformData.MaxSurfels = Max(1, GSurfelPoolMaxCount);
    ResolveUniformData.SurfelPageSize = Max(1, GSurfelPageSize);
    ResolveUniformData.SurfelPageTableCapacity = Max(1, GSurfelCellPageTableCapacity);
    ResolveUniformData.NeighborCellRadius = Clamp(gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);

    auto ResolveUniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("SurfelGIResolveUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ResolveUniformData));
    ResolveUniformBuffer->UpdateBufferData(&ResolveUniformData, sizeof(ResolveUniformData));

    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SurfelGI Resolve", Vector4(0.85f, 0.6f, 0.2f, 1.0f));
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, SurfelGI_Resolve);
    jRHIUtil::DispatchCompute(RenderFrameContextPtr, ResolveRT->GetTexture(),
        [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
        {
            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelPoolBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelCellPageTableBuffer.get(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), GSurfelIrradianceBuffer.get(), EResourceLayout::SHADER_READ_ONLY);

            InOutShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), SamplerState)));
            InOutShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::NORMAL)->GetTexture(), SamplerState)));
            InOutShaderBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelPoolBuffer.get())));
            InOutShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelCellPageTableBuffer.get())));
            InOutShaderBindingArray.Add(jShaderBinding::Create(5, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jBufferResource>(GSurfelIrradianceBuffer.get())));
            InOutShaderBindingArray.Add(jShaderBinding::Create(6, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(ResolveUniformBuffer.get()), true));
        },
        [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
        {
            jShaderInfo ShaderInfo;
            ShaderInfo.SetName(jNameStatic("SurfelGIResolve_CS"));
            ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SurfelGIResolve_cs.hlsl"));
            ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            ShaderInfo.SetEntryPoint(jNameStatic("main"));
            return g_rhi->CreateShader(ShaderInfo);
        });
}

void jRenderer::ApplySurfelGI()
{
    if (!gOptions.UseSurfelGI || !jSceneRenderTarget::SurfelGI_Resolve_RT)
        return;
    if (!RenderFrameContextPtr || !RenderFrameContextPtr->SceneRenderTargetPtr)
        return;

    auto TempColorRT = jRenderTargetPool::GetRenderTargetForOneFrame(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info);
    jRHIUtil::DrawQuad(RenderFrameContextPtr, TempColorRT, { 0, 0, SCR_WIDTH, SCR_HEIGHT },
        [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
        {
            jTexture* InTexture = InRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture();
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT,
                InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture, SamplerState)));
        },
        [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("CopyPS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            return g_rhi->CreateShader(shaderInfo);
        });

    struct alignas(16) jApplySurfelGIUniformBuffer
    {
        float SurfelGIIntensity = 1.0f;
        int32 SceneWidth = 0;
        int32 SceneHeight = 0;
        int32 Padding0 = 0;
    };
    static_assert((sizeof(jApplySurfelGIUniformBuffer) % 16) == 0, "jApplySurfelGIUniformBuffer size must be 16-byte aligned");

    jApplySurfelGIUniformBuffer ApplyUniformData;
    ApplyUniformData.SurfelGIIntensity = Max(0.0f, gOptions.SurfelGIIntensity);
    ApplyUniformData.SceneWidth = SCR_WIDTH;
    ApplyUniformData.SceneHeight = SCR_HEIGHT;

    auto ApplyUniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("ApplySurfelGIUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ApplyUniformData));
    ApplyUniformBuffer->UpdateBufferData(&ApplyUniformData, sizeof(ApplyUniformData));

    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "ApplySurfelGI", Vector4(0.95f, 0.55f, 0.2f, 1.0f));
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, Apply_SurfelGI);
    jRHIUtil::DispatchCompute(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(),
        [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
        {
            const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), TempColorRT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::SurfelGI_Resolve_RT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jTextureResource>(TempColorRT->GetTexture(), nullptr)));
            InOutShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jTextureResource>(jSceneRenderTarget::SurfelGI_Resolve_RT->GetTexture(), SamplerState)));
            InOutShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jTextureResource>(InRenderFrameContextPtr->SceneRenderTargetPtr->GetGBuffer(EGBufferType::ALBEDO)->GetTexture(), SamplerState)));
            InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::COMPUTE,
                InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(ApplyUniformBuffer.get())));
        },
        [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
        {
            jShaderInfo shaderInfo;
            shaderInfo.SetName(jNameStatic("ApplySurfelGI_CS"));
            shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/ApplySurfelGI_cs.hlsl"));
            shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
            return g_rhi->CreateShader(shaderInfo);
        });
}
