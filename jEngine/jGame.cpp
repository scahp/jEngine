#include "pch.h"
#include "jGame.h"
#include "Math\Vector.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jLight.h"
#include "Scene/jRenderObject.h"
#include "Scene/jMeshObject.h"
#include "jPrimitiveUtil.h"
#include "FileLoader/jImageFileLoader.h"
#include "FileLoader/jModelLoader.h"
#include "FileLoader/jFile.h"
#include "RHI/jFrameBufferPool.h"
#include "RHI/jRenderTargetPool.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI\Vulkan\jTexture_Vulkan.h"
#include "Shader\Spirv\jSpirvHelper.h"
#include "RHI\Vulkan\jShader_Vulkan.h"
#include "RHI\Vulkan\jRHIType_Vulkan.h"

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
		CurrentPipelineStateInfo = (jPipelineStateInfo_Vulkan*)g_rhi_vk->CreatePipelineStateInfo(PipelineStateFixed, Shader, bPositionOnly ? RenderObject->VertexBuffer_PositionOnly : RenderObject->VertexBuffer, RenderPass, shaderBindings);
	}
	void Draw()
	{
		for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
		{
			ShaderBindingInstances[i]->Bind((VkPipelineLayout)CurrentPipelineStateInfo->GetPipelineLayoutHandle(), i);
		}

		// Bind Pipeline
		CurrentPipelineStateInfo->Bind();

		// Draw
		RenderObject->Draw(nullptr, Shader, {});
	}

	std::vector<const jShaderBindingInstance*> ShaderBindingInstances;

	jView* View = nullptr;
	jRenderPass* RenderPass = nullptr;
	jShader* Shader = nullptr;
	jRenderObject* RenderObject = nullptr;
	jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
	jPipelineStateInfo_Vulkan* CurrentPipelineStateInfo = nullptr;
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

    for (auto iter : jObject::GetStaticObject())
    {
        iter->Update(deltaTime);

        iter->RenderObject->UpdateWorldMatrix();
    }

    // 정리해야함
    DirectionalLight->Update(deltaTime);
}

void jGame::Draw()
{
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Draw");

    jCommandBuffer_Vulkan* commandBuffer = (jCommandBuffer_Vulkan*)g_rhi_vk->CommandBufferManager->GetOrCreateCommandBuffer();
    const uint32_t imageIndex = g_rhi_vk->BeginRenderFrame(commandBuffer);

    static std::shared_ptr<jRenderTarget> BasePassColorPtr[3];
	static std::shared_ptr<jRenderTarget> FinalColorPtr[3];
    {
        {
            auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
			jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create((EMSAASamples)g_rhi_vk->MsaaSamples);
            auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
            auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

            g_rhi_vk->CurrentPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
                , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));
        }

        {
            auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
            jMultisampleStateInfo* MultisampleState = TMultisampleStateInfo<true, 0.2f, false, false>::Create(EMSAASamples::COUNT_1);
            auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
            auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

            g_rhi_vk->CurrentPipelineStateFixed_Shadow = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
                , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT));
        }

        static jRenderPass_Vulkan* ShadowMapRenderPass = nullptr;
        if (!ShadowMapRenderPass)
        {
            DirectionalLight->ShadowMapPtr = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, /*ETextureFilter::LINEAR, ETextureFilter::LINEAR, */false, 1 }));

            const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

            jAttachment* depth = new jAttachment(DirectionalLight->ShadowMapPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE
				, ClearColor, ClearDepth, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);
            jAttachment* resolve = nullptr;

			g_rhi_vk->TransitionImageLayoutImmediate(DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);


            ShadowMapRenderPass = new jRenderPass_Vulkan(nullptr, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
            ShadowMapRenderPass->CreateRenderPass();
        }

        static std::vector<jRenderPass_Vulkan*> RenderPasses;

        // 동적인 부분들 패스에 따라 달라짐
        static bool s_Initializes = false;
        if (!s_Initializes)
        {
            s_Initializes = true;

            for (int32 i = 0; i < g_rhi_vk->Swapchain->Images.size(); ++i)
            {
                std::shared_ptr<jRenderTarget> DepthPtr = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
                    { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, /*ETextureFilter::LINEAR, ETextureFilter::LINEAR, */false, g_rhi_vk->MsaaSamples }));

                std::shared_ptr<jRenderTarget> ResolveColorPtr;

                const auto& extent = g_rhi_vk->Swapchain->GetExtent();
                const jSwapchainImage* image = g_rhi_vk->Swapchain->GetSwapchainImage(i);

                auto SwapChainRTPtr = jRenderTarget::CreateFromTexture(image->TexturePtr);

                if (g_rhi_vk->MsaaSamples > 1)
                {
                    BasePassColorPtr[i] = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
                        { ETextureType::TEXTURE_2D, ETextureFormat::BGRA8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->MsaaSamples }));

                    ResolveColorPtr = SwapChainRTPtr;
                }
                else
                {
                    BasePassColorPtr[i] = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
                        { ETextureType::TEXTURE_2D, ETextureFormat::BGRA8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->MsaaSamples }));

					FinalColorPtr[i] = SwapChainRTPtr;
					g_rhi_vk->TransitionImageLayoutImmediate(FinalColorPtr[i]->GetTexture(), EImageLayout::GENERAL);
                }

                const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
                const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

                jAttachment* color = new jAttachment(BasePassColorPtr[i], EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
					, EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
                jAttachment* depth = new jAttachment(DepthPtr, EAttachmentLoadStoreOp::CLEAR_DONTCARE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
					, EImageLayout::UNDEFINED, EImageLayout::DEPTH_STENCIL_ATTACHMENT);
                jAttachment* resolve = nullptr;

                if (g_rhi_vk->MsaaSamples > 1)
                {
                    resolve = new jAttachment(ResolveColorPtr, EAttachmentLoadStoreOp::DONTCARE_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth
						, EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
                }

                jRenderPass_Vulkan* renderPass = new jRenderPass_Vulkan(color, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
                renderPass->CreateRenderPass();
                RenderPasses.push_back(renderPass);
            }
        }

        // 정리 해야 함
        DirectionalLight->PrepareShaderBindingInstance();

#define USE_HLSL 1

#define STATIC_COMMAND 0

        // Shadow
        {
            static jShader* Shader = nullptr;
            if (!Shader)
            {
                jShaderInfo shaderInfo;
                shaderInfo.name = jName("shadow_test");
#if USE_HLSL
                shaderInfo.vs = jName("Resource/Shaders/hlsl/shadow_vs.hlsl");
                shaderInfo.fs = jName("Resource/Shaders/hlsl/shadow_fs.hlsl");
#else
                shaderInfo.vs = jName("Resource/Shaders/glsl/shadow_vs.glsl");
                shaderInfo.fs = jName("Resource/Shaders/glsl/shadow_fs.glsl");
#endif
                Shader = g_rhi->CreateShader(shaderInfo);
            }

            static jView View;
            View.Camera = DirectionalLight->GetLightCamra();
            View.DirectionalLight = DirectionalLight;

            g_rhi_vk->CurrentCommandBuffer = commandBuffer;

#if STATIC_COMMAND
            static std::vector<jDrawCommand<true>> ShadowPasses;
            if (ShadowPasses.size() <= 0)
#else
            std::vector<jDrawCommand<true>> ShadowPasses;
#endif
            {
                for (auto iter : jObject::GetStaticObject())
                {
                    auto newCommand = jDrawCommand<true>(&View, iter->RenderObject, ShadowMapRenderPass, Shader, &g_rhi_vk->CurrentPipelineStateFixed_Shadow, { });
                    newCommand.PrepareToDraw();
                    ShadowPasses.push_back(newCommand);
                }
            }

            //////////////////////////////////////////////////////////////////////////
            // 여기서 부터 렌더 로직 시작
            if (ensure(commandBuffer->Begin()))
            {
                g_rhi_vk->QueryPool.ResetQueryPool(commandBuffer);
				g_rhi_vk->TransitionImageLayout(commandBuffer, DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_ATTACHMENT);

                {
                    SCOPE_GPU_PROFILE(ShadowPass);
                    ShadowMapRenderPass->BeginRenderPass(commandBuffer);

                    for (auto& command : ShadowPasses)
                    {
                        command.Draw();
                    }

                    // Finishing up
                    ShadowMapRenderPass->EndRenderPass();
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
#if USE_HLSL
                shaderInfo.vs = jName("Resource/Shaders/hlsl/shader_vs.hlsl");
                shaderInfo.fs = jName("Resource/Shaders/hlsl/shader_fs.hlsl");
#else
                shaderInfo.vs = jName("Resource/Shaders/glsl/shader_vs.glsl");
                shaderInfo.fs = jName("Resource/Shaders/glsl/shader_fs.glsl");
#endif
                Shader = g_rhi->CreateShader(shaderInfo);
            }

            static jView View;
            View.Camera = MainCamera;
            View.DirectionalLight = DirectionalLight;

            g_rhi_vk->CurrentCommandBuffer = commandBuffer;

#if STATIC_COMMAND
            static std::vector<jDrawCommand<false>> BasePasses;
            if (BasePasses.size() <= 0)
#else
            std::vector<jDrawCommand<false>> BasePasses;
#endif
            {
                for (auto iter : jObject::GetStaticObject())
                {
                    auto newCommand = jDrawCommand<false>(&View, iter->RenderObject, RenderPasses[imageIndex], Shader, &g_rhi_vk->CurrentPipelineStateFixed, { });
                    newCommand.PrepareToDraw();
                    BasePasses.push_back(newCommand);
                }
            }

			g_rhi_vk->TransitionImageLayout(commandBuffer, DirectionalLight->ShadowMapPtr->GetTexture(), EImageLayout::DEPTH_STENCIL_READ_ONLY);
			g_rhi_vk->TransitionImageLayout(commandBuffer, BasePassColorPtr[imageIndex]->GetTexture(), EImageLayout::COLOR_ATTACHMENT);

            //////////////////////////////////////////////////////////////////////////
            // 여기서 부터 렌더 로직 시작
            //if (ensure(commandBuffer.Begin()))
            {
                SCOPE_GPU_PROFILE(BasePass);

                RenderPasses[imageIndex]->BeginRenderPass(commandBuffer);

                //vkCmdWriteTimestamp((VkCommandBuffer)commandBuffer.GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_rhi_vk->vkQueryPool, 0);

                for (auto command : BasePasses)
                {
                    command.Draw();
                }

                // Finishing up
                RenderPasses[imageIndex]->EndRenderPass();

                //vkCmdWriteTimestamp((VkCommandBuffer)commandBuffer.GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_rhi_vk->vkQueryPool, 1);
            }
            //ensure(commandBuffer->End());

            // 여기 까지 렌더 로직 끝
            //////////////////////////////////////////////////////////////////////////
        }
    }
	{
		g_rhi_vk->TransitionImageLayout(commandBuffer, BasePassColorPtr[imageIndex]->GetTexture(), EImageLayout::SHADER_READ_ONLY);
        g_rhi_vk->TransitionImageLayout(commandBuffer, FinalColorPtr[imageIndex]->GetTexture(), EImageLayout::GENERAL);

        static VkDescriptorSetLayout descriptorSetLayout[3] = { nullptr };

        if (!descriptorSetLayout[imageIndex])
        {
            VkDescriptorSetLayoutBinding setLayoutBindingReadOnly{};
            setLayoutBindingReadOnly.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            setLayoutBindingReadOnly.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            setLayoutBindingReadOnly.binding = 0;
            setLayoutBindingReadOnly.descriptorCount = 1;

            VkDescriptorSetLayoutBinding setLayoutBindingWriteOnly{};
            setLayoutBindingWriteOnly.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            setLayoutBindingWriteOnly.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            setLayoutBindingWriteOnly.binding = 1;
            setLayoutBindingWriteOnly.descriptorCount = 1;

            std::vector<VkDescriptorSetLayoutBinding> setlayoutBindings = {
                setLayoutBindingReadOnly,		// Binding 0 : Input image (read-only)
                setLayoutBindingWriteOnly,		// Binding 1: Output image (write)
            };

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
            descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCreateInfo.pBindings = setlayoutBindings.data();
            descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(setlayoutBindings.size());
            verify(VK_SUCCESS == vkCreateDescriptorSetLayout(g_rhi_vk->Device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout[imageIndex]));
        }

        static VkPipelineLayout pipelineLayout[3] = { nullptr };
        if (!pipelineLayout[imageIndex])
        {
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCreateInfo.setLayoutCount = 1;
            pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout[imageIndex];
            verify(VK_SUCCESS == vkCreatePipelineLayout(g_rhi_vk->Device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout[imageIndex]));
        }

        static VkDescriptorPool descriptorPool[3] = { nullptr };
        if (!descriptorPool[imageIndex])
        {
            VkDescriptorPoolSize descriptorPoolSize{};
            descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorPoolSize.descriptorCount = 3;

            VkDescriptorPoolSize descriptorPoolSize2{};
            descriptorPoolSize2.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            descriptorPoolSize2.descriptorCount = 3;

            std::vector<VkDescriptorPoolSize> poolSizes = {
                descriptorPoolSize,descriptorPoolSize2
            };

            VkDescriptorPoolCreateInfo descriptorPoolInfo{};
            descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolInfo.poolSizeCount = (uint32)poolSizes.size();
            descriptorPoolInfo.pPoolSizes = poolSizes.data();
            descriptorPoolInfo.maxSets = 3;
            verify(VK_SUCCESS == vkCreateDescriptorPool(g_rhi_vk->Device, &descriptorPoolInfo, nullptr, &descriptorPool[imageIndex]));
        }

        static VkDescriptorSet descriptorSet[3] = { nullptr };
        if (!descriptorSet[imageIndex])
        {
            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
            descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool = descriptorPool[imageIndex];
            descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout[imageIndex];
            descriptorSetAllocateInfo.descriptorSetCount = 1;
            verify(VK_SUCCESS == vkAllocateDescriptorSets(g_rhi_vk->Device, &descriptorSetAllocateInfo, &descriptorSet[imageIndex]));

            VkDescriptorImageInfo colorImageInfo{};
            colorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorImageInfo.imageView = (VkImageView)BasePassColorPtr[imageIndex]->GetViewHandle();
            colorImageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();

            VkWriteDescriptorSet writeDescriptorSet{};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.dstSet = descriptorSet[imageIndex];
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.pImageInfo = &colorImageInfo;
            writeDescriptorSet.descriptorCount = 1;

            VkDescriptorImageInfo targetImageInfo{};
            targetImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            targetImageInfo.imageView = (VkImageView)FinalColorPtr[imageIndex]->GetViewHandle();
            targetImageInfo.sampler = jTexture_Vulkan::CreateDefaultSamplerState();

            VkWriteDescriptorSet writeDescriptorSet2{};
            writeDescriptorSet2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet2.dstSet = descriptorSet[imageIndex];
            writeDescriptorSet2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writeDescriptorSet2.dstBinding = 1;
            writeDescriptorSet2.pImageInfo = &targetImageInfo;
            writeDescriptorSet2.descriptorCount = 1;

            std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets = {
                writeDescriptorSet,
                writeDescriptorSet2
            };
            vkUpdateDescriptorSets(g_rhi_vk->Device, (uint32)computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, nullptr);
        }

        static VkPipeline pipeline[3] = { nullptr };
        if (!pipeline[imageIndex])
        {
            VkComputePipelineCreateInfo computePipelineCreateInfo{};
            computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            computePipelineCreateInfo.layout = pipelineLayout[imageIndex];
            computePipelineCreateInfo.flags = 0;

            jShaderInfo shaderInfo;
            shaderInfo.name = jName("emboss");
            shaderInfo.cs = jName("Resource/Shaders/hlsl/emboss_cs.hlsl");
            static jShader_Vulkan* shader = (jShader_Vulkan*)g_rhi_vk->CreateShader(shaderInfo);
            computePipelineCreateInfo.stage = shader->ShaderStages[0];

            verify(VK_SUCCESS == vkCreateComputePipelines(g_rhi_vk->Device, g_rhi_vk->PipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline[imageIndex]));
        }

		vkCmdBindPipeline((VkCommandBuffer)commandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline[imageIndex]);
		vkCmdBindDescriptorSets((VkCommandBuffer)commandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout[imageIndex], 0, 1, &descriptorSet[imageIndex], 0, 0);
		vkCmdDispatch((VkCommandBuffer)commandBuffer->GetHandle(), g_rhi_vk->Swapchain->Extent.x / 16, g_rhi_vk->Swapchain->Extent.y / 16, 1);
	}
    jImGUI_Vulkan::Get().Draw(commandBuffer, imageIndex);
	ensure(commandBuffer->End());

    {
        g_rhi_vk->EndRenderFrame(commandBuffer);

        g_rhi_vk->CommandBufferManager->ReturnCommandBuffer(commandBuffer);
    }
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
