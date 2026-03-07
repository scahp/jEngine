#include "pch.h"
#include "jRenderer.h"
#include "jOptions.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/jRHI.h"
#include "RHI/jRHIUtil.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Material/jMaterial.h"
#include "FileLoader/jImageFileLoader.h"
#include <unordered_map>

namespace
{
    struct jHWRTDISceneConstantBuffer
    {
        Matrix ProjectionToWorld;
        Vector CameraPosition;
        float NormalBias = 0.1f;
        uint32 NumDirectionalLights = 0;
        uint32 NumPointLights = 0;
        uint32 NumSpotLights = 0;
        uint32 DebugViewMode = 0;
        float DebugLineWidth = 0.02f;
        float DebugUVScale = 16.0f;
        float DebugPrimitiveIDScale = 1.0f;
        uint32 ForceMipLevel0 = 0;
        float ShadowRayStartOffset = 0.001f;
        float Padding0 = 0.0f;
        float Padding1 = 0.0f;
        float Padding2 = 0.0f;
    };

    struct jHWRTDIMaterialInstanceUniform
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

    enum : uint32
    {
        HWRTDI_MaterialFlag_HasAlbedoTexture = 1u << 0,
        HWRTDI_MaterialFlag_HasNormalTexture = 1u << 1,
        HWRTDI_MaterialFlag_HasRMTexture = 1u << 2,
        HWRTDI_MaterialFlag_UseSRGBAlbedoTexture = 1u << 3,
        HWRTDI_MaterialFlag_IsSkyMaterial = 1u << 4,
        HWRTDI_MaterialFlag_UseAlphaCutout = 1u << 5,
        HWRTDI_MaterialFlag_NonOpaqueGeometry = 1u << 6
    };
}

bool jRenderer::IsUseHWRTDirectLighting() const
{
    return gOptions.UseHWRTDirectLighting
        && gOptions.UseRaytracing
        && GSupportRaytracing
        && RenderFrameContextPtr
        && RenderFrameContextPtr->RaytracingScene
        && RenderFrameContextPtr->RaytracingScene->IsValid()
        && !RenderFrameContextPtr->UseForwardRenderer;
}

bool jRenderer::HWRTDirectLightingPass()
{
    if (!IsUseHWRTDirectLighting())
        return false;

    auto* RaytracingScene = RenderFrameContextPtr->RaytracingScene;
    if (!RaytracingScene || !RaytracingScene->TLASBufferPtr || RaytracingScene->InstanceList.empty())
        return false;

    if (!RaytracingScene->RaytracingOutputPtr
        || RaytracingScene->RaytracingOutputPtr->Width != (int32)SCR_WIDTH
        || RaytracingScene->RaytracingOutputPtr->Height != (int32)SCR_HEIGHT
        || RaytracingScene->RaytracingOutputPtr->Format != ETextureFormat::RGBA16F)
    {
        RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)SCR_WIDTH, (uint32)SCR_HEIGHT, (uint32)1, (uint32)1
            , ETextureFormat::RGBA16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
    }

    jTexture* HWRTDIOutput = RaytracingScene->RaytracingOutputPtr.get();
    if (!HWRTDIOutput)
        return false;

    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "HWRTDirectLighting", Vector4(0.8f, 0.2f, 0.0f, 1.0f));
    SCOPE_CPU_PROFILE(HWRTDirectLighting);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, HWRTDirectLighting);

    auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

    std::vector<jRaytracingPipelineShader> RaytracingShaders;
    {
        jRaytracingPipelineShader NewShader;
        jShaderInfo ShaderInfo;

        ShaderInfo.SetName(jNameStatic("HWRTDI_Miss"));
        ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/HWRT_DI.hlsl"));
        ShaderInfo.SetEntryPoint(jNameStatic("PrimaryMissShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_MISS);
        NewShader.MissShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.MissEntryPoint = TEXT("PrimaryMissShader");

        ShaderInfo.SetName(jNameStatic("HWRTDI_Raygen"));
        ShaderInfo.SetEntryPoint(jNameStatic("RaygenShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_RAYGEN);
        NewShader.RaygenShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.RaygenEntryPoint = TEXT("RaygenShader");

        ShaderInfo.SetName(jNameStatic("HWRTDI_ClosestHit"));
        ShaderInfo.SetEntryPoint(jNameStatic("PrimaryClosestHitShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT);
        NewShader.ClosestHitShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.ClosestHitEntryPoint = TEXT("PrimaryClosestHitShader");

        ShaderInfo.SetName(jNameStatic("HWRTDI_AnyHit"));
        ShaderInfo.SetEntryPoint(jNameStatic("PrimaryAnyHitShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_ANYHIT);
        NewShader.AnyHitShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.AnyHitEntryPoint = TEXT("PrimaryAnyHitShader");

        NewShader.HitGroupName = TEXT("DefaultHit");
        RaytracingShaders.push_back(NewShader);
    }
    {
        jRaytracingPipelineShader NewShader;
        jShaderInfo ShaderInfo;
        ShaderInfo.SetName(jNameStatic("HWRTDI_ShadowMiss"));
        ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/HWRT_DI.hlsl"));
        ShaderInfo.SetEntryPoint(jNameStatic("ShadowMissShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_MISS);
        NewShader.MissShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.MissEntryPoint = TEXT("ShadowMissShader");

        ShaderInfo.SetName(jNameStatic("HWRTDI_ShadowClosestHit"));
        ShaderInfo.SetEntryPoint(jNameStatic("ShadowClosestHitShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT);
        NewShader.ClosestHitShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.ClosestHitEntryPoint = TEXT("ShadowClosestHitShader");

        ShaderInfo.SetName(jNameStatic("HWRTDI_ShadowAnyHit"));
        ShaderInfo.SetEntryPoint(jNameStatic("ShadowAnyHitShader"));
        ShaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_ANYHIT);
        NewShader.AnyHitShader = g_rhi->CreateShader(ShaderInfo);
        NewShader.AnyHitEntryPoint = TEXT("ShadowAnyHitShader");
        NewShader.HitGroupName = TEXT("ShadowHit");

        RaytracingShaders.push_back(NewShader);
    }

    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

    ShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jBufferResource>(RaytracingScene->TLASBufferPtr.get()), true));
    ShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jTextureResource>(HWRTDIOutput, nullptr), false));

    jHWRTDISceneConstantBuffer SceneCB;
    SceneCB.ProjectionToWorld = jCamera::GetMainCamera()->GetInverseViewProjectionMatrix();
    SceneCB.CameraPosition = jCamera::GetMainCamera()->Pos;
    SceneCB.DebugViewMode = (gOptions.HWRTDebugViewMode >= 0) ? (uint32)gOptions.HWRTDebugViewMode : 0u;
    SceneCB.DebugLineWidth = (gOptions.HWRTDebugLineWidth > 0.0005f) ? gOptions.HWRTDebugLineWidth : 0.0005f;
    SceneCB.DebugUVScale = (gOptions.HWRTDebugUVScale > 1.0f) ? gOptions.HWRTDebugUVScale : 1.0f;
    SceneCB.DebugPrimitiveIDScale = (gOptions.HWRTDebugPrimitiveIDScale > 0.1f) ? gOptions.HWRTDebugPrimitiveIDScale : 0.1f;
    SceneCB.ForceMipLevel0 = gOptions.HWRTForceMipLevel0 ? 1u : 0u;
    SceneCB.NormalBias = (gOptions.HWRTNormalBias > 0.0f) ? gOptions.HWRTNormalBias : 0.0f;
    SceneCB.ShadowRayStartOffset = (gOptions.HWRTShadowRayStartOffset > 0.0f) ? gOptions.HWRTShadowRayStartOffset : 0.0f;

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
    std::vector<const IUniformBufferBlock*> DirectionalLightBuffers;
    std::vector<const IUniformBufferBlock*> PointLightBuffers;
    std::vector<const IUniformBufferBlock*> SpotLightBuffers;
    std::vector<std::shared_ptr<IUniformBufferBlock>> RefCountMaintainer;

    const auto CreateOneFrameUniformBuffer = [&](jName Name, const void* Data, uint32 Size)
    {
        auto UniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(Name, jLifeTimeType::OneFrame, Size));
        UniformBuffer->UpdateBufferData(Data, Size);
        RefCountMaintainer.push_back(UniformBuffer);
        return UniformBuffer;
    };

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
        check(Material);

        jHWRTDIMaterialInstanceUniform MaterialUniform;
        if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Albedo].Texture)
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_HasAlbedoTexture;
        if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Normal].Texture)
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_HasNormalTexture;
        if (Material->TexData[(int32)jMaterial::EMaterialTextureType::Metallic].Texture)
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_HasRMTexture;
        if (Material->IsUseSRGBAlbedoTexture())
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_UseSRGBAlbedoTexture;
        if (Material->IsUseSphericalMap())
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_IsSkyMaterial;
        const bool UseAlphaCutout = Material->HasAlbedoTexture() && Material->IsRaytracingAlphaTestEnabled();
        if (UseAlphaCutout)
        {
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_UseAlphaCutout;
            MaterialUniform.MaterialFlags |= HWRTDI_MaterialFlag_NonOpaqueGeometry;
        }
        MaterialUniform.AlphaCutoff = Clamp(Material->RaytracingAlphaCutoff, 0.0f, 1.0f);

        const jSamplerStateInfo* AlbedoSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Albedo);
        const jSamplerStateInfo* NormalSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Normal);
        const jSamplerStateInfo* RMSamplerState = Material->GetTextureSamplerState(jMaterial::EMaterialTextureType::Metallic);
        MaterialUniform.AlbedoSamplerIndex = GetOrAddSamplerIndex(AlbedoSamplerStates, AlbedoSamplerIndexMap, AlbedoSamplerState);
        MaterialUniform.NormalSamplerIndex = GetOrAddSamplerIndex(NormalSamplerStates, NormalSamplerIndexMap, NormalSamplerState);
        MaterialUniform.RMSamplerIndex = GetOrAddSamplerIndex(RMSamplerStates, RMSamplerIndexMap, RMSamplerState);

        auto MaterialUniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_MaterialInstance"), &MaterialUniform, sizeof(MaterialUniform));
        MaterialInstanceBuffers.push_back(MaterialUniformBuffer.get());

        AlbedoTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Albedo), nullptr, 0 });
        NormalTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Normal), nullptr, 0 });
        RMTextures.push_back({ Material->GetTexture(jMaterial::EMaterialTextureType::Metallic), nullptr, 0 });
    }

    uint32 NumDirectionalLights = 0;
    uint32 NumPointLights = 0;
    uint32 NumSpotLights = 0;
    for (jLight* Light : jLight::GetLights())
    {
        if (!Light)
            continue;

        switch (Light->Type)
        {
        case ELightType::DIRECTIONAL:
        {
            const auto* DirectionalLight = static_cast<jDirectionalLight*>(Light);
            auto UniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_DirectionalLight"), &DirectionalLight->GetLightData(), sizeof(jDirectionalLightUniformBufferData));
            DirectionalLightBuffers.push_back(UniformBuffer.get());
            ++NumDirectionalLights;
            break;
        }
        case ELightType::POINT:
        {
            const auto* PointLight = static_cast<jPointLight*>(Light);
            auto UniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_PointLight"), &PointLight->GetLightData(), sizeof(jPointLightUniformBufferData));
            PointLightBuffers.push_back(UniformBuffer.get());
            ++NumPointLights;
            break;
        }
        case ELightType::SPOT:
        {
            const auto* SpotLight = static_cast<jSpotLight*>(Light);
            auto UniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_SpotLight"), &SpotLight->GetLightData(), sizeof(jSpotLightUniformBufferData));
            SpotLightBuffers.push_back(UniformBuffer.get());
            ++NumSpotLights;
            break;
        }
        default:
            break;
        }
    }

    if (DirectionalLightBuffers.empty())
    {
        const jDirectionalLightUniformBufferData DummyLightData = {};
        auto UniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_DirectionalLightDummy"), &DummyLightData, sizeof(DummyLightData));
        DirectionalLightBuffers.push_back(UniformBuffer.get());
    }
    if (PointLightBuffers.empty())
    {
        const jPointLightUniformBufferData DummyLightData = {};
        auto UniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_PointLightDummy"), &DummyLightData, sizeof(DummyLightData));
        PointLightBuffers.push_back(UniformBuffer.get());
    }
    if (SpotLightBuffers.empty())
    {
        const jSpotLightUniformBufferData DummyLightData = {};
        auto UniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_SpotLightDummy"), &DummyLightData, sizeof(DummyLightData));
        SpotLightBuffers.push_back(UniformBuffer.get());
    }

    SceneCB.NumDirectionalLights = NumDirectionalLights;
    SceneCB.NumPointLights = NumPointLights;
    SceneCB.NumSpotLights = NumSpotLights;

    auto SceneUniformBuffer = CreateOneFrameUniformBuffer(jNameStatic("HWRTDI_SceneData"), &SceneCB, sizeof(SceneCB));
    ShaderBindingArray.Add(jShaderBinding::Create(2, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jUniformBufferResource>(SceneUniformBuffer.get()), true));

    const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
        , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT
        , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();
    ShaderBindingArray.Add(jShaderBinding::Create(3, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jSamplerResource>(SamplerState)));

    jTexture* EnvTexture = jSceneRenderTarget::CubeEnvMap2 ? jSceneRenderTarget::CubeEnvMap2 : GWhiteCubeTexture.get();
    ShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jTextureResource>(EnvTexture, nullptr)));

    std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

    jShaderBindingArray BindlessShaderBindingArray[14];
    BindlessShaderBindingArray[0].Add(jShaderBinding::CreateBindless(0, (uint32)VertexAndIndexOffsetBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(VertexAndIndexOffsetBuffers), false));
    BindlessShaderBindingArray[1].Add(jShaderBinding::CreateBindless(0, (uint32)IndexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(IndexBuffers), false));
    BindlessShaderBindingArray[2].Add(jShaderBinding::CreateBindless(0, (uint32)RenderObjectBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(RenderObjectBuffers), false));
    BindlessShaderBindingArray[3].Add(jShaderBinding::CreateBindless(0, (uint32)VertexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jBufferResourceBindless>(VertexBuffers), false));
    BindlessShaderBindingArray[4].Add(jShaderBinding::CreateBindless(0, (uint32)MaterialInstanceBuffers.size(), EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jUniformBufferResourceBindless>(MaterialInstanceBuffers)));
    BindlessShaderBindingArray[5].Add(jShaderBinding::CreateBindless(0, (uint32)AlbedoTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jTextureResourceBindless>(AlbedoTextures)));
    BindlessShaderBindingArray[6].Add(jShaderBinding::CreateBindless(0, (uint32)NormalTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jTextureResourceBindless>(NormalTextures)));
    BindlessShaderBindingArray[7].Add(jShaderBinding::CreateBindless(0, (uint32)RMTextures.size(), EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jTextureResourceBindless>(RMTextures)));
    BindlessShaderBindingArray[8].Add(jShaderBinding::CreateBindless(0, (uint32)AlbedoSamplerStates.size(), EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jSamplerResourceBindless>(AlbedoSamplerStates)));
    BindlessShaderBindingArray[9].Add(jShaderBinding::CreateBindless(0, (uint32)NormalSamplerStates.size(), EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jSamplerResourceBindless>(NormalSamplerStates)));
    BindlessShaderBindingArray[10].Add(jShaderBinding::CreateBindless(0, (uint32)RMSamplerStates.size(), EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jSamplerResourceBindless>(RMSamplerStates)));
    BindlessShaderBindingArray[11].Add(jShaderBinding::CreateBindless(0, (uint32)DirectionalLightBuffers.size(), EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jUniformBufferResourceBindless>(DirectionalLightBuffers)));
    BindlessShaderBindingArray[12].Add(jShaderBinding::CreateBindless(0, (uint32)PointLightBuffers.size(), EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jUniformBufferResourceBindless>(PointLightBuffers)));
    BindlessShaderBindingArray[13].Add(jShaderBinding::CreateBindless(0, (uint32)SpotLightBuffers.size(), EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING
        , ResourceInlineAllocator.Alloc<jUniformBufferResourceBindless>(SpotLightBuffers)));

    std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstanceBindless[_countof(BindlessShaderBindingArray)];
    for (int32 i = 0; i < _countof(BindlessShaderBindingArray); ++i)
    {
        GlobalShaderBindingInstanceBindless[i] = g_rhi->CreateShaderBindingInstance(BindlessShaderBindingArray[i], jShaderBindingInstanceType::SingleFrame);
    }

    jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
    GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);
    for (int32 i = 0; i < _countof(GlobalShaderBindingInstanceBindless); ++i)
    {
        GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstanceBindless[i]->ShaderBindingsLayouts);
    }

    jRaytracingPipelineData RaytracingPipelineData;
    RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);
    RaytracingPipelineData.MaxPayloadSize = sizeof(Vector4);
    RaytracingPipelineData.MaxTraceRecursionDepth = 2;
    auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData, GlobalShaderBindingLayoutArray, nullptr);

    jShaderBindingInstanceArray ShaderBindingInstanceArray;
    ShaderBindingInstanceArray.Add(GlobalShaderBindingInstance.get());
    for (int32 i = 0; i < _countof(GlobalShaderBindingInstanceBindless); ++i)
    {
        ShaderBindingInstanceArray.Add(GlobalShaderBindingInstanceBindless[i].get());
    }

    jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
    for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
    {
        ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
        const std::vector<uint32>* DynamicOffsets = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
        if (DynamicOffsets && DynamicOffsets->size())
        {
            ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
        }
    }
    ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;
    g_rhi->BindRaytracingShaderBindingInstances(CmdBuffer, RaytracingPipelineState, ShaderBindingInstanceCombiner, 0);
    RaytracingPipelineState->Bind(RenderFrameContextPtr);

    g_rhi->TransitionLayout(CmdBuffer, HWRTDIOutput, EResourceLayout::UAV);

    jRaytracingDispatchData TracingData;
    TracingData.Width = SCR_WIDTH;
    TracingData.Height = SCR_HEIGHT;
    TracingData.Depth = 1;
    TracingData.PipelineState = RaytracingPipelineState;
    g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

    g_rhi->TransitionLayout(CmdBuffer, HWRTDIOutput, EResourceLayout::SHADER_READ_ONLY);

    jRHIUtil::DrawQuad(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr, { 0, 0, SCR_WIDTH, SCR_HEIGHT }
        , [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllocator)
        {
            g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), HWRTDIOutput, EResourceLayout::SHADER_READ_ONLY);

            const jSamplerStateInfo* CopySamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            InOutShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
                , InOutResourceInlineAllocator.Alloc<jTextureResource>(HWRTDIOutput, CopySamplerState)));
        }
        , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
        {
            jShaderInfo ShaderInfo;
            ShaderInfo.SetName(jNameStatic("HWRTDI_CopyPS"));
            ShaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
            ShaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            return g_rhi->CreateShader(ShaderInfo);
        });

    return true;
}
