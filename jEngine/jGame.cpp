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
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#include "jOptions.h"
#include "FileLoader/jModelLoader.h"
#include "Scene/jMeshObject.h"
#include "FileLoader/jImageFileLoader.h"
#include "Renderer/jSceneRenderTargets.h"    // 임시
#include "RHI/DX12/jShader_DX12.h"
#include "dxcapi.h"
#include "RHI/DX12/jVertexBuffer_DX12.h"
#include "RHI/DX12/jIndexBuffer_DX12.h"
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/Vulkan/jVertexBuffer_Vulkan.h"
#include "RHI/Vulkan/jIndexBuffer_Vulkan.h"

jRHI* g_rhi = nullptr;
jBuffer_DX12* jGame::ScratchASBuffer = nullptr;
jBuffer_DX12* jGame::TopLevelASBuffer = nullptr;
jBuffer_DX12* jGame::InstanceDescUploadBuffer = nullptr;
jObject* jGame::Sphere = nullptr;
std::function<void(void* InCmdBuffer)> jGame::UpdateTopLevelAS;

jBuffer_Vulkan* jGame::TLAS_Vulkan = nullptr;

jGame::jGame()
{
}

jGame::~jGame()
{
}

void jGame::ProcessInput(float deltaTime)
{
	static float MoveDistancePerSecond = 200.0f;
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

#if USE_SPONZA
	// Create main camera
    const Vector mainCameraPos(-559.937622f, 116.339653f, 84.3709946f);
    const Vector mainCameraTarget(-260.303925f, 105.498116f, 94.4834976f);
    //const Vector mainCameraPos(0, 0, -1000);
    //const Vector mainCameraTarget(300, 0, 0);
    MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 1.0f, 1250.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
    jCamera::AddCamera(0, MainCamera);

    // Create lights
    NormalDirectionalLight = jLight::CreateDirectionalLight(Vector(0.1f, -0.5f, 0.1f) // AppSettings.DirecionalLightDirection
        , Vector4(30.0f), Vector(1.0f), Vector(1.0f), 64);
    PointLight = jLight::CreatePointLight(Vector(10.0f, 100.0f, 10.0f), Vector4(1.0f, 0.75f, 0.75f, 1.0f) * LightColorScale, 1500.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
    SpotLight = jLight::CreateSpotLight(Vector(0.0f, 60.0f, 5.0f), Vector(1.0f, -1.0f, 0.4f).GetNormalize(), Vector4(0.0f, 1.0f, 0.0f, 1.0f) * LightColorScale, 2000.0f, 0.35f, 1.0f, Vector(1.0f, 1.0f, 1.0f), Vector(1.0f), 64.0f);
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
		#if USE_SPONZA_PBR		
		Sponza = jModelLoader::GetInstance().LoadFromFile("Resource/sponza_pbr/sponza.glb", "Resource/sponza_pbr");
		#else
		Sponza = jModelLoader::GetInstance().LoadFromFile("Resource/sponza/sponza.dae", "Resource/");
		#endif
		jObject::AddObject(Sponza);
		SpawnedObjects.push_back(Sponza);

		//for (int32 i = 0; i < 2; ++i)
		//{
		//	Sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f + i * 100), 1.0, 60, Vector(30.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		//	//Sphere = jPrimitiveUtil::CreateCube(Vector(65.0f, 35.0f, 10.0f), Vector::OneVector, Vector(150), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		//	//Sphere->RenderObjects[0]->SetRot(Sphere->RenderObjects[0]->GetRot() + Vector(0.0f, 0.0f, DegreeToRadian(90.0f)));
		//	Sphere->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
		//		{
		//			float RotationSpeed = 100.0f;
		//			thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(0.0f, 0.0f, DegreeToRadian(180.0f)) * RotationSpeed * deltaTime);
		//		};
		//	jObject::AddObject(Sphere);
		//	SpawnedObjects.push_back(Sphere);
		//}

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

	if (IsUseDX12())
	{
		auto CmdBuffer = g_rhi_dx12->BeginSingleTimeCommands();
		std::vector<jRenderObject*> RTObjects;
		for (int32 i = 0; i < jObject::GetStaticRenderObject().size(); ++i)
		{
			jRenderObject* RObj = jObject::GetStaticRenderObject()[i];

			auto VertexBuffer_PositionOnly = RObj->GeometryDataPtr->VertexBuffer_PositionOnly;
			auto IndexBuffer = RObj->GeometryDataPtr->IndexBuffer;

			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

			auto VertexBufferDX12 = (jVertexBuffer_DX12*)VertexBuffer_PositionOnly;
			ID3D12Resource* VtxBufferDX12 = VertexBufferDX12->BindInfos.Buffers[0];

			if (GetDX12TextureComponentCount(GetDX12TextureFormat(VertexBufferDX12->BindInfos.InputElementDescs[0].Format)) < 3)
				continue;

			D3D12_GPU_VIRTUAL_ADDRESS VertexStart = VtxBufferDX12->GetGPUVirtualAddress();
			int32 VertexCount = VertexBufferDX12->GetElementCount();
			auto ROE = dynamic_cast<jRenderObjectElement*>(RObj);
			Vector2i VertexIndexOffset{};
			if (ROE)
			{
				VertexStart += VertexBufferDX12->Streams[0].Stride * ROE->SubMesh.StartVertex;
				VertexCount = ROE->SubMesh.EndVertex - ROE->SubMesh.StartVertex + 1;

				VertexIndexOffset = Vector2i(ROE->SubMesh.StartVertex, ROE->SubMesh.StartFace);
			}
			RObj->VertexAndIndexOffsetBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(Vector2i), 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_COMMON
				, &VertexIndexOffset, sizeof(Vector2i), TEXT("VertexAndIndexOffsetBuffer"));
			jBufferUtil_DX12::CreateShaderResourceView((jBuffer_DX12*)RObj->VertexAndIndexOffsetBuffer, sizeof(Vector2i), 1);

			RTObjects.push_back(RObj);

			if (IndexBuffer)
			{
				auto IndexBufferDX12 = (jIndexBuffer_DX12*)IndexBuffer;
				ComPtr<ID3D12Resource> IdxBufferDX12 = IndexBufferDX12->BufferPtr->Buffer;
				auto& indexStreamData = IndexBufferDX12->IndexStreamData;

				D3D12_GPU_VIRTUAL_ADDRESS IndexStart = IndexBufferDX12->BufferPtr->GetGPUAddress();
				int32 IndexCount = IndexBuffer->GetElementCount();
				if (ROE)
				{
					IndexStart += indexStreamData->Param->GetElementSize() * ROE->SubMesh.StartFace;
					IndexCount = ROE->SubMesh.EndFace - ROE->SubMesh.StartFace + 1;
				}

				geometryDesc.Triangles.IndexBuffer = IndexStart;
				geometryDesc.Triangles.IndexCount = IndexCount;
				geometryDesc.Triangles.IndexFormat = indexStreamData->Param->GetElementSize() == 16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
			}
			else
			{
				geometryDesc.Triangles.IndexBuffer = {};
				geometryDesc.Triangles.IndexCount = 0;
				geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
			}
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexFormat = VertexBufferDX12->BindInfos.InputElementDescs[0].Format;
			geometryDesc.Triangles.VertexCount = VertexCount;
			geometryDesc.Triangles.VertexBuffer.StartAddress = VertexStart;
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = VertexBufferDX12->Streams[0].Stride;

			// Opaque로 지오메트를 등록하면, 히트 쉐이더에서 더이상 쉐이더를 만들지 않을 것이므로 최적화에 좋다.
			//geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

			// Acceleration structure 에 필요한 크기를 요청함
			// 첫번째 지오메트리
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo{};
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs{};
			{
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				bottomLevelInputs.Flags = buildFlags;
				bottomLevelInputs.pGeometryDescs = &geometryDesc;
				bottomLevelInputs.NumDescs = 1;
				bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;


				g_rhi_dx12->Device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
				if (!JASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0))
					continue;
			}

			auto AllocateUAVBuffer = [](ID3D12Resource** OutResource, ID3D12Device* InDevice
				, uint64 InBufferSize, D3D12_RESOURCE_STATES InInitialResourceState
				, const wchar_t* InResourceName)
				{
					auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
					auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(InBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
					if (JFAIL(InDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE
						, &bufferDesc, InInitialResourceState, nullptr, IID_PPV_ARGS(OutResource))))
					{
						return false;
					}

					if (InResourceName)
						(*OutResource)->SetName(InResourceName);

					return true;
				};

			// Acceleration structure를 위한 리소스를 할당함
				// Acceleration structure는 default heap에서 생성된 리소스에만 있을 수 있음. (또는 그에 상응하는 heap)
				// Default heap은 CPU 읽기/쓰기 접근이 필요없기 때문에 괜찮음.
				// Acceleration structure를 포함하는 리소스는 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE 상태로 생성해야 함.
				// 그리고 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS 플래그를 가져야 함. ALLOW_UNORDERED_ACCESS는 두가지 간단한 정보를 요구함:
				// - 시스템은 백그라운드에서 Acceleration structure 빌드를 구현할 때 이러한 유형의 액세스를 수행할 것입니다.
				// - 앱의 관점에서, acceleration structure에 쓰기/읽기의 동기화는 UAV barriers를 통해서 얻어짐.

			D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
			RObj->BottomLevelASBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, 0, EBufferCreateFlag::UAV, initialResourceState
				, nullptr, 0, TEXT("BottomLevelAccelerationStructure"));

			//ComPtr<ID3D12Resource> m_bottomLevelAccelerationStructure;
			//AllocateUAVBuffer(&m_bottomLevelAccelerationStructure, g_rhi_dx12->Device.Get()
			//    , bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, initialResourceState, TEXT("BottomLevelAccelerationStructure"));

			// Bottom level acceleration structure desc
			//ComPtr<ID3D12Resource> scratchResource;
			RObj->ScratchASBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfo.ScratchDataSizeInBytes, 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_COMMON
				, nullptr, 0, TEXT("ScratchResourceGeometry"));
			//AllocateUAVBuffer(&scratchResource, g_rhi_dx12->Device.Get(), bottomLevelPrebuildInfo.ScratchDataSizeInBytes
			//    , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResourceGeometry1");

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc{};
			bottomLevelBuildDesc.Inputs = bottomLevelInputs;
			bottomLevelBuildDesc.ScratchAccelerationStructureData = ((jBuffer_DX12*)RObj->ScratchASBuffer)->GetGPUAddress();
			bottomLevelBuildDesc.DestAccelerationStructureData = ((jBuffer_DX12*)RObj->BottomLevelASBuffer)->GetGPUAddress();

			CmdBuffer->CommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
			auto temp = CD3DX12_RESOURCE_BARRIER::UAV(((jBuffer_DX12*)RObj->BottomLevelASBuffer)->Buffer.Get());
			CmdBuffer->CommandList->ResourceBarrier(1, &temp);
		}

		jGame::UpdateTopLevelAS = [RTObjects](void* InCmdBuffer) 
		{
			jCommandBuffer_DX12* InCmdBufferDX12 = (jCommandBuffer_DX12*)InCmdBuffer;
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
			inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
			inputs.NumDescs = (uint32)RTObjects.size();
			inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
			g_rhi_dx12->Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
			if (info.ResultDataMaxSizeInBytes)
			{
				const bool IsUpdate = !!ScratchASBuffer;
				if (IsUpdate)
				{
					D3D12_RESOURCE_BARRIER uavBarrier = {};
					uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					uavBarrier.UAV.pResource = TopLevelASBuffer->Buffer.Get();
					InCmdBufferDX12->CommandList->ResourceBarrier(1, &uavBarrier);
				}
				else
				{
					ScratchASBuffer = jBufferUtil_DX12::CreateBuffer(info.ScratchDataSizeInBytes, 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						, nullptr, 0, TEXT("TLAS Scratch Buffer"));

					TopLevelASBuffer = jBufferUtil_DX12::CreateBuffer(info.ScratchDataSizeInBytes, 0, EBufferCreateFlag::UAV, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
						, nullptr, 0, TEXT("TLAS Result Buffer"));

					InstanceDescUploadBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * RTObjects.size(), 0
						, EBufferCreateFlag::CPUAccess, D3D12_RESOURCE_STATE_GENERIC_READ
						, nullptr, 0, TEXT("TLAS Result Buffer"));
				}

				D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = (D3D12_RAYTRACING_INSTANCE_DESC*)InstanceDescUploadBuffer->Map();

				for (int32 i = 0; i < RTObjects.size(); ++i)
				{
					jRenderObject* RObj = RTObjects[i];
					RObj->UpdateWorldMatrix();

					instanceDescs[i].InstanceID = i;
					instanceDescs[i].InstanceContributionToHitGroupIndex = 0;
					memcpy(instanceDescs[i].Transform, &RObj->World.m, sizeof(instanceDescs[i].Transform));
					instanceDescs[i].InstanceMask = 0xFF;
					instanceDescs[i].AccelerationStructure = ((jBuffer_DX12*)RObj->BottomLevelASBuffer)->GetGPUAddress();
					for (int32 k = 0; k < 3; ++k)
					{
						for (int32 m = 0; m < 4; ++m)
						{
							instanceDescs[i].Transform[k][m] = RObj->World.m[m][k];
						}
					}
					if (gOptions.EarthQuake)
					{
						instanceDescs[i].Transform[0][3] = sin((float)g_rhi_dx12->CurrentFrameNumber);
						instanceDescs[i].Transform[1][3] = cos((float)g_rhi_dx12->CurrentFrameNumber);
						instanceDescs[i].Transform[2][3] = sin((float)g_rhi_dx12->CurrentFrameNumber) * 2;
					}
					instanceDescs[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
				}
				InstanceDescUploadBuffer->Unmap();

				// TLAS 생성
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
				asDesc.Inputs = inputs;
				asDesc.Inputs.InstanceDescs = InstanceDescUploadBuffer->GetGPUAddress();
				asDesc.DestAccelerationStructureData = TopLevelASBuffer->GetGPUAddress();
				asDesc.ScratchAccelerationStructureData = ScratchASBuffer->GetGPUAddress();

				if (IsUpdate)
				{
					asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
					asDesc.SourceAccelerationStructureData = TopLevelASBuffer->Buffer->GetGPUVirtualAddress();
				}

				InCmdBufferDX12->CommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
				auto temp = CD3DX12_RESOURCE_BARRIER::UAV(TopLevelASBuffer->Buffer.Get());
				InCmdBufferDX12->CommandList->ResourceBarrier(1, &temp);
			}
		};
		UpdateTopLevelAS(CmdBuffer);
		g_rhi_dx12->EndSingleTimeCommands(CmdBuffer);
	}
	else if (IsUseVulkan())
    {
        auto getBufferDeviceAddress = [](VkBuffer buffer)
            {
                VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
                bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                bufferDeviceAI.buffer = buffer;
                return g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &bufferDeviceAI);
            };

        auto BLASTransformBuffer = new jBuffer_Vulkan();
        {
            jBufferUtil_Vulkan::AllocateBuffer(
				EVulkanBufferBits::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY | EVulkanBufferBits::SHADER_DEVICE_ADDRESS,
                EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
				, jObject::GetStaticRenderObject().size() * sizeof(VkTransformMatrixKHR), *BLASTransformBuffer);

			VkTransformMatrixKHR transformMatrix = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f
			};
			auto MappedPtr = (VkTransformMatrixKHR*)BLASTransformBuffer->Map();
			for(int32 i=0;i<jObject::GetStaticRenderObject().size();++i)
			{
				MappedPtr[i] = transformMatrix;
			}
			BLASTransformBuffer->Unmap();
        
			BLASTransformBuffer->DeviceAddress = getBufferDeviceAddress(BLASTransformBuffer->Buffer);
        }

        std::vector<jRenderObject*> RTObjects;
        for (int32 i = 0; i < jObject::GetStaticRenderObject().size(); ++i)
        {
            jRenderObject* RObj = jObject::GetStaticRenderObject()[i];

            auto VertexBuffer_PositionOnly = RObj->GeometryDataPtr->VertexBuffer_PositionOnly;
            auto IndexBuffer = RObj->GeometryDataPtr->IndexBuffer;

			VkAccelerationStructureGeometryKHR geometry{};
            geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;

            auto VertexBufferVulkan = (jVertexBuffer_Vulkan*)VertexBuffer_PositionOnly;
			VkBuffer VtxBufferVulkan = VertexBufferVulkan->BindInfos.Buffers[0];

			if (GetVulkanTextureComponentCount(GetVulkanTextureFormat(VertexBufferVulkan->BindInfos.AttributeDescriptions[0].format)) < 3)
				continue;

            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VertexBufferVulkan->BindInfos.AttributeDescriptions[0].format;

			VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
			vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(VtxBufferVulkan);

			uint64 VertexStart = vertexBufferDeviceAddress.deviceAddress;
            int32 VertexCount = VertexBufferVulkan->GetElementCount();
            auto ROE = dynamic_cast<jRenderObjectElement*>(RObj);
            Vector2i VertexIndexOffset{};
            if (ROE)
            {
                VertexStart += VertexBufferVulkan->Streams[0].Stride * ROE->SubMesh.StartVertex;
                VertexCount = ROE->SubMesh.EndVertex - ROE->SubMesh.StartVertex + 1;

                VertexIndexOffset = Vector2i(ROE->SubMesh.StartVertex, ROE->SubMesh.StartFace);
            }

			RObj->VertexAndIndexOffsetBuffer = new jBuffer_Vulkan();
            jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::TRANSFER_DST | EVulkanBufferBits::VERTEX_BUFFER
                , EVulkanMemoryBits::DEVICE_LOCAL, sizeof(Vector2i), *(jBuffer_Vulkan*)RObj->VertexAndIndexOffsetBuffer);

            RTObjects.push_back(RObj);

			uint64 IndexStart = 0;
			uint32 PrimitiveCount = 0;
            if (IndexBuffer)
            {
                auto IndexBufferVulkan = (jIndexBuffer_Vulkan*)IndexBuffer;
				IndexBufferVulkan->BufferPtr->Buffer;
                
                VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
				indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(IndexBufferVulkan->BufferPtr->Buffer);

                auto& indexStreamData = IndexBufferVulkan->IndexStreamData;

				IndexStart = indexBufferDeviceAddress.deviceAddress;
                int32 IndexCount = IndexBuffer->GetElementCount();
                if (ROE)
                {
                    IndexStart += indexStreamData->Param->GetElementSize() * ROE->SubMesh.StartFace;
                    IndexCount = ROE->SubMesh.EndFace - ROE->SubMesh.StartFace + 1;
                }

                geometry.geometry.triangles.indexType = indexStreamData->Param->GetElementSize() == 16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                geometry.geometry.triangles.indexData = VkDeviceOrHostAddressConstKHR(IndexStart);

				PrimitiveCount = IndexCount / 3;
            }
			else
			{
				PrimitiveCount = VertexCount / 3;
			}

            geometry.geometry.triangles.vertexData = VkDeviceOrHostAddressConstKHR(VertexStart);
            geometry.geometry.triangles.maxVertex = VertexCount;
			geometry.geometry.triangles.vertexStride = VertexBufferVulkan->Streams[0].Stride;
			geometry.geometry.triangles.transformData.deviceAddress = BLASTransformBuffer->DeviceAddress + sizeof(VkTransformMatrixKHR) * i;

			std::vector<VkAccelerationStructureGeometryKHR> geometries{ geometry };

            VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
            accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            accelerationStructureBuildGeometryInfo.geometryCount = (uint32)geometries.size();
            accelerationStructureBuildGeometryInfo.pGeometries = geometries.data();

			std::vector<uint32_t> maxPrimitiveCounts{ PrimitiveCount };

            VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
            accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
			g_rhi_vk->vkGetAccelerationStructureBuildSizesKHR(
				g_rhi_vk->Device,
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &accelerationStructureBuildGeometryInfo,
				maxPrimitiveCounts.data(),
                &accelerationStructureBuildSizesInfo);

            RObj->BottomLevelASBuffer = new jBuffer_Vulkan();
			jBuffer_Vulkan* BLAS_Vulkan = (jBuffer_Vulkan*)RObj->BottomLevelASBuffer;
			{
				jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::ACCELERATION_STRUCTURE_STORAGE | EVulkanBufferBits::SHADER_DEVICE_ADDRESS,
					EVulkanMemoryBits::DEVICE_LOCAL, accelerationStructureBuildSizesInfo.accelerationStructureSize, *BLAS_Vulkan);

                VkAccelerationStructureCreateInfoKHR accelerationStructureCreate_info{};
                accelerationStructureCreate_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                accelerationStructureCreate_info.buffer = BLAS_Vulkan->Buffer;
                accelerationStructureCreate_info.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
                accelerationStructureCreate_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                g_rhi_vk->vkCreateAccelerationStructureKHR(g_rhi_vk->Device, &accelerationStructureCreate_info, nullptr, &BLAS_Vulkan->AccelerationStructure);
			}

			auto BLASScratch_Vulkan = new jBuffer_Vulkan();
			{
				jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::SHADER_DEVICE_ADDRESS | EVulkanBufferBits::STORAGE_BUFFER,
					EVulkanMemoryBits::DEVICE_LOCAL, accelerationStructureBuildSizesInfo.buildScratchSize, *BLASScratch_Vulkan);

				VkBufferDeviceAddressInfoKHR scratchBufferDeviceAddresInfo{};
				scratchBufferDeviceAddresInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
				scratchBufferDeviceAddresInfo.buffer = BLASScratch_Vulkan->Buffer;
				BLASScratch_Vulkan->DeviceAddress = g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &scratchBufferDeviceAddresInfo);
			}

			VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
			accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            accelerationBuildGeometryInfo.dstAccelerationStructure = BLAS_Vulkan->AccelerationStructure;
            accelerationBuildGeometryInfo.geometryCount = (uint32)geometries.size();
            accelerationBuildGeometryInfo.pGeometries = geometries.data();
            accelerationBuildGeometryInfo.scratchData.deviceAddress = BLASScratch_Vulkan->DeviceAddress;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
            buildRangeInfo.firstVertex = 0;
            buildRangeInfo.primitiveOffset = 0;
            buildRangeInfo.primitiveCount = PrimitiveCount;
            buildRangeInfo.transformOffset = 0;

			std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &buildRangeInfo };

            VkCommandBuffer CmdBufferVk = g_rhi_vk->BeginSingleTimeCommands();

            g_rhi_vk->vkCmdBuildAccelerationStructuresKHR(
                CmdBufferVk,
                1,
                &accelerationBuildGeometryInfo,
                accelerationBuildStructureRangeInfos.data());

			g_rhi_vk->EndSingleTimeCommands(CmdBufferVk);

            VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
            accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            accelerationDeviceAddressInfo.accelerationStructure = BLAS_Vulkan->AccelerationStructure;
			BLAS_Vulkan->DeviceAddress = g_rhi_vk->vkGetAccelerationStructureDeviceAddressKHR(g_rhi_vk->Device, &accelerationDeviceAddressInfo);

			delete BLASScratch_Vulkan;
        }
		g_rhi_vk->Flush();

        jGame::UpdateTopLevelAS = [RTObjects](void* InCmdBuffer)
        {
			VkCommandBuffer CmdBufferVk = (VkCommandBuffer)InCmdBuffer;
			jCommandBuffer_Vulkan* InCmdBufferVulkan = (jCommandBuffer_Vulkan*)InCmdBuffer;

			auto Instance_Vulkan = new jBuffer_Vulkan();
			Instance_Vulkan = new jBuffer_Vulkan();
            {
                jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::SHADER_DEVICE_ADDRESS | EVulkanBufferBits::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY
					, EVulkanMemoryBits::DEVICE_LOCAL | EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
					, sizeof(VkAccelerationStructureInstanceKHR) * RTObjects.size(), *Instance_Vulkan);
            }

            VkBufferDeviceAddressInfoKHR instanceDataDeviceAddress{};
            instanceDataDeviceAddress.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            instanceDataDeviceAddress.buffer = Instance_Vulkan->Buffer;
			Instance_Vulkan->DeviceAddress = g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &instanceDataDeviceAddress);

			//std::vector<VkAccelerationStructureGeometryKHR> ASGeoInfos;

			VkAccelerationStructureInstanceKHR* MappedPointer = (VkAccelerationStructureInstanceKHR*)Instance_Vulkan->Map();
            for (int32 i = 0; i < RTObjects.size(); ++i)
            {
                jRenderObject* RObj = RTObjects[i];
                RObj->UpdateWorldMatrix();

				MappedPointer[i].instanceCustomIndex = i;
				MappedPointer[i].mask = 0xFF;
				MappedPointer[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
				MappedPointer[i].accelerationStructureReference = ((jBuffer_Vulkan*)RObj->BottomLevelASBuffer)->DeviceAddress;
                for (int32 k = 0; k < 3; ++k)
                {
                    for (int32 m = 0; m < 4; ++m)
                    {
						MappedPointer[i].transform.matrix[k][m] = RObj->World.m[m][k];
                    }
                }
                if (gOptions.EarthQuake)
                {
					MappedPointer[i].transform.matrix[0][3] = sin((float)g_rhi_dx12->CurrentFrameNumber);
					MappedPointer[i].transform.matrix[1][3] = cos((float)g_rhi_dx12->CurrentFrameNumber);
					MappedPointer[i].transform.matrix[2][3] = sin((float)g_rhi_dx12->CurrentFrameNumber) * 2;
                }

				//ASGeoInfos.push_back(accelerationStructureGeometry);
			}
			Instance_Vulkan->Unmap();

            VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
            accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
            accelerationStructureGeometry.geometry.instances.data
                = VkDeviceOrHostAddressConstKHR(Instance_Vulkan->DeviceAddress);

            // Get Size info
            VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
            accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            accelerationStructureBuildGeometryInfo.geometryCount = 1;
            accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

			uint32 primitive_count = (uint32)RTObjects.size();

            VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
            accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
			g_rhi_vk->vkGetAccelerationStructureBuildSizesKHR(
                g_rhi_vk->Device,
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &accelerationStructureBuildGeometryInfo,
                &primitive_count,
                &accelerationStructureBuildSizesInfo);

            TLAS_Vulkan = new jBuffer_Vulkan();
            {
                jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::ACCELERATION_STRUCTURE_STORAGE | EVulkanBufferBits::SHADER_DEVICE_ADDRESS,
                    EVulkanMemoryBits::DEVICE_LOCAL, accelerationStructureBuildSizesInfo.accelerationStructureSize, *TLAS_Vulkan);
            }
            VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
            accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelerationStructureCreateInfo.buffer = TLAS_Vulkan->Buffer;
            accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
            accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            g_rhi_vk->vkCreateAccelerationStructureKHR(g_rhi_vk->Device, &accelerationStructureCreateInfo, nullptr, &TLAS_Vulkan->AccelerationStructure);

            // Create a small scratch buffer used during build of the bottom level acceleration structure
            auto TLASScratch_Vulkan = new jBuffer_Vulkan();
            jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::ACCELERATION_STRUCTURE_STORAGE | EVulkanBufferBits::SHADER_DEVICE_ADDRESS | EVulkanBufferBits::STORAGE_BUFFER,
                EVulkanMemoryBits::DEVICE_LOCAL, accelerationStructureBuildSizesInfo.buildScratchSize, *TLASScratch_Vulkan);

            VkBufferDeviceAddressInfoKHR scratchBufferDeviceAddresInfo{};
            scratchBufferDeviceAddresInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            scratchBufferDeviceAddresInfo.buffer = TLASScratch_Vulkan->Buffer;
			TLASScratch_Vulkan->DeviceAddress = g_rhi_vk->vkGetBufferDeviceAddressKHR(g_rhi_vk->Device, &scratchBufferDeviceAddresInfo);

            VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
            accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            accelerationBuildGeometryInfo.dstAccelerationStructure = TLAS_Vulkan->AccelerationStructure;
            accelerationBuildGeometryInfo.geometryCount = 1;
            accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
            accelerationBuildGeometryInfo.scratchData.deviceAddress = TLASScratch_Vulkan->DeviceAddress;

            VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
            accelerationStructureBuildRangeInfo.primitiveCount = primitive_count;
            accelerationStructureBuildRangeInfo.primitiveOffset = 0;
            accelerationStructureBuildRangeInfo.firstVertex = 0;
            accelerationStructureBuildRangeInfo.transformOffset = 0;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

			g_rhi_vk->vkCmdBuildAccelerationStructuresKHR(
				CmdBufferVk,
                1,
                &accelerationBuildGeometryInfo,
                accelerationBuildStructureRangeInfos.data());

            VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
            accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            accelerationDeviceAddressInfo.accelerationStructure = TLAS_Vulkan->AccelerationStructure;
			TLAS_Vulkan->DeviceAddress = g_rhi_vk->vkGetAccelerationStructureDeviceAddressKHR(g_rhi_vk->Device, &accelerationDeviceAddressInfo);
		};
        auto CmdBufferVk = g_rhi_vk->BeginSingleTimeCommands();
		jGame::UpdateTopLevelAS(CmdBufferVk);
		g_rhi_vk->EndSingleTimeCommands(CmdBufferVk);
        g_rhi_vk->Flush();
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
		MainCamera->UpdateCamera();

	gOptions.CameraPos = MainCamera->Pos;
	if (NormalDirectionalLight)
	{
		Vector& SunDir = NormalDirectionalLight->GetLightData().Direction;
		SunDir = gOptions.SunDir;
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

		jRenderer renderer(renderFrameContext, View);
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

	for (jObject* iter : SpawnedObjects)
	{
		delete iter;
	}
	SpawnedObjects.clear();

    DirectionalLight = nullptr;		// 현재 사용중인 Directional light 의 레퍼런스이므로 그냥 nullptr 설정
	delete NormalDirectionalLight;
	//delete CascadeDirectionalLight;
	delete PointLight;
	delete SpotLight;
	delete AmbientLight;
	delete MainCamera;

    delete DirectionalLightInfo;
    delete PointLightInfo;
    delete SpotLightInfo;
	delete DirectionalLightShadowMapUIDebug;

	// 임시
    jSceneRenderTarget::IrradianceMap.reset();
	jSceneRenderTarget::FilteredEnvMap.reset();
	jSceneRenderTarget::CubeEnvMap.reset();
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

	auto sphere = jPrimitiveUtil::CreateSphere(Vector(65.0f, 35.0f, 10.0f), 1.0, 150, Vector(30.0f), Vector4(0.8f, 0.0f, 0.0f, 1.0f));
	sphere->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
        float RotationSpeed = 100.0f;
        thisObject->RenderObjects[0]->SetRot(thisObject->RenderObjects[0]->GetRot() + Vector(0.0f, 0.0f, DegreeToRadian(180.0f)) * RotationSpeed * deltaTime);
	};
	jObject::AddObject(sphere);
	SpawnedObjects.push_back(sphere);

	auto sphere2 = jPrimitiveUtil::CreateSphere(Vector(150.0f, 5.0f, 0.0f), 1.0, 150, Vector(10.0f), Vector4(0.8f, 0.4f, 0.6f, 1.0f));
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

        GeometryDataPtr->VertexStream_InstanceData = std::make_shared<jVertexStreamData>();
        GeometryDataPtr->VertexStream_InstanceData->ElementCount = _countof(instanceData);
        GeometryDataPtr->VertexStream_InstanceData->StartLocation = (int32)GeometryDataPtr->VertexStream->GetEndLocation();
        GeometryDataPtr->VertexStream_InstanceData->BindingIndex = (int32)GeometryDataPtr->VertexStream->Params.size();
        GeometryDataPtr->VertexStream_InstanceData->VertexInputRate = EVertexInputRate::INSTANCE;
        GeometryDataPtr->VertexStream_InstanceData->Params.push_back(streamParam);
        GeometryDataPtr->VertexBuffer_InstanceData = g_rhi->CreateVertexBuffer(GeometryDataPtr->VertexStream_InstanceData);

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
        GeometryDataPtr->VertexStream_InstanceData = std::make_shared<jVertexStreamData>();
        GeometryDataPtr->VertexStream_InstanceData->ElementCount = _countof(instanceData);
        GeometryDataPtr->VertexStream_InstanceData->StartLocation = (int32)GeometryDataPtr->VertexStream->GetEndLocation();
        GeometryDataPtr->VertexStream_InstanceData->BindingIndex = (int32)GeometryDataPtr->VertexStream->Params.size();
        GeometryDataPtr->VertexStream_InstanceData->VertexInputRate = EVertexInputRate::INSTANCE;
        GeometryDataPtr->VertexStream_InstanceData->Params.push_back(streamParam);
        GeometryDataPtr->VertexBuffer_InstanceData = g_rhi->CreateVertexBuffer(GeometryDataPtr->VertexStream_InstanceData);

        // Create indirect draw buffer
        {
            check(GeometryDataPtr->VertexStream_InstanceData);

            std::vector<VkDrawIndirectCommand> indrectCommands;

            const int32 instanceCount = GeometryDataPtr->VertexStream_InstanceData->ElementCount;
            const int32 vertexCount = GeometryDataPtr->VertexStream->ElementCount;
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
            jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
                , bufferSize, stagingBuffer);

            stagingBuffer.UpdateBuffer(indrectCommands.data(), bufferSize);

            jBuffer_Vulkan* temp = new jBuffer_Vulkan();
			check(!GeometryDataPtr->IndirectCommandBuffer);
			GeometryDataPtr->IndirectCommandBuffer = temp;
            jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::TRANSFER_DST | EVulkanBufferBits::INDIRECT_BUFFER, EVulkanMemoryBits::DEVICE_LOCAL
                , bufferSize, *temp);
            jBufferUtil_Vulkan::CopyBuffer(stagingBuffer.Buffer, (VkBuffer)GeometryDataPtr->IndirectCommandBuffer->GetHandle(), bufferSize
				, stagingBuffer.AllocatedSize, GeometryDataPtr->IndirectCommandBuffer->GetAllocatedSize());

            stagingBuffer.Release();
        }

        jObject::AddObject(obj);
        SpawnedObjects.push_back(obj);
    }
}

