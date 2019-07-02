#include "pch.h"
#include "jGame.h"
#include "jRHI_OpenGL.h"
#include "Math\Vector.h"
#include "jCamera.h"
#include "jObject.h"
#include "jLight.h"
#include "jPrimitiveUtil.h"
#include "jRHI.h"
#include "jRenderObject.h"
#include "jShadowVolume.h"
#include "jShadowVolumeRenderer.h"
#include "jShadowMapRenderer.h"
#include "jShadowAppProperties.h"
#include "jImageFileLoader.h"
#include "jHairModelLoader.h"
#include "jMeshObject.h"
#include "jModelLoader.h"
#include "jFile.h"
#include "jRenderTargetPool.h"
#include "glad\glad.h"

jRHI* g_rhi = nullptr;

jGame::jGame()
{
	g_rhi = new jRHI_OpenGL();
}

jGame::~jGame()
{
}

void jGame::ProcessInput()
{
	// Process Key Event
	if (g_KeyState['a'] || g_KeyState['A']) MainCamera->MoveShift(-1.0f);
	if (g_KeyState['d'] || g_KeyState['D']) MainCamera->MoveShift(1.0f);
	//if (g_KeyState['1']) MainCamera->RotateForwardAxis(-0.1f);
	//if (g_KeyState['2']) MainCamera->RotateForwardAxis(0.1f);
	//if (g_KeyState['3']) MainCamera->RotateUpAxis(-0.1f);
	//if (g_KeyState['4']) MainCamera->RotateUpAxis(0.1f);
	//if (g_KeyState['5']) MainCamera->RotateRightAxis(-0.1f);
	//if (g_KeyState['6']) MainCamera->RotateRightAxis(0.1f);
	if (g_KeyState['w'] || g_KeyState['W']) MainCamera->MoveForward(1.0f);
	if (g_KeyState['s'] || g_KeyState['S']) MainCamera->MoveForward(-1.0f);
}

void jGame::Setup()
{
	//////////////////////////////////////////////////////////////////////////
	g_rhi->EnableSRGB(true);

	//const Vector mainCameraPos(172.66f, 166.47f, -180.63f);
	//const Vector mainCameraTarget(171.96f, 166.02f, -180.05f);
	const Vector mainCameraPos(165.0f, 125.0f, -136.0f);
	const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
	MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 1.0f, 1000.0f, SCR_WIDTH, SCR_HEIGHT, true);
	jCamera::AddCamera(0, MainCamera);

	// Light creation step
	DirectionalLight = jLight::CreateDirectionalLight(jShadowAppSettingProperties::GetInstance().DirecionalLightDirection
		//, Vector4(0.5f, 0.5f, 0.5f, 1.0f)
		, Vector4(1.0f)
		, Vector(1.0f), Vector(1.0f), 64);
	//AmbientLight = jLight::CreateAmbientLight(Vector(0.7f, 0.8f, 0.8f), Vector(0.1f));
	AmbientLight = jLight::CreateAmbientLight(Vector(1.0f), Vector(0.1f));

	//PointLight = jLight::CreatePointLight(jShadowAppSettingProperties::GetInstance().PointLightPosition, Vector4(2.0f, 0.7f, 0.7f, 1.0f), 500.0f, Vector(1.0f, 1.0f, 1.0f), Vector(0.4f, 0.4f, 0.4f), 64.0f);
	//SpotLight = jLight::CreateSpotLight(jShadowAppSettingProperties::GetInstance().SpotLightPosition, jShadowAppSettingProperties::GetInstance().SpotLightDirection, Vector4(0.0f, 1.0f, 0.0f, 1.0f), 500.0f, 0.7f, 1.0f, Vector(1.0f, 1.0f, 1.0f), Vector(0.4f, 0.4f, 0.4f), 64.0f);

	auto directionalLightDebug = jPrimitiveUtil::CreateDirectionalLightDebug(Vector(250, 260, 0)*0.5f, Vector::OneVector * 10.0f, 10.0f, MainCamera, DirectionalLight, "Image/sun.png");
	g_DebugObjectArray.push_back(directionalLightDebug);

	//auto pointLightDebug = jPrimitiveUtil::CreatePointLightDebug(Vector(10.0f), MainCamera, PointLight, "Image/bulb.png");
	//g_DebugObjectArray.push_back(pointLightDebug);

	//auto spotLightDebug = jPrimitiveUtil::CreateSpotLightDebug(Vector(10.0f), MainCamera, SpotLight, "Image/spot.png");
	//g_DebugObjectArray.push_back(spotLightDebug);

	MainCamera->AddLight(DirectionalLight);
	//MainCamera->AddLight(PointLight);
	//MainCamera->AddLight(SpotLight);
	MainCamera->AddLight(AmbientLight);

	// Shader creation step
	CREATE_SHADER_VS_FS("Simple", "Shaders/color_only_vs.glsl", "Shaders/color_only_fs.glsl");
	CREATE_SHADER_VS_FS("ShadowMap", "Shaders/vs_shadowMap.glsl", "Shaders/fs_shadowMap.glsl");
	CREATE_SHADER_VS_FS("Base", "Shaders/shadowmap/vs.glsl", "Shaders/shadowmap/fs.glsl");
	CREATE_SHADER_VS_FS("ShadowMapOmni", "Shaders/shadowmap/vs_omniDirectionalShadowMap.glsl", "Shaders/shadowmap/fs_omniDirectionalShadowMap.glsl");
	CREATE_SHADER_VS_FS("ShadowVolumeBase", "Shaders/shadowvolume/vs.glsl", "Shaders/shadowvolume/fs.glsl");
	CREATE_SHADER_VS_FS("ShadowVolumeInfinityFar", "Shaders/shadowvolume/vs_infinityFar.glsl", "Shaders/shadowvolume/fs_infinityFar.glsl");
	CREATE_SHADER_VS_FS("ExpDeepShadowMapGen", "Shaders/shadowmap/vs_expDeepShadowMap.glsl", "Shaders/shadowmap/fs_expDeepShadowMap.glsl");
	CREATE_SHADER_VS_FS("DeepShadowMapGen", "Shaders/shadowmap/vs_shadowMap.glsl", "Shaders/shadowmap/fs_deepShadowMap.glsl");
	CREATE_SHADER_VS_FS_WITH_OPTION("Hair", "shaders/shadowmap/vs_hair.glsl", "shaders/shadowmap/fs_hair.glsl", true, true);
	CREATE_SHADER_VS_FS_WITH_OPTION("ExpDeepShadowFull", "shaders/fullscreen/vs_deepshadow.glsl", "shaders/fullscreen/fs_expdeepshadow.glsl", true, true);
	CREATE_SHADER_VS_FS("DeepShadowFull", "shaders/fullscreen/vs_deepshadow.glsl", "shaders/fullscreen/fs_deepshadow.glsl");
	CREATE_SHADER_VS_FS("DeepShadowAA", "shaders/fullscreen/vs_deepshadow.glsl", "shaders/fullscreen/fs_deepshadow_aa.glsl");
	CREATE_SHADER_VS_FS_WITH_OPTION("ExpDeferred", "shaders/shadowmap/vs_deferred.glsl", "shaders/shadowmap/fs_expDeferred.glsl", true, true);
	CREATE_SHADER_VS_FS_WITH_OPTION("Deferred", "shaders/shadowmap/vs_deferred.glsl", "shaders/shadowmap/fs_deferred.glsl", true, true);
	CREATE_SHADER_CS("cs", "Shaders/compute/compute_example.glsl");
	CREATE_SHADER_CS("cs_sort", "Shaders/compute/compute_sort_linkedlist.glsl");
	CREATE_SHADER_CS("cs_link", "Shaders/compute/compute_link_linkedlist.glsl");

	//auto quad = jPrimitiveUtil::CreateQuad(Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), Vector(1000.0f, 1000.0f, 1000.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
	//quad->SetPlane(jPlane(Vector(0.0, 1.0, 0.0), -0.1f));
	////quad->SkipShadowMapGen = true;
	//quad->SkipUpdateShadowVolume = true;
	//g_StaticObjectArray.push_back(quad);

	//auto gizmo = jPrimitiveUtil::CreateGizmo(Vector::ZeroVector, Vector::ZeroVector, Vector::OneVector);
	//g_StaticObjectArray.push_back(gizmo);

	//auto triangle = jPrimitiveUtil::CreateTriangle(Vector(60.0, 100.0, 20.0), Vector::OneVector, Vector(40.0, 40.0, 40.0), Vector4(0.5f, 0.1f, 1.0f, 1.0f));
	//triangle->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	thisObject->RenderObject->Rot.x += 0.05f;
	//};
	//g_StaticObjectArray.push_back(triangle);

	//auto cube = jPrimitiveUtil::CreateCube(Vector(-60.0f, 55.0f, -20.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	//cube->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	thisObject->RenderObject->Rot.z += 0.005f;
	//};
	//g_StaticObjectArray.push_back(cube);
	//
	//auto cube2 = jPrimitiveUtil::CreateCube(Vector(-65.0f, 35.0f, 10.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	//g_StaticObjectArray.push_back(cube2);

	//auto capsule = jPrimitiveUtil::CreateCapsule(Vector(30.0f, 30.0f, -80.0f), 40.0f, 10.0f, 20, Vector(1.0f), Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	//capsule->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	thisObject->RenderObject->Rot.x -= 0.01f;
	//};
	//g_StaticObjectArray.push_back(capsule);

	//auto cone = jPrimitiveUtil::CreateCone(Vector(0.0f, 50.0f, 60.0f), 40.0f, 20.0f, 15, Vector::OneVector, Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	//cone->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	thisObject->RenderObject->Rot.y += 0.03f;
	//};
	//g_StaticObjectArray.push_back(cone);

	//auto cylinder = jPrimitiveUtil::CreateCylinder(Vector(-30.0f, 60.0f, -60.0f), 20.0f, 10.0f, 20, Vector::OneVector, Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	//cylinder->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	thisObject->RenderObject->Rot.x += 0.05f;
	//};
	//g_StaticObjectArray.push_back(cylinder);

	//auto quad2 = jPrimitiveUtil::CreateQuad(Vector(-20.0f, 80.0f, 40.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	//quad2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	thisObject->RenderObject->Rot.z += 0.08f;
	//};
	//g_StaticObjectArray.push_back(quad2);

	//auto sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f), 1.0, 16, Vector(30.0f), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
	//sphere->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	//thisObject->RenderObject->Rot.z += 0.01f;
	//	thisObject->RenderObject->Rot.z = DegreeToRadian(180.0f);
	//};
	//Sphere = sphere;
	//g_StaticObjectArray.push_back(sphere);

	//auto sphere2 = jPrimitiveUtil::CreateSphere(Vector(150.0f, 5.0f, 0.0f), 1.0, 16, Vector(10.0f), Vector4(0.8f, 0.4f, 0.6f, 1.0f));
	//sphere2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	//{
	//	const float startY = 5.0f;
	//	const float endY = 100;
	//	const float speed = 1.5f;
	//	static bool dir = true;
	//	thisObject->RenderObject->Pos.y += dir ? speed : -speed;
	//	if (thisObject->RenderObject->Pos.y < startY || thisObject->RenderObject->Pos.y > endY)
	//	{
	//		dir = !dir;
	//		thisObject->RenderObject->Pos.y += dir ? speed : -speed;
	//	}
	//};
	//Sphere = sphere2;
	//g_StaticObjectArray.push_back(sphere2);

	//auto billboard = jPrimitiveUtil::CreateBillobardQuad(Vector(0.0f, 60.0f, 80.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(1.0f, 0.0f, 1.0f, 1.0f), MainCamera);
	//g_StaticObjectArray.push_back(billboard);

	auto hairObject = jHairModelLoader::GetInstance().CreateHairObject("Model/straight.hair");
	g_HairObjectArray.push_back(hairObject);

	auto headModel = jModelLoader::GetInstance().LoadFromFile("Model/woman.x");
	g_StaticObjectArray.push_back(headModel);

	//auto quad1 = jPrimitiveUtil::CreateQuad(Vector(0.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	//quad1->RenderObject->Rot = jShadowAppSettingProperties::GetInstance().DirecionalLightDirection.GetEulerAngleFrom();
	//g_HairObjectArray.push_back(quad1);

	//auto quad2 = jPrimitiveUtil::CreateQuad(Vector(1.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	//quad2->RenderObject->Rot = jShadowAppSettingProperties::GetInstance().DirecionalLightDirection.GetEulerAngleFrom();
	//g_HairObjectArray.push_back(quad2);

	//auto quad3 = jPrimitiveUtil::CreateQuad(Vector(2.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	//quad3->RenderObject->Rot = jShadowAppSettingProperties::GetInstance().DirecionalLightDirection.GetEulerAngleFrom();
	//g_HairObjectArray.push_back(quad3);

	//////////////////////////////////////////////////////////////////////////
	auto shadowVolumeRenderer = new jShadowVolumeRenderer();
	shadowVolumeRenderer->Setup();
	ShadowRendererMap[EShadowType::ShadowVolume] = shadowVolumeRenderer;

	auto shadowMapRenderer = new jShadowMapRenderer();
	shadowMapRenderer->Setup();
	ShadowRendererMap[EShadowType::ShadowMap] = shadowMapRenderer;

	GBuffer = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RGBA32F, EFormat::RGBA, EFormatType::FLOAT, SCR_WIDTH, SCR_HEIGHT, 4 });

	ssbo = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("shader_data"));
	ssbo->UpdateBufferData(&shader_data, sizeof(shader_data));

	linkedListEntryDepthAlphaNext = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("LinkedListEntryDepthAlphaNext"));
	linkedListEntryDepthAlphaNext->UpdateBufferData(nullptr, linkedListDepthSize* (sizeof(float) * 2 + sizeof(uint32) * 2));

	startElementBuf = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("StartElementBufEntry"));
	startElementBuf->UpdateBufferData(nullptr, (linkedListDepthSize) * sizeof(int32));

	linkedListEntryNeighbors = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("LinkedListEntryNeighbors"));
	linkedListEntryNeighbors->UpdateBufferData(nullptr, linkedListDepthSize * sizeof(int32) * 2);

	atomicBuffer = static_cast<jAtomicCounterBuffer_OpenGL*>(g_rhi->CreateAtomicCounterBuffer("LinkedListCounter", 3));

	uint32 zero = 0;
	atomicBuffer->UpdateBufferData(&zero, sizeof(zero));

	FullscreenQuad = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
}

void jGame::Update(float deltaTime)
{
	atomicBuffer->ClearBuffer(0);
	startElementBuf->ClearBuffer(-1);

	bool expDeepShadowMap = jShadowAppSettingProperties::GetInstance().ExponentDeepShadowOn;
	if (GBuffer->Begin())
	{
		g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

		if (auto Current_Deferred_Shader = expDeepShadowMap ? jShader::GetShader("ExpDeferred") : jShader::GetShader("Deferred"))
		{
			for (auto& iter : g_StaticObjectArray)
				iter->Draw(MainCamera, Current_Deferred_Shader, nullptr);

			for (auto& iter : g_HairObjectArray)
				iter->Draw(MainCamera, Current_Deferred_Shader);
		}

		GBuffer->End();
	}

	//////////////////////////////////////////////////////////////////////////

	{
		//////////////////////////////////////////////////////////////////////////
		MainCamera->UpdateCamera();
		if (DirectionalLight)
			DirectionalLight->ShadowMapData->ShadowMapCamera->UpdateCamera();
		//////////////////////////////////////////////////////////////////////////

		// 1.1 Directional Light ShadowMap Generation
		g_rhi->SetRenderTarget(DirectionalLight->ShadowMapData->ShadowMapRenderTarget);
		g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

		g_rhi->EnableDepthTest(true);
		g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);

		if (auto deepShadowMapGenShader = expDeepShadowMap ? jShader::GetShader("ExpDeepShadowMapGen") : jShader::GetShader("DeepShadowMapGen"))
		{
			g_rhi->SetShader(deepShadowMapGenShader);
			ssbo->Bind(deepShadowMapGenShader);
			atomicBuffer->Bind(deepShadowMapGenShader);
			startElementBuf->Bind(deepShadowMapGenShader);
			linkedListEntryDepthAlphaNext->Bind(deepShadowMapGenShader);

			g_rhi->EnableDepthBias(true);
			g_rhi->SetDepthBias(1.0f, 1.0f);

			for (auto& iter : g_StaticObjectArray)
			{
				if (!iter->SkipShadowMapGen)
					iter->Draw(DirectionalLight->ShadowMapData->ShadowMapCamera, deepShadowMapGenShader, DirectionalLight);
			}

			for (auto& iter : g_HairObjectArray)
				iter->Draw(DirectionalLight->ShadowMapData->ShadowMapCamera, deepShadowMapGenShader);
			
			g_rhi->EnableDepthBias(false);
		}
	}
	//////////////////////////////////////////////////////////////////////////

	if (auto shader = jShader::GetShader("cs_sort"))
	{
		g_rhi->SetShader(shader);
		ssbo->Bind(shader);
		atomicBuffer->Bind(shader);
		startElementBuf->Bind(shader);
		linkedListEntryDepthAlphaNext->Bind(shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT), shader);
		g_rhi->DispatchCompute(SM_WIDTH / 16, SM_HEIGHT / 8, 1);
	}

	if (auto shader = jShader::GetShader("cs_link"))
	{
		g_rhi->SetShader(shader);
		ssbo->Bind(shader);
		startElementBuf->Bind(shader);
		linkedListEntryDepthAlphaNext->Bind(shader);
		linkedListEntryNeighbors->Bind(shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT), shader);
		g_rhi->DispatchCompute(SM_WIDTH / 16, SM_HEIGHT / 8, 1);
	}

	// todo debug test, should remove this
	if (DirectionalLight)
		DirectionalLight->Data.Direction = jShadowAppSettingProperties::GetInstance().DirecionalLightDirection;
	if (PointLight)
		PointLight->Data.Position = jShadowAppSettingProperties::GetInstance().PointLightPosition;
	if (SpotLight)
	{
		SpotLight->Data.Direction = jShadowAppSettingProperties::GetInstance().SpotLightDirection;
		SpotLight->Data.Position = jShadowAppSettingProperties::GetInstance().SpotLightPosition;
	}

	MainCamera->UpdateCamera();
	if (DirectionalLight)
		DirectionalLight->ShadowMapData->ShadowMapCamera->UpdateCamera();
	if (PointLight)
	{
		for (auto& iter : PointLight->ShadowMapData->ShadowMapCamera)
			iter->UpdateCamera();
	}
	if (SpotLight)
	{
		for (auto& iter : SpotLight->ShadowMapData->ShadowMapCamera)
			iter->UpdateCamera();
	}
	
	static auto rednerTargetAA = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RGBA, EFormat::RGBA, EFormatType::FLOAT, SCR_WIDTH, SCR_HEIGHT, 1 });
	if (rednerTargetAA)
	{
		if (rednerTargetAA->Begin())
		{
			g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
			g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

			if (auto deepShadowFull_Shader = expDeepShadowMap ? jShader::GetShader("ExpDeepShadowFull") : jShader::GetShader("DeepShadowFull"))
			{
				g_rhi->SetShader(deepShadowFull_Shader);
				startElementBuf->Bind(deepShadowFull_Shader);
				linkedListEntryDepthAlphaNext->Bind(deepShadowFull_Shader);
				linkedListEntryNeighbors->Bind(deepShadowFull_Shader);
				FullscreenQuad->Draw(MainCamera, deepShadowFull_Shader, DirectionalLight);
			}
	
			rednerTargetAA->End();
		}
	}

	g_rhi->SetRenderTarget(nullptr);
	g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	FullscreenQuad->RenderObject->tex_object = rednerTargetAA->GetTexture();
	if (auto shader = jShader::GetShader("DeepShadowAA"))
		FullscreenQuad->Draw(MainCamera, shader, nullptr);

	if (auto shader = jShader::GetShader("Base"))
	{
		for (auto& iter : g_DebugObjectArray)
		{
			iter->Update(deltaTime);
			iter->Draw(MainCamera, shader, nullptr);
		}
	}

	return;

	for (auto& iter : g_BoundBoxObjectArray)
		iter->Update(deltaTime);

	for (auto& iter : g_BoundSphereObjectArray)
		iter->Update(deltaTime);

	Renderer->Render(MainCamera);
}

void jGame::OnMouseMove(int32 xOffset, int32 yOffset)
{
	if (g_MouseState[EMouseButtonType::LEFT])
	{
		if (abs(xOffset))
			MainCamera->RotateYAxis(xOffset * -0.005f);
		if (abs(yOffset))
			MainCamera->RotateRightAxis(yOffset * -0.005f);
	}
}

void jGame::Teardown()
{
	Renderer->Teardown();
}

void jGame::UpdateSettings()
{
	if (ShadowType != jShadowAppSettingProperties::GetInstance().ShadowType)
	{
		ShadowType = jShadowAppSettingProperties::GetInstance().ShadowType;

		switch (jShadowAppSettingProperties::GetInstance().ShadowType)
		{
		case EShadowType::ShadowVolume:
			Renderer = ShadowRendererMap[EShadowType::ShadowVolume];
			break;
		case EShadowType::ShadowMap:
			Renderer = ShadowRendererMap[EShadowType::ShadowMap];
			break;
		}

		jShadowAppSettingProperties::GetInstance().SwitchShadowType(jAppSettings::GetInstance().Get("MainPannel"));
	}

	Renderer->UpdateSettings();
}
