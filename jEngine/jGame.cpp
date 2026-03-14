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
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "Code/Engine/ConsoleVariables/jConsoleVariable.h"
#include "Renderer/jPointLightDrawCommandGenerator.h"
#include "Renderer/jSpotLightDrawCommandGenerator.h"
#include "PathTracingDataLoader/jPathTracingData.h"
#include "PathTracingDataLoader/PathTracingDataLoader.h"
#include "PathTracingDataLoader/GLTFLoader.h"
#include "Renderer/jRenderer_PathTracing.h"
#include "FileLoader/jFile.h"

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

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
	// If console is visible, don't process game input
	// All console keys (ESC, `, ') are handled by ImGui in RenderInputField
	if (jConsole::Get().IsVisible())
	{
		return;
	}

	// Note: ` and ' keys are handled in ImGui (jConsole.cpp RenderInputField)
	// to avoid double-triggering issues between Win32 and ImGui input systems

	static float MoveDistancePerSecond = 200.0f;
	//static float MoveDistancePerSecond = 1.0f;
	//static float MoveDistancePerSecond = 10.0f;
	const float CurrentDistance = MoveDistancePerSecond * deltaTime;

	// Process Key Event
	if (IsKeyDown(EInputKey::A)) MainCamera->MoveShift(-CurrentDistance);
	if (IsKeyDown(EInputKey::D)) MainCamera->MoveShift(CurrentDistance);
	//if (g_KeyState['1']) MainCamera->RotateForwardAxis(-0.1f);
	//if (g_KeyState['2']) MainCamera->RotateForwardAxis(0.1f);
	//if (g_KeyState['3']) MainCamera->RotateUpAxis(-0.1f);
	//if (g_KeyState['4']) MainCamera->RotateUpAxis(0.1f);
	//if (g_KeyState['5']) MainCamera->RotateRightAxis(-0.1f);
	//if (g_KeyState['6']) MainCamera->RotateRightAxis(0.1f);
	if (IsKeyDown(EInputKey::W)) MainCamera->MoveForward(CurrentDistance);
	if (IsKeyDown(EInputKey::S)) MainCamera->MoveForward(-CurrentDistance);
	if (IsKeyDown(EInputKey::PLUS)) MoveDistancePerSecond = Max(MoveDistancePerSecond + 10.0f, 0.0f);
	if (IsKeyDown(EInputKey::MINUS)) MoveDistancePerSecond = Max(MoveDistancePerSecond - 10.0f, 0.0f);

#ifdef ENABLE_EDITOR_FEATURES
	// Editor-specific input handling (e.g., Placement Tool)
	if (g_Editor)
	{
		g_Editor->Placement.ProcessInput(deltaTime, MainCamera, gOptions.LightColorScale);
	}
#endif
}

void jGame::Setup()
{
 	srand(static_cast<uint32>(time(NULL)));

	// Register test console variables
	{
		// External variable examples (connected to gOptions)
		static jConsoleVariableBool* cvar_UseVRS = new jConsoleVariableBool("r.vrs", &gOptions.UseVRS, "Enable Variable Rate Shading");
		static jConsoleVariableBool* cvar_UseSSGI = new jConsoleVariableBool("r.ssgi.enable", &gOptions.UseSSGI, "Enable Screen Space Global Illumination");
        static jConsoleVariableBool* cvar_UseHWRTDirectLighting = new jConsoleVariableBool("r.hwrt.direct_lighting.enable", &gOptions.UseHWRTDirectLighting, "Enable HWRT direct lighting path");
        static jConsoleVariableInt* cvar_HWRTDirectLightingMode = new jConsoleVariableInt("r.hwrt.direct_lighting.mode", &gOptions.HWRTDirectLightingMode, "HWRT direct lighting mode (0=DispatchRays, 1=InlineRayQuery)");
        static jConsoleVariableBool* cvar_UseSurfelGI = new jConsoleVariableBool("r.surfelgi.enable", &gOptions.UseSurfelGI, "Enable SurfelGI prototype pool update");
        static jConsoleVariableInt* cvar_SurfelGIReservoirPerCellLimit = new jConsoleVariableInt("r.surfelgi.reservoir.per_cell_limit", &gOptions.SurfelGIReservoirPerCellLimit, "SurfelGI reservoir per-cell page size limit");
        static jConsoleVariableFloat* cvar_SurfelGIFaceMarginRadiusScale = new jConsoleVariableFloat("r.surfelgi.face_margin_radius_scale", &gOptions.SurfelGIFaceMarginRadiusScale, "SurfelGI reservoir candidate face margin radius scale");
static jConsoleVariableBool* cvar_SurfelGIInlineRayEnable = new jConsoleVariableBool("r.surfelgi.inline_ray.enable", &gOptions.SurfelGIInlineRayEnable, "Enable SurfelGI inline ray irradiance gather pass");
static jConsoleVariableBool* cvar_SurfelGIInlineRayGuideEnable = new jConsoleVariableBool("r.surfelgi.inline_ray.guide_enable", &gOptions.SurfelGIInlineRayGuideEnable, "Enable SurfelGI inline ray directional guiding");
static jConsoleVariableInt* cvar_SurfelGIInlineRayCount = new jConsoleVariableInt("r.surfelgi.inline_ray.count", &gOptions.SurfelGIInlineRayCount, "SurfelGI inline ray count per surfel");
        static jConsoleVariableFloat* cvar_SurfelGIInlineRayMaxDistance = new jConsoleVariableFloat("r.surfelgi.inline_ray.max_distance", &gOptions.SurfelGIInlineRayMaxDistance, "SurfelGI inline ray max trace distance");
        static jConsoleVariableFloat* cvar_SurfelGIInlineRayNormalBias = new jConsoleVariableFloat("r.surfelgi.inline_ray.normal_bias", &gOptions.SurfelGIInlineRayNormalBias, "SurfelGI inline ray origin normal bias");
        static jConsoleVariableFloat* cvar_SurfelGIInlineRayHistoryBlend = new jConsoleVariableFloat("r.surfelgi.inline_ray.history_blend", &gOptions.SurfelGIInlineRayHistoryBlend, "SurfelGI inline ray MSME short-window control");
        static jConsoleVariableFloat* cvar_SurfelGIIntensity = new jConsoleVariableFloat("r.surfelgi.intensity", &gOptions.SurfelGIIntensity, "SurfelGI resolve/apply intensity");
        static jConsoleVariableInt* cvar_SurfelGIIrradianceDebugMode = new jConsoleVariableInt("r.surfelgi.visualize.irradiance_mode", &gOptions.SurfelGIIrradianceDebugMode, "SurfelGI irradiance visualize mode (0=mean, 1=short_mean, 2=variance, 3=inconsistency, 4=count_vbbr)");
        static jConsoleVariableBool* cvar_SurfelGIHoverRayDebug = new jConsoleVariableBool("r.surfelgi.visualize.hover_rays", &gOptions.ShowSurfelGIHoverRayDebug, "Show hover surfel inline-ray directions (red=guide, blue=cosine)");
        static jConsoleVariableInt* cvar_SurfelGISurfelsPerCell[SURFEL_GI_CASCADE_COUNT] = {};
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            char surfelsPerCellName[128];
            char surfelsPerCellDesc[128];
            sprintf_s(surfelsPerCellName, "r.surfelgi.cascade%d.surfels_per_cell", cascade);
            sprintf_s(surfelsPerCellDesc, "SurfelGI cascade %d desired surfels per cell", cascade);

            if (!cvar_SurfelGISurfelsPerCell[cascade])
            {
                cvar_SurfelGISurfelsPerCell[cascade] = new jConsoleVariableInt(surfelsPerCellName, &gOptions.SurfelGISurfelsPerCell[cascade], surfelsPerCellDesc);
            }
        }
		static jConsoleVariableInt* cvar_SSGIRayCount = new jConsoleVariableInt("r.ssgi.raycount", &gOptions.SSGIRayCount, "SSGI ray count per pixel");
		static jConsoleVariableFloat* cvar_SSGIIntensity = new jConsoleVariableFloat("r.ssgi.intensity", &gOptions.SSGIIntensity, "SSGI intensity multiplier");

		// Internal variable examples (standalone test variables)
		static jConsoleVariableBool* cvar_DebugDraw = new jConsoleVariableBool("debug.draw", false, "Enable debug drawing");
		static jConsoleVariableInt* cvar_DebugLevel = new jConsoleVariableInt("debug.level", 0, "Debug verbosity level (0-3)");
		static jConsoleVariableFloat* cvar_TimeScale = new jConsoleVariableFloat("game.timescale", 1.0f, "Game time scale multiplier");
		static jConsoleVariableString* cvar_PlayerName = new jConsoleVariableString("player.name", "Player", "Player name");

		jConsole::Get().Log("Test console variables registered.");
	}

#if ENABLE_PBR
	// PBR will use light color as a flux,
	float LightColorScale = 20000.0f;
#else
	float LightColorScale = 1.0f;
#endif

#if USE_SPONZA
	// Create main camera
    const Vector mainCameraPos(1124.351929f, 31.903732f, 18.574120f);
    const Vector mainCameraTarget(Vector(1124.351929f, 31.903732f, 18.574120f) + Vector(-0.927020f, 0.264276f, -0.266068f));
    MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 5000.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
    jCamera::AddCamera(0, MainCamera);

	jDirectionalLight* DirectionalLight = nullptr;
	jPointLight* PointLight = nullptr;
	jSpotLight* SpotLight = nullptr;
	#if !USE_PATH_TRACING		// todo : this hard code should be removed.
    // Create lights
	{
		Vector lightColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
		DirectionalLight = jLight::CreateDirectionalLight(gOptions.DefaultSunDir
			, Vector4(lightColor.x, lightColor.y, lightColor.z, 1.0f), Vector(1.0f), Vector(1.0f), 64);

		PointLight = jLight::CreatePointLight(Vector(10.0f, 100.0f, 10.0f), Vector4(1.0f, 0.75f, 0.75f, 1.0f) * LightColorScale, 1500.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
		SpotLight = jLight::CreateSpotLight(Vector(0.0f, 60.0f, 5.0f), Vector(1.0f, -1.0f, 0.4f).GetNormalize(), Vector4(0.0f, 1.0f, 0.0f, 1.0f) * LightColorScale, 2000.0f, 0.35f, 1.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
        if (SpotLight)
        {
			SpotLight->PreUpdateLambda = [](jLight* light, float InDeltaTime)
            {
                auto SpotLight = (jSpotLight*)(light);
                check(SpotLight);
                SpotLight->SetDirection(Matrix::MakeRotateY(1.0f * InDeltaTime).TransformDirection(SpotLight->GetLightData().Direction));
            };
        }
	}
	#endif // !USE_PATH_TRACING
#else
	// Create main camera
	//const Vector mainCameraPos(-111.6f, 17.49f, 3.11f);
	//const Vector mainCameraTarget(282.378632f, 17.6663227f, -1.00448179f);
    const Vector mainCameraPos(172.66f, 160.0f, -180.63f);
    const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
    MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 1500.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
    jCamera::AddCamera(0, MainCamera);

    // Create lights
    Vector lightColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
    DirectionalLight = jLight::CreateDirectionalLight(gOptions.DefaultSunDir
        , Vector4(lightColor.x, lightColor.y, lightColor.z, 1.0f), Vector(1.0f), Vector(1.0f), 64);
    //CascadeDirectionalLight = jLight::CreateCascadeDirectionalLight(AppSettings.DirecionalLightDirection
    //	, Vector4(0.6f), Vector(1.0f), Vector(1.0f), 64);
    //AmbientLight = jLight::CreateAmbientLight(Vector(0.2f, 0.5f, 1.0f), Vector(0.05f));		// sky light color
    PointLight = jLight::CreatePointLight(Vector(10.0f, 100.0f, 10.0f), Vector4(1.0f, 0.75f, 0.75f, 1.0f) * LightColorScale, 150.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
    SpotLight = jLight::CreateSpotLight(Vector(0.0f, 80.0f, 5.0f), Vector(1.0f, -1.0f, 0.4f).GetNormalize(), Vector4(0.2f, 1.0f, 0.2f, 1.0f) * LightColorScale, 200.0f, 0.35f, 0.5f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
#endif

	if (DirectionalLight)
		jLight::AddLights(DirectionalLight);
	if (PointLight)
		jLight::AddLights(PointLight);
	if (SpotLight)
		jLight::AddLights(SpotLight);

	//PointLight->IsShadowCaster = false;
	//SpotLight->IsShadowCaster = false;

    //auto cube = jPrimitiveUtil::CreateCube(Vector(0.0f, 60.0f, 5.0f), Vector::OneVector, Vector::OneVector * 10.f, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
    //jObject::AddObject(cube);
    //SpawnedObjects.push_back(cube);

	// Create light info for debugging light infomation
    if (DirectionalLight)
    {
        DirectionalLightInfo = jPrimitiveUtil::CreateDirectionalLightDebug(Vector(250, 400, 0) * 0.5f, Vector::OneVector * 10.0f, 10.0f, MainCamera, DirectionalLight, "Image/sun.png");
        // jObject::AddDebugObject(DirectionalLightInfo);
		jObject::AddDebugObject(DirectionalLightInfo->BillboardObject);
		// jObject::AddDebugObject(DirectionalLightInfo->ArrowSegementObject);

#ifdef ENABLE_EDITOR_FEATURES
		// Add to Placement Tool
		if (g_Editor)
		{
			PlacedObjectInfo info;
			info.Object = DirectionalLightInfo->BillboardObject;
			info.Type = EPlacedObjectType::LIGHT;
			info.LightType = EPlacementLightType::DIRECTIONAL;
			info.LightPtr = DirectionalLight;
			g_Editor->Placement.PlacedObjects.push_back(info);
		}
#endif
    }

    if (PointLight)
    {
        PointLightInfo = jPrimitiveUtil::CreatePointLightDebug(Vector(10.0f), MainCamera, PointLight, "Image/bulb.png");
        jObject::AddDebugObject(PointLightInfo->BillboardObject);

#ifdef ENABLE_EDITOR_FEATURES
		// Add to Placement Tool
		if (g_Editor)
		{
			PlacedObjectInfo info;
			info.Object = PointLightInfo->BillboardObject;
			info.Type = EPlacedObjectType::LIGHT;
			info.LightType = EPlacementLightType::POINT;
			info.LightPtr = PointLight;
			g_Editor->Placement.PlacedObjects.push_back(info);
		}
#endif
    }

    if (SpotLight)
    {
        SpotLightInfo = jPrimitiveUtil::CreateSpotLightDebug(Vector(10.0f), MainCamera, SpotLight, "Image/spot.png");
        jObject::AddDebugObject(SpotLightInfo->BillboardObject);

#ifdef ENABLE_EDITOR_FEATURES
		// Add to Placement Tool
		if (g_Editor)
		{
			PlacedObjectInfo info;
			info.Object = SpotLightInfo->BillboardObject;
			info.Type = EPlacedObjectType::LIGHT;
			info.LightType = EPlacementLightType::SPOT;
			info.LightPtr = SpotLight;
			g_Editor->Placement.PlacedObjects.push_back(info);
		}
#endif
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
	#if !USE_PATH_TRACING		// todo : this hard code should be removed.
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
	#endif // !USE_PATH_TRACING

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
	
#if USE_PATH_TRACING
    static bool initialized = false;
    if (!initialized)
    {
        initialized = true;

		jFile::SearchFilesRecursive(gPathTracingScenes, "Resource/PathTracing", { ".scene" });
        gPathTracingScenesNameOnly.resize(gPathTracingScenes.size());
        for (int32 i = 0; i < gPathTracingScenes.size(); ++i)
        {
            gPathTracingScenesNameOnly[i] = jFile::ExtractFileName(gPathTracingScenes[i]);
        }

        if (gPathTracingScenesNameOnly.size() > 0)
            gSelectedScene = gPathTracingScenesNameOnly[0].c_str();

		gSelectedSceneIndex = 3;
		gSelectedScene = gPathTracingScenesNameOnly[gSelectedSceneIndex].c_str();
    }

	if (!gPathTracingScene)
	{
		gPathTracingScene = jPathTracingLoadData::LoadPathTracingData(gPathTracingScenes[gSelectedSceneIndex]);
		gPathTracingScene->CreateSceneFor_jEngine(this);
	}
#endif // USE_PATH_TRACING

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

#if USE_PATH_TRACING
	static int32 LastSelectedIndex = gSelectedSceneIndex;
	if (gSelectedSceneIndex != LastSelectedIndex)
	{
		g_rhi->Flush();

		gPathTracingScene->ClearSceneFor_jEngine(this);
        gPathTracingScene = jPathTracingLoadData::LoadPathTracingData(gPathTracingScenes[gSelectedSceneIndex].c_str());
        gPathTracingScene->CreateSceneFor_jEngine(this);

		g_rhi->RaytracingScene->Clear();

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

		LastSelectedIndex = gSelectedSceneIndex;
	}
#endif // USE_PATH_TRACING

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

    // Sync raytracing scene with placement/editor changes
    if (GSupportRaytracing && g_rhi && g_rhi->RaytracingScene && g_rhi->RaytracingScene->ShouldUpdate())
    {
        jRatracingInitializer Initializer;
        Initializer.RenderObjects = jObject::GetStaticRenderObject();

        Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
        g_rhi->RaytracingScene->CreateOrUpdateBLAS(Initializer);
        g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
        g_rhi->Finish(); // todo : replace with proper UAV barrier

        Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
        g_rhi->RaytracingScene->CreateOrUpdateTLAS(Initializer);
        g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
        g_rhi->Finish(); // todo : replace with proper UAV barrier
    }

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

	for (auto light : jLight::GetLights())
	{
		light->Update(deltaTime);
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

#if USE_PATH_TRACING
        jRenderer_PathTracing renderer(renderFrameContext, View);
        renderer.Render();
#else
        jRenderer renderer(renderFrameContext, View);
        renderer.Render();
#endif // USE_PATH_TRACING

		g_rhi->EndRenderFrame(renderFrameContext);
	}
    jMemStack::Get()->Flush();
}

void jGame::OnMouseButton()
{
#ifdef ENABLE_EDITOR_FEATURES
	// Handle object picking on left mouse button click
	if (g_Editor && g_Editor->Placement.EnablePlacementMode && g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Clicked)
	{
		// Don't pick when manipulating Gizmo
		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
			return;

		// Don't pick when clicking on UI
		if (ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
			return;

		// Get current mouse position
		double xpos, ypos;
		if (IsUseVulkan())
		{
			GLFWwindow* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
			glfwGetCursorPos(window, &xpos, &ypos);
		}
		else if (IsUseDX12())
		{
			POINT pt;
			if (GetCursorPos(&pt))
			{
				HWND hwnd = (HWND)g_rhi->GetWindow();
				ScreenToClient(hwnd, &pt);
				xpos = pt.x;
				ypos = pt.y;
			}
		}

		// Request object picking
		g_Editor->Placement.bPickRequested = true;
		g_Editor->Placement.PickMouseX = (int32)xpos;
		g_Editor->Placement.PickMouseY = (int32)ypos;
	}
#endif
}

void jGame::OnMouseMove(int32 xOffset, int32 yOffset)
{
	if (g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down)
	{
#ifdef ENABLE_EDITOR_FEATURES
		// Don't rotate camera when manipulating Gizmo
		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
			return;
#endif

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
	
	delete MainCamera;
    MainCamera = nullptr;

    DirectionalLightInfo = nullptr;
    PointLightInfo = nullptr;
    SpotLightInfo = nullptr;
	DirectionalLightShadowMapUIDebug = nullptr;

    ReleaseSurfelGIResources();
    jSceneRenderTarget::ReleasePersistentResources();
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
        streamParam->Attributes.push_back({.UnderlyingType=EBufferElementType::FLOAT, .Stride=sizeof(Vector4)});
        streamParam->Attributes.push_back({.UnderlyingType=EBufferElementType::FLOAT, .Stride=sizeof(Vector)});
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
        streamParam->Attributes.push_back({.UnderlyingType=EBufferElementType::FLOAT, .Stride=sizeof(Vector4)});
        streamParam->Attributes.push_back({.UnderlyingType=EBufferElementType::FLOAT, .Stride=sizeof(Vector)});
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
				, indrectCommands.data(), bufferSize, jName(TEXT("IndirectBuffer")));
        }

        jObject::AddObject(obj);
        SpawnedObjects.push_back(obj);
    }
}
