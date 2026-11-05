#include "pch.h"
#include "jRenderer_PathTracing.h"
#include "RHI/jRaytracingScene.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jRHI.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Material/jMaterial.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jPathTracingLight.h"
#include "jOptions.h"
#include "PathTracingDataLoader/jPathTracingData.h"
#include "FileLoader/jImageFileLoader.h"
#include "Shader/jShader.h"
#include "Shader/jShaderParameterSet.h"
#ifdef _WIN32
#include "RHI/DX12/jPipelineStateInfo_DX12.h"
#include "RHI/DX12/jRHI_DX12.h"
#endif

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jPathTracingSceneConstantBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, projectionToWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, cameraPosition)
    SHADER_UNIFORM_BUFFER_MEMBER(float, focalDistance)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, cameraDirection)
    SHADER_UNIFORM_BUFFER_MEMBER(float, lensRadius)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, AccumulateNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, HaltonJitter)
    SHADER_UNIFORM_BUFFER_MEMBER(float, RayMaxDistance)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, UseEnvironmentLight)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jPathTracingGlobalParameters)
    SHADER_ACCELERATION_STRUCTURE(Scene)
    SHADER_RW_TEXTURE2D(RenderTarget)
    SHADER_UNIFORM_BUFFER(jPathTracingSceneConstantBuffer, g_sceneCB)
    SHADER_SAMPLER(DefaultSamplerState)
    SHADER_TEXTURECUBE_SRV(EnvTexture)
END_SHADER_PARAMETER_SET()

    struct jPathTracingBindlessUInt2
    {
    };

    template <>
    struct TShaderParameterHLSLTypeInfo<jPathTracingBindlessUInt2>
    {
        static constexpr const char* GetTypeName() { return "uint2"; }
        static void AppendTypeDeclaration(std::string&) {}
    };

BEGIN_SHADER_STRUCT(MaterialUniformBuffer)
    SHADER_STRUCT_MEMBER(Vector, baseColor)
    SHADER_STRUCT_MEMBER(float, anisotropic)
    SHADER_STRUCT_MEMBER(Vector, emission)
    SHADER_STRUCT_MEMBER(int32, lightId)
    SHADER_STRUCT_MEMBER(float, metallic)
    SHADER_STRUCT_MEMBER(float, roughness)
    SHADER_STRUCT_MEMBER(float, subsurface)
    SHADER_STRUCT_MEMBER(float, specularTint)
    SHADER_STRUCT_MEMBER(float, sheen)
    SHADER_STRUCT_MEMBER(float, sheenTint)
    SHADER_STRUCT_MEMBER(float, clearcoat)
    SHADER_STRUCT_MEMBER(float, clearcoatGloss)
    SHADER_STRUCT_MEMBER(float, specTrans)
    SHADER_STRUCT_MEMBER(float, ior)
    SHADER_STRUCT_MEMBER(float, mediumType)
    SHADER_STRUCT_MEMBER(float, mediumDensity)
    SHADER_STRUCT_MEMBER(Vector, mediumColor)
    SHADER_STRUCT_MEMBER(float, mediumAnisotropy)
    SHADER_STRUCT_MEMBER(int32, baseColorTexId)
    SHADER_STRUCT_MEMBER(int32, metallicRoughnessTexID)
    SHADER_STRUCT_MEMBER(int32, normalmapTexID)
    SHADER_STRUCT_MEMBER(int32, emissionmapTexID)
    SHADER_STRUCT_MEMBER(float, opacity)
    SHADER_STRUCT_MEMBER(float, alphaMode)
    SHADER_STRUCT_MEMBER(float, alphaCutoff)
    SHADER_STRUCT_MEMBER(float, padding2)
END_SHADER_STRUCT()

BEGIN_SHADER_STRUCT(LightUniformBuffer)
    SHADER_STRUCT_MEMBER(Vector, position)
    SHADER_STRUCT_MEMBER(float, radius)
    SHADER_STRUCT_MEMBER(Vector, emission)
    SHADER_STRUCT_MEMBER(float, area)
    SHADER_STRUCT_MEMBER(Vector, u)
    SHADER_STRUCT_MEMBER(int32, type)
    SHADER_STRUCT_MEMBER(Vector, v)
END_SHADER_STRUCT()

BEGIN_SHADER_BINDLESS_SET(jPathTracingBindlessParameters)
    // Bindless tables are assigned consecutive spaces starting at the binder's current space.
    SHADER_BINDLESS_STRUCTURED_BUFFER(jPathTracingBindlessUInt2, VertexIndexOffsetArray)
    SHADER_BINDLESS_BUFFER(uint32, IndexBindlessArray)
    SHADER_BINDLESS_STRUCTURED_BUFFER(RenderObjectUniformBuffer, RenderObjParamArray)
    SHADER_BINDLESS_BYTEADDRESS_BUFFER(VerticesBindlessArray)
    SHADER_BINDLESS_UNIFORM_BUFFER(MaterialUniformBuffer, MaterialBindlessArray)
    SHADER_BINDLESS_UNIFORM_BUFFER(LightUniformBuffer, LightBindlessArray)
    SHADER_BINDLESS_TEXTURE2D(TextureBindlessArray)
    SHADER_BINDLESS_TEXTURE2D(AuthoredAlbedoArray)
    SHADER_BINDLESS_TEXTURE2D(AuthoredNormalArray)
END_SHADER_BINDLESS_SET()

namespace
{
    struct jPathTracingRaytracingShaderBase : public jShader
    {
        using jShader::jShader;

        DECLARE_SHADER_PARAMETER_SETS(jPathTracingGlobalParameters)

        DECLARE_DEFINE(USE_BINDLESS_RESOURCE, 0, 1);

        using ShaderPermutation = jPermutation<USE_BINDLESS_RESOURCE>;
        ShaderPermutation Permutation;

        static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation)
        {
            if (InPermutation.Get<USE_BINDLESS_RESOURCE>() != 0)
                InOutBinder.AddBindless<jPathTracingBindlessParameters>();
        }
    };

    struct jShaderPathTracingMeshMissShader : public jPathTracingRaytracingShaderBase
    {
        DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderPathTracingMeshMissShader, jPathTracingRaytracingShaderBase, Permutation)
    };

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPathTracingMeshMissShader
        , "Miss"
        , "Resource/Shaders/hlsl/PathTracing.hlsl"
        , ""
        , "MeshMissShader"
        , EShaderAccessStageFlag::RAYTRACING_MISS)

    struct jShaderPathTracingRaygenShader : public jPathTracingRaytracingShaderBase
    {
        DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderPathTracingRaygenShader, jPathTracingRaytracingShaderBase, Permutation)
    };

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPathTracingRaygenShader
        , "Raygen"
        , "Resource/Shaders/hlsl/PathTracing.hlsl"
        , ""
        , "RaygenShader"
        , EShaderAccessStageFlag::RAYTRACING_RAYGEN)

    struct jShaderPathTracingMeshClosestHitShader : public jPathTracingRaytracingShaderBase
    {
        DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderPathTracingMeshClosestHitShader, jPathTracingRaytracingShaderBase, Permutation)
    };

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPathTracingMeshClosestHitShader
        , "ClosestHit"
        , "Resource/Shaders/hlsl/PathTracing.hlsl"
        , ""
        , "MeshClosestHitShader"
        , EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT)

    struct jShaderPathTracingLightClosestHitShader : public jPathTracingRaytracingShaderBase
    {
        DECLARE_SHADER_WITH_PERMUTATION_EX(jShaderPathTracingLightClosestHitShader, jPathTracingRaytracingShaderBase, Permutation)
    };

    IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPathTracingLightClosestHitShader
        , "ClosestHit"
        , "Resource/Shaders/hlsl/PathTracing.hlsl"
        , ""
        , "LightClosestHitShader"
        , EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT)
}

void jRenderer_PathTracing::Setup()
{

}

void jRenderer_PathTracing::Render()
{
	SCOPE_CPU_PROFILE(Render);

	{
		SCOPE_CPU_PROFILE(PoolReset);
		check(RenderFrameContextPtr->GetActiveCommandBuffer());
		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			if (g_rhi->GetQueryTimePool((ECommandBufferType)i))
			{
				g_rhi->GetQueryTimePool((ECommandBufferType)i)->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
			}
		}
		if (g_rhi->GetQueryOcclusionPool())
		{
			g_rhi->GetQueryOcclusionPool()->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
		}

		// Vulkan need to queue submmit to reset query pool, and replace CurrentSemaphore with GraphicQueueSubmitSemaphore
		RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
		RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();

		//ShadowpassOcclusionTest.Init();
		//BasepassOcclusionTest.Init();
	}

	Setup();
	PathTracing();
	DebugPasses();
	UIPass();
}

void jRenderer_PathTracing::PathTracing()
{
    int32 width = SCR_WIDTH;
    int32 height = SCR_HEIGHT;
#ifdef _WIN32
    if (IsUseDX12() && static_cast<const jRHI_DX12*>(g_rhi)->IsUsingWarp())
    {
        const float scale = std::min({1.f, 480.f / width, 272.f / height});
        width = std::max(1, static_cast<int32>(width * scale));
        height = std::max(1, static_cast<int32>(height * scale));
    }
#endif
    auto& output = RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr;
    const bool recreateOutput = !output || output->Width != width || output->Height != height || output->Format != ETextureFormat::RGBA32F;
	if (recreateOutput)
	{
		output = g_rhi->Create2DTexture(width, height, 1, 1
			, ETextureFormat::RGBA32F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
	}

	jTexture* PathTracingOutput = output.get();
	check(PathTracingOutput);

	{
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "PathTracing", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(PathTracing);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, PathTracing);

		auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

		// Create RaytracingShaders
		std::vector<jRaytracingPipelineShader> RaytracingShaders;
		{
            jPathTracingRaytracingShaderBase::ShaderPermutation RaytracingPermutation;
            RaytracingPermutation.SetIndex<jPathTracingRaytracingShaderBase::USE_BINDLESS_RESOURCE>(1);

			{
				jRaytracingPipelineShader NewShader;
				jShaderInfo shaderInfo;

                shaderInfo = jShaderPathTracingMeshMissShader::GShaderInfo;
                shaderInfo.SetPermutationId(RaytracingPermutation.GetPermutationId());
				shaderInfo.AddPreProcessor("MAX_RECURSION_DEPTH", std::to_string(gOptions.MaxRecursionDepthForPathTracing).c_str());
				shaderInfo.AddPreProcessor("MAX_RAY_PER_PIXEL", std::to_string(gOptions.RayPerPixelForPathTracing).c_str());
                ApplyShaderInfoCustomization<jShaderPathTracingMeshMissShader>(shaderInfo);

				// First hit group for mesh
				NewShader.MissShader = g_rhi->CreateShader<jShaderPathTracingMeshMissShader>(shaderInfo);
				NewShader.MissEntryPoint = TEXT("MeshMissShader");

                shaderInfo = jShaderPathTracingRaygenShader::GShaderInfo;
                shaderInfo.SetPermutationId(RaytracingPermutation.GetPermutationId());
				shaderInfo.AddPreProcessor("MAX_RECURSION_DEPTH", std::to_string(gOptions.MaxRecursionDepthForPathTracing).c_str());
				shaderInfo.AddPreProcessor("MAX_RAY_PER_PIXEL", std::to_string(gOptions.RayPerPixelForPathTracing).c_str());
                ApplyShaderInfoCustomization<jShaderPathTracingRaygenShader>(shaderInfo);
				NewShader.RaygenShader = g_rhi->CreateShader<jShaderPathTracingRaygenShader>(shaderInfo);
				NewShader.RaygenEntryPoint = TEXT("RaygenShader");

                shaderInfo = jShaderPathTracingMeshClosestHitShader::GShaderInfo;
                shaderInfo.SetPermutationId(RaytracingPermutation.GetPermutationId());
				shaderInfo.AddPreProcessor("MAX_RECURSION_DEPTH", std::to_string(gOptions.MaxRecursionDepthForPathTracing).c_str());
				shaderInfo.AddPreProcessor("MAX_RAY_PER_PIXEL", std::to_string(gOptions.RayPerPixelForPathTracing).c_str());
                ApplyShaderInfoCustomization<jShaderPathTracingMeshClosestHitShader>(shaderInfo);
				NewShader.ClosestHitShader = g_rhi->CreateShader<jShaderPathTracingMeshClosestHitShader>(shaderInfo);
				NewShader.ClosestHitEntryPoint = TEXT("MeshClosestHitShader");

				NewShader.HitGroupName = TEXT("DefaultHit");

				RaytracingShaders.push_back(NewShader);
			}
			{
				jRaytracingPipelineShader NewShader;
				jShaderInfo shaderInfo;
                shaderInfo = jShaderPathTracingLightClosestHitShader::GShaderInfo;
                shaderInfo.SetPermutationId(RaytracingPermutation.GetPermutationId());
                shaderInfo.AddPreProcessor("MAX_RECURSION_DEPTH", std::to_string(gOptions.MaxRecursionDepthForPathTracing).c_str());
				shaderInfo.AddPreProcessor("MAX_RAY_PER_PIXEL", std::to_string(gOptions.RayPerPixelForPathTracing).c_str());
                ApplyShaderInfoCustomization<jShaderPathTracingLightClosestHitShader>(shaderInfo);

				// Second hit gorup for light
				NewShader.ClosestHitShader = g_rhi->CreateShader<jShaderPathTracingLightClosestHitShader>(shaderInfo);
				NewShader.ClosestHitEntryPoint = TEXT("LightClosestHitShader");

				NewShader.HitGroupName = TEXT("LightHit");

				RaytracingShaders.push_back(NewShader);
			}
		}

		jShaderBindingArray ShaderBindingArray;
		jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

		jPathTracingSceneConstantBuffer sceneCB;
		auto mainCamera = jCamera::GetMainCamera();
		sceneCB.cameraPosition = mainCamera->Pos;
		sceneCB.projectionToWorld = mainCamera->GetInverseViewProjectionMatrix();
		sceneCB.focalDistance = mainCamera->FocalDist;
		sceneCB.lensRadius = mainCamera->Aperture;
		sceneCB.FrameNumber = g_rhi->GetCurrentFrameNumber();
        sceneCB.RayMaxDistance = std::max(1000.f, mainCamera->Far);
        // Model scenes have no legacy analytic emitters. Use the already-bound sky probe.
        sceneCB.UseEnvironmentLight = !gPathTracingScene || gPathTracingScene->lights.empty();

		static jOptions OldOptions = gOptions;
		static auto OldMatrix = sceneCB.projectionToWorld;
		static uint32 AccumulateNumber = 0;
        static int32 LastSelectedIndex = gSelectedSceneIndex;
		if (recreateOutput || !gOptions.UseAccumulateRay
			|| OldMatrix != sceneCB.projectionToWorld 
			|| OldOptions != gOptions 
			|| gSelectedSceneIndex != LastSelectedIndex)
		{
			LastSelectedIndex = gSelectedSceneIndex;
			OldMatrix = sceneCB.projectionToWorld;
			memcpy(&OldOptions, &gOptions, sizeof(gOptions));
			AccumulateNumber = 0;
		}
		else
		{
			++AccumulateNumber;
		}
		sceneCB.AccumulateNumber = AccumulateNumber;

		static const Vector2 HaltonJitter[] = {
			Vector2(0.0f,      -0.333334f),
			Vector2(-0.5f,     0.333334f),
			Vector2(0.5f,      -0.777778f),
			Vector2(-0.75f,    -0.111112f),
			Vector2(0.25f,     0.555556f),
			Vector2(-0.25f,    -0.555556f),
			Vector2(0.75f,     0.111112f),
			Vector2(-0.875f,   0.777778f),
			Vector2(0.125f,    -0.925926f),
			Vector2(-0.375f,   -0.259260f),
			Vector2(0.625f,    0.407408f),
			Vector2(-0.625f,   -0.703704f),
			Vector2(0.375f,    -0.037038f),
			Vector2(-0.125f,   0.629630f),
			Vector2(0.875f,    -0.481482f),
			Vector2(-0.9375f,  0.185186f)
		};

		sceneCB.HaltonJitter = HaltonJitter[g_rhi->GetCurrentFrameNumber() % _countof(HaltonJitter)] / Vector2((float)width, (float)height);

		auto SceneUniformBufferPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("SceneData"), jLifeTimeType::OneFrame, sizeof(sceneCB));
		SceneUniformBufferPtr->UpdateBufferData(&sceneCB, sizeof(sceneCB));

		const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
			, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
			, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

		jSceneRenderTarget::CubeEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_cubemp.dds")).lock().get();
		jPathTracingGlobalParameters GlobalParameters;
		GlobalParameters.Scene.Buffer = RenderFrameContextPtr->RaytracingScene->TLASBufferPtr.get();
		GlobalParameters.RenderTarget.Texture = PathTracingOutput;
		GlobalParameters.g_sceneCB.Buffer = SceneUniformBufferPtr;
		GlobalParameters.DefaultSamplerState.SamplerState = SamplerState;
		GlobalParameters.EnvTexture.Texture = jSceneRenderTarget::CubeEnvMap2;
		jShaderParameterSet::BuildShaderBindings(GlobalParameters, EShaderAccessStageFlag::ALL_RAYTRACING, ShaderBindingArray, ResourceInlineAllactor);

		// Create ShaderBindingLayout and ShaderBindingInstance Instance for this draw call
		std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance;
		GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
		jShaderBindingInstanceGroup ShaderBindingGroup;
		ShaderBindingGroup.Add(GlobalShaderBindingInstance);

		// Bindless
		std::vector<const jBuffer*> VertexAndInexOffsetBuffers;
		std::vector<const jBuffer*> IndexBuffers;
		std::vector<const jBuffer*> TestUniformBuffers;
		std::vector<const jBuffer*> VertexBuffers;
		std::vector<const IUniformBufferBlock*> MaterialBuffers;
		std::vector<const IUniformBufferBlock*> LightBuffers;
		std::vector<jTextureResourceBindless::jTextureBindData> Textures;
        std::map<jTexture*, int32> textureIds;
        if (gPathTracingScene)
            for (auto* texture : gPathTracingScene->textures)
            {
                textureIds.emplace(texture, int32(Textures.size()));
                Textures.push_back({texture, nullptr, 0});
            }
        const auto textureIndex = [&](const jMaterial& material, jMaterial::EMaterialTextureType slot)
        {
            auto* texture = material.TexData[int(slot)].Texture;
            if (!texture) return -1;
            const auto [it, inserted] = textureIds.emplace(texture, int32(Textures.size()));
            if (inserted) Textures.push_back({texture, nullptr, 0});
            return it->second;
        };
		std::vector<jTextureResourceBindless::jTextureBindData> AuthoredAlbedo, AuthoredNormal;
		std::vector<std::shared_ptr<IUniformBufferBlock>> RefCountMaintainer;

		{
			const int32 NumOfStaticRenderObjects = (int32)g_rhi->RaytracingScene->InstanceList.size();
			VertexAndInexOffsetBuffers.reserve(NumOfStaticRenderObjects);
			IndexBuffers.reserve(NumOfStaticRenderObjects);
			TestUniformBuffers.reserve(NumOfStaticRenderObjects);
			VertexBuffers.reserve(NumOfStaticRenderObjects);
			MaterialBuffers.reserve(NumOfStaticRenderObjects);

			for (int32 i = 0; i < NumOfStaticRenderObjects; ++i)
			{
				jRenderObject* RObj = g_rhi->RaytracingScene->InstanceList[i];
				RObj->CreateShaderBindingInstance();

				VertexAndInexOffsetBuffers.push_back(RObj->VertexAndIndexOffsetBuffer.get());
				IndexBuffers.push_back(RObj->GeometryDataPtr->IndexBufferPtr->GetBuffer());
				TestUniformBuffers.push_back(RObj->TestUniformBuffer.get());
				VertexBuffers.push_back(RObj->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

				check(RObj->MaterialPtr);


				MaterialUniformBuffer fallback{};
                fallback.baseColor = Vector(1.0f); fallback.roughness = 0.5f; fallback.ior = 1.5f;
                fallback.lightId = -1; fallback.baseColorTexId = fallback.metallicRoughnessTexID = -1;
                fallback.normalmapTexID = fallback.emissionmapTexID = -1; fallback.opacity = 1.0f;
                const auto& material = *RObj->MaterialPtr;
                const auto& payload = material.MaterialDataPtr;
                const bool hasLegacyPayload = payload && payload->GetDataSizeInBytes() == sizeof(MaterialUniformBuffer);
                if (hasLegacyPayload)
                    std::memcpy(&fallback, payload->GetData(), sizeof(MaterialUniformBuffer));
                fallback.padding2 = 0.f;
                if (const auto& surface = material.Surface)
                {
                    using Slot = jMaterial::EMaterialTextureType;
                    fallback.baseColor = Vector(material.BaseColorOpacity);
                    fallback.opacity = material.BaseColorOpacity.w;
                    fallback.emission = material.Emissive;
                    fallback.metallic = material.MetallicFactor;
                    fallback.roughness = material.RoughnessFactor;
                    fallback.specTrans = surface->Transmission;
                    fallback.ior = surface->Ior;
                    fallback.clearcoat = surface->ClearCoat;
                    fallback.clearcoatGloss = surface->ClearCoatGloss;
                    fallback.subsurface = surface->Subsurface;
                    fallback.anisotropic = surface->Anisotropy;
                    fallback.specularTint = surface->SpecularTint;
                    fallback.sheen = surface->Sheen;
                    fallback.sheenTint = surface->SheenTint;
                    fallback.alphaMode = float(surface->AlphaMode);
                    fallback.alphaCutoff = surface->AlphaCutoff;
                    fallback.baseColorTexId = textureIndex(material, Slot::Albedo);
                    fallback.normalmapTexID = textureIndex(material, Slot::Normal);
                    fallback.metallicRoughnessTexID = textureIndex(material, Slot::Metallic);
                    fallback.emissionmapTexID = textureIndex(material, Slot::Emission);
                    fallback.padding2 = surface->LegacyPackedMap ? 2.f : 1.f;
                }
                else if (!hasLegacyPayload)
                {
                    using Slot = jMaterial::EMaterialTextureType;
                    fallback.baseColor = material.UseMaterialProperties ? Vector(material.BaseColorOpacity) : Vector(1.f);
                    fallback.opacity = material.UseMaterialProperties ? material.BaseColorOpacity.w : 1.f;
                    fallback.emission = material.UseMaterialProperties ? material.Emissive : Vector(0.f);
                    fallback.baseColorTexId = textureIndex(material, Slot::Albedo);
                    fallback.normalmapTexID = textureIndex(material, Slot::Normal);
                    fallback.metallicRoughnessTexID = textureIndex(material, Slot::Metallic);
                    fallback.emissionmapTexID = textureIndex(material, Slot::Emission);
                    // Native raster imports read metallic B / roughness G directly;
                    // runtime factors override them, while untextured slots use gOptions.
                    const bool hasPackedMap = fallback.metallicRoughnessTexID >= 0;
                    fallback.metallic = material.UseMaterialProperties ? material.MetallicFactor
                        : (hasPackedMap ? 1.f : gOptions.Metallic);
                    fallback.roughness = material.UseMaterialProperties ? material.RoughnessFactor
                        : (hasPackedMap ? 1.f : gOptions.Roughness);
                    fallback.alphaMode = material.IsRaytracingAlphaTestEnabled() ? 1.f : 2.f;
                    fallback.alphaCutoff = material.RaytracingAlphaCutoff;
                    fallback.padding2 = 1.f;
                }
                auto MaterialUniformParametersPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("jMaterialUniformBuffer"), jLifeTimeType::OneFrame, sizeof(MaterialUniformBuffer));
                MaterialUniformParametersPtr->UpdateBufferData(&fallback, sizeof(MaterialUniformBuffer));
                AuthoredAlbedo.push_back({ RObj->MaterialPtr->GetTexture(jMaterial::EMaterialTextureType::Albedo), nullptr, 0 });
                AuthoredNormal.push_back({ RObj->MaterialPtr->GetTexture(jMaterial::EMaterialTextureType::Normal), nullptr, 0 });
				MaterialBuffers.push_back(MaterialUniformParametersPtr.get());
				RefCountMaintainer.push_back(MaterialUniformParametersPtr);
			}

			// Gather PathTracing lights
			std::vector<jPathTracingLight*> PathTracingLights;
			PathTracingLights.reserve(jLight::GetLights().size());
			for (auto light : jLight::GetLights())
			{
				if (light->Type == ELightType::PATH_TRACING)
					PathTracingLights.push_back((jPathTracingLight*)light);
			}

			// Create OneFrame uniform buffer for PathTracing Lights
			const int32 NumOfPathTracingLights = (int32)PathTracingLights.size();
			for (int32 i = 0; i < (int32)PathTracingLights.size(); ++i)
			{
				const jPathTracingLightUniformBufferData& LightData = PathTracingLights[i]->GetLightData();
				auto LightUniformParametersPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("jLightUniformBuffer"), jLifeTimeType::OneFrame, sizeof(LightData));
				LightUniformParametersPtr->UpdateBufferData(&LightData, sizeof(LightData));
				LightBuffers.push_back(LightUniformParametersPtr.get());
				RefCountMaintainer.push_back(LightUniformParametersPtr);
			}

            if (Textures.empty()) Textures.push_back({GWhiteTexture.get(), nullptr, 0});
            // DX12 descriptor ranges require a nonzero count even when a scene
            // has no analytic lights. No hit group references this dummy entry.
            if (LightBuffers.empty())
            {
                const LightUniformBuffer emptyLight{};
                auto buffer = g_rhi->CreateUniformBufferBlock(jNameStatic("EmptyPathTracingLight"), jLifeTimeType::OneFrame, sizeof(emptyLight));
                buffer->UpdateBufferData(&emptyLight, sizeof(emptyLight));
                LightBuffers.push_back(buffer.get());
                RefCountMaintainer.push_back(std::move(buffer));
            }
		}

        jPathTracingBindlessParameters BindlessParameters;
        BindlessParameters.VertexIndexOffsetArray.Buffers = VertexAndInexOffsetBuffers;
        BindlessParameters.IndexBindlessArray.Buffers = IndexBuffers;
        BindlessParameters.RenderObjParamArray.Buffers = TestUniformBuffers;
        BindlessParameters.VerticesBindlessArray.Buffers = VertexBuffers;
        BindlessParameters.MaterialBindlessArray.Buffers = MaterialBuffers;
        BindlessParameters.LightBindlessArray.Buffers = LightBuffers;
        BindlessParameters.TextureBindlessArray.Textures = Textures;
        BindlessParameters.AuthoredAlbedoArray.Textures = AuthoredAlbedo;
        BindlessParameters.AuthoredNormalArray.Textures = AuthoredNormal;
        std::vector<std::shared_ptr<jShaderBindingInstance>> BindlessShaderBindingInstances =
            jShaderBindlessSet::CreateShaderBindingInstances(BindlessParameters, EShaderAccessStageFlag::ALL_RAYTRACING, jShaderBindingInstanceType::SingleFrame);
		ShaderBindingGroup.Add(BindlessShaderBindingInstances);

		// Create RaytracingPipelineState
		jRaytracingPipelineData RaytracingPipelineData;
		RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);	                // float2 barycentrics

		RaytracingPipelineData.MaxPayloadSize = sizeof(Vector) * 4 + sizeof(uint32) * 3;
		RaytracingPipelineData.MaxTraceRecursionDepth = 31;
		auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData
			, ShaderBindingGroup.GetLayoutArray(), nullptr);
        if (!RaytracingPipelineState) return;
#ifdef _WIN32
        if (IsUseDX12())
        {
            const auto& pipeline = *static_cast<jPipelineStateInfo_DX12*>(RaytracingPipelineState);
            if (!pipeline.RaytracingStateObject || !pipeline.RaygenBuffer || !pipeline.MissBuffer || !pipeline.HitGroupBuffer)
            {
                std::cerr << "Path tracing pipeline or shader table creation failed" << std::endl;
                return;
            }
        }
#endif
		g_rhi->BindRaytracingShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), RaytracingPipelineState, ShaderBindingGroup.GetCombiner(), 0);

		// Binding Raytracing Pipeline State
		RaytracingPipelineState->Bind(RenderFrameContextPtr);

		g_rhi->TransitionLayout(CmdBuffer, PathTracingOutput, EResourceLayout::UAV);

		// Dispatch Rays
		jRaytracingDispatchData TracingData;
		TracingData.Width = width;
		TracingData.Height = height;
		TracingData.Depth = 1;
		TracingData.PipelineState = RaytracingPipelineState;
		g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

#ifdef _WIN32
        const bool traceDiagnostics = GetEnvironmentVariableA("JENGINE_TEST_PATH_TRACING", nullptr, 0) > 0;
#else
        const bool traceDiagnostics = std::getenv("JENGINE_TEST_PATH_TRACING") != nullptr;
#endif
        if (traceDiagnostics)
        {
            static uint32 dispatchCount = 0;
            ++dispatchCount;
            if (dispatchCount == 1 || dispatchCount % 60 == 0)
            {
                const auto& instances = RenderFrameContextPtr->RaytracingScene->InstanceList;
                const auto indexStream = instances.empty() || !instances[0]->GeometryDataPtr->IndexBufferPtr
                    ? nullptr : instances[0]->GeometryDataPtr->IndexBufferPtr->IndexStreamData;
                std::cout << "PathTracing dispatch: count=" << dispatchCount
                    << " accumulation=" << sceneCB.AccumulateNumber
                    << " size=" << width << 'x' << height
                    << " rays_per_pixel=" << gOptions.RayPerPixelForPathTracing
                    << " bounces=" << gOptions.MaxRecursionDepthForPathTracing
                    << " instances=" << instances.size()
                    << " index_bytes=" << (indexStream && indexStream->Param ? indexStream->Param->GetElementSize() : 0)
                    << std::endl;
            }
        }

		g_rhi->TransitionLayout(CmdBuffer, PathTracingOutput, EResourceLayout::SHADER_READ_ONLY);
	}

	{
		DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "SimpleTonemap", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
		SCOPE_CPU_PROFILE(SimpleTonemap);
		SCOPE_GPU_PROFILE(RenderFrameContextPtr, SimpleTonemap);

		Vector2i Size(SCR_WIDTH, SCR_HEIGHT);
		Vector2i Padding(10, 10);
		const Vector4i DrawRect = Vector4i(0, 0, Size.x, Size.y);
		jRHIUtil::DrawQuad(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr, DrawRect
			, [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
			{
				jTexture* InTexture = PathTracingOutput;
				g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

				const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
					, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
					, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

				jRHIUtil::BuildSingleTextureFragmentBindings(InTexture, SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
			}
			, [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
				{
					jShaderInfo shaderInfo;
					shaderInfo.SetName(jNameStatic("SimpleTonemap"));
					shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SimpleTonemap_ps.hlsl"));
					shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
					jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
					return g_rhi->CreateShader(shaderInfo);
				});
	}
}


// Real generated RT declarations, exported without opening a window or device.
int ExportEditorPathTracingShaderFixtures(const char* directory, bool vulkan)
{
    try
    {
        gAPIType = vulkan ? EAPIType::Vulkan : EAPIType::DX12;
        std::ofstream file(std::filesystem::path(directory) / "editor-path-mesh.hlsl");
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << "#define COMPUTE_SHADER 0\n#define PIXEL_SHADER 0\n#define RAY_TRACING 1\n"
            "#define MAX_RECURSION_DEPTH 8\n#define MAX_RAY_PER_PIXEL 1\n#define USE_BINDLESS_RESOURCE 1\n"
            << jShaderParameterSet::GenerateHLSL<jPathTracingGlobalParameters>(0)
            << jShaderBindlessSet::GenerateHLSL<jPathTracingBindlessParameters>(1)
            << "\n#include \"PathTracing.hlsl\"\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor path tracing shader export failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
