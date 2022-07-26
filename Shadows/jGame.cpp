#include "pch.h"
#include "jGame.h"
#include "Math\Vector.h"
#include "jCamera.h"
#include "jObject.h"
#include "jLight.h"
#include "jPrimitiveUtil.h"
#include "jRHI.h"
#include "jRenderObject.h"
#include "jShadowVolume.h"
#include "jShadowAppProperties.h"
#include "jImageFileLoader.h"
#include "jHairModelLoader.h"
#include "jMeshObject.h"
#include "jModelLoader.h"
#include "jFile.h"
#include "jFrameBufferPool.h"
#include "glad\glad.h"
#include "jDeferredRenderer.h"
#include "jForwardRenderer.h"
#include "jPipeline.h"
#include "jVertexAdjacency.h"
#include <time.h>
#include <stdlib.h>

#if USE_OPENGL
#include "jRHI_OpenGL.h"
#elif USE_VULKAN
#include "jRHI_Vulkan.h"
#endif
#include "jRenderTargetPool.h"
#include "ImGui\jImGui.h"

jRHI* g_rhi = nullptr;

jGame::jGame()
{
#if USE_OPENGL
	g_rhi = new jRHI_OpenGL();
#elif USE_VULKAN
	g_rhi = new jRHI_Vulkan();
#endif
}

jGame::~jGame()
{
}

void jGame::ProcessInput()
{
	static float speed = 1.0f;

	// Process Key Event
	if (g_KeyState['a'] || g_KeyState['A']) MainCamera->MoveShift(-speed);
	if (g_KeyState['d'] || g_KeyState['D']) MainCamera->MoveShift(speed);
	//if (g_KeyState['1']) MainCamera->RotateForwardAxis(-0.1f);
	//if (g_KeyState['2']) MainCamera->RotateForwardAxis(0.1f);
	//if (g_KeyState['3']) MainCamera->RotateUpAxis(-0.1f);
	//if (g_KeyState['4']) MainCamera->RotateUpAxis(0.1f);
	//if (g_KeyState['5']) MainCamera->RotateRightAxis(-0.1f);
	//if (g_KeyState['6']) MainCamera->RotateRightAxis(0.1f);
	if (g_KeyState['w'] || g_KeyState['W']) MainCamera->MoveForward(speed);
	if (g_KeyState['s'] || g_KeyState['S']) MainCamera->MoveForward(-speed);
	if (g_KeyState['+']) speed = Max(speed + 0.1f, 0.0f);
	if (g_KeyState['-']) speed = Max(speed - 0.1f, 0.0f);
}

void jGame::Setup()
{
	srand(static_cast<uint32>(time(NULL)));

	auto& AppSettings = jShadowAppSettingProperties::GetInstance();

	// Create main camera
	const Vector mainCameraPos(172.66f, 160.0f, -180.63f);
	const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
	MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 1000.0f, SCR_WIDTH, SCR_HEIGHT, true);
	jCamera::AddCamera(0, MainCamera);

	// Create lights
	NormalDirectionalLight = jLight::CreateDirectionalLight(Vector(-1.0f, -1.0f, -1.0f) // AppSettings.DirecionalLightDirection
		, Vector4(0.6f), Vector(1.0f), Vector(1.0f), 64);
	//CascadeDirectionalLight = jLight::CreateCascadeDirectionalLight(AppSettings.DirecionalLightDirection
	//	, Vector4(0.6f), Vector(1.0f), Vector(1.0f), 64);
	//AmbientLight = jLight::CreateAmbientLight(Vector(0.2f, 0.5f, 1.0f), Vector(0.05f));		// sky light color
	//PointLight = jLight::CreatePointLight(jShadowAppSettingProperties::GetInstance().PointLightPosition, Vector4(2.0f, 0.7f, 0.7f, 1.0f), 500.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
	//SpotLight = jLight::CreateSpotLight(jShadowAppSettingProperties::GetInstance().SpotLightPosition, jShadowAppSettingProperties::GetInstance().SpotLightDirection, Vector4(0.0f, 1.0f, 0.0f, 1.0f), 500.0f, 0.7f, 1.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);

	// Select one of directional light
	DirectionalLight = NormalDirectionalLight;

	// Create light info for debugging light infomation
	if (DirectionalLight)
	{
		DirectionalLightInfo = jPrimitiveUtil::CreateDirectionalLightDebug(Vector(250, 260, 0) * 0.5f, Vector::OneVector * 10.0f, 10.0f, MainCamera, DirectionalLight, "Image/sun.png");
		if (AppSettings.ShowDirectionalLightInfo)
			jObject::AddDebugObject(DirectionalLightInfo);
	}

	//if (PointLight)
	//{
	//	PointLightInfo = jPrimitiveUtil::CreatePointLightDebug(Vector(10.0f), MainCamera, PointLight, "Image/bulb.png");
	//	if (AppSettings.ShowPointLightInfo)
	//		jObject::AddDebugObject(PointLightInfo);
	//}

	//if (SpotLight)
	//{
	//	SpotLightInfo = jPrimitiveUtil::CreateSpotLightDebug(Vector(10.0f), MainCamera, SpotLight, "Image/spot.png");
	//	if (AppSettings.ShowSpotLightInfo)
	//		jObject::AddDebugObject(SpotLightInfo);
	//}

	//// Main camera is linked with lights which will be used.
	//if (DirectionalLight)
	//	MainCamera->AddLight(DirectionalLight);
	//if (PointLight)
	//	MainCamera->AddLight(PointLight);
	//if (SpotLight)
	//	MainCamera->AddLight(SpotLight);
	//MainCamera->AddLight(AmbientLight);

	//// Create UI primitive to visualize shadowmap for debugging
	//DirectionalLightShadowMapUIDebug = jPrimitiveUtil::CreateUIQuad({ 0.0f, 0.0f }, { 150, 150 }, DirectionalLight->GetShadowMap());
	//if (DirectionalLightShadowMapUIDebug)
	//	jObject::AddUIDebugObject(DirectionalLightShadowMapUIDebug);

	// Select spawning object type
	SpawnObjects(ESpawnedType::TestPrimitive);

	// Prepare pipeline which have rendering passes.
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::SSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_SSM)));
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::PCF, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_SSM_PCF)));
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::PCSS, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_SSM_PCSS)));
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::VSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_VSM)));
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::ESM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_ESM)));
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::EVSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_EVSM)));
	ShadowPipelineSetMap.insert(std::make_pair(EShadowMapType::CSM_SSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_CSM_SSM)));

	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::SSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_SSM)));
	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::PCF, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_SSM_PCF_Poisson)));
	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::PCSS, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_SSM_PCSS_Poisson)));
	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::VSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_VSM)));
	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::ESM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_ESM)));
	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::EVSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_EVSM)));	
	ShadowPoissonSamplePipelineSetMap.insert(std::make_pair(EShadowMapType::CSM_SSM, CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_CSM_SSM)));

	ShadowVolumePipelineSet = CREATE_PIPELINE_SET_WITH_SETUP(jForwardPipelineSet_ShadowVolume);

	CurrentShadowMapType = jShadowAppSettingProperties::GetInstance().ShadowMapType;

	//// Prepare renderer which are both forward and deferred. It can be switched depend on shadow type
	//const auto currentShadowPipelineSet = (jShadowAppSettingProperties::GetInstance().ShadowType == EShadowType::ShadowMap) 
	//	? ShadowPipelineSetMap[CurrentShadowMapType]  : ShadowVolumePipelineSet;
	//ForwardRenderer = new jForwardRenderer(currentShadowPipelineSet);
	//ForwardRenderer->Setup();

	//// FrameBuffer 생성 필요
	//// DeferredRenderer = new jDeferredRenderer({ ETextureType::TEXTURE_2D, ETextureFormat::RGBA32F, EDepthBufferType::DEPTH16, SCR_WIDTH, SCR_HEIGHT, 4 });
	//DeferredRenderer = new jDeferredRenderer({ ETextureType::TEXTURE_2D, ETextureFormat::RGBA32F, SCR_WIDTH, SCR_HEIGHT, 4 });
	//DeferredRenderer->Setup();
}

void jGame::SpawnObjects(ESpawnedType spawnType)
{
	if (spawnType != SpawnedType)
	{
		SpawnedType = spawnType;
		switch (SpawnedType)
		{
		case ESpawnedType::Hair:
			SpawnHairObjects();
			break;
		case ESpawnedType::TestPrimitive:
			SpawnTestPrimitives();
			break;
		case ESpawnedType::CubePrimitive:
			SapwnCubePrimitives();
			break;
		}
	}
}

void jGame::RemoveSpawnedObjects()
{
	for (auto& iter : SpawnedObjects)
	{
		JASSERT(iter);
		jObject::RemoveObject(iter);
		delete iter;
	}
	SpawnedObjects.clear();
}

// todo 위치 정해야 함
template <bool bPositionOnly>
class jDrawCommand
{
public:
	jDrawCommand(jView* view, jRenderObject* renderObject, jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed, std::vector<const jShaderBindingInstance*> shaderBindingInstances)
		: View(view), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed)
	{
		ShaderBindingInstances.reserve(shaderBindingInstances.size() + 3);
		ShaderBindingInstances.insert(ShaderBindingInstances.begin(), shaderBindingInstances.begin(), shaderBindingInstances.end());
	}

	void PrepareToDraw()
	{
		RenderObject->UpdateRenderObjectUniformBuffer(View);
		RenderObject->PrepareShaderBindingInstance();

		// GetShaderBindings
		View->GetShaderBindingInstance(ShaderBindingInstances);

		// GetShaderBindings
		RenderObject->GetShaderBindingInstance(ShaderBindingInstances);

		// Bind ShaderBindings
		std::vector<const jShaderBindings*> shaderBindings;
		shaderBindings.reserve(ShaderBindingInstances.size());
		for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
			shaderBindings.push_back(ShaderBindingInstances[i]->ShaderBindings);

		// Create Pipeline
		CurrentPipelineStateInfo = jPipelineStateInfo(PipelineStateFixed, Shader, bPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer, RenderPass, shaderBindings);
		CurrentPipelineStateInfo.CreateGraphicsPipelineState();
	}
	void Draw()
	{
		for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
		{
			ShaderBindingInstances[i]->Bind(CurrentPipelineStateInfo.vkPipelineLayout, i);
		}

		// Bind Pipeline
		CurrentPipelineStateInfo.Bind();

		// Draw
		RenderObject->Draw(nullptr, Shader, {});
	}

	std::vector<const jShaderBindingInstance*> ShaderBindingInstances;

	jView* View = nullptr;
	jRenderPass* RenderPass = nullptr;
	jShader* Shader = nullptr;
	jRenderObject* RenderObject = nullptr;
	jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
	jPipelineStateInfo CurrentPipelineStateInfo;
};

void jGame::Update(float deltaTime)
{
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Update");

	// Update application property by using UI Pannel.
	// UpdateAppSetting();

	// Update main camera
	MainCamera->UpdateCamera();

	//// Update lights
	//const int32 numOfLights = MainCamera->GetNumOfLight();
	//for (int32 i = 0; i < numOfLights; ++i)
	//{
	//	auto light = MainCamera->GetLight(i);
	//	JASSERT(light);
	//	light->Update(deltaTime);
	//}

	//for (auto iter : jObject::GetStaticObject())
	//	iter->Update(deltaTime);

	//for (auto& iter : jObject::GetBoundBoxObject())
	//	iter->Update(deltaTime);

	//for (auto& iter : jObject::GetBoundSphereObject())
	//	iter->Update(deltaTime);

	//for (auto& iter : jObject::GetDebugObject())
	//	iter->Update(deltaTime);

	// Update object which have dirty flag
	jObject::FlushDirtyState();

	//// Render all objects by using selected renderer
	//Renderer->Render(MainCamera);

	uint32_t imageIndex = 0;
	jCommandBuffer_Vulkan commandBuffer = g_rhi_vk->CommandBufferManager.GetOrCreateCommandBuffer();
	{
// 이 함수는 아래 3가지 기능을 수행함
// 1. 스왑체인에서 이미지를 얻음
// 2. Framebuffer 에서 Attachment로써 얻어온 이미지를 가지고 커맨드 버퍼를 실행
// 3. 스왑체인 제출을 위해 이미지를 반환

// 스왑체인을 동기화는 2가지 방법
// 1. Fences		: vkWaitForFences 를 사용하여 프로그램에서 fences의 상태에 접근 할 수 있음.
//						Fences는 어플리케이션과 렌더링 명령의 동기화를 위해 설계됨
// 2. Semaphores	: 세마포어는 안됨.
//						Semaphores 는 커맨드 Queue 내부 혹은 Queue 들 사이에 명령어 동기화를 위해서 설계됨

// 현재는 Draw 와 presentation 커맨드를 동기화 하는 곳에 사용할 것이므로 세마포어가 적합.
// 2개의 세마포어를 만들 예정
// 1. 이미지를 획득해서 렌더링 준비가 완료된 경우 Signal(Lock 이 풀리는) 되는 것 (imageAvailableSemaphore)
// 2. 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것 (renderFinishedSemaphore)

		VkFence lastCommandBufferFence = g_rhi_vk->swapChainCommandBufferFences[g_rhi_vk->currenFrame];

		// timeout 은 nanoseconds. UINT64_MAX 는 타임아웃 없음
#if MULTIPLE_FRAME
		VkResult acquireNextImageResult = vkAcquireNextImageKHR(g_rhi_vk->device, g_rhi_vk->swapChain, UINT64_MAX, g_rhi_vk->imageAvailableSemaphores[g_rhi_vk->currenFrame], VK_NULL_HANDLE, &imageIndex);
		if (acquireNextImageResult != VK_SUCCESS)
			return;

		// 이전 프레임에서 현재 사용하려는 이미지를 사용중에 있나? (그렇다면 펜스를 기다려라)
		if (lastCommandBufferFence != VK_NULL_HANDLE)
			vkWaitForFences(g_rhi_vk->device, 1, &lastCommandBufferFence, VK_TRUE, UINT64_MAX);

		// 이 프레임에서 펜스를 사용한다고 마크 해둠
		g_rhi_vk->swapChainCommandBufferFences[g_rhi_vk->currenFrame] = commandBuffer.Fence;
#else
		VkResult acquireNextImageResult = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
#endif // MULTIPLE_FRAME

		// 여기서는 VK_SUCCESS or VK_SUBOPTIMAL_KHR 은 성공했다고 보고 계속함.
		// VK_ERROR_OUT_OF_DATE_KHR : 스왑체인이 더이상 서피스와 렌더링하는데 호환되지 않는 경우. (보통 윈도우 리사이즈 이후)
		// VK_SUBOPTIMAL_KHR : 스왑체인이 여전히 서피스에 제출 가능하지만, 서피스의 속성이 더이상 정확하게 맞지 않음.
		if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR)
		{
			g_rhi_vk->RecreateSwapChain();		// 이 경우 렌더링이 더이상 불가능하므로 즉시 스왑체인을 새로 만듬.
			return;
		}
		else if (acquireNextImageResult != VK_SUCCESS && acquireNextImageResult != VK_SUBOPTIMAL_KHR)
		{
			check(0);
			return;
		}
	}

	{
		{
			auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
			jMultisampleStateInfo* MultisampleState = nullptr;
			switch (g_rhi_vk->msaaSamples)
			{
			case VK_SAMPLE_COUNT_1_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_1, true, 0.2f, false, false>::Create();
				break;
			case VK_SAMPLE_COUNT_2_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_2, true, 0.2f, false, false>::Create();
				break;
			case VK_SAMPLE_COUNT_4_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_4, true, 0.2f, false, false>::Create();
				break;
			case VK_SAMPLE_COUNT_8_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_8, true, 0.2f, false, false>::Create();
				break;
			case VK_SAMPLE_COUNT_16_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_16, true, 0.2f, false, false>::Create();
				break;
			case VK_SAMPLE_COUNT_32_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_32, true, 0.2f, false, false>::Create();
				break;
			case VK_SAMPLE_COUNT_64_BIT:
				MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_64, true, 0.2f, false, false>::Create();
				break;
			default:
				break;
			}
			auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
			auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

			g_rhi_vk->CurrentPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
				, jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));
		}

		{
			auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
			jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_1, true, 0.2f, false, false>::Create();
			auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
			auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

			g_rhi_vk->CurrentPipelineStateFixed_Shadow = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
				, jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));			
		}

		for (auto iter : jObject::GetStaticObject())
		{
			iter->Update(deltaTime);

			iter->RenderObject->UpdateWorldMatrix();
		}

		// 정리해야함
		DirectionalLight->Update(deltaTime);

		static jRenderPass_Vulkan* renderPass = nullptr;
		if (!renderPass)
		{
			DirectionalLight->ShadowMapPtr = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
				{ ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, /*ETextureFilter::LINEAR, ETextureFilter::LINEAR, */false, 1 }));

			const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
			const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

			jAttachment* depth = new jAttachment(DirectionalLight->ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth, true);
			jAttachment* resolve = nullptr;

			auto textureDepth = (jTexture_Vulkan*)depth->RenderTargetPtr->GetTexture();
			auto depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
			g_rhi_vk->TransitionImageLayout(textureDepth->Image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);

			renderPass = new jRenderPass_Vulkan(nullptr, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
			renderPass->CreateRenderPass();
			//RenderPasses.push_back(renderPass);
		}

		// 정리 해야 함
		DirectionalLight->PrepareShaderBindingInstance();

#define STATIC_COMMAND 0

		// Shadow
		{
			static jShader* Shader = nullptr;
			if (!Shader)
			{
				jShaderInfo shaderInfo;
				shaderInfo.name = jName("shadow_test");
				shaderInfo.vs = jName("Shaders/Vulkan/shadow_vs.glsl");
				shaderInfo.fs = jName("Shaders/Vulkan/shadow_fs.glsl");
				Shader = g_rhi->CreateShader(shaderInfo);
			}

			static jView View;
			View.Camera = DirectionalLight->GetLightCamra();
			View.DirectionalLight = DirectionalLight;

			g_rhi_vk->CurrentCommandBuffer = &commandBuffer;

#if STATIC_COMMAND
			static std::vector<jDrawCommand<true>> ShadowPasses;
			if (ShadowPasses.size() <= 0)
#else
			std::vector<jDrawCommand<true>> ShadowPasses;
#endif
			{
				for (auto iter : jObject::GetStaticObject())
				{
					auto newCommand = jDrawCommand<true>(&View, iter->RenderObject, renderPass, Shader, &g_rhi_vk->CurrentPipelineStateFixed_Shadow, { });
					newCommand.PrepareToDraw();
					ShadowPasses.push_back(newCommand);
				}
			}

			//////////////////////////////////////////////////////////////////////////
			// 여기서 부터 렌더 로직 시작
			if (ensure(commandBuffer.Begin()))
			{
				g_rhi_vk->QueryPool.ResetQueryPool(&commandBuffer);

				{
					SCOPE_GPU_PROFILE(ShadowPass);
					renderPass->BeginRenderPass(&commandBuffer);

					for (auto& command : ShadowPasses)
					{
						command.Draw();
					}

					// Finishing up
					renderPass->EndRenderPass();
				}

				//ensure(commandBuffer.End());
			}

			// 여기 까지 렌더 로직 끝
			//////////////////////////////////////////////////////////////////////////
		}

		// BasePass
		if (1)
		{
			static jShader* Shader = nullptr;
			if (!Shader)
			{
				jShaderInfo shaderInfo;
				shaderInfo.name = jName("default_test");
				shaderInfo.vs = jName("Shaders/Vulkan/shader_vs.glsl");
				shaderInfo.fs = jName("Shaders/Vulkan/shader_fs.glsl");
				Shader = g_rhi->CreateShader(shaderInfo);
			}

			static jView View;
			View.Camera = MainCamera;
			View.DirectionalLight = DirectionalLight;

			g_rhi_vk->CurrentCommandBuffer = &commandBuffer;

#if STATIC_COMMAND
			static std::vector<jDrawCommand<false>> BasePasses;
			if (BasePasses.size() <= 0)
#else
			std::vector<jDrawCommand<false>> BasePasses;
#endif
			{
				for (auto iter : jObject::GetStaticObject())
				{
					auto newCommand = jDrawCommand<false>(&View, iter->RenderObject, g_rhi_vk->RenderPasses[imageIndex], Shader, &g_rhi_vk->CurrentPipelineStateFixed, { });
					newCommand.PrepareToDraw();
					BasePasses.push_back(newCommand);
				}
			}

			//////////////////////////////////////////////////////////////////////////
			// 여기서 부터 렌더 로직 시작
			//if (ensure(commandBuffer.Begin()))
			{
				SCOPE_GPU_PROFILE(BasePass);

				g_rhi_vk->RenderPasses[imageIndex]->BeginRenderPass(&commandBuffer);

				//vkCmdWriteTimestamp((VkCommandBuffer)commandBuffer.GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_rhi_vk->vkQueryPool, 0);

				for (auto command : BasePasses)
				{
					command.Draw();
				}

				// Finishing up
				g_rhi_vk->RenderPasses[imageIndex]->EndRenderPass();

				jImGUI_Vulkan::Get().Draw((VkCommandBuffer)commandBuffer.GetHandle(), imageIndex);

				//vkCmdWriteTimestamp((VkCommandBuffer)commandBuffer.GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_rhi_vk->vkQueryPool, 1);
			}
			ensure(commandBuffer.End());

			// 여기 까지 렌더 로직 끝
			//////////////////////////////////////////////////////////////////////////
		}
	}
	{
		// Submitting the command buffer
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

#if MULTIPLE_FRAME
		VkSemaphore waitsemaphores[] = { g_rhi_vk->imageAvailableSemaphores[g_rhi_vk->currenFrame] };
#else
		VkSemaphore waitsemaphores[] = { imageAvailableSemaphore };
#endif // MULTIPLE_FRAME
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitsemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer.GetRef();

#if MULTIPLE_FRAME
		VkSemaphore signalSemaphores[] = { g_rhi_vk->renderFinishedSemaphores[g_rhi_vk->currenFrame] };
#else
		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
#endif // MULTIPLE_FRAME
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		// SubmitInfo를 동시에 할수도 있음.
#if MULTIPLE_FRAME
		vkResetFences(g_rhi_vk->device, 1, &commandBuffer.Fence);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

		// 마지막에 Fences 파라메터는 커맨드 버퍼가 모두 실행되고나면 Signaled 될 Fences.
		if (!ensure(vkQueueSubmit(g_rhi_vk->GraphicsQueue.Queue, 1, &submitInfo, commandBuffer.Fence) == VK_SUCCESS))
			return;
#else
		if (!ensure(vkQueueSubmit(g_rhi_vk->GraphicsQueue.Queue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS))
			return;
#endif // MULTIPLE_FRAME

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { g_rhi_vk->swapChain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;

		// 여러개의 스왑체인에 제출하는 경우만 모든 스왑체인으로 잘 제출 되었나 확인하는데 사용
		// 1개인 경우 그냥 vkQueuePresentKHR 의 리턴값을 확인하면 됨.
		presentInfo.pResults = nullptr;			// Optional
		VkResult queuePresentResult = vkQueuePresentKHR(g_rhi_vk->PresentQueue.Queue, &presentInfo);

		// 세마포어의 일관된 상태를 보장하기 위해서(세마포어 로직을 변경하지 않으려 노력한듯 함) vkQueuePresentKHR 이후에 framebufferResized 를 체크하는 것이 중요함.
		if ((queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR) || (queuePresentResult == VK_SUBOPTIMAL_KHR) || g_rhi_vk->framebufferResized)
		{
			g_rhi_vk->RecreateSwapChain();
			g_rhi_vk->framebufferResized = false;
		}
		else if (queuePresentResult != VK_SUCCESS)
		{
			check(0);
			return;
		}

		// CPU 가 큐에 작업을 제출하는 속도가 GPU 에서 소모하는 속도보다 빠른 경우 큐에 작업이 계속 쌓이거나 
		// 여러 프레임에 걸쳐 동시에 imageAvailableSemaphore 와 renderFinishedSemaphore를 재사용하게 되는 문제가 있음.
		// 1). 한프레임을 마치고 큐가 빌때까지 기다리는 것으로 해결할 수 있음. 한번에 1개의 프레임만 완성 가능(최적의 해결방법은 아님)
		// 2). 여러개의 프레임을 동시에 처리 할수있도록 확장. 동시에 진행될 수 있는 최대 프레임수를 지정해줌.
#if MULTIPLE_FRAME
		g_rhi_vk->currenFrame = (g_rhi_vk->currenFrame + 1) % g_rhi_vk->swapChainImageViews.size();
#else
		vkQueueWaitIdle(PresentQueue);
#endif // MULTIPLE_FRAME

		g_rhi_vk->CommandBufferManager.ReturnCommandBuffer(commandBuffer);
	}
}

void jGame::UpdateAppSetting()
{
	auto& appSetting =  jShadowAppSettingProperties::GetInstance();

	// Rotating spot light
	appSetting.SpotLightDirection = Matrix::MakeRotateY(0.01f).TransformDirection(appSetting.SpotLightDirection);

	// Chaning normal or cascade shadow map depend on shadow type.
	bool changedDirectionalLight = false;
	if (appSetting.ShadowMapType == EShadowMapType::CSM_SSM)
	{
		if (DirectionalLight != CascadeDirectionalLight)
		{
			MainCamera->RemoveLight(DirectionalLight);
			DirectionalLight = CascadeDirectionalLight;
			MainCamera->AddLight(DirectionalLight);
			changedDirectionalLight = true;
		}
	}
	else
	{
		if (DirectionalLight != NormalDirectionalLight)
		{
			MainCamera->RemoveLight(DirectionalLight);
			DirectionalLight = NormalDirectionalLight;
			MainCamera->AddLight(DirectionalLight);
			changedDirectionalLight = true;
		}
	}

	if (changedDirectionalLight)
	{
		// Update directional light info by using selected shadowmap
		if (DirectionalLightInfo)
			((jDirectionalLightPrimitive*)(DirectionalLightInfo))->Light = DirectionalLight;

		// Update debugging shadow map of directional light
		const auto shaderMapRenderTarget = DirectionalLight->ShadowMapData->ShadowMapFrameBuffer;
		const float aspect = static_cast<float>(shaderMapRenderTarget->Info.Height) / shaderMapRenderTarget->Info.Width;
		if (DirectionalLightShadowMapUIDebug)
		{
			DirectionalLightShadowMapUIDebug->SetTexture(DirectionalLight->ShadowMapData->ShadowMapFrameBuffer->GetTexture());
			DirectionalLightShadowMapUIDebug->Size.y = DirectionalLightShadowMapUIDebug->Size.x * aspect;
		}
	}

	bool isChangedShadowMode = false;

	// Chaning ShadowType or ShadowMapType
	//  - Spawn object depending on new type
	//  - Switch pipeline set and renderer depending on new type
	const bool isChangedShadowType = CurrentShadowType != appSetting.ShadowType;
	const bool isChangedShadowMapType = (CurrentShadowMapType != appSetting.ShadowMapType);
	if (appSetting.ShadowType == EShadowType::ShadowMap)
	{
		if (appSetting.ShadowMapType == EShadowMapType::DeepShadowMap_DirectionalLight)
		{
			Renderer = DeferredRenderer;
			appSetting.ShowPointLightInfo = false;
			appSetting.ShowSpotLightInfo = false;

			SpawnObjects(ESpawnedType::Hair);
			isChangedShadowMode = true;
		}
		else
		{
			Renderer = ForwardRenderer;

			const bool isChangedPoisson = (UsePoissonSample != appSetting.UsePoissonSample);
			if (isChangedShadowType || isChangedPoisson || isChangedShadowMapType)
			{
				CurrentShadowType = appSetting.ShadowType;
				UsePoissonSample = appSetting.UsePoissonSample;
				CurrentShadowMapType = appSetting.ShadowMapType;
				auto newPipelineSet = UsePoissonSample ? ShadowPoissonSamplePipelineSetMap[CurrentShadowMapType] : ShadowPipelineSetMap[CurrentShadowMapType];
				Renderer->SetChangePipelineSet(newPipelineSet);
				isChangedShadowMode = true;
			}

			if (appSetting.ShadowMapType == EShadowMapType::CSM_SSM)
				SpawnObjects(ESpawnedType::CubePrimitive);
			else
				SpawnObjects(ESpawnedType::TestPrimitive);
		}
		MainCamera->IsInfinityFar = false;
	}
	else if (appSetting.ShadowType == EShadowType::ShadowVolume)
	{
		Renderer = ForwardRenderer;
		if (DirectionalLight && DirectionalLight->ShadowMapData && DirectionalLight->ShadowMapData->ShadowMapCamera)
			DirectionalLight->ShadowMapData->ShadowMapCamera->IsPerspectiveProjection = false;

		const bool isChangedShadowType = CurrentShadowType != appSetting.ShadowType;
		if (isChangedShadowType)
		{
			CurrentShadowType = appSetting.ShadowType;
			Renderer->SetChangePipelineSet(ShadowVolumePipelineSet);
			isChangedShadowMode = true;
		}

		SpawnObjects(ESpawnedType::TestPrimitive);
		MainCamera->IsInfinityFar = true;
	}

	// Update Light's MaterialData to cache it again
	if (isChangedShadowMode)
	{
		for (int32 i = 0; i < MainCamera->GetNumOfLight(); ++i)
		{
			jLight* light = MainCamera->GetLight(i);
			if (light)
			{
				light->DirtyMaterialData = true;
			}
		}
	}

	// Update visibility of sub menu in UI Panel
	if (isChangedShadowType)
		appSetting.SwitchShadowType(jAppSettings::GetInstance().Get("MainPannel"));
	if (isChangedShadowMapType)
		appSetting.SwitchShadowMapType(jAppSettings::GetInstance().Get("MainPannel"));

	// Switching visibility of light info by using UI panel
	static auto s_showDirectionalLightInfo = appSetting.ShowDirectionalLightInfo;
	static auto s_showPointLightInfo = appSetting.ShowPointLightInfo;
	static auto s_showSpotLightInfo = appSetting.ShowSpotLightInfo;

	static auto s_showDirectionalLightOn = appSetting.DirectionalLightOn;
	static auto s_showPointLightOn = appSetting.PointLightOn;
	static auto s_showSpotLightOn = appSetting.SpotLightOn;

	const auto compareFunc = [](bool& LHS, const bool& RHS, auto func)
	{
		if (LHS != RHS)
		{
			LHS = RHS;
			func(LHS);
		}
	};

	compareFunc(s_showDirectionalLightInfo, appSetting.ShowDirectionalLightInfo, [this](const auto& param) {
		if (param)
			jObject::AddDebugObject(DirectionalLightInfo);
		else
			jObject::RemoveDebugObject(DirectionalLightInfo);
	});

	compareFunc(s_showDirectionalLightOn, appSetting.DirectionalLightOn, [this](const auto& param) {
		if (param)
			MainCamera->AddLight(DirectionalLight);
		else
			MainCamera->RemoveLight(DirectionalLight);
	});

	compareFunc(s_showPointLightInfo, appSetting.ShowPointLightInfo, [this](const auto& param) {
		if (param)
			jObject::AddDebugObject(PointLightInfo);
		else
			jObject::RemoveDebugObject(PointLightInfo);
	});

	compareFunc(s_showPointLightOn, appSetting.PointLightOn, [this](const auto& param) {
		if (param)
			MainCamera->AddLight(PointLight);
		else
			MainCamera->RemoveLight(PointLight);
	});

	compareFunc(s_showSpotLightInfo, appSetting.ShowSpotLightInfo, [this](const auto& param) {
		if (param)
			jObject::AddDebugObject(SpotLightInfo);
		else
			jObject::RemoveDebugObject(SpotLightInfo);
	});

	compareFunc(s_showSpotLightOn, appSetting.SpotLightOn, [this](const auto& param) {
		if (param)
			MainCamera->AddLight(SpotLight);
		else
			MainCamera->RemoveLight(SpotLight);
	});
	
	// Update light direction and position by using UI Panel.
	if (DirectionalLight)
		DirectionalLight->Data.Direction = appSetting.DirecionalLightDirection;
	if (PointLight)
		PointLight->Data.Position = appSetting.PointLightPosition;
	if (SpotLight)
	{
		SpotLight->Data.Direction = appSetting.SpotLightDirection;
		SpotLight->Data.Position = appSetting.SpotLightPosition;
	}

	// Update visibility of shadowmap debuging by using UI Panel
	if (DirectionalLightShadowMapUIDebug)
		DirectionalLightShadowMapUIDebug->Visible = appSetting.ShowDirectionalLightMap;
}

void jGame::OnMouseButton()
{
	
}

void jGame::OnMouseMove(int32 xOffset, int32 yOffset)
{
	if (g_MouseState[EMouseButtonType::LEFT])
	{
		constexpr float PitchScale = 0.0025f;
		constexpr float YawScale = -0.0025f;
		MainCamera->SetEulerAngle(MainCamera->GetEulerAngle() + Vector(yOffset * PitchScale, xOffset * YawScale, 0.0f));
	}
}

void jGame::Teardown()
{
	if (Renderer)
		Renderer->Teardown();
}

void jGame::SpawnHairObjects()
{
	RemoveSpawnedObjects();

	auto hairObject = jHairModelLoader::GetInstance().CreateHairObject("Model/straight.hair");
	jObject::AddObject(hairObject);
	SpawnedObjects.push_back(hairObject);

	auto headModel = jModelLoader::GetInstance().LoadFromFile("Model/woman.x");
	jObject::AddObject(headModel);
	SpawnedObjects.push_back(headModel);
}

void jGame::SpawnTestPrimitives()
{
	RemoveSpawnedObjects();

	auto quad = jPrimitiveUtil::CreateQuad(Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), Vector(1000.0f, 1000.0f, 1000.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
	quad->SetPlane(jPlane(Vector(0.0, 1.0, 0.0), -0.1f));
	quad->SkipUpdateShadowVolume = true;
	jObject::AddObject(quad);
	SpawnedObjects.push_back(quad);

	auto gizmo = jPrimitiveUtil::CreateGizmo(Vector::ZeroVector, Vector::ZeroVector, Vector::OneVector);
	gizmo->SkipShadowMapGen = true;
	jObject::AddObject(gizmo);
	SpawnedObjects.push_back(gizmo);

	auto triangle = jPrimitiveUtil::CreateTriangle(Vector(60.0, 100.0, 20.0), Vector::OneVector, Vector(40.0, 40.0, 40.0), Vector4(0.5f, 0.1f, 1.0f, 1.0f));
	triangle->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(0.05f, 0.0f, 0.0f));
	};
	jObject::AddObject(triangle);
	SpawnedObjects.push_back(triangle);

	auto cube = jPrimitiveUtil::CreateCube(Vector(-60.0f, 55.0f, -20.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	cube->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(0.0f, 0.0f, 0.005f));
	};
	jObject::AddObject(cube);
	SpawnedObjects.push_back(cube);

	auto cube2 = jPrimitiveUtil::CreateCube(Vector(-65.0f, 35.0f, 10.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	jObject::AddObject(cube2);
	SpawnedObjects.push_back(cube2);

	auto capsule = jPrimitiveUtil::CreateCapsule(Vector(30.0f, 30.0f, -80.0f), 40.0f, 10.0f, 20, Vector(1.0f), Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	capsule->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(-0.01f, 0.0f, 0.0f));
	};
	jObject::AddObject(capsule);
	SpawnedObjects.push_back(capsule);

	auto cone = jPrimitiveUtil::CreateCone(Vector(0.0f, 50.0f, 60.0f), 40.0f, 20.0f, 15, Vector::OneVector, Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	cone->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(0.0f, 0.03f, 0.0f));
	};
	jObject::AddObject(cone);
	SpawnedObjects.push_back(cone);

	auto cylinder = jPrimitiveUtil::CreateCylinder(Vector(-30.0f, 60.0f, -60.0f), 20.0f, 10.0f, 20, Vector::OneVector, Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	cylinder->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(0.05f, 0.0f, 0.0f));
	};
	jObject::AddObject(cylinder);
	SpawnedObjects.push_back(cylinder);

	auto quad2 = jPrimitiveUtil::CreateQuad(Vector(-20.0f, 80.0f, 40.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	quad2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(0.0f, 0.0f, 0.08f));
	};
	jObject::AddObject(quad2);
	SpawnedObjects.push_back(quad2);

	auto sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f), 1.0, 16, Vector(30.0f), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
	sphere->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->SetRot(thisObject->RenderObject->GetRot() + Vector(0.0f, 0.0f, DegreeToRadian(180.0f)));
	};
	Sphere = sphere;
	jObject::AddObject(sphere);
	SpawnedObjects.push_back(sphere);

	auto sphere2 = jPrimitiveUtil::CreateSphere(Vector(150.0f, 5.0f, 0.0f), 1.0, 16, Vector(10.0f), Vector4(0.8f, 0.4f, 0.6f, 1.0f));
	sphere2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		const float startY = 5.0f;
		const float endY = 100;
		const float speed = 1.5f;
		static bool dir = true;
		auto Pos = thisObject->RenderObject->GetPos();
		Pos.y += dir ? speed : -speed;
		if (Pos.y < startY || Pos.y > endY)
		{
			dir = !dir;
			Pos.y += dir ? speed : -speed;
		}
		thisObject->RenderObject->SetPos(Pos);
	};
	Sphere = sphere2;
	jObject::AddObject(sphere2);
	SpawnedObjects.push_back(sphere2);

	auto billboard = jPrimitiveUtil::CreateBillobardQuad(Vector(0.0f, 60.0f, 80.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(1.0f, 0.0f, 1.0f, 1.0f), MainCamera);
	jObject::AddObject(billboard);
	SpawnedObjects.push_back(billboard);
}

void jGame::SpawnGraphTestFunc()
{
	Vector PerspectiveVector[90];
	Vector OrthographicVector[90];
	{
		{
			static jCamera* pCamera = jCamera::CreateCamera(Vector(0.0), Vector(0.0, 0.0, 1.0), Vector(0.0, 1.0, 0.0), DegreeToRadian(90), 10.0, 100.0, 100.0, 100.0, true);
			pCamera->UpdateCamera();
			int cnt = 0;
			auto MV = pCamera->Projection * pCamera->View;
			for (int i = 0; i < 90; ++i)
			{
				PerspectiveVector[cnt++] = MV.TransformPoint(Vector({ 0.0f, 0.0f, 10.0f + static_cast<float>(i) }));
			}

			for (int i = 0; i < _countof(PerspectiveVector); ++i)
				PerspectiveVector[i].z = (PerspectiveVector[i].z + 1.0f) * 0.5f;
		}
		{
			static jCamera* pCamera = jCamera::CreateCamera(Vector(0.0), Vector(0.0, 0.0, 1.0), Vector(0.0, 1.0, 0.0), DegreeToRadian(90), 10.0, 100.0, 100.0, 100.0, false);
			pCamera->UpdateCamera();
			int cnt = 0;
			auto MV = pCamera->Projection * pCamera->View;
			for (int i = 0; i < 90; ++i)
			{
				OrthographicVector[cnt++] = MV.TransformPoint(Vector({ 0.0f, 0.0f, 10.0f + static_cast<float>(i) }));
			}

			for (int i = 0; i < _countof(OrthographicVector); ++i)
				OrthographicVector[i].z = (OrthographicVector[i].z + 1.0f) * 0.5f;
		}
	}
	std::vector<Vector2> graph1;
	std::vector<Vector2> graph2;

	float scale = 100.0f;
	for (int i = 0; i < _countof(PerspectiveVector); ++i)
		graph1.push_back(Vector2(static_cast<float>(i*2), PerspectiveVector[i].z * scale));
	for (int i = 0; i < _countof(OrthographicVector); ++i)
		graph2.push_back(Vector2(static_cast<float>(i*2), OrthographicVector[i].z* scale));

	auto graphObj1 = jPrimitiveUtil::CreateGraph2D({ 360, 350 }, {360, 300}, graph1);
	jObject::AddUIDebugObject(graphObj1);

	auto graphObj2 = jPrimitiveUtil::CreateGraph2D({ 360, 700 }, { 360, 300 }, graph2);
	jObject::AddUIDebugObject(graphObj2);
}

void jGame::SapwnCubePrimitives()
{
	RemoveSpawnedObjects();

	for (int i = 0; i < 20; ++i)
	{
		float height = 5.0f * i;
		auto cube = jPrimitiveUtil::CreateCube(Vector(-500.0f + i * 50.0f, height / 2.0f, 20.0f), Vector::OneVector, Vector(10.0f, height, 20.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		jObject::AddObject(cube);
		SpawnedObjects.push_back(cube);
		cube = jPrimitiveUtil::CreateCube(Vector(-500.0f + i * 50.0f, height / 2.0f, 20.0f + i * 20.0f), Vector::OneVector, Vector(10.0f, height, 10.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		jObject::AddObject(cube);
		SpawnedObjects.push_back(cube);
		cube = jPrimitiveUtil::CreateCube(Vector(-500.0f + i * 50.0f, height / 2.0f, 20.0f - i * 20.0f), Vector::OneVector, Vector(20.0f, height, 10.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		jObject::AddObject(cube);
		SpawnedObjects.push_back(cube);
	}

	auto quad = jPrimitiveUtil::CreateQuad(Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), Vector(1000.0f, 1000.0f, 1000.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
	quad->SetPlane(jPlane(Vector(0.0, 1.0, 0.0), -0.1f));
	jObject::AddObject(quad);
	SpawnedObjects.push_back(quad);
}
