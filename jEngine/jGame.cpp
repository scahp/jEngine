#include "pch.h"
#include "jGame.h"
#include "Math/Vector.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jRenderObject.h"
#include "Profiler/jPerformanceProfile.h"
#include "Renderer/jRenderer.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "FileLoader/jModelLoader.h"
#include "Scene/jMeshObject.h"
#include "FileLoader/jImageFileLoader.h"
#include "Renderer/jSceneRenderTargets.h"    // 임시
#include "dxcapi.h"
#include "RHI/jRaytracingScene.h"
#include "Renderer/jDirectionalLightDrawCommandGenerator.h"
#include "Renderer/jPointLightDrawCommandGenerator.h"
#include "Renderer/jSpotLightDrawCommandGenerator.h"
#include "PathTracingDataLoader/jPathTracingData.h"
#include "PathTracingDataLoader/PathTracingDataLoader.h"
#include "PathTracingDataLoader/GLTFLoader.h"
#include "Renderer/jRenderer_PathTracing.h"

jRHI* g_rhi = nullptr;
jObject* jGame::Sphere = nullptr;

jGame::jGame()
{
}

jGame::~jGame()
{
}

void jGame::ProcessInput(float deltaTime)
{
	//static float MoveDistancePerSecond = 200.0f;
	static float MoveDistancePerSecond = 1.0f;
	const float CurrentDistance = MoveDistancePerSecond * deltaTime;

	// Process Key Event
	if (g_KeyState['a'] || g_KeyState['A']) MainCamera->MoveShift(-CurrentDistance);
	if (g_KeyState['d'] || g_KeyState['D']) MainCamera->MoveShift(CurrentDistance);
	//if (g_KeyState['1']) MainCamera->RotateForwardAxis(-0.1f);
	//if (g_KeyState['2']) MainCamera->RotateForwardAxis(0.1f);
	//if (g_KeyState['3']) MainCamera->RotateUpAxis(-0.1f);
	//if (g_KeyState['4']) MainCamera->RotateUpAxis(0.1f);
	//if (g_KeyState['5']) MainCamera->RotateRightAxis(-0.1f);
	//if (g_KeyState['6']) MainCamera->RotateRightAxis(0.1f);
	if (g_KeyState['w'] || g_KeyState['W']) MainCamera->MoveForward(CurrentDistance);
	if (g_KeyState['s'] || g_KeyState['S']) MainCamera->MoveForward(-CurrentDistance);
	if (g_KeyState['+']) MoveDistancePerSecond = Max(MoveDistancePerSecond + 10.0f, 0.0f);
	if (g_KeyState['-']) MoveDistancePerSecond = Max(MoveDistancePerSecond - 10.0f, 0.0f);
}

void jGame::Setup()
{
 	srand(static_cast<uint32>(time(NULL)));

#if ENABLE_PBR
	// PBR will use light color as a flux,
	float LightColorScale = 20000.0f;
#else
	float LightColorScale = 1.0f;
#endif

	static bool LoadPathTracing = true;

#if USE_SPONZA
	// Create main camera
    const Vector mainCameraPos(-559.937622f, 116.339653f, 84.3709946f);
    const Vector mainCameraTarget(-260.303925f, 105.498116f, 94.4834976f);
    MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 5000.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
    jCamera::AddCamera(0, MainCamera);

    // Create lights
	if (!LoadPathTracing)
	{
		NormalDirectionalLight = jLight::CreateDirectionalLight(Vector(0.1f, -0.5f, 0.1f) // AppSettings.DirecionalLightDirection
			, Vector4(30.0f), Vector(1.0f), Vector(1.0f), 64);
		PointLight = jLight::CreatePointLight(Vector(10.0f, 100.0f, 10.0f), Vector4(1.0f, 0.75f, 0.75f, 1.0f) * LightColorScale, 1500.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
		SpotLight = jLight::CreateSpotLight(Vector(0.0f, 60.0f, 5.0f), Vector(1.0f, -1.0f, 0.4f).GetNormalize(), Vector4(0.0f, 1.0f, 0.0f, 1.0f) * LightColorScale, 2000.0f, 0.35f, 1.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
	}
#else
	// Create main camera
	//const Vector mainCameraPos(-111.6f, 17.49f, 3.11f);
	//const Vector mainCameraTarget(282.378632f, 17.6663227f, -1.00448179f);
    const Vector mainCameraPos(172.66f, 160.0f, -180.63f);
    const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
    MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 1500.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
    jCamera::AddCamera(0, MainCamera);

    // Create lights
    NormalDirectionalLight = jLight::CreateDirectionalLight(Vector(-1.0f, -1.0f, -0.3f) // AppSettings.DirecionalLightDirection
        , Vector4(0.05f), Vector(1.0f), Vector(1.0f), 64);
    //CascadeDirectionalLight = jLight::CreateCascadeDirectionalLight(AppSettings.DirecionalLightDirection
    //	, Vector4(0.6f), Vector(1.0f), Vector(1.0f), 64);
    //AmbientLight = jLight::CreateAmbientLight(Vector(0.2f, 0.5f, 1.0f), Vector(0.05f));		// sky light color
    PointLight = jLight::CreatePointLight(Vector(10.0f, 100.0f, 10.0f), Vector4(1.0f, 0.75f, 0.75f, 1.0f) * LightColorScale, 150.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
    SpotLight = jLight::CreateSpotLight(Vector(0.0f, 80.0f, 5.0f), Vector(1.0f, -1.0f, 0.4f).GetNormalize(), Vector4(0.2f, 1.0f, 0.2f, 1.0f) * LightColorScale, 200.0f, 0.35f, 0.5f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
#endif

	if (NormalDirectionalLight)
		jLight::AddLights(NormalDirectionalLight);
	if (PointLight)
		jLight::AddLights(PointLight);
	if (SpotLight)
		jLight::AddLights(SpotLight);

	//PointLight->IsShadowCaster = false;
	//SpotLight->IsShadowCaster = false;

    //auto cube = jPrimitiveUtil::CreateCube(Vector(0.0f, 60.0f, 5.0f), Vector::OneVector, Vector::OneVector * 10.f, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
    //jObject::AddObject(cube);
    //SpawnedObjects.push_back(cube);

	// Select one of directional light
	DirectionalLight = NormalDirectionalLight;

	// Create light info for debugging light infomation
    if (DirectionalLight)
    {
        DirectionalLightInfo = jPrimitiveUtil::CreateDirectionalLightDebug(Vector(250, 400, 0) * 0.5f, Vector::OneVector * 10.0f, 10.0f, MainCamera, DirectionalLight, "Image/sun.png");
        // jObject::AddDebugObject(DirectionalLightInfo);
		jObject::AddDebugObject(DirectionalLightInfo->BillboardObject);
		// jObject::AddDebugObject(DirectionalLightInfo->ArrowSegementObject);
    }

    if (PointLight)
    {
        PointLightInfo = jPrimitiveUtil::CreatePointLightDebug(Vector(10.0f), MainCamera, PointLight, "Image/bulb.png");
        jObject::AddDebugObject(PointLightInfo->BillboardObject);
    }

    if (SpotLight)
    {
        SpotLightInfo = jPrimitiveUtil::CreateSpotLightDebug(Vector(10.0f), MainCamera, SpotLight, "Image/spot.png");
        jObject::AddDebugObject(SpotLightInfo->BillboardObject);
    }

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
#if !USE_SPONZA
	SpawnObjects(ESpawnedType::TestPrimitive);
#endif
	//SpawnObjects(ESpawnedType::InstancingPrimitive);
	//SpawnObjects(ESpawnedType::IndirectDrawPrimitive);

	//ResourceLoadCompleteEvent = std::async(std::launch::async, [&]()
	//{
#if USE_SPONZA
	if (!LoadPathTracing)
	{
	#if USE_SPONZA_PBR		
	Sponza = jModelLoader::GetInstance().LoadFromFile("Resource/sponza_pbr/sponza.glb", "Resource/sponza_pbr");
	#else
	Sponza = jModelLoader::GetInstance().LoadFromFile("Resource/sponza/sponza.dae", "Resource/");
	#endif
	jObject::AddObject(Sponza);
	SpawnedObjects.push_back(Sponza);

    for (int32 i = 0; i < 1; ++i)
    {
        Sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f + i * 100), 1.0, 60, 30, Vector(30.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        jObject::AddObject(Sphere);
        SpawnedObjects.push_back(Sphere);
    }
	}

		//auto random_double = []() -> float
		//{
		//	// Returns a random real in [0,1).
		//	return rand() / (RAND_MAX + 1.0f);
		//};

		//int32 cnt = 0;

		//srand(123);

  //      // Plane
  //      {
  //          auto NewPrimitive = jPrimitiveUtil::CreateQuad(Vector(0.0f, -1.0f, 0.0f), Vector(1.0f), Vector(200.0f), Vector4::ColorWhite);
  //          jObject::AddObject(NewPrimitive);
  //          SpawnedObjects.push_back(NewPrimitive);
  //      }

		//// Small Sphere
		//const float radius = 0.3f;
  //      int32 w = 11, h = 11;
  //      int32 totalCount = (w * 2 * h * 2) + 3 + 1;     // small balls, big balls, plane
		//for (int32 i = -w; i < w; ++i)
		//{
		//	for (int32 j = -h; j < h; ++j, ++cnt)
		//	{
		//		float r = radius;
		//		auto t = Vector(
		//			(float)(i * radius * 5.0f) + (radius * 4.0f * random_double())
		//			, -0.7f
		//			, (float)(j * radius * 5.0f) + (radius * 4.0f * random_double()));

		//		auto NewPrimitive = jPrimitiveUtil::CreateSphere(t, 1.0, 38, 16, Vector(r), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
  //              jObject::AddObject(NewPrimitive);
  //              SpawnedObjects.push_back(NewPrimitive);
		//	}
		//}

		//// Big Sphere
  //      for (int32 i = 0; i < 3; ++i)
  //      {
  //          auto s = XMMatrixScaling(1.0f, 1.0f, 1.0f);
  //          auto t = Vector(0.0f + i * 2, 0.0f, 0.0f + i * 2);
  //          
		//	auto NewPrimitive = jPrimitiveUtil::CreateSphere(t, 1.0, 38, 16, Vector(1.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		//	jObject::AddObject(NewPrimitive);
		//	SpawnedObjects.push_back(NewPrimitive);
  //      }

        //auto sphere2 = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f + 130.0f), 1.0, 150, Vector(30.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        //jObject::AddObject(sphere2);
        //SpawnedObjects.push_back(sphere2);
        //if (sphere2->RenderObjects[0])
        //{
        //    auto MaterialSphere = std::make_shared<jMaterial>();
        //    MaterialSphere->bUseSphericalMap = true;
        //    jName FilePath = jName("Image/grace_probe.hdr");
        //    MaterialSphere->TexData[(int32)jMaterial::EMaterialTextureType::Albedo].FilePath = FilePath;
        //    MaterialSphere->TexData[(int32)jMaterial::EMaterialTextureType::Albedo].Texture
        //        = jImageFileLoader::GetInstance().LoadTextureFromFile(FilePath, false, true).lock().get();
        //    sphere2->RenderObjects[0]->MaterialPtr = MaterialSphere;
        //}

#endif
		//{
		//	jScopedLock s(&AsyncLoadLock);
		//	CompletedAsyncLoadObjects.push_back(Sponza);
		//}
	//});
	
	jPathTracingLoadData* LoadedPathTracingData = nullptr;
	if (LoadPathTracing)
	{
		std::string sceneName = "Resource/PathTracing/cornell_box/cornell_box_sphere.scene";
		LoadedPathTracingData = jPathTracingLoadData::LoadPathTracingData(sceneName);
		LoadedPathTracingData->CreateSceneFor_jEngine(this);
	}

	g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here
	if (GSupportRaytracing)
	{
		jRatracingInitializer Initializer;
		Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
		Initializer.RenderObjects = jObject::GetStaticRenderObject();
		g_rhi->RaytracingScene->CreateOrUpdateBLAS(Initializer);
		g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
		g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here

		Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
		g_rhi->RaytracingScene->CreateOrUpdateTLAS(Initializer);
		g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
		g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here
	}

	// todo : Need to move
	{
		if (!jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive)
			jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

		if (!jPointLightDrawCommandGenerator::PointLightSphere)
			jPointLightDrawCommandGenerator::PointLightSphere = jPrimitiveUtil::CreateSphere(Vector::ZeroVector, 1.0, 16, 8, Vector(1.0f), Vector4::OneVector);

		if (!jSpotLightDrawCommandGenerator::SpotLightCone)
			jSpotLightDrawCommandGenerator::SpotLightCone = jPrimitiveUtil::CreateCone(Vector::ZeroVector, 1.0, 1.0, 20, Vector::OneVector, Vector4::OneVector, false, false);
	}
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
		case ESpawnedType::InstancingPrimitive:
			SpawnInstancingPrimitives();
			break;
		case ESpawnedType::IndirectDrawPrimitive:
			SpawnIndirectDrawPrimitives();
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

void jGame::Update(float deltaTime)
{
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Update");

	//if (CompletedAsyncLoadObjects.size() > 0)
	//{
 //       jScopedLock s(&AsyncLoadLock);
	//	for (auto iter : CompletedAsyncLoadObjects)
	//	{
 //           jObject::AddObject(iter);
 //           SpawnedObjects.push_back(iter);
	//	}
	//	CompletedAsyncLoadObjects.clear();
	//}

	// Update application property by using UI Pannel.
	// UpdateAppSetting();

	// Update main camera
	if (MainCamera)
	{
		MainCamera->UpdateCamera();

		gOptions.CameraPos = MainCamera->Pos;
		if (NormalDirectionalLight)
		{
			Vector& SunDir = NormalDirectionalLight->GetLightData().Direction;
			SunDir = gOptions.SunDir;
		}
	}
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

    for (auto& iter : jObject::GetStaticObject())
    {
        iter->Update(deltaTime);

		for(auto& RenderObject : iter->RenderObjects)
			RenderObject->UpdateWorldMatrix();
    }

	for (auto& iter : jObject::GetDebugObject())
	{
		iter->Update(deltaTime);

        for (auto& RenderObject : iter->RenderObjects)
            RenderObject->UpdateWorldMatrix();
	}

    // 정리해야함
	if (DirectionalLight)
		DirectionalLight->Update(deltaTime);
	if (PointLight)
		PointLight->Update(deltaTime);
	if (SpotLight)
	{
		SpotLight->SetDirection(Matrix::MakeRotateY(1.0f * deltaTime).TransformDirection(SpotLight->GetLightData().Direction));
		SpotLight->Update(deltaTime);
    } 

}

void jGame::Draw()
{
	SCOPE_CPU_PROFILE(Draw);
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Draw");

	{
		std::shared_ptr<jRenderFrameContext> renderFrameContext = g_rhi->BeginRenderFrame();
		if (!renderFrameContext)
			return;
		
		jView View(MainCamera, jLight::GetLights());
		View.PrepareViewUniformBufferShaderBindingInstance();

        //jRenderer renderer(renderFrameContext, View);
        //renderer.Render();
		jRenderer_PathTracing renderer(renderFrameContext, View);
		renderer.Render();

		g_rhi->EndRenderFrame(renderFrameContext);
	}
    jMemStack::Get()->Flush();
}

void jGame::OnMouseButton()
{
	
}

void jGame::OnMouseMove(int32 xOffset, int32 yOffset)
{
	if (g_MouseState[EMouseButtonType::LEFT])
	{
		constexpr float PitchScale = 0.0025f;
		constexpr float YawScale = 0.0025f;
		if (MainCamera)
			MainCamera->SetEulerAngle(MainCamera->GetEulerAngle() + Vector(yOffset * PitchScale, xOffset * YawScale, 0.0f));
	}
}

void jGame::Resize(int32 width, int32 height)
{
	if (MainCamera)
	{
		MainCamera->Width = width;
		MainCamera->Height = height;
	}
}

void jGame::Release()
{
	g_rhi->Flush();

	delete jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive;
	jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive = nullptr;
	
	delete jPointLightDrawCommandGenerator::PointLightSphere;
	jPointLightDrawCommandGenerator::PointLightSphere = nullptr;
	
	delete jSpotLightDrawCommandGenerator::SpotLightCone;
	jSpotLightDrawCommandGenerator::SpotLightCone = nullptr;

	delete jSceneRenderTarget::GlobalFullscreenPrimitive;
	jSceneRenderTarget::GlobalFullscreenPrimitive = nullptr;

	SpawnedObjects.clear();
	for(auto it : jObject::s_StaticObjects)
	{
		delete it;
	}
	for(auto it : jLight::s_Lights)
	{
		delete it;
	}

    DirectionalLight = nullptr;		// 현재 사용중인 Directional light 의 레퍼런스이므로 그냥 nullptr 설정
    DirectionalLight = nullptr;
    NormalDirectionalLight = nullptr;
    CascadeDirectionalLight = nullptr;
    PointLight = nullptr;
    SpotLight = nullptr;
    AmbientLight = nullptr;
	
	delete MainCamera;
    MainCamera = nullptr;

    DirectionalLightInfo = nullptr;
    PointLightInfo = nullptr;
    SpotLightInfo = nullptr;
	DirectionalLightShadowMapUIDebug = nullptr;

	// 임시
    jSceneRenderTarget::IrradianceMap.reset();
	jSceneRenderTarget::FilteredEnvMap.reset();
	jSceneRenderTarget::CubeEnvMap.reset();
	jSceneRenderTarget::HistoryBuffer.reset();
	jSceneRenderTarget::HistoryDepthBuffer.reset();
    jSceneRenderTarget::GaussianV.reset();
    jSceneRenderTarget::GaussianH.reset();
    jSceneRenderTarget::AOProjection.reset();
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
		thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(5.0f, 0.0f, 0.0f) * deltaTime);
	};
	jObject::AddObject(triangle);
	SpawnedObjects.push_back(triangle);

	auto cube = jPrimitiveUtil::CreateCube(Vector(-60.0f, 55.0f, -20.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	cube->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(0.0f, 0.0f, 0.5f) * deltaTime);
	};
	jObject::AddObject(cube);
	SpawnedObjects.push_back(cube);

	auto cube2 = jPrimitiveUtil::CreateCube(Vector(-65.0f, 35.0f, 10.0f), Vector::OneVector, Vector(50.0f, 50.0f, 50.0f), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	jObject::AddObject(cube2);
	SpawnedObjects.push_back(cube2);

	auto capsule = jPrimitiveUtil::CreateCapsule(Vector(30.0f, 30.0f, -80.0f), 40.0f, 10.0f, 20, Vector(1.0f), Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	capsule->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(-1.0f, 0.0f, 0.0f) * deltaTime);
	};
	jObject::AddObject(capsule);
	SpawnedObjects.push_back(capsule);

	auto cone = jPrimitiveUtil::CreateCone(Vector(0.0f, 50.0f, 60.0f), 40.0f, 20.0f, 15, Vector::OneVector, Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	cone->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(0.0f, 3.0f, 0.0f) * deltaTime);
	};
	jObject::AddObject(cone);
	SpawnedObjects.push_back(cone);

	auto cylinder = jPrimitiveUtil::CreateCylinder(Vector(-30.0f, 60.0f, -60.0f), 20.0f, 10.0f, 20, Vector::OneVector, Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	cylinder->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(5.0f, 0.0f, 0.0f) * deltaTime);
	};
	jObject::AddObject(cylinder);
	SpawnedObjects.push_back(cylinder);

	auto quad2 = jPrimitiveUtil::CreateQuad(Vector(-20.0f, 80.0f, 40.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	quad2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(0.0f, 0.0f, 8.0f) * deltaTime);
	};
	jObject::AddObject(quad2);
	SpawnedObjects.push_back(quad2);

	auto sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f), 1.0, 150, 75, Vector(30.0f), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
	sphere->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
        float RotationSpeed = 100.0f;
        thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(0.0f, 0.0f, DegreeToRadian(180.0f)) * RotationSpeed * deltaTime);
	};
	jObject::AddObject(sphere);
	SpawnedObjects.push_back(sphere);

	auto sphere2 = jPrimitiveUtil::CreateSphere(Vector(150.0f, 5.0f, 0.0f), 1.0, 150, 75, Vector(10.0f), Vector4(0.8f, 0.4f, 0.6f, 1.0f));
	sphere2->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		const float startY = 5.0f;
		const float endY = 100;
		const float speed = 150.0f * deltaTime;
		static bool dir = true;
		auto Pos = thisObject->RenderObjects[0]->GetPos();
		Pos.y += dir ? speed : -speed;
		if (Pos.y < startY || Pos.y > endY)
		{
			dir = !dir;
			Pos.y += dir ? speed : -speed;
		}
		thisObject->RenderObjects[0]->SetPos(Pos);
	};
	jObject::AddObject(sphere2);
	SpawnedObjects.push_back(sphere2);

	auto billboard = jPrimitiveUtil::CreateBillobardQuad(Vector(0.0f, 60.0f, 80.0f), Vector::OneVector, Vector(20.0f, 20.0f, 20.0f), Vector4(1.0f, 0.0f, 1.0f, 1.0f), MainCamera);
	jObject::AddObject(billboard);
	SpawnedObjects.push_back(billboard);

	//const float Size = 20.0f;

	//for (int32 i = 0; i < 10; ++i)
	//{
	//	for (int32 j = 0; j < 10; ++j)
	//	{
	//		for (int32 k = 0; k < 5; ++k)
	//		{
	//			auto cube = jPrimitiveUtil::CreateCube(Vector(i * 25.0f, k * 25.0f, j * 25.0f), Vector::OneVector, Vector(Size), Vector4(0.7f, 0.7f, 0.7f, 1.0f));
	//			jObject::AddObject(cube);
	//			SpawnedObjects.push_back(cube);
	//		}
	//	}
	//}
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

void jGame::SpawnInstancingPrimitives()
{
    struct jInstanceData
    {
        Vector4 Color;
        Vector W;
    };
    jInstanceData instanceData[100];

    const float colorStep = 1.0f / (float)sqrt(_countof(instanceData));
    Vector4 curStep = Vector4(colorStep, colorStep, colorStep, 1.0f);

    for (int32 i = 0; i < _countof(instanceData); ++i)
    {
        float x = (float)(i / 10);
        float y = (float)(i % 10);
        instanceData[i].W = Vector(y * 10.0f, x * 10.0f, 0.0f);
        instanceData[i].Color = curStep;
        if (i < _countof(instanceData) / 3)
            curStep.x += colorStep;
        else if (i < _countof(instanceData) / 2)
            curStep.y += colorStep;
        else if (i < _countof(instanceData))
            curStep.z += colorStep;
    }

    {
        auto obj = jPrimitiveUtil::CreateTriangle(Vector(0.0f, 0.0f, 0.0f), Vector::OneVector * 8.0f, Vector::OneVector, Vector4(1.0f, 0.0f, 0.0f, 1.0f));

        auto streamParam = std::make_shared<jStreamParam<jInstanceData>>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(Vector4)));
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(Vector)));
        streamParam->Stride = sizeof(jInstanceData);
        streamParam->Name = jName("InstanceData");
        streamParam->Data.resize(100);
        memcpy(&streamParam->Data[0], instanceData, sizeof(instanceData));

		auto& GeometryDataPtr = obj->RenderObjects[0]->GeometryDataPtr;

        GeometryDataPtr->VertexStream_InstanceDataPtr = std::make_shared<jVertexStreamData>();
        GeometryDataPtr->VertexStream_InstanceDataPtr->ElementCount = _countof(instanceData);
        GeometryDataPtr->VertexStream_InstanceDataPtr->StartLocation = (int32)GeometryDataPtr->VertexStreamPtr->GetEndLocation();
        GeometryDataPtr->VertexStream_InstanceDataPtr->BindingIndex = (int32)GeometryDataPtr->VertexStreamPtr->Params.size();
        GeometryDataPtr->VertexStream_InstanceDataPtr->VertexInputRate = EVertexInputRate::INSTANCE;
        GeometryDataPtr->VertexStream_InstanceDataPtr->Params.push_back(streamParam);
        GeometryDataPtr->VertexBuffer_InstanceDataPtr = g_rhi->CreateVertexBuffer(GeometryDataPtr->VertexStream_InstanceDataPtr);

        jObject::AddObject(obj);
        SpawnedObjects.push_back(obj);
    }
}

void jGame::SpawnIndirectDrawPrimitives()
{
    struct jInstanceData
    {
        Vector4 Color;
        Vector W;
    };
    jInstanceData instanceData[100];

    const float colorStep = 1.0f / (float)sqrt(_countof(instanceData));
    Vector4 curStep = Vector4(colorStep, colorStep, colorStep, 1.0f);

    for (int32 i = 0; i < _countof(instanceData); ++i)
    {
        float x = (float)(i / 10);
        float y = (float)(i % 10);
        instanceData[i].W = Vector(y * 10.0f, x * 10.0f, 0.0f);
        instanceData[i].Color = curStep;
        if (i < _countof(instanceData) / 3)
            curStep.x += colorStep;
        else if (i < _countof(instanceData) / 2)
            curStep.y += colorStep;
        else if (i < _countof(instanceData))
            curStep.z += colorStep;
    }

    {
        auto obj = jPrimitiveUtil::CreateTriangle(Vector(0.0f, 0.0f, 0.0f), Vector::OneVector * 8.0f, Vector::OneVector, Vector4(1.0f, 0.0f, 0.0f, 1.0f));

        auto streamParam = std::make_shared<jStreamParam<jInstanceData>>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(Vector4)));
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(Vector)));
        streamParam->Stride = sizeof(jInstanceData);
        streamParam->Name = jName("InstanceData");
        streamParam->Data.resize(100);
        memcpy(&streamParam->Data[0], instanceData, sizeof(instanceData));

		auto& GeometryDataPtr = obj->RenderObjects[0]->GeometryDataPtr;
        GeometryDataPtr->VertexStream_InstanceDataPtr = std::make_shared<jVertexStreamData>();
        GeometryDataPtr->VertexStream_InstanceDataPtr->ElementCount = _countof(instanceData);
        GeometryDataPtr->VertexStream_InstanceDataPtr->StartLocation = (int32)GeometryDataPtr->VertexStreamPtr->GetEndLocation();
        GeometryDataPtr->VertexStream_InstanceDataPtr->BindingIndex = (int32)GeometryDataPtr->VertexStreamPtr->Params.size();
        GeometryDataPtr->VertexStream_InstanceDataPtr->VertexInputRate = EVertexInputRate::INSTANCE;
        GeometryDataPtr->VertexStream_InstanceDataPtr->Params.push_back(streamParam);
        GeometryDataPtr->VertexBuffer_InstanceDataPtr = g_rhi->CreateVertexBuffer(GeometryDataPtr->VertexStream_InstanceDataPtr);

        // Create indirect draw buffer
        {
            check(GeometryDataPtr->VertexStream_InstanceDataPtr);

            std::vector<VkDrawIndirectCommand> indrectCommands;

            const int32 instanceCount = GeometryDataPtr->VertexStream_InstanceDataPtr->ElementCount;
            const int32 vertexCount = GeometryDataPtr->VertexStreamPtr->ElementCount;
            for (int32 i = 0; i < instanceCount; ++i)
            {
                VkDrawIndirectCommand command;
                command.vertexCount = vertexCount;
                command.instanceCount = 1;
                command.firstVertex = 0;
                command.firstInstance = i;
                indrectCommands.emplace_back(command);
            }

            const size_t bufferSize = indrectCommands.size() * sizeof(VkDrawIndirectCommand);

			check(!GeometryDataPtr->IndirectCommandBufferPtr);
			GeometryDataPtr->IndirectCommandBufferPtr = g_rhi->CreateStructuredBuffer(bufferSize, 0, sizeof(VkDrawIndirectCommand), EBufferCreateFlag::IndirectCommand, EResourceLayout::TRANSFER_DST
				, indrectCommands.data(), bufferSize, TEXT("IndirectBuffer"));
        }

        jObject::AddObject(obj);
        SpawnedObjects.push_back(obj);
    }
}

