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
	if (g_KeyState['1']) MainCamera->RotateForwardAxis(-0.1f);
	if (g_KeyState['2']) MainCamera->RotateForwardAxis(0.1f);
	if (g_KeyState['3']) MainCamera->RotateUpAxis(-0.1f);
	if (g_KeyState['4']) MainCamera->RotateUpAxis(0.1f);
	if (g_KeyState['5']) MainCamera->RotateRightAxis(-0.1f);
	if (g_KeyState['6']) MainCamera->RotateRightAxis(0.1f);
	if (g_KeyState['w'] || g_KeyState['W']) MainCamera->MoveForward(1.0f);
	if (g_KeyState['s'] || g_KeyState['S']) MainCamera->MoveForward(-1.0f);
}

void jGame::Setup()
{
	const Vector mainCameraPos(172.66f, 166.47f, -180.63f);
	const Vector mainCameraTarget(171.96f, 166.02f, -180.05f);
	//const Vector mainCameraPos(50.0f, 50.0f, 50.0f);
	//const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
	MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 500.0f, SCR_WIDTH, SCR_HEIGHT, true);
	jCamera::AddCamera(0, MainCamera);

	// Light creation step
	DirectionalLight = jLight::CreateDirectionalLight(jShadowAppSettingProperties::GetInstance().DirecionalLightDirection, Vector4(0.5f, 0.5f, 0.5f, 1.0f), Vector(1.0f), Vector(0.4f), 64);
	AmbientLight = jLight::CreateAmbientLight(Vector(0.7f, 0.8f, 0.8f), Vector(0.2f));

	PointLight = jLight::CreatePointLight(jShadowAppSettingProperties::GetInstance().PointLightPosition, Vector4(2.0f, 0.7f, 0.7f, 1.0f), 500.0f, Vector(1.0f, 1.0f, 1.0f), Vector(0.4f, 0.4f, 0.4f), 64.0f);
	SpotLight = jLight::CreateSpotLight(jShadowAppSettingProperties::GetInstance().SpotLightPosition, jShadowAppSettingProperties::GetInstance().SpotLightDirection
		, Vector4(0.0f, 1.0f, 0.0f, 1.0f), 500.0f, 0.7f, 1.0f, Vector(1.0f, 1.0f, 1.0f), Vector(0.4f, 0.4f, 0.4f), 64.0f);

	MainCamera->AddLight(DirectionalLight);
	MainCamera->AddLight(PointLight);
	MainCamera->AddLight(SpotLight);
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

	auto quad = jPrimitiveUtil::CreateQuad(Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), Vector(1000.0f, 1000.0f, 1000.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
	quad->SetPlane(jPlane(Vector(0.0, 1.0, 0.0), -0.1f));
	//quad->SkipShadowMapGen = true;
	quad->SkipUpdateShadowVolume = true;
	g_StaticObjectArray.push_back(quad);

	auto gizmo = jPrimitiveUtil::CreateGizmo(Vector::ZeroVector, Vector::ZeroVector, Vector::OneVector);
	g_StaticObjectArray.push_back(gizmo);

	auto triangle = jPrimitiveUtil::CreateTriangle(Vector(60.0, 100.0, 20.0), Vector::OneVector, Vector(40.0, 40.0, 40.0), Vector4(0.5f, 0.1f, 1.0f, 1.0f));
	triangle->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->Rot.x += 0.05f;
	};
	g_StaticObjectArray.push_back(triangle);

	auto cube = jPrimitiveUtil::CreateCube(Vector(-60.0f, 55.0f, -20.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	cube->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->Rot.z += 0.005f;
	};
	g_StaticObjectArray.push_back(cube);
	
	auto cube2 = jPrimitiveUtil::CreateCube(Vector(-65.0f, 35.0f, 10.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	g_StaticObjectArray.push_back(cube2);

	auto capsule = jPrimitiveUtil::CreateCapsule(Vector(30.0f, 30.0f, -80.0f), 40.0f, 10.0f, 20, Vector(1.0f), Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	capsule->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->Rot.x -= 0.01f;
	};
	g_StaticObjectArray.push_back(capsule);

	auto cone = jPrimitiveUtil::CreateCone(Vector(0.0f, 50.0f, 60.0f), 40.0f, 20.0f, 15, Vector::OneVector, Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	cone->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->Rot.y += 0.03f;
	};
	g_StaticObjectArray.push_back(cone);

	auto cylinder = jPrimitiveUtil::CreateCylinder(Vector(-30.0f, 60.0f, -60.0f), 20.0f, 10.0f, 20, Vector::OneVector, Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	cylinder->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->Rot.x += 0.05f;
	};
	g_StaticObjectArray.push_back(cylinder);

	auto quad2 = jPrimitiveUtil::CreateQuad(Vector(-20.0f, 80.0f, 40.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	quad2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObject->Rot.z += 0.08f;
	};
	g_StaticObjectArray.push_back(quad2);

	auto sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f), 1.0, 16, Vector(30.0f), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
	sphere->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		//thisObject->RenderObject->Rot.z += 0.01f;
		thisObject->RenderObject->Rot.z = DegreeToRadian(180.0f);
	};
	Sphere = sphere;
	g_StaticObjectArray.push_back(sphere);

	auto sphere2 = jPrimitiveUtil::CreateSphere(Vector(150.0f, 5.0f, 0.0f), 1.0, 16, Vector(10.0f), Vector4(0.8f, 0.4f, 0.6f, 1.0f));
	sphere2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		const float startY = 5.0f;
		const float endY = 100;
		const float speed = 1.5f;
		static bool dir = true;
		thisObject->RenderObject->Pos.y += dir ? speed : -speed;
		if (thisObject->RenderObject->Pos.y < startY || thisObject->RenderObject->Pos.y > endY)
		{
			dir = !dir;
			thisObject->RenderObject->Pos.y += dir ? speed : -speed;
		}
	};
	Sphere = sphere2;
	g_StaticObjectArray.push_back(sphere2);

	auto billboard = jPrimitiveUtil::CreateBillobardQuad(Vector(0.0f, 60.0f, 80.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(1.0f, 0.0f, 1.0f, 1.0f), MainCamera);
	g_StaticObjectArray.push_back(billboard);

	auto directionalLightDebug = jPrimitiveUtil::CreateDirectionalLightDebug(Vector(100.0f), Vector::OneVector * 10.0f, 10.0f, MainCamera, DirectionalLight, "Image/sun.png");
	g_DebugObjectArray.push_back(directionalLightDebug);

	auto pointLightDebug = jPrimitiveUtil::CreatePointLightDebug(Vector(10.0f), MainCamera, PointLight, "Image/bulb.png");
	g_DebugObjectArray.push_back(pointLightDebug);

	auto spotLightDebug = jPrimitiveUtil::CreateSpotLightDebug(Vector(10.0f), MainCamera, SpotLight, "Image/spot.png");
	g_DebugObjectArray.push_back(spotLightDebug);

	//////////////////////////////////////////////////////////////////////////
	auto shadowVolumeRenderer = new jShadowVolumeRenderer();
	shadowVolumeRenderer->Setup();
	ShadowRendererMap[EShadowType::ShadowVolume] = shadowVolumeRenderer;

	auto shadowMapRenderer = new jShadowMapRenderer();
	shadowMapRenderer->Setup();
	ShadowRendererMap[EShadowType::ShadowMap] = shadowMapRenderer;
}

void jGame::Update(float deltaTime)
{
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
	DirectionalLight->ShadowMapData->ShadowMapCamera->UpdateCamera();
	for (auto& iter : PointLight->ShadowMapData->ShadowMapCamera)
		iter->UpdateCamera();
	for (auto& iter : SpotLight->ShadowMapData->ShadowMapCamera)
		iter->UpdateCamera();

	for (auto& iter : g_StaticObjectArray)
		iter->Update(deltaTime);

	for (auto& iter : g_BoundBoxObjectArray)
		iter->Update(deltaTime);

	for (auto& iter : g_BoundSphereObjectArray)
		iter->Update(deltaTime);

	for (auto& iter : g_DebugObjectArray)
		iter->Update(deltaTime);

	Renderer->Render(MainCamera);

	glFlush();
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
