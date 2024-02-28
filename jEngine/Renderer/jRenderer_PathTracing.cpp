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
		|| RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr->Height != (int32)SCR_HEIGHT)
	{
		RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr = g_rhi->Create2DTexture((uint32)SCR_WIDTH, (uint32)SCR_HEIGHT, (uint32)1, (uint32)1
			, ETextureFormat::RGBA32F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
	}

	jTexture* RayTracingOutput = RenderFrameContextPtr->RaytracingScene->RaytracingOutputPtr.get();
	check(RayTracingOutput);

	{
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "PathTracing", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(PathTracing);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, PathTracing);

		auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

		// Create RaytracingShaders
		std::vector<jRaytracingPipelineShader> RaytracingShaders;
		{
			{
				jRaytracingPipelineShader NewShader;
				jShaderInfo shaderInfo;

				// First hit group for mesh
				shaderInfo.SetName(jNameStatic("Miss"));
				shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/PathTracing.hlsl"));
				shaderInfo.SetEntryPoint(jNameStatic("MeshMissShader"));
				shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_MISS);
				NewShader.MissShader = g_rhi->CreateShader(shaderInfo);
				NewShader.MissEntryPoint = TEXT("MeshMissShader");

				shaderInfo.SetName(jNameStatic("Raygen"));
				shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/PathTracing.hlsl"));
				shaderInfo.SetEntryPoint(jNameStatic("RaygenShader"));
				shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_RAYGEN);
				NewShader.RaygenShader = g_rhi->CreateShader(shaderInfo);
				NewShader.RaygenEntryPoint = TEXT("RaygenShader");

				shaderInfo.SetName(jNameStatic("ClosestHit"));
				shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/PathTracing.hlsl"));
				shaderInfo.SetEntryPoint(jNameStatic("MeshClosestHitShader"));
				shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT);
				NewShader.ClosestHitShader = g_rhi->CreateShader(shaderInfo);
				NewShader.ClosestHitEntryPoint = TEXT("MeshClosestHitShader");

				//shaderInfo.SetName(jNameStatic("AnyHit"));
				//shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/PathTracing.hlsl"));
				//shaderInfo.SetEntryPoint(jNameStatic("MeshAnyHitShader"));
				//shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_ANYHIT);
				//NewShader.AnyHitShader = g_rhi->CreateShader(shaderInfo);
				//NewShader.AnyHitEntryPoint = TEXT("MeshAnyHitShader");

				NewShader.HitGroupName = TEXT("DefaultHit");

				RaytracingShaders.push_back(NewShader);
			}
			{
				jRaytracingPipelineShader NewShader;
				jShaderInfo shaderInfo;

				// Second hit gorup for light
				shaderInfo.SetName(jNameStatic("ClosestHit"));
				shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/PathTracing.hlsl"));
				shaderInfo.SetEntryPoint(jNameStatic("LightClosestHitShader"));
				shaderInfo.SetShaderType(EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT);
				NewShader.ClosestHitShader = g_rhi->CreateShader(shaderInfo);
				NewShader.ClosestHitEntryPoint = TEXT("LightClosestHitShader");

				NewShader.HitGroupName = TEXT("LightHit");

				RaytracingShaders.push_back(NewShader);
			}
		}

		jShaderBindingArray ShaderBindingArray;
		jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
		ShaderBindingArray.Add(jShaderBinding::Create(0, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
			ResourceInlineAllactor.Alloc<jBufferResource>(RenderFrameContextPtr->RaytracingScene->TLASBufferPtr.get()), true));
		ShaderBindingArray.Add(jShaderBinding::Create(1, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::ALL_RAYTRACING,
			ResourceInlineAllactor.Alloc<jTextureResource>(RayTracingOutput, nullptr), false));

		struct SceneConstantBuffer
		{
			Matrix projectionToWorld;
			Vector cameraPosition;
			float focalDistance;
			Vector cameraDirection;
			float lensRadius;
			uint32 FrameNumber;
			uint32 AccumulateNumber;
			Vector2 HaltonJitter;
		};

		SceneConstantBuffer sceneCB;
		auto mainCamera = jCamera::GetMainCamera();
		sceneCB.cameraPosition = mainCamera->Pos;
		sceneCB.projectionToWorld = mainCamera->GetInverseViewProjectionMatrix();
		sceneCB.focalDistance = mainCamera->FocalDist;
		sceneCB.lensRadius = mainCamera->Aperture;
		sceneCB.FrameNumber = g_rhi->GetCurrentFrameNumber();

		static jOptions OldOptions = gOptions;
		static auto OldMatrix = sceneCB.projectionToWorld;
		static uint32 AccumulateNumber = 0;
		if (!gOptions.UseAccumulateRay || OldMatrix != sceneCB.projectionToWorld || OldOptions != gOptions)
		{
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

		ShaderBindingArray.Add(jShaderBinding::Create(4, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING,
			ResourceInlineAllactor.Alloc<jUniformBufferResource>(SceneUniformBufferPtr.get()), true));

		// Create ShaderBindingLayout and ShaderBindingInstance Instance for this draw call
		std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstance;
		GlobalShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

		jShaderBindingLayoutArray GlobalShaderBindingLayoutArray;
		GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstance->ShaderBindingsLayouts);

		// Bindless
		jShaderBindingArray BindlessShaderBindingArray[6];
		std::vector<const jBuffer*> VertexAndInexOffsetBuffers;
		std::vector<const jBuffer*> IndexBuffers;
		std::vector<const jBuffer*> TestUniformBuffers;
		std::vector<const jBuffer*> VertexBuffers;
		std::vector<const IUniformBufferBlock*> MaterialBuffers;
		std::vector<const IUniformBufferBlock*> LightBuffers;
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

			BindlessShaderBindingArray[0].Add(jShaderBinding::CreateBindless(0, (uint32)VertexAndInexOffsetBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
				ResourceInlineAllactor.Alloc<jBufferResourceBindless>(VertexAndInexOffsetBuffers), false));
			BindlessShaderBindingArray[1].Add(jShaderBinding::CreateBindless(0, (uint32)IndexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
				ResourceInlineAllactor.Alloc<jBufferResourceBindless>(IndexBuffers), false));
			BindlessShaderBindingArray[2].Add(jShaderBinding::CreateBindless(0, (uint32)TestUniformBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
				ResourceInlineAllactor.Alloc<jBufferResourceBindless>(TestUniformBuffers), false));
			BindlessShaderBindingArray[3].Add(jShaderBinding::CreateBindless(0, (uint32)VertexBuffers.size(), EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_RAYTRACING,
				ResourceInlineAllactor.Alloc<jBufferResourceBindless>(VertexBuffers), false));
			BindlessShaderBindingArray[4].Add(jShaderBinding::CreateBindless(0, (uint32)MaterialBuffers.size(), EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING,
				ResourceInlineAllactor.Alloc<jUniformBufferResourceBindless>(MaterialBuffers)));
			BindlessShaderBindingArray[5].Add(jShaderBinding::CreateBindless(0, (uint32)LightBuffers.size(), EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_RAYTRACING,
				ResourceInlineAllactor.Alloc<jUniformBufferResourceBindless>(LightBuffers)));
		}

		std::shared_ptr<jShaderBindingInstance> GlobalShaderBindingInstanceBindless[_countof(BindlessShaderBindingArray)];
		for (int32 i = 0; i < _countof(BindlessShaderBindingArray); ++i)
		{
			GlobalShaderBindingInstanceBindless[i] = g_rhi->CreateShaderBindingInstance(BindlessShaderBindingArray[i], jShaderBindingInstanceType::SingleFrame);
		}

		for (int32 i = 0; i < _countof(BindlessShaderBindingArray); ++i)
		{
			GlobalShaderBindingLayoutArray.Add(GlobalShaderBindingInstanceBindless[i]->ShaderBindingsLayouts);
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
		for (int32 i = 0; i < _countof(GlobalShaderBindingInstanceBindless); ++i)
		{
			ShaderBindingInstanceArray.Add(GlobalShaderBindingInstanceBindless[i].get());
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

		g_rhi->TransitionLayout(CmdBuffer, RayTracingOutput, EResourceLayout::UAV);

		// Dispatch Rays
		jRaytracingDispatchData TracingData;
		TracingData.Width = SCR_WIDTH;
		TracingData.Height = SCR_HEIGHT;
		TracingData.Depth = 1;
		TracingData.PipelineState = RaytracingPipelineState;
		g_rhi->DispatchRay(RenderFrameContextPtr, TracingData);

		g_rhi->TransitionLayout(CmdBuffer, RayTracingOutput, EResourceLayout::SHADER_READ_ONLY);
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
				jTexture* InTexture = RayTracingOutput;
				g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

				const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
					, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
					, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

				InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
					, InOutResourceInlineAllactor.Alloc<jTextureResource>(InTexture, SamplerState)));
			}
			, [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
				{
					jShaderInfo shaderInfo;
					shaderInfo.SetName(jNameStatic("SimpleTonemap"));
					shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/SimpleTonemap_ps.hlsl"));
					shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
					return g_rhi->CreateShader(shaderInfo);
				});
	}
}
