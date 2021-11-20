#include "pch.h"
#include "jRHI_DirectX12.h"
#include "jImageFileLoader.h"
#include "CompiledShaders/Raytracing.hlsl.h"
#include <limits>
#include <vector>
#include <string>
#include <fstream>
#include "jShaderCompiler_DirectX12.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const wchar_t* jRHI_DirectX12::c_hitGroupName = L"MyHitGroup";
const wchar_t* jRHI_DirectX12::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* jRHI_DirectX12::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* jRHI_DirectX12::c_missShaderName = L"MyMissShader";

bool BufferUtil::AllocateUploadBuffer(ID3D12Resource** OutResource, ID3D12Device* InDevice
	, void* InData, uint64 InDataSize, const wchar_t* InResourceName)
{
	auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(InDataSize);
	if (JFAIL(InDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE
		, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(OutResource))))
	{
		return false;
	}

	if (InResourceName)
		(*OutResource)->SetName(InResourceName);
	
	if (InData && InDataSize > 0)
	{
		void* pMappedData = nullptr;
		(*OutResource)->Map(0, nullptr, &pMappedData);
		memcpy(pMappedData, InData, InDataSize);
		(*OutResource)->Unmap(0, nullptr);
	}
	return true;
}

bool BufferUtil::AllocateUAVBuffer(ID3D12Resource** OutResource, ID3D12Device* InDevice
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
}

ShaderTable::ShaderTable(ID3D12Device* InDevice, uint32 InNumOfShaderRecords, uint32 InShaderRecordSize, const wchar_t* InResourceName /*= nullptr*/)
{
	m_shaderRecords.reserve(InNumOfShaderRecords);

	m_shaderRecordSize = Align(InShaderRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	const uint32 bufferSize = InNumOfShaderRecords & m_shaderRecordSize;
	BufferUtil::AllocateUploadBuffer(&m_resource, InDevice, InResourceName);

#if _DEBUG
	m_name = InResourceName;
#endif

	CD3DX12_RANGE readRange(0, 0);
	if (JFAIL(m_resource->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedShaderRecords))))
		return;
}

void ShaderTable::push_back(const ShaderRecord& InShaderRecord)
{
	if (JASSERT(m_shaderRecords.size() < m_shaderRecords.capacity()))
		return;

	m_shaderRecords.push_back(InShaderRecord);

	memcpy(m_mappedShaderRecords, InShaderRecord.m_shaderIdentifier
		, InShaderRecord.m_shaderIdentifierSize);

	if (InShaderRecord.m_localRootArguments)
	{
		memcpy(m_mappedShaderRecords + InShaderRecord.m_shaderIdentifierSize
			, InShaderRecord.m_localRootArguments, InShaderRecord.m_localRootArgumentsSize);
	}

	m_mappedShaderRecords += m_shaderRecordSize;
}

void ShaderTable::DebugPrint(std::unordered_map<void*, std::wstring> shaderIdToStringMap)
{
#if _DEBUG
	std::wstringstream wstr;
	wstr << L"|--------------------------------------------------------------------\n";
	wstr << L"|Shader table - " << m_name.c_str() << L": "
		<< m_shaderRecordSize << L" | "
		<< m_shaderRecords.size() * m_shaderRecordSize << L" bytes\n";

	for (UINT i = 0; i < m_shaderRecords.size(); i++)
	{
		wstr << L"| [" << i << L"]: ";
		wstr << shaderIdToStringMap[m_shaderRecords[i].m_shaderIdentifier] << L", ";
		wstr << m_shaderRecords[i].m_shaderIdentifierSize << L" + " << m_shaderRecords[i].m_localRootArgumentsSize << L" bytes \n";
	}
	wstr << L"|--------------------------------------------------------------------\n";
	wstr << L"\n";
	OutputDebugStringW(wstr.str().c_str());
#endif
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		// Save the DXSample* passed in to CreateWindow.
		LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
	}
	return 0;

	case WM_KEYDOWN:
		return 0;

	case WM_KEYUP:
		return 0;

	case WM_PAINT:
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

HWND jRHI_DirectX12::CreateMainWindow() const
{
	auto hInstance = GetModuleHandle(NULL);

	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, static_cast<LONG>(SCR_WIDTH), static_cast<LONG>(SCR_HEIGHT) };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	auto hWnd = CreateWindow(
		windowClass.lpszClassName,
		TEXT("DX12"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,        // We have no parent window.
		nullptr,        // We aren't using menus.
		hInstance,
		nullptr);

	return hWnd;
}

void GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter1** InAdapter
	, bool requestHighPerformanceAdapter = false)
{
	*InAdapter = nullptr;

	ComPtr<IDXGIAdapter1> adapter;

	ComPtr<IDXGIFactory6> factory6;
	if (JOK(InFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
	{
		const auto GpuPreference = requestHighPerformanceAdapter
			? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;
		
		uint32 adapterIndex = 0;
		while (factory6->EnumAdapterByGpuPreference(adapterIndex, GpuPreference, IID_PPV_ARGS(&adapter)))
		{
			++adapterIndex;

			if (!adapter)
				continue;

			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// 기본 렌더 드라이버 어댑터는 선택하지 않음, 소프트웨어 렌더러를 원하면 warpAdapter를 사용
				continue;
			}

			// 어댑터가 Direct3D 12를 지원하는지 체크하고 가능하면 device 를 생성한다.
			if (JOK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
				break;
		}
	}
	else
	{
		uint32 adapterIndex = 0;
		while (InFactory->EnumAdapters1(adapterIndex, &adapter))
		{
			++adapterIndex;

			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

			// 어댑터가 Direct3D 12를 지원하는지 체크하고 가능하면 device 를 생성한다.
			if (JOK(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))))
				break;
		}
	}

	*InAdapter = adapter.Detach();
}

void jRHI_DirectX12::WaitForGPU()
{
	if (m_commandQueue && m_fence && m_fenceEvent)
	{
		uint64 fenceValue = m_fenceValue[m_frameIndex];
		if (JFAIL(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
			return;

		if (JFAIL(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent)))
			return;

		WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
		++m_fenceValue[m_frameIndex];
	}
}

void jRHI_DirectX12::Initialize()
{
	HWND hWnd = CreateMainWindow();
	
	// 1. Device
	uint32 dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// 디버그 레이어 켬 ("optional feature" 그래픽 툴을 요구함)
	// 주의 : 디버그 레이어를 device 생성 후에 하면 활성화된 device를 무효화 됨.
	{
		ComPtr<ID3D12Debug> debugController;
		if (JOK(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// 디버그 레이어를 추가적으로 켬
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	if (JFAIL(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
		return;

	bool UseWarpDevice = false;		// Software rasterizer 사용 여부
	if (UseWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if (JFAIL(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
			return;

		if (JFAIL((D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))))
			return;
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		if (JFAIL(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
			return;
	}

	// 2. Command
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if (JFAIL(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
		return;

	// 3. Swapchain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = SCR_WIDTH;
	swapChainDesc.Height = SCR_HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	if (JFAIL(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hWnd
		, &swapChainDesc, nullptr, nullptr, &swapChain)))
	{
		return;
	}

	// 풀스크린으로 전환하지 않을 것이므로 아래 처럼 설정
	factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

	swapChain.As(&m_swapChain);
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// 4. Heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (JFAIL(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
		return;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 1;			// 1 - ratracing output texture UAV
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (JFAIL(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap))))
		return;

	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_cbvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 5. CommandAllocators, Commandlist, RTV for FrameCount
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (uint32 i = 0; i < FrameCount; ++i)
	{
		if (JFAIL(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
			return;

		m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);

		if (JFAIL(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator[i]))))
			return;
	}

	if (JFAIL(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator[0].Get()
		, nullptr, IID_PPV_ARGS(&m_commandList))))
	{
		return;
	}
	m_commandList->Close();

	// 6. Create sync object
	if (JFAIL(m_device->CreateFence(m_fenceValue[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
		return;

	++m_fenceValue[m_frameIndex];

	m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
	if (!m_fenceEvent)
	{
		if (JFAIL(HRESULT_FROM_WIN32(GetLastError())))
			return;
	}

	WaitForGPU();

	// 7. Raytracing device and commandlist
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData{};
	if (JFAIL(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))))
		return;

	if (featureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		return;

	if (JFAIL(m_device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice))))
		return;

	if (JFAIL(m_commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList))))
		return;

	// 8. CreateRootSignature
	{
		CD3DX12_DESCRIPTOR_RANGE UAVDescriptor;
		UAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
		rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &UAVDescriptor);
		rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(_countof(rootParameters), rootParameters);
		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> error;
		if (JFAIL(D3D12SerializeRootSignature(&globalRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
		{
			if (error)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
				error->Release();
			}
			return;
		}

		if (JFAIL(m_device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
			, IID_PPV_ARGS(&m_raytracingGlobalRootSignature))))
		{
			return;
		}
	}

	{
#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

		CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
		rootParameters[LocalRootSignatureParams::ViewportConstantSlot].InitAsConstants(SizeOfInUint32(m_rayGenCB), 0, 0);

		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(_countof(rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> error;
		if (JFAIL(D3D12SerializeRootSignature(&localRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
		{
			if (error)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
				error->Release();
			}
			return;
		}

		if (JFAIL(m_device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
			, IID_PPV_ARGS(&m_raytracingLocalRootSignature))))
		{
			return;
		}
	}

	// 9. DXR PipeplineStateObject
	// ----------------------------------------------------
	// 1 - DXIL Library
	// 1 - Triangle hit group
	// 1 - Shader config
	// 2 - Local root signature and association
	// 1 - Global root signature
	// 1 - Pipeline config
	// ----------------------------------------------------
	std::array<D3D12_STATE_SUBOBJECT, 7> subobjects;
	uint32 index = 0;

	// 1). DXIL 라이브러리 생성
	D3D12_DXIL_LIBRARY_DESC dxilDesc{};
	std::vector<D3D12_EXPORT_DESC> exportDesc;
	std::vector<std::wstring> exportName;
	{
		D3D12_STATE_SUBOBJECT subobject{};
		ComPtr<ID3DBlob> ShaderBlob = jShaderCompiler_DirectX12::Get().Compile(TEXT("Shaders/HLSL/Raytracing.hlsl"), TEXT("lib_6_3"));
		if (ShaderBlob)
		{
			const wchar_t* entryPoint[] = { jRHI_DirectX12::c_raygenShaderName, jRHI_DirectX12::c_missShaderName, jRHI_DirectX12::c_closestHitShaderName };
			subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
			subobject.pDesc = &dxilDesc;

			exportDesc.resize(_countof(entryPoint));
			exportName.resize(_countof(entryPoint));
			dxilDesc.DXILLibrary.pShaderBytecode = ShaderBlob->GetBufferPointer();
			dxilDesc.DXILLibrary.BytecodeLength = ShaderBlob->GetBufferSize();
			dxilDesc.NumExports = _countof(entryPoint);
			dxilDesc.pExports = exportDesc.data();

			for (uint32 i = 0; i < _countof(entryPoint); ++i)
			{
				exportName[i] = entryPoint[i];
				exportDesc[i].Name = exportName[i].c_str();
				exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
				exportDesc[i].ExportToRename = nullptr;
			}
		}
		subobjects[index++] = subobject;
	}

	// 2). Triangle hit group
	D3D12_HIT_GROUP_DESC hitgroupDesc{};
	{
		hitgroupDesc.AnyHitShaderImport = nullptr;
		hitgroupDesc.ClosestHitShaderImport = jRHI_DirectX12::c_closestHitShaderName;
		hitgroupDesc.HitGroupExport = jRHI_DirectX12::c_hitGroupName;

		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		subobject.pDesc = &hitgroupDesc;
		subobjects[index++] = subobject;
	}

	// 3). Shader Config
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
	{
		shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);	// float2 barycentrics
		shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);		// float4 color

		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		subobject.pDesc = &shaderConfig;
		subobjects[index++] = subobject;
	}

	// 4). Local root signature and association
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association{};
	{
		D3D12_STATE_SUBOBJECT subobject{};

		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		subobject.pDesc = m_raytracingLocalRootSignature.GetAddressOf();
		subobjects[index] = subobject;

		association.NumExports = 1;
		association.pExports = &jRHI_DirectX12::c_raygenShaderName;
		association.pSubobjectToAssociate = &subobjects[index++];

		D3D12_STATE_SUBOBJECT subobject2{};
		subobject2.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subobject2.pDesc = &association;
		subobjects[index++] = subobject2;
	}

	// 5). Global root signature
	{
		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		subobject.pDesc = m_raytracingGlobalRootSignature.GetAddressOf();
		subobjects[index++] = subobject;
	}

	// 6). Pipeline Config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
	{
		pipelineConfig.MaxTraceRecursionDepth = 1;

		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		subobject.pDesc = &pipelineConfig;

		subobjects[index++] = subobject;
	}

	// Create pipeline state
	D3D12_STATE_OBJECT_DESC stateObjectDesc;
	stateObjectDesc.NumSubobjects = index;
	stateObjectDesc.pSubobjects = subobjects.data();
	stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	if (JFAIL(m_dxrDevice->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_dxrStateObject))))
		return;

	//////////////////////////////////////////////////////////////////////////
	// 10. Create vertex and index buffer
	uint16 indices[] =
	{
		0, 1, 2
	};

	float depthValue = 1.0f;
	float offset = 0.7f;

	struct Vertex { float v1, v2, v3; };
	Vertex vertices[] =
	{
		0, -offset, depthValue,
		-offset, offset, depthValue,
		offset, offset, depthValue
	};
	BufferUtil::AllocateUploadBuffer(&m_vertexBuffer, m_device.Get(), vertices, sizeof(vertices));
	BufferUtil::AllocateUploadBuffer(&m_indexBuffer, m_device.Get(), indices, sizeof(indices));

	//////////////////////////////////////////////////////////////////////////
	// 11. AccelerationStructures
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.IndexBuffer = m_indexBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.IndexCount = static_cast<uint16>(m_indexBuffer->GetDesc().Width) / sizeof(uint16);
	geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.VertexCount = static_cast<uint32>(m_vertexBuffer->GetDesc().Width) / sizeof(Vertex);
	geometryDesc.Triangles.VertexBuffer.StartAddress = m_vertexBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

	// Opaque로 지오메트를 등록하면, 히트 쉐이더에서 더이상 쉐이더를 만들지 않을 것이므로 최적화에 좋다.
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	// Acceleration structure 에 필요한 크기를 요청함
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs{};
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = 1;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo{};
	m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	if (!JASSERT(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0))
		return;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo{};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = topLevelInputs;
	bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelInputs.pGeometryDescs = &geometryDesc;
	
	m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
	if (!JASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0))
		return;

	ComPtr<ID3D12Resource> scratchResource;
	const uint64 scratchResourceBufferSize 
		= std::max(topLevelPrebuildInfo.ScratchDataSizeInBytes, bottomLevelPrebuildInfo.ScratchDataSizeInBytes);
	BufferUtil::AllocateUAVBuffer(&scratchResource, m_device.Get(), scratchResourceBufferSize
		, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

	// Acceleration structure를 위한 리소스를 할당함
	// Acceleration structure는 default heap에서 생성된 리소스에만 있을 수 있음. (또는 그에 상응하는 heap)
	// Default heap은 CPU 읽기/쓰기 접근이 필요없기 때문에 괜찮음.
	// Acceleration structure를 포함하는 리소스는 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE 상태로 생성해야 함.
	// 그리고 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS 플래그를 가져야 함. ALLOW_UNORDERED_ACCESS는 두가지 간단한 정보를 요구함:
	// - 시스템은 백그라운드에서 Acceleration structure 빌드를 구현할 때 이러한 유형의 액세스를 수행할 것입니다.
	// - 앱의 관점에서, acceleration structure에 쓰기/읽기의 동기화는 UAV barriers를 통해서 얻어짐.
	D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	BufferUtil::AllocateUAVBuffer(&m_bottomLevelAccelerationStructure, m_device.Get()
		, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, initialResourceState, TEXT("BottomLevelAccelerationStructure"));
	BufferUtil::AllocateUAVBuffer(&m_topLevelAccelerationStructure, m_device.Get()
		, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, initialResourceState, TEXT("TopLevelAccelerationStructure"));

	// Bottom-level acceleration structure 를 위한 instance Desc 생성
	ComPtr<ID3D12Resource> instanceDescs;
	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
	instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
	instanceDesc.InstanceMask = 1;
	instanceDesc.AccelerationStructure = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
	BufferUtil::AllocateUploadBuffer(&instanceDescs, m_device.Get(), &instanceDesc, sizeof(instanceDesc), TEXT("InstanceDescs"));

	// Bottom level acceleration structure desc
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc{};
	bottomLevelBuildDesc.Inputs = bottomLevelInputs;
	bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
	bottomLevelBuildDesc.DestAccelerationStructureData = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();

	// Top level acceleration structure desc
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc{};
	topLevelInputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
	topLevelBuildDesc.Inputs = topLevelInputs;
	topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();

	// Acceleration structure 빌드
	if (JFAIL(m_commandList->Reset(m_commandAllocator[m_frameIndex].Get(), nullptr)))
		return;

	m_dxrCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAccelerationStructure.Get()));
	m_dxrCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);

	if (JFAIL(m_commandList->Close()))
		return;
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	WaitForGPU();

	// 12. ShaderTable
	const uint16 shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
	if (JFAIL(m_dxrStateObject.As(&stateObjectProperties)))
		return;
	
	void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
	void* misssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
	void* hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);

	// Raygen shader table
	{
		m_rayGenCB.viewport = { -1.0f, -1.0f, 1.0f, 1.0f };
		const float aspectRatio = static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT);
		if (SCR_WIDTH <= SCR_HEIGHT)
		{
			const float border = 0.1f;
			if (SCR_WIDTH <= SCR_HEIGHT)
			{
				m_rayGenCB.stencil =
				{
					-1.0f + border, -1.0f + border * aspectRatio,
					1.0f - border, 1.0f - border * aspectRatio
				};
			}
			else
			{
				m_rayGenCB.stencil =
				{
					-1.0f + border / aspectRatio, -1.0f + border,
					1.0f - border / aspectRatio, 1.0f + border
				};
			}
		}

		struct RootArguments
		{
			RayGenConstantBuffer cb;
		};
		RootArguments rootArguments;
		rootArguments.cb = m_rayGenCB;

		const uint16 numShaderRecords = 1;
		const uint16 shaderRecordSize = shaderIdentifierSize * sizeof(rootArguments);
		ShaderTable rayGenShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("RayGenShaderTable"));
		rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));
		m_rayGenShaderTable = rayGenShaderTable.GetResource();
	}

	// Miss shader table
	{
		const uint16 numShaderRecords = 1;
		const uint16 shaderRecordSize = shaderIdentifierSize;
		ShaderTable missShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("MissShaderTable"));
		missShaderTable.push_back(ShaderRecord(misssShaderIdentifier, shaderIdentifierSize));
		m_missShaderTable = missShaderTable.GetResource();
	}

	// Hit group shader table
	{
		const uint16 numShaderRecords = 1;
		const uint16 shaderRecordSize = shaderIdentifierSize;
		ShaderTable hitGroupShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("HitGroupShader"));
		hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize));
		m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
	}

	//////////////////////////////////////////////////////////////////////////
	// 13. Raytracing Output Resouce

	// 출력 리소스를 생성. 차원과 포맷은 swap-chain과 매치 되어야 함
	auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(BackbufferFormat
		, SCR_WIDTH, SCR_HEIGHT, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (JFAIL(m_device->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE
		, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput))))
	{
		return;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
	auto descriptorHeapCpuBase = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
	if (m_raytracingOutputResourceUAVDescriptorHeapIndex >= m_cbvHeap->GetDesc().NumDescriptors)
	{
		m_raytracingOutputResourceUAVDescriptorHeapIndex = m_descriptorsAllocated++;
		uavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase
			, m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
	m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart()
		, m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);

	//////////////////////////////////////////////////////////////////////////
	ShowWindow(hWnd, SW_SHOW);

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		Render();
	}
}

void jRHI_DirectX12::Release()
{

}


void jRHI_DirectX12::Render()
{
	// Prepare
	if (JFAIL(m_commandAllocator[m_frameIndex]->Reset()))
		return;
	if (JFAIL(m_commandList->Reset(m_commandAllocator[m_frameIndex].Get(), nullptr)))
		return;

	// DoRaytracing
	m_commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

	D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
	m_commandList->SetDescriptorHeaps(1, m_cbvHeap.GetAddressOf());
	m_commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot
		, m_raytracingOutputResourceUAVGpuDescriptor);
	m_commandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot
		, m_topLevelAccelerationStructure->GetGPUVirtualAddress());

	// 각 Shader table은 단 한개의 shader record를 가지기 때문에 stride가 그 사이즈와 동일함
	dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
	dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
	dispatchDesc.HitGroupTable.StrideInBytes = dispatchDesc.HitGroupTable.SizeInBytes;

	dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
	dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
	dispatchDesc.MissShaderTable.StrideInBytes = dispatchDesc.MissShaderTable.SizeInBytes;

	dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;

	dispatchDesc.Width = SCR_WIDTH;
	dispatchDesc.Height = SCR_HEIGHT;
	dispatchDesc.Depth = 1;

	m_dxrCommandList->SetPipelineState1(m_dxrStateObject.Get());
	m_dxrCommandList->DispatchRays(&dispatchDesc);

	// CopyRaytracingOutputToBackbuffer
	D3D12_RESOURCE_BARRIER preCopyBarriers[2];
	preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get()
		, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get()
		, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	m_commandList->ResourceBarrier(_countof(preCopyBarriers), preCopyBarriers);

	m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_raytracingOutput.Get());

	D3D12_RESOURCE_BARRIER postCopyBarriers[2];
	postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get()
		, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get()
		, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_commandList->ResourceBarrier(_countof(postCopyBarriers), postCopyBarriers);

	// Present
	if (JFAIL(m_commandList->Close()))
		return;
	ID3D12CommandList* commandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	static bool IsAllowTearing = false;
	HRESULT hr = S_OK;
	if (IsAllowTearing)
	{
		hr = m_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}
	else
	{
		// 첫번째 아규먼트는 VSync가 될때까지 기다림, 어플리케이션은 다음 VSync까지 잠재운다.
		// 이렇게 하는 것은 렌더링 한 프레임 중 화면에 나오지 못하는 프레임의 cycle을 아끼기 위해서임.
		hr = m_swapChain->Present(1, 0);
	}

	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		// 디바이스 로스트 처리
	}
	else
	{
		if (JFAIL(hr))
			return;

		const uint64 currentFenceValue = m_fenceValue[m_frameIndex];
		if (JFAIL(m_commandQueue->Signal(m_fence.Get(), currentFenceValue)))
			return;

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		if (m_fence->GetCompletedValue() < m_fenceValue[m_frameIndex])
		{
			if (JFAIL(m_fence->SetEventOnCompletion(m_fenceValue[m_frameIndex], m_fenceEvent)))
				return;
			WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
		}
		m_fenceValue[m_frameIndex] = currentFenceValue + 1;
	}
}


void jRHI_DirectX12::OnDeviceLost()
{

}

void jRHI_DirectX12::OnDeviceRestored()
{

}


