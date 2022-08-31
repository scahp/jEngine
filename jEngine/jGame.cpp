#include "pch.h"
#include "jGame.h"
#include "Math\Vector.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jLight.h"
#include "Scene/jRenderObject.h"
#include "Profiler/jPerformanceProfile.h"
#include "Renderer/jForwardRenderer.h"
#include "jPrimitiveUtil.h"
#include "RHI\Vulkan\jVulkanBufferUtil.h"

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
	MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 1000.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
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
	//SpawnObjects(ESpawnedType::TestPrimitive);
	//SpawnObjects(ESpawnedType::InstancingPrimitive);
	SpawnObjects(ESpawnedType::IndirectDrawPrimitive);
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

	// Update application property by using UI Pannel.
	// UpdateAppSetting();

	// Update main camera
	if (MainCamera)
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
	if (DirectionalLight)
		DirectionalLight->Update(deltaTime);
}

void jGame::Draw()
{
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Draw");

    std::shared_ptr<jRenderFrameContext> renderFrameContext = g_rhi->BeginRenderFrame();
	if (!renderFrameContext)
		return;

	jForwardRenderer forwardRenderer(renderFrameContext, jView(MainCamera, DirectionalLight));
	forwardRenderer.Setup();
	forwardRenderer.Render();

	g_rhi->EndRenderFrame(renderFrameContext);
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
	for (jObject* iter : SpawnedObjects)
	{
		delete iter;
	}
	SpawnedObjects.clear();

    DirectionalLight = nullptr;		// 현재 사용중인 Directional light 의 레퍼런스이므로 그냥 nullptr 설정
	delete NormalDirectionalLight;
	delete CascadeDirectionalLight;
	delete PointLight;
	delete SpotLight;
	delete AmbientLight;
	delete MainCamera;

    delete DirectionalLightInfo;
    delete PointLightInfo;
    delete SpotLightInfo;
	delete DirectionalLightShadowMapUIDebug;
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

        obj->RenderObject->VertexStream_InstanceData = std::make_shared<jVertexStreamData>();
        obj->RenderObject->VertexStream_InstanceData->ElementCount = _countof(instanceData);
        obj->RenderObject->VertexStream_InstanceData->StartLocation = (int32)obj->RenderObject->VertexStream->GetEndLocation();
        obj->RenderObject->VertexStream_InstanceData->BindingIndex = (int32)obj->RenderObject->VertexStream->Params.size();
        obj->RenderObject->VertexStream_InstanceData->VertexInputRate = EVertexInputRate::INSTANCE;
        obj->RenderObject->VertexStream_InstanceData->Params.push_back(streamParam);
        obj->RenderObject->VertexBuffer_InstanceData = g_rhi->CreateVertexBuffer(obj->RenderObject->VertexStream_InstanceData);

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

        obj->RenderObject->VertexStream_InstanceData = std::make_shared<jVertexStreamData>();
        obj->RenderObject->VertexStream_InstanceData->ElementCount = _countof(instanceData);
        obj->RenderObject->VertexStream_InstanceData->StartLocation = (int32)obj->RenderObject->VertexStream->GetEndLocation();
        obj->RenderObject->VertexStream_InstanceData->BindingIndex = (int32)obj->RenderObject->VertexStream->Params.size();
        obj->RenderObject->VertexStream_InstanceData->VertexInputRate = EVertexInputRate::INSTANCE;
        obj->RenderObject->VertexStream_InstanceData->Params.push_back(streamParam);
        obj->RenderObject->VertexBuffer_InstanceData = g_rhi->CreateVertexBuffer(obj->RenderObject->VertexStream_InstanceData);

        // Create indirect draw buffer
        {
            check(obj->RenderObject->VertexStream_InstanceData);

            std::vector<VkDrawIndirectCommand> indrectCommands;

            const int32 instanceCount = obj->RenderObject->VertexStream_InstanceData->ElementCount;
            const int32 vertexCount = obj->RenderObject->VertexStream->ElementCount;
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

            jBuffer_Vulkan stagingBuffer;
            jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                , bufferSize, stagingBuffer);

            stagingBuffer.UpdateBuffer(indrectCommands.data(), bufferSize);

            jBuffer_Vulkan* temp = new jBuffer_Vulkan();
            obj->RenderObject->IndirectCommandBuffer = temp;
            jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                , bufferSize, *temp);
            jVulkanBufferUtil::CopyBuffer(stagingBuffer.Buffer, (VkBuffer)obj->RenderObject->IndirectCommandBuffer->GetHandle(), bufferSize);

            stagingBuffer.Release();
        }

        jObject::AddObject(obj);
        SpawnedObjects.push_back(obj);
    }
}
