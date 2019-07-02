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
	jShaderInfo info;
	info.vs = "Shaders/color_only_vs.glsl";
	info.fs = "Shaders/color_only_fs.glsl";
	SimpleShader = jShader::CreateShader(info);

	info.vs = "Shaders/vs_shadowMap.glsl";
	info.fs = "Shaders/fs_shadowMap.glsl";
	ShadowMapShader = jShader::CreateShader(info);

	info.vs = "Shaders/shadowmap/vs.glsl";
	info.fs = "Shaders/shadowmap/fs.glsl";
	BaseShader = jShader::CreateShader(info);

	info.vs = "Shaders/shadowmap/vs_omniDirectionalShadowMap.glsl";
	info.fs = "Shaders/shadowmap/fs_omniDirectionalShadowMap.glsl";
	ShadowMapOmniShader = jShader::CreateShader(info);

	info.vs = "Shaders/shadowvolume/vs.glsl";
	info.fs = "Shaders/shadowvolume/fs.glsl";
	ShadowVolumeBaseShader = jShader::CreateShader(info);

	info.vs = "Shaders/shadowvolume/vs_infinityFar.glsl";
	info.fs = "Shaders/shadowvolume/fs_infinityFar.glsl";
	ShadowVolumeInfinityFarShader = jShader::CreateShader(info);

	jShaderInfo deepShadowMapInfo;
	deepShadowMapInfo.vs = "Shaders/shadowmap/vs_expDeepShadowMap.glsl";
	deepShadowMapInfo.fs = "Shaders/shadowmap/fs_expDeepShadowMap.glsl";
	ExpDeepShadowMapGenShader = g_rhi->CreateShader(deepShadowMapInfo);

	deepShadowMapInfo.vs = "Shaders/shadowmap/vs_shadowMap.glsl";
	deepShadowMapInfo.fs = "Shaders/shadowmap/fs_deepShadowMap.glsl";
	DeepShadowMapGenShader = g_rhi->CreateShader(deepShadowMapInfo);

	jShaderInfo Hair;
	Hair.vs = "shaders/shadowmap/vs_hair.glsl";
	Hair.fs = "shaders/shadowmap/fs_hair.glsl";
	Hair.vsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	Hair.fsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	Hair_Shader = jShader::CreateShader(Hair);

	jShaderInfo deepShadowFull;
	deepShadowFull.vsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	deepShadowFull.fsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	deepShadowFull.vs = "shaders/fullscreen/vs_deepshadow.glsl";
	deepShadowFull.fs = "shaders/fullscreen/fs_expdeepshadow.glsl";
	ExpDeepShadowFull_Shader = jShader::CreateShader(deepShadowFull);

	deepShadowFull.vs = "shaders/fullscreen/vs_deepshadow.glsl";
	deepShadowFull.fs = "shaders/fullscreen/fs_deepshadow.glsl";
	DeepShadowFull_Shader = jShader::CreateShader(deepShadowFull);

	jShaderInfo deepShadowAA;
	deepShadowAA.vs = "shaders/fullscreen/vs_deepshadow.glsl";
	deepShadowAA.fs = "shaders/fullscreen/fs_deepshadow_aa.glsl";
	DeepShadowAA_Shader = jShader::CreateShader(deepShadowAA);

	jShaderInfo Deferred;
	Deferred.vsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	Deferred.fsPreProcessor = "#define USE_TEXTURE 1\r\n#define USE_MATERIAL 1";
	Deferred.vs = "shaders/shadowmap/vs_deferred.glsl";
	Deferred.fs = "shaders/shadowmap/fs_expDeferred.glsl";
	ExpDeferred_Shader = jShader::CreateShader(Deferred);
	Deferred.vs = "shaders/shadowmap/vs_deferred.glsl";
	Deferred.fs = "shaders/shadowmap/fs_deferred.glsl";
	Deferred_Shader = jShader::CreateShader(Deferred);

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
}

void jGame::Update(float deltaTime)
{
	static jShaderInfo cs;
	cs.cs = "Shaders/compute/compute_example.glsl";
	static jShader_OpenGL* simpleComputeShader = nullptr;
	
	static jTexture_OpenGL* texture = nullptr;
	static jRenderTarget_OpenGL* renderTarget = nullptr;
	static jTexture_OpenGL* renderTarget_texture_rt = nullptr;
	static jImageData data;

	static jShaderInfo cs_sort;
	cs_sort.cs = "Shaders/compute/compute_sort_linkedlist.glsl";
	static jShader_OpenGL* cs_sortShader = nullptr;
	
	static jShaderInfo cs_link;
	cs_link.cs = "Shaders/compute/compute_link_linkedlist.glsl";
	static jShader_OpenGL* cs_linkShader = nullptr;

	struct shader_data_t
	{
		float camera_position[4] = { 1, 2, 3, 4 };
		float light_position[4] = { 5, 6, 7, 8 };
		float light_diffuse[4] = { 9, 10, 11, 12 };
	} shader_data;

	static jShaderStorageBufferObject_OpenGL* ssbo = nullptr;
	static jShaderStorageBufferObject_OpenGL* startElementBuf = nullptr;
	static jShaderStorageBufferObject_OpenGL* linkedListEntryDepthAlphaNext = nullptr;
	static jShaderStorageBufferObject_OpenGL* linkedListEntryNeighbors = nullptr;
	static jAtomicCounterBuffer_OpenGL* atomicBuffer = nullptr;

	const int32 linkedlistDepth = 50;
	auto linkedListDepthSize = SM_WIDTH * SM_HEIGHT * linkedlistDepth;

	if (!simpleComputeShader)
	{
		simpleComputeShader = static_cast<jShader_OpenGL*>(jShader::CreateShader(cs));
		cs_sortShader = static_cast<jShader_OpenGL*>(jShader::CreateShader(cs_sort));
		cs_linkShader = static_cast<jShader_OpenGL*>(jShader::CreateShader(cs_link));

		jImageFileLoader::GetInstance().LoadTextureFromFile(data, "Image/Sun.png");
		texture = static_cast<jTexture_OpenGL*>(g_rhi->CreateTextureFromData(&data.ImageData[0], data.Width, data.Height));
		renderTarget = static_cast<jRenderTarget_OpenGL*>(jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RGBA, EFormat::RGBA, EFormatType::BYTE, data.Width, data.Height, 1 }));
		renderTarget_texture_rt = static_cast<jTexture_OpenGL*>(renderTarget->GetTexture());

		ssbo = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("shader_data"));
		ssbo->UpdateBufferData(&shader_data, sizeof(shader_data));

		linkedListEntryDepthAlphaNext = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("LinkedListEntryDepthAlphaNext"));
		linkedListEntryDepthAlphaNext->UpdateBufferData(nullptr, linkedListDepthSize * (sizeof(float) * 2 + sizeof(uint32) * 2));

		startElementBuf = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("StartElementBufEntry"));
		startElementBuf->UpdateBufferData(nullptr, (linkedListDepthSize) * sizeof(int32));

		linkedListEntryNeighbors = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("LinkedListEntryNeighbors"));
		linkedListEntryNeighbors->UpdateBufferData(nullptr, linkedListDepthSize * sizeof(int32) * 2);

		atomicBuffer = static_cast<jAtomicCounterBuffer_OpenGL*>(g_rhi->CreateAtomicCounterBuffer("LinkedListCounter", 3));

		uint32 zero = 0;
		atomicBuffer->UpdateBufferData(&zero, sizeof(zero));
	}

	bool expDeepShadowMap = jShadowAppSettingProperties::GetInstance().ExponentDeepShadowOn;
	if (GBuffer->Begin())
	{
		g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

		auto Current_Deferred_Shader = expDeepShadowMap ? ExpDeferred_Shader : Deferred_Shader;

		for (auto& iter : g_StaticObjectArray)
			iter->Draw(MainCamera, Current_Deferred_Shader, nullptr);

		for (auto& iter : g_HairObjectArray)
			iter->Draw(MainCamera, Current_Deferred_Shader);		
		GBuffer->End();
	}

	//////////////////////////////////////////////////////////////////////////

	auto deepShadowMapGenShader = expDeepShadowMap ? ExpDeepShadowMapGenShader : DeepShadowMapGenShader;

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

		glUseProgram(static_cast<jShader_OpenGL*>(deepShadowMapGenShader)->program);
		ssbo->Bind(deepShadowMapGenShader);
		atomicBuffer->ClearBuffer(0);
		atomicBuffer->Bind(deepShadowMapGenShader);
		startElementBuf->ClearBuffer(-1);
		startElementBuf->Bind(deepShadowMapGenShader);
		linkedListEntryDepthAlphaNext->Bind(deepShadowMapGenShader);

		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.0f, 1.0f);

		for (auto& iter : g_StaticObjectArray)
		{
			if (!iter->SkipShadowMapGen)
				iter->Draw(DirectionalLight->ShadowMapData->ShadowMapCamera, deepShadowMapGenShader, DirectionalLight);
		}

		for (auto& iter : g_HairObjectArray)
			iter->Draw(DirectionalLight->ShadowMapData->ShadowMapCamera, deepShadowMapGenShader);
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	//////////////////////////////////////////////////////////////////////////

	glUseProgram(cs_sortShader->program);
	ssbo->Bind(cs_sortShader);
	atomicBuffer->Bind(cs_sortShader);
	startElementBuf->Bind(cs_sortShader);
	linkedListEntryDepthAlphaNext->Bind(cs_sortShader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH), cs_sortShader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT), cs_sortShader);
	glDispatchCompute(SM_WIDTH / 16, SM_HEIGHT / 8, 1);

	glUseProgram(cs_linkShader->program);
	ssbo->Bind(cs_linkShader);
	startElementBuf->Bind(cs_linkShader);
	linkedListEntryDepthAlphaNext->Bind(cs_linkShader);
	linkedListEntryNeighbors->Bind(cs_linkShader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH), cs_linkShader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT), cs_linkShader);
	glDispatchCompute(SM_WIDTH / 16, SM_HEIGHT / 8, 1);

	//g_rhi->SetRenderTarget(nullptr);
	//g_rhi->SetClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	//g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));
	//
	//ssbo->Bind(Hair_Shader);
	//startElementBuf->Bind(Hair_Shader);
	//linkedListEntryDepthAlphaNext->Bind(Hair_Shader);

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
	
	auto deepShadowFull_Shader = expDeepShadowMap ? ExpDeepShadowFull_Shader : DeepShadowFull_Shader;
	static auto fsQuad = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

	static auto rednerTargetAA = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RGBA, EFormat::RGBA, EFormatType::FLOAT, SCR_WIDTH, SCR_HEIGHT, 1 });
	if (rednerTargetAA)
	{
		if (rednerTargetAA->Begin())
		{
			g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
			g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

			glUseProgram(static_cast<jShader_OpenGL*>(deepShadowFull_Shader)->program);

			startElementBuf->Bind(deepShadowFull_Shader);
			linkedListEntryDepthAlphaNext->Bind(deepShadowFull_Shader);
			linkedListEntryNeighbors->Bind(deepShadowFull_Shader);
			fsQuad->Draw(MainCamera, deepShadowFull_Shader, DirectionalLight);
	
			rednerTargetAA->End();
		}
	}

	g_rhi->SetRenderTarget(nullptr);
	g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	fsQuad->RenderObject->tex_object = rednerTargetAA->GetTexture();
	fsQuad->Draw(MainCamera, DeepShadowAA_Shader, nullptr);

	for (auto& iter : g_DebugObjectArray)
	{
		iter->Update(deltaTime);
		iter->Draw(MainCamera, BaseShader, nullptr);
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
