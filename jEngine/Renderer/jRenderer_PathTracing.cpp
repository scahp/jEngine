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

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jPathTracingSceneConstantBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, projectionToWorld)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, cameraPosition)
    SHADER_UNIFORM_BUFFER_MEMBER(float, focalDistance)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector, cameraDirection)
    SHADER_UNIFORM_BUFFER_MEMBER(float, lensRadius)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, FrameNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(uint32, AccumulateNumber)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, HaltonJitter)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jPathTracingGlobalParameters)
    SHADER_ACCELERATION_STRUCTURE(Scene)
    SHADER_RW_TEXTURE2D(RenderTarget)
    SHADER_UNIFORM_BUFFER(jPathTracingSceneConstantBuffer, g_sceneCB)
    SHADER_SAMPLER(DefaultSamplerState)
    SHADER_TEXTURECUBE_SRV(EnvTexture)
END_SHADER_PARAMETER_SET()

namespace
{
    struct jPathTracingBindlessUInt2
    {
    };

    template <>
    struct TShaderParameterHLSLTypeInfo<jPathTracingBindlessUInt2>
    {
        static constexpr const char* GetTypeName() { return "uint2"; }
        static void AppendTypeDeclaration(std::string&) {}
    };
}

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
	if (!RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr
		|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Width != (int32)SCR_WIDTH
		|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Height != (int32)SCR_HEIGHT
		|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Format != ETextureFormat::RGBA32F)
	{
		RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)SCR_WIDTH, (uint32)SCR_HEIGHT, (uint32)1, (uint32)1
			, ETextureFormat::RGBA32F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
	}

	jTexture* PathTracingOutput = RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get();
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

		static jOptions OldOptions = gOptions;
		static auto OldMatrix = sceneCB.projectionToWorld;
		static uint32 AccumulateNumber = 0;
        static int32 LastSelectedIndex = gSelectedSceneIndex;
		if (!gOptions.UseAccumulateRay 
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

		static Vector2 HaltonJitter[] = {
			Vector2(0.0f,      -0.333334f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.5f,     0.333334f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.5f,      -0.777778f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.75f,    -0.111112f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.25f,     0.555556f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.25f,    -0.555556f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.75f,     0.111112f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.875f,   0.777778f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.125f,    -0.925926f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.375f,   -0.259260f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.625f,    0.407408f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.625f,   -0.703704f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.375f,    -0.037038f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.125f,   0.629630f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(0.875f,    -0.481482f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT),
			Vector2(-0.9375f,  0.185186f) / Vector2((float)SCR_WIDTH, (float)SCR_HEIGHT)
		};

		sceneCB.HaltonJitter = HaltonJitter[g_rhi->GetCurrentFrameNumber() % _countof(HaltonJitter)];

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

		jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
		GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);

		// Bindless
		std::vector<const jBuffer*> VertexAndInexOffsetBuffers;
		std::vector<const jBuffer*> IndexBuffers;
		std::vector<const jBuffer*> TestUniformBuffers;
		std::vector<const jBuffer*> VertexBuffers;
		std::vector<const IUniformBufferBlock*> MaterialBuffers;
		std::vector<const IUniformBufferBlock*> LightBuffers;
		std::vector<jTextureResourceBindless::jTextureBindData> Textures;
		std::vector<std::shared_ptr<IUniformBufferBlock>> RefCountMaintainer;

		{
			const int32 NumOfStaticRenderObjects = (int32)jObject::GetStaticRenderObject().size();
			VertexAndInexOffsetBuffers.reserve(NumOfStaticRenderObjects);
			IndexBuffers.reserve(NumOfStaticRenderObjects);
			TestUniformBuffers.reserve(NumOfStaticRenderObjects);
			VertexBuffers.reserve(NumOfStaticRenderObjects);
			MaterialBuffers.reserve(NumOfStaticRenderObjects);

			for (int32 i = 0; i < NumOfStaticRenderObjects; ++i)
			{
				jRenderObject* RObj = jObject::GetStaticRenderObject()[i];
				RObj->CreateShaderBindingInstance();

				VertexAndInexOffsetBuffers.push_back(RObj->VertexAndIndexOffsetBuffer.get());
				IndexBuffers.push_back(RObj->GeometryDataPtr->IndexBufferPtr->GetBuffer());
				TestUniformBuffers.push_back(RObj->TestUniformBuffer.get());
				VertexBuffers.push_back(RObj->GeometryDataPtr->VertexBufferPtr->GetBuffer(0));

				check(RObj->MaterialPtr);


				auto MaterialUniformParametersPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("jMaterialUniformBuffer"), jLifeTimeType::OneFrame, RObj->MaterialPtr->MaterialDataPtr->GetDataSizeInBytes());
				MaterialUniformParametersPtr->UpdateBufferData(RObj->MaterialPtr->MaterialDataPtr->GetData(), RObj->MaterialPtr->MaterialDataPtr->GetDataSizeInBytes());
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

			check(gPathTracingScene);
			if (gPathTracingScene->textures.empty())
			{
				Textures.resize(1);
				Textures[0].Texture = GWhiteTexture.get();
			}
			else
			{
				Textures.resize(gPathTracingScene->textures.size());
				for (int32 i = 0; i < (int32)gPathTracingScene->textures.size(); ++i)
				{
					Textures[i].Texture = gPathTracingScene->textures[i];
				}
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
        std::vector<std::shared_ptr<jShaderBindingInstance>> BindlessShaderBindingInstances =
            jShaderBindlessSet::CreateShaderBindingInstances(BindlessParameters, EShaderAccessStageFlag::ALL_RAYTRACING, jShaderBindingInstanceType::SingleFrame);

		for (const auto& BindlessShaderBindingInstance : BindlessShaderBindingInstances)
		{
			GlobalShaderBindingLayoutArray.Add(BindlessShaderBindingInstance->ShaderBindingsLayouts);
		}

		// Create RaytracingPipelineState
		jRaytracingPipelineData RaytracingPipelineData;
		RaytracingPipelineData.MaxAttributeSize = 2 * sizeof(float);	                // float2 barycentrics

		RaytracingPipelineData.MaxPayloadSize = sizeof(Vector) * 4 + sizeof(uint32) * 3;
		RaytracingPipelineData.MaxTraceRecursionDepth = 31;
		auto RaytracingPipelineState = g_rhi->CreateRaytracingPipelineStateInfo(RaytracingShaders, RaytracingPipelineData
			, GlobalShaderBindingLayoutArray, nullptr);

		// Binding RaytracingShader resources
		jShaderBindingInstanceArray ShaderBindingInstanceArray;
		ShaderBindingInstanceArray.Add(GlobalShaderBindingInstance.get());
		for (const auto& BindlessShaderBindingInstance : BindlessShaderBindingInstances)
		{
			ShaderBindingInstanceArray.Add(BindlessShaderBindingInstance.get());
		}

		jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
		for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
		{
			ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
			const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
			if (pDynamicOffsetTest && pDynamicOffsetTest->size())
			{
				ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
			}
		}
		ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;
		g_rhi->BindRaytracingShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), RaytracingPipelineState, ShaderBindingInstanceCombiner, 0);

		// Binding Raytracing Pipeline State
		RaytracingPipelineState->Bind(RenderFrameContextPtr);

		g_rhi->TransitionLayout(CmdBuffer, PathTracingOutput, EResourceLayout::UAV);

		// Dispatch Rays
		jRaytracingDispatchData TracingData;
		TracingData.Width = SCR_WIDTH;
		TracingData.Height = SCR_HEIGHT;
		TracingData.Depth = 1;
		TracingData.PipelineState = RaytracingPipelineState;
		g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

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
