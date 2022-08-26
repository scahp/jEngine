#include "pch.h"
#include "jRHI_DirectX12.h"
#include "jShaderCompiler_DirectX12.h"
#include <iomanip>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const wchar_t* jRHI_DirectX12::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* jRHI_DirectX12::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* jRHI_DirectX12::c_missShaderName = L"MyMissShader";
const wchar_t* jRHI_DirectX12::c_triHitGroupName = L"TriHitGroup";
const wchar_t* jRHI_DirectX12::c_planeHitGroupName = L"PlaneHitGroup";
const wchar_t* jRHI_DirectX12::c_planeclosestHitShaderName = L"MyPlaneClosestHitShader";

constexpr uint32 c_AllowTearing = 0x1;
constexpr uint32 c_RequireTearingSupport = 0x2;
constexpr uint32 g_MaxRecursionDepth = 10;

inline float random_double() 
{
    // Returns a random real in [0,1).
    return rand() / (RAND_MAX + 1.0f);
}

// Pretty-print a state object tree.
inline void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
{
	std::wstringstream wstr;
	wstr << L"\n";
	wstr << L"--------------------------------------------------------------------\n";
	wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
	if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
	if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

	auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
	{
		std::wostringstream woss;
		for (UINT i = 0; i < numExports; i++)
		{
			woss << L"|";
			if (depth > 0)
			{
				for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
			}
			woss << L" [" << i << L"]: ";
			if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
			woss << exports[i].Name << L"\n";
		}
		return woss.str();
	};

	for (UINT i = 0; i < desc->NumSubobjects; i++)
	{
		wstr << L"| [" << i << L"]: ";
		switch (desc->pSubobjects[i].Type)
		{
		case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
			wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
			wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
			wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
		{
			wstr << L"DXIL Library 0x";
			auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
			wstr << ExportTree(1, lib->NumExports, lib->pExports);
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
		{
			wstr << L"Existing Library 0x";
			auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << collection->pExistingCollection << L"\n";
			wstr << ExportTree(1, collection->NumExports, collection->pExports);
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
		{
			wstr << L"Subobject to Exports Association (Subobject [";
			auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
			UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
			wstr << index << L"])\n";
			for (UINT j = 0; j < association->NumExports; j++)
			{
				wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
			}
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
		{
			wstr << L"DXIL Subobjects to Exports Association (";
			auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
			wstr << association->SubobjectToAssociate << L")\n";
			for (UINT j = 0; j < association->NumExports; j++)
			{
				wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
			}
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
		{
			wstr << L"Raytracing Shader Config\n";
			auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
			wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
			wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
		{
			wstr << L"Raytracing Pipeline Config\n";
			auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
			wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
		{
			wstr << L"Hit Group (";
			auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
			wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
			wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
			wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
			break;
		}
		}
		wstr << L"|--------------------------------------------------------------------\n";
	}
	wstr << L"\n";
	OutputDebugStringW(wstr.str().c_str());
}

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
	const uint32 bufferSize = InNumOfShaderRecords * m_shaderRecordSize;
	BufferUtil::AllocateUploadBuffer(&m_resource, InDevice, nullptr, bufferSize, InResourceName);

#if _DEBUG
	m_name = InResourceName;
#endif

	CD3DX12_RANGE readRange(0, 0);
	if (JFAIL(m_resource->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedShaderRecords))))
		return;
}

void ShaderTable::push_back(const ShaderRecord& InShaderRecord)
{
    if (ensure(m_shaderRecords.size() >= m_shaderRecords.capacity()))
        return;

	m_shaderRecords.push_back(InShaderRecord);

    uint32 incrementSize = InShaderRecord.m_shaderIdentifierSize;

	memcpy(m_mappedShaderRecords, InShaderRecord.m_shaderIdentifier
		, InShaderRecord.m_shaderIdentifierSize);

	if (InShaderRecord.m_localRootArguments)
	{
		memcpy(m_mappedShaderRecords + InShaderRecord.m_shaderIdentifierSize
			, InShaderRecord.m_localRootArguments, InShaderRecord.m_localRootArgumentsSize);
        incrementSize += InShaderRecord.m_localRootArgumentsSize;
	}

	//m_mappedShaderRecords += incrementSize;
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

jRHI_DirectX12* pRHIDirectX12 = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    static bool sSizeChanged = false;
    static bool sIsDraging = false;
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

    case WM_SIZE:
    {
        RECT clientRect = {};
        GetClientRect(hWnd, &clientRect);

        uint32 width = clientRect.right - clientRect.left;
        uint32 height = clientRect.bottom - clientRect.top;
        const bool isSizeMininized = wParam == SIZE_MINIMIZED;

        if (width != SCR_WIDTH || height != SCR_HEIGHT)
        {
			SCR_WIDTH = width;
            SCR_HEIGHT = height;
			IsSizeMinimize = isSizeMininized;
            sSizeChanged = true;

            if (!sIsDraging)
            {
                sSizeChanged = false;
                if (pRHIDirectX12)
                    pRHIDirectX12->OnHandleResized(SCR_WIDTH, SCR_HEIGHT, IsSizeMinimize);
            }
        }
    }
    return 0;
    case WM_ENTERSIZEMOVE:
        sIsDraging = true;
        break;
    case WM_EXITSIZEMOVE:
    {
        if (sSizeChanged && sIsDraging)
        {
            sSizeChanged = false;
            if (pRHIDirectX12)
                pRHIDirectX12->OnHandleResized(SCR_WIDTH, SCR_HEIGHT, IsSizeMinimize);
        }
        sIsDraging = false;
    }
    return 0;
    case WM_PAINT:
        if (pRHIDirectX12)
        {
            pRHIDirectX12->Update();
            pRHIDirectX12->Render();
        }
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

int32 GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter1** InAdapter
	, bool requestHighPerformanceAdapter = false)
{
	*InAdapter = nullptr;

    uint32 adapterIndex = 0;
	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<IDXGIFactory6> factory6;
	if (JOK(InFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
	{
		const auto GpuPreference = requestHighPerformanceAdapter
			? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;
		
		while (S_OK != factory6->EnumAdapterByGpuPreference(adapterIndex, GpuPreference, IID_PPV_ARGS(&adapter)))
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

            break;
		}
	}
	else
	{
		while (S_OK != InFactory->EnumAdapters1(adapterIndex, &adapter))
		{
			++adapterIndex;

			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

            break;
		}
	}

	*InAdapter = adapter.Detach();
    return adapterIndex;
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

bool jRHI_DirectX12::Initialize()
{
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
		return false;

    ComPtr<IDXGIFactory5> factory5;
    HRESULT hr = factory.As(&factory5);
    if (SUCCEEDED(hr))
    {
        BOOL allowTearing = false;
        hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));

        if (FAILED(hr) || !allowTearing)
        {
            OutputDebugStringA("WARNING: Variable refresh rate displays are not supported.\n");
            if (m_options & c_RequireTearingSupport)
            {
                JASSERT(!L"Error: Sample must be run on an OS with tearing support.\n");
            }
            m_options &= ~c_AllowTearing;
        }
        else
        {
            m_options |= c_AllowTearing;
        }
    }

	bool UseWarpDevice = false;		// Software rasterizer 사용 여부
	if (UseWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if (JFAIL(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
			return false;

		if (JFAIL((D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))))
			return false;
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		const int32 ResultAdapterID = GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        if (JFAIL(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
        {
            return false;
        }
        else
        {
            DXGI_ADAPTER_DESC desc;
            hardwareAdapter->GetDesc(&desc);
            AdapterID = ResultAdapterID;
            AdapterName = desc.Description;

#ifdef _DEBUG
            wchar_t buff[256] = {};
            swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", AdapterID, desc.VendorId, desc.DeviceId, desc.Description);
            OutputDebugStringW(buff);
#endif
        }
	}

	// 2. Command
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if (JFAIL(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
		return false;

	// 3. Swapchain
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = SCR_WIDTH;
	swapChainDesc.Height = SCR_HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags = (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain;
	if (JFAIL(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hWnd
		, &swapChainDesc, nullptr, nullptr, &swapChain)))
	{
		return false;
	}

	// 풀스크린으로 전환하지 않을 것이므로 아래 처럼 설정
	// factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

	swapChain.As(&m_swapChain);
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

 	// 4. Heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (JFAIL(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
		return false;

    //////////////////////////////////////////////////////////////////////////
    // 5. Initialize Camera and lighting
    {
        auto frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Setup material
        m_cubeCB.albedo = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
        m_planeCB.albedo = XMFLOAT4(0.5, 0.5, 0.5, 1.0f);

        // Setup camera
        m_eye = { 9.0f, 1.0f, 5.0f, 1.0f };
        m_at = { 0.0f, 0.0f, 0.0f, 1.0f };
        XMVECTOR right = { 1.0f, 0.0f, 0.0f, 0.0f };

        XMVECTOR direction = XMVector4Normalize(m_at - m_eye);
        // m_up = XMVector3Normalize(XMVector3Cross(direction, right));
        m_up = { 0.0f, 1.0f, 0.0f };

        //// Rotate camera around Y axis
        //XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(0.0f));
        //m_eye = XMVector3Transform(m_eye, rotate);
        //m_up = XMVector3Transform(m_up, rotate);

        UpdateCameraMatrices();

        // Setup lights
        XMFLOAT4 lightPosition;
        XMFLOAT4 lightAmbientColor;
        XMFLOAT4 lightDiffuseColor;

        lightPosition = XMFLOAT4(0.0f, 1.8f, -3.0f, 0.0f);
        m_sceneCB[frameIndex].lightPosition = XMLoadFloat4(&lightPosition);

        lightAmbientColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        m_sceneCB[frameIndex].lightAmbientColor = XMLoadFloat4(&lightAmbientColor);

        lightDiffuseColor = XMFLOAT4(0.5f, 0.3f, 0.3f, 1.0f);
        m_sceneCB[frameIndex].lightDiffuseColor = XMLoadFloat4(&lightDiffuseColor);

        m_sceneCB[frameIndex].focalDistance = m_focalDistance;
        m_sceneCB[frameIndex].lensRadius = m_lensRadius;

        // 모든 프레임의 버퍼 인스턴스에 초기값을 설정해줌
        for (auto& sceneCB : m_sceneCB)
        {
            sceneCB = m_sceneCB[frameIndex];
        }
    }


	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
	// 2 - vertex and index buffer SRV for Cube
    // 1 - vertex buffer SRV for Plane
	// 1 - ratracing output texture UAV
    // 1 - imgui
	cbvHeapDesc.NumDescriptors = 5;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (JFAIL(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap))))
		return false;

	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_cbvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 6. CommandAllocators, Commandlist, RTV for FrameCount
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (uint32 i = 0; i < FrameCount; ++i)
	{
		if (JFAIL(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
			return false;

		m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);

		if (JFAIL(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator[i]))))
			return false;
	}

	if (JFAIL(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator[0].Get()
		, nullptr, IID_PPV_ARGS(&m_commandList))))
	{
		return false;
	}
	m_commandList->Close();

	// 7. Create sync object
	if (JFAIL(m_device->CreateFence(m_fenceValue[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
		return false;

	++m_fenceValue[m_frameIndex];

	m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
	if (!m_fenceEvent)
	{
		if (JFAIL(HRESULT_FROM_WIN32(GetLastError())))
			return false;
	}

	WaitForGPU();

	// 8. Raytracing device and commandlist
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData{};
	if (JFAIL(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))))
		return false;

	if (featureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		return false;

	if (JFAIL(m_device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice))))
		return false;

	if (JFAIL(m_commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList))))
		return false;

	// 9. CreateRootSignatures
	{
		// global root signature는 DispatchRays 함수 호출로 만들어지는 레이트레이싱 쉐이더의 전체에 공유됨.

		CD3DX12_DESCRIPTOR_RANGE ranges[3];		// 가장 빈번히 사용되는 것을 앞에 둘 수록 최적화에 좋음
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);		// 1 output texture
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);		// 2 static index and vertex buffer for cube
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);		// 1 vertex buffer for plane

		CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
		rootParameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]);
        rootParameters[GlobalRootSignatureParams::PlaneVertexBufferSlot].InitAsDescriptorTable(1, &ranges[2]);
		rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
		rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
		rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);

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
			return false;
		}

		if (JFAIL(m_device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
			, IID_PPV_ARGS(&m_raytracingGlobalRootSignature))))
		{
			return false;
		}
	}

	{
#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

		CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
		rootParameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cubeCB), 1);

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
			return false;
		}

		if (JFAIL(m_device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
			, IID_PPV_ARGS(&m_raytracingLocalRootSignature))))
		{
			return false;
		}
	}

	// 10. DXR PipeplineStateObject
	// ----------------------------------------------------
	// 1 - DXIL Library
	// 2 - Triangle and plane hit group
	// 1 - Shader config
	// 4 - Local root signature and association
	// 1 - Global root signature
	// 1 - Pipeline config
	// ----------------------------------------------------
	std::array<D3D12_STATE_SUBOBJECT, 10> subobjects;
	uint32 index = 0;

	// 1). DXIL 라이브러리 생성
	D3D12_DXIL_LIBRARY_DESC dxilDesc{};
	std::vector<D3D12_EXPORT_DESC> exportDesc;
	ComPtr<IDxcBlob> ShaderBlob;
	std::vector<std::wstring> exportName;
	{
		D3D12_STATE_SUBOBJECT subobject{};
		ShaderBlob = jShaderCompiler_DirectX12::Get().Compile(TEXT("Resource/Shaders/hlsl/RaytracingCubeAndPlane.hlsl"), TEXT("lib_6_3"));
		if (ShaderBlob)
		{
			const wchar_t* entryPoint[] = { jRHI_DirectX12::c_raygenShaderName, jRHI_DirectX12::c_closestHitShaderName, jRHI_DirectX12::c_missShaderName, jRHI_DirectX12::c_planeclosestHitShaderName };
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

	// 2). Triangle and plane hit group
    // Triangle hit group
	D3D12_HIT_GROUP_DESC hitgroupDesc{};
	{
		hitgroupDesc.AnyHitShaderImport = nullptr;
		hitgroupDesc.ClosestHitShaderImport = jRHI_DirectX12::c_closestHitShaderName;
		hitgroupDesc.HitGroupExport = jRHI_DirectX12::c_triHitGroupName;
		hitgroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		subobject.pDesc = &hitgroupDesc;
		subobjects[index++] = subobject;
	}

    // Plane hit group
    D3D12_HIT_GROUP_DESC planeHitGroupDesc{};
    {
        planeHitGroupDesc.AnyHitShaderImport = nullptr;
        planeHitGroupDesc.ClosestHitShaderImport = jRHI_DirectX12::c_planeclosestHitShaderName;
        planeHitGroupDesc.HitGroupExport = jRHI_DirectX12::c_planeHitGroupName;
        planeHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        D3D12_STATE_SUBOBJECT subobject{};
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobject.pDesc = &planeHitGroupDesc;
        subobjects[index++] = subobject;
    }

	// 3). Shader Config
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
	{
		shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);	// float2 barycentrics
		shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float) + sizeof(int32);		// float4 color + float maxDepth

		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		subobject.pDesc = &shaderConfig;
		subobjects[index++] = subobject;
	}

	// 4). Local root signature and association

    // triangle hit root signature and association
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association{};
	{
		D3D12_STATE_SUBOBJECT subobject{};

		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		subobject.pDesc = m_raytracingLocalRootSignature.GetAddressOf();
		subobjects[index] = subobject;

		association.NumExports = 1;
		association.pExports = &jRHI_DirectX12::c_triHitGroupName;
		association.pSubobjectToAssociate = &subobjects[index++];

		D3D12_STATE_SUBOBJECT subobject2{};
		subobject2.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subobject2.pDesc = &association;
		subobjects[index++] = subobject2;
	}

    // empty root signature and associate it with the plane hit group and miss shader
    //D3D12_ROOT_SIGNATURE_DESC emptyDesc{};
    CD3DX12_ROOT_PARAMETER rootParametersSecondGeometry[LocalRootSignatureParams::Count];
    rootParametersSecondGeometry[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cubeCB), 1);

    CD3DX12_ROOT_SIGNATURE_DESC emptyDesc(_countof(rootParametersSecondGeometry), rootParametersSecondGeometry);
    emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    
    const WCHAR* emptyRootExport[] = { c_planeclosestHitShaderName, c_missShaderName };
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION emptyAssociation{};
    {
#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        if (JFAIL(D3D12SerializeRootSignature(&emptyDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
        {
            if (error)
            {
                OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
                error->Release();
            }
            return false;
        }

        if (JFAIL(m_device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
            , IID_PPV_ARGS(&m_raytracingEmptyLocalRootSignature))))
        {
            return false;
        }

        D3D12_STATE_SUBOBJECT subobject;
        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        subobject.pDesc = m_raytracingEmptyLocalRootSignature.GetAddressOf();
        subobjects[index] = subobject;

        association.NumExports = _countof(emptyRootExport);
        association.pExports = emptyRootExport;
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
		// pipelineConfig.MaxTraceRecursionDepth = D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
        pipelineConfig.MaxTraceRecursionDepth = g_MaxRecursionDepth;

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

#if _DEBUG
	PrintStateObjectDesc(&stateObjectDesc);
#endif

	if (JFAIL(m_dxrDevice->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_dxrStateObject))))
		return false;

	//////////////////////////////////////////////////////////////////////////
	// 11. Create vertex and index buffer

    if (JFAIL(m_commandList->Reset(m_commandAllocator[m_frameIndex].Get(), nullptr)))
        return false;

    const int32 slice = 100;
    const int32 verticesCount = ((slice + 1) * (slice / 2) + 2);
    const int32 verticesElementCount = ((slice + 1) * (slice / 2) + 2) / 3;

    uint16 indices[((slice) / 2 - 2) * slice * 6 + (slice * 2 * 3)];

    int32 iCount = 0;
    int32 toNextSlice = slice + 1;
    int32 cntIndex = 0;
    for (int32 k = 0; k < (slice) / 2 - 2; ++k, iCount += 1)
    {
        for (int32 i = 0; i < slice; ++i, iCount += 1)
        {
            indices[cntIndex++] = iCount; indices[cntIndex++] = (iCount + 1); indices[cntIndex++] = (iCount + toNextSlice);
            indices[cntIndex++] = (iCount + toNextSlice); indices[cntIndex++] = (iCount + 1); indices[cntIndex++] = (iCount + toNextSlice + 1);
        }
    }

    for (int32 i = 0; i < slice; ++i, iCount += 1)
    {
        indices[cntIndex++] = (iCount);
        indices[cntIndex++] = (iCount + 1);
        indices[cntIndex++] = (verticesCount - 1);
    }

    iCount = 0;
    for (int32 i = 0; i < slice; ++i, iCount += 1)
    {
        indices[cntIndex++] = (iCount);
        indices[cntIndex++] = (verticesCount - 2);
        indices[cntIndex++] = (iCount + 1);
    }

	// Vertex and normal
    Vertex vertices[verticesCount];


    const float stepRadian = DegreeToRadian(360.0f / slice);
    const float radius = 1.0f;
    Vector offset = {0.0f, 0.0f, 0.0f};
    int32 cnt = 0;
    for (int32 j = 0; j < slice / 2; ++j)
    {
        for (int32 i = 0; i <= slice; ++i, ++cnt)
        {
            const Vector temp(offset.x + cosf(stepRadian * i) * radius * sinf(stepRadian * (j + 1))
                , offset.z + cosf(stepRadian * (j + 1)) * radius
                , offset.y + sinf(stepRadian * i) * radius * sinf(stepRadian * (j + 1)));
            vertices[cnt].position = XMFLOAT3(temp.x, temp.y, temp.z);
            
            const Vector normal = temp.GetNormalize();
            vertices[cnt].normal = XMFLOAT3(normal.x, normal.y, normal.z);
        }
    }

    // top
    {
        vertices[cnt].position = XMFLOAT3(0.0f, radius, 0.0f);
        Vector normal = Vector(0.0f, radius, 0.0f).GetNormalize();
        vertices[cnt].normal = XMFLOAT3(normal.x, normal.y, normal.z);
        ++cnt;
    }

    // down
    {    
        vertices[cnt].position = XMFLOAT3(0.0f, -radius, 0.0f);
        Vector normal = Vector(0.0f, -radius, 0.0f).GetNormalize();
        vertices[cnt].normal = XMFLOAT3(normal.x, normal.y, normal.z);
        ++cnt;
    }

	BufferUtil::AllocateUploadBuffer(&m_vertexBuffer, m_device.Get(), vertices, sizeof(vertices), TEXT("SphereVB"));
	BufferUtil::AllocateUploadBuffer(&m_indexBuffer, m_device.Get(), indices, sizeof(indices), TEXT("SphereIB"));

	// 버택스 버퍼와 인덱스 버퍼는 디스크립터 테이블로 쉐이더로 전달됨
	// 디스크립터 힙에서 버택스 버퍼 디스크립터는 인덱스 버퍼 디스크립터에 바로다음에 있어야 함
	{
		// IndexBuffer SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = sizeof(indices) / 4;
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		srvDesc.Buffer.StructureByteStride = 0;

		uint32 descriptorIndex = UINT_MAX;
		if (descriptorIndex >= m_cbvHeap->GetDesc().NumDescriptors)
		{
			descriptorIndex = m_allocatedDescriptors++;
		}
		m_indexBufferCpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_cbvDescriptorSize);

		m_device->CreateShaderResourceView(m_indexBuffer.Get(), &srvDesc, m_indexBufferCpuDescriptor);
		m_indexBufferGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_cbvDescriptorSize);
	}
	{
		// VertexBuffer SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = _countof(vertices);
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.StructureByteStride = sizeof(vertices[0]);

		uint32 descriptorIndex = UINT_MAX;
		if (descriptorIndex >= m_cbvHeap->GetDesc().NumDescriptors)
		{
			descriptorIndex = m_allocatedDescriptors++;
		}
		m_vertexBufferCpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_cbvDescriptorSize);

		m_device->CreateShaderResourceView(m_vertexBuffer.Get(), &srvDesc, m_vertexBufferCpuDescriptor);
		m_vertexBufferGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_cbvDescriptorSize);
	}

	//////////////////////////////////////////////////////////////////////////
	// 12. AccelerationStructures
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.IndexBuffer = m_indexBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.IndexCount = static_cast<uint32>(m_indexBuffer->GetDesc().Width) / sizeof(uint16);
	geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.VertexCount = static_cast<uint32>(m_vertexBuffer->GetDesc().Width) / sizeof(Vertex);
	geometryDesc.Triangles.VertexBuffer.StartAddress = m_vertexBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

	// Opaque로 지오메트를 등록하면, 히트 쉐이더에서 더이상 쉐이더를 만들지 않을 것이므로 최적화에 좋다.
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

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


        m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
        if (!ensure(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0))
            return false;
    }

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

    // Bottom level acceleration structure desc
    ComPtr<ID3D12Resource> scratchResource;
    BufferUtil::AllocateUAVBuffer(&scratchResource, m_device.Get(), bottomLevelPrebuildInfo.ScratchDataSizeInBytes
        , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResourceGeometry1");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc{};
    bottomLevelBuildDesc.Inputs = bottomLevelInputs;
    bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
    bottomLevelBuildDesc.DestAccelerationStructureData = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();

    m_dxrCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAccelerationStructure.Get());
		m_commandList->ResourceBarrier(1, &barrier);
	}

    //////////////////////////////////////////////////////////////////////////

    // 두번째 지오메트리
    Vertex secondVertices[] = {
            XMFLOAT3(-100.0f, -1.0f, -100.0f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
            XMFLOAT3(100.0f, -1.0f, 100.f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
            XMFLOAT3(-100.0f, -1.0f, 100.0f),   XMFLOAT3(0.0f, 1.0f, 0.0f),

            XMFLOAT3(-100.0f, -1.0f, -100.0f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
            XMFLOAT3(100.0f, -1.0f, -100.0f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
            XMFLOAT3(100.0f, -1.0f, 100.0f),   XMFLOAT3(0.0f, 1.0f, 0.0f),
    };

    BufferUtil::AllocateUploadBuffer(&m_vertexBufferSecondGeometry, m_device.Get(), secondVertices, sizeof(secondVertices), TEXT("SecondGeometryVB"));

    {
        // VertexBuffer SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = _countof(secondVertices);
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.StructureByteStride = sizeof(secondVertices[0]);

        uint32 descriptorIndex = UINT_MAX;
        if (descriptorIndex >= m_cbvHeap->GetDesc().NumDescriptors)
        {
            descriptorIndex = m_allocatedDescriptors++;
        }
        m_vertexBufferCpuDescriptorSecondGeometry = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, m_cbvDescriptorSize);

        m_device->CreateShaderResourceView(m_vertexBufferSecondGeometry.Get(), &srvDesc, m_vertexBufferCpuDescriptorSecondGeometry);
        m_vertexBufferGpuDescriptorSecondGeometry = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_cbvDescriptorSize);
    }

    D3D12_RAYTRACING_GEOMETRY_DESC secondGeometryDesc{};
    secondGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    secondGeometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    secondGeometryDesc.Triangles.Transform3x4 = 0;
    secondGeometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    secondGeometryDesc.Triangles.VertexCount = static_cast<uint32>(m_vertexBufferSecondGeometry->GetDesc().Width) / sizeof(Vertex);
    secondGeometryDesc.Triangles.VertexBuffer.StartAddress = m_vertexBufferSecondGeometry->GetGPUVirtualAddress();
    secondGeometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);


    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfoSecondGeometry{};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputsSecondGeometry{};
    {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bottomLevelInputsSecondGeometry.Flags = buildFlags;
        bottomLevelInputsSecondGeometry.pGeometryDescs = &secondGeometryDesc;
        bottomLevelInputsSecondGeometry.NumDescs = 1;
        bottomLevelInputsSecondGeometry.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

        m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputsSecondGeometry, &bottomLevelPrebuildInfoSecondGeometry);
        if (!ensure(bottomLevelPrebuildInfoSecondGeometry.ResultDataMaxSizeInBytes > 0))
            return false;
    }

    BufferUtil::AllocateUAVBuffer(&m_bottomLevelAccelerationStructureSecondGeometry, m_device.Get()
        , bottomLevelPrebuildInfoSecondGeometry.ResultDataMaxSizeInBytes, initialResourceState, TEXT("BottomLevelAccelerationStructure"));

    ComPtr<ID3D12Resource> scratchResourceSecondGeometry;
    BufferUtil::AllocateUAVBuffer(&scratchResourceSecondGeometry, m_device.Get(), bottomLevelPrebuildInfoSecondGeometry.ScratchDataSizeInBytes
        , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResourceGeometry2");

    // Bottom level acceleration structure desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDescSecondGeometry{};
    bottomLevelBuildDescSecondGeometry.Inputs = bottomLevelInputsSecondGeometry;
    bottomLevelBuildDescSecondGeometry.ScratchAccelerationStructureData = scratchResourceSecondGeometry->GetGPUVirtualAddress();
    bottomLevelBuildDescSecondGeometry.DestAccelerationStructureData = m_bottomLevelAccelerationStructureSecondGeometry->GetGPUVirtualAddress();

    m_dxrCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDescSecondGeometry, 0, nullptr);
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAccelerationStructureSecondGeometry.Get());
		m_commandList->ResourceBarrier(1, &barrier);
	}

    if (!ensure(BuildTopLevelAS(TLASBuffer, false, 0.0f, Vector::ZeroVector)))
        return false;

	if (JFAIL(m_commandList->Close()))
		return false;
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	WaitForGPU();

	// 13. ShaderTable
	const uint16 shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
	if (JFAIL(m_dxrStateObject.As(&stateObjectProperties)))
		return false;
	
	void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
	void* misssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
	void* triHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_triHitGroupName);
    void* planeHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_planeHitGroupName);

	// Raygen shader table
	{
		const uint16 numShaderRecords = 1;
		const uint16 shaderRecordSize = shaderIdentifierSize;
		ShaderTable rayGenShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("RayGenShaderTable"));
		rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
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

	// Triangle Hit group shader table
	{
		struct RootArguments
		{
			CubeConstantBuffer cb;
		};
		RootArguments rootArguments;
		rootArguments.cb = m_cubeCB;

		const uint16 numShaderRecords = 2;
		const uint16 shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);       // 큰 사이즈 기준으로 2개 만듬
		ShaderTable hitGroupShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("HitGroupShaderTable"));
		hitGroupShaderTable.push_back(ShaderRecord(triHitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));

        RootArguments planeRootArguments;
        planeRootArguments.cb = m_planeCB;
        hitGroupShaderTable.push_back(ShaderRecord(planeHitGroupShaderIdentifier, shaderIdentifierSize, &planeRootArguments, sizeof(planeRootArguments)));
		m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
	}

	//////////////////////////////////////////////////////////////////////////
	// 14. Raytracing Output Resouce

	// 출력 리소스를 생성. 차원과 포맷은 swap-chain과 매치 되어야 함
	auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(BackbufferFormat
		, SCR_WIDTH, SCR_HEIGHT, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (JFAIL(m_device->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE
		, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput))))
	{
		return false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
	auto descriptorHeapCpuBase = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
	if (m_raytracingOutputResourceUAVDescriptorHeapIndex >= m_cbvHeap->GetDesc().NumDescriptors)
	{
		m_raytracingOutputResourceUAVDescriptorHeapIndex = m_allocatedDescriptors++;
		uavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase
			, m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
	m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart()
		, m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);

	//////////////////////////////////////////////////////////////////////////
	// 15. CreateConstantBuffers
	// 상수 버퍼 메모리를 만들고 CPU와 GPU 주소를 매핑함
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	// 프레임별 상수버퍼를 만든다, 매프레임 업데이트 되기 때문
	size_t cbSize = FrameCount * sizeof(AlignedSceneConstantBuffer);
	const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

	if (JFAIL(m_device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE
		, &constantBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_perFrameConstants))))
	{
		return false;
	}

	// 상수 버퍼를 매핑하고 heap 포인터를 캐싱한다.
	// 앱 종료시 까지 매핑을 해제하지 않음. 살아있는 동안 버퍼의 매핑을 유지해도 괜찮음.
	CD3DX12_RANGE readRange(0, 0);		// CPU에서 읽지 않으려는 의도
	if (JFAIL(m_perFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData))))
		return false;

	//////////////////////////////////////////////////////////////////////////
	ShowWindow(m_hWnd, SW_SHOW);
    pRHIDirectX12 = this;

    InitializeImGui();

    return true;
}

bool jRHI_DirectX12::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return true;
}

void jRHI_DirectX12::Release()
{
	WaitForGPU();
    
    ReleaseImGui();

	//////////////////////////////////////////////////////////////////////////
	// 14. Raytracing Output Resouce
	m_raytracingOutput.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 13. ShaderTable
	m_rayGenShaderTable.Reset();
	m_missShaderTable.Reset();
	m_hitGroupShaderTable.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 12. AccelerationStructures
	m_bottomLevelAccelerationStructure.Reset();
	//m_topLevelAccelerationStructure.Reset();
    TLASBuffer.Result.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 11. Create vertex and index buffer
	m_vertexBuffer.Reset();
	m_indexBuffer.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 10. DXR PipeplineStateObject
	m_dxrStateObject.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 9. CreateRootSignature
	m_raytracingGlobalRootSignature.Reset();
	m_raytracingLocalRootSignature.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 8. Raytracing device and commandlist
	m_dxrDevice.Reset();
	m_dxrCommandList.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 7. Create sync object
	m_fence.Reset();
	CloseHandle(m_fenceEvent);

	//////////////////////////////////////////////////////////////////////////
	// 6. CommandAllocators, Commandlist, RTV for FrameCount
	for (uint32 i = 0; i < FrameCount; ++i)
	{
		m_renderTargets[i].Reset();
	}

    //////////////////////////////////////////////////////////////////////////
    // 5. Initialize Camera and lighting

	//////////////////////////////////////////////////////////////////////////
	// 4. Heap
	m_rtvHeap.Reset();
	m_cbvHeap.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 3. Swapchain
	m_swapChain.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 2. Command
	m_commandList.Reset();
	for (uint32 i = 0; i < FrameCount; ++i)
	{
		m_commandAllocator[i].Reset();
	}
	m_commandQueue.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 1. Device
	m_device.Reset();

}

void jRHI_DirectX12::UpdateCameraMatrices()
{
	auto frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	m_sceneCB[frameIndex].cameraPosition = m_eye;

	const float m_aspectRatio = SCR_WIDTH / (float)SCR_HEIGHT;

	const float fovAngleY = 45.0f;
	const XMMATRIX view = XMMatrixLookAtLH(m_eye, m_at, m_up);
	const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 125.0f);
	const XMMATRIX viewProj = view * proj;

    m_sceneCB[frameIndex].cameraDirection = XMVector3Normalize(m_at - m_eye);
	m_sceneCB[frameIndex].projectionToWorld = XMMatrixInverse(nullptr, viewProj);
    m_sceneCB[frameIndex].NumOfStartingRay = 20;        // 첫 Ray 생성시 NumOfStartingRay 개를 쏴서 보간하도록 함. 노이즈를 줄여줌
}

bool jRHI_DirectX12::BuildTopLevelAS(TopLevelAccelerationStructureBuffers& InBuffers, bool InIsUpdate, float InRotationY, Vector InTranslation)
{
    int32 w = 11, h = 11;
    int32 totalCount = (w * 2 * h * 2) + 3 + 1;     // small balls, big balls, plane
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = totalCount;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    if (!ensure(info.ResultDataMaxSizeInBytes > 0))
        return false;

    if (InIsUpdate)
    {
        // Update 요청이 온다면 TLAS 는 이미 DispatchRay()의 호출에서 사용되고있다.
        // 버퍼가 업데이트 되기 전에 UAV 베리어를 read 연산의 끝에 넣어줘야 한다.

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = InBuffers.Result.Get();
        m_dxrCommandList->ResourceBarrier(1, &uavBarrier);
    }
    else
    {
        // Update 요청이 아니면, 버퍼를 새로 만들어야 함, 그렇지 않으면 그대로 refit(이미 존재하는 TLAS를 업데이트) 될 것임.
        BufferUtil::AllocateUAVBuffer(&InBuffers.Scratch, m_device.Get(), info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, TEXT("TLAS Scratch Buffer"));
        BufferUtil::AllocateUAVBuffer(&InBuffers.Result, m_device.Get(), info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, TEXT("TLAS Result Buffer"));
        BufferUtil::AllocateUploadBuffer(&InBuffers.InstanceDesc, m_device.Get(), nullptr, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalCount, TEXT("TLAS Instance Desc"));
    }

    D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = nullptr;
    auto hr = InBuffers.InstanceDesc->Map(0, nullptr, (void**)&instanceDescs);
    if (JFAIL(hr))
        return false;

    ZeroMemory(instanceDescs, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalCount);

    int32 cnt = 0;

    srand(123);
    const float radius = 0.3f;
    for (int32 i = -w; i < w; ++i)
    {
        for (int32 j = -h; j < h; ++j, ++cnt)
        {
            float r = radius;
            auto s = XMMatrixScaling(r, r, r);
            auto t = XMMatrixTranslation(
                (float)(i * radius * 5.0f) + (radius * 4.0f * random_double())
                , -0.7f
                , (float)(j * radius * 5.0f) + (radius * 4.0f * random_double()));
            auto m = XMMatrixTranspose(XMMatrixMultiply(s, t));

            instanceDescs[cnt].InstanceID = cnt;
            instanceDescs[cnt].InstanceContributionToHitGroupIndex = 0;
            memcpy(instanceDescs[cnt].Transform, &m, sizeof(instanceDescs[cnt].Transform));
            instanceDescs[cnt].InstanceMask = 1;
            instanceDescs[cnt].AccelerationStructure = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
        }
    }

    for (int32 i = 0; i < 3; ++i)
    {
        auto s = XMMatrixScaling(1.0f, 1.0f, 1.0f);
        auto t = XMMatrixTranslation(0.0f + i * 2, 0.0f, 0.0f + i * 2);
        auto m = XMMatrixTranspose(XMMatrixMultiply(s, t));

        instanceDescs[cnt].InstanceID = cnt;
        instanceDescs[cnt].InstanceContributionToHitGroupIndex = 0;
        memcpy(instanceDescs[cnt].Transform, &m, sizeof(instanceDescs[cnt].Transform));
        instanceDescs[cnt].InstanceMask = 1;
        instanceDescs[cnt].AccelerationStructure = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();

        ++cnt;
    }

    auto mIdentity = XMMatrixTranspose(XMMatrixIdentity());

    instanceDescs[cnt].InstanceID = cnt;
    instanceDescs[cnt].InstanceContributionToHitGroupIndex = 1;
    memcpy(instanceDescs[cnt].Transform, &mIdentity, sizeof(instanceDescs[cnt].Transform));
    instanceDescs[cnt].InstanceMask = 1;
    instanceDescs[cnt].AccelerationStructure = m_bottomLevelAccelerationStructureSecondGeometry->GetGPUVirtualAddress();
    instanceDescs[cnt].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;

    InBuffers.InstanceDesc->Unmap(0, nullptr);

    // TLAS 생성
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.Inputs.InstanceDescs = InBuffers.InstanceDesc->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = InBuffers.Result->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = InBuffers.Scratch->GetGPUVirtualAddress();

    // 만약 업데이트 중이라면 source buffer에 업데이트 플래그를 설정해준다.
    if (InIsUpdate)
    {
        asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        asDesc.SourceAccelerationStructureData = InBuffers.Result->GetGPUVirtualAddress();
    }

    m_dxrCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // 레이트레이싱 연산에서 Acceleration structure를 사용하기 전에 UAV 베리어를 추가해야 함.
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = InBuffers.Result.Get();
    m_dxrCommandList->ResourceBarrier(1, &uavBarrier);

    return true;
}

void jRHI_DirectX12::CalculateFrameStats()
{
    static int frameCnt = 0;
    static double elapsedTime = 0.0f;
    double totalTime = GetTickCount() / 1000.0f;
    frameCnt++;

    // Compute averages over one second period.
    if ((totalTime - elapsedTime) >= 1.0f)
    {
        float diff = static_cast<float>(totalTime - elapsedTime);
        float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

        frameCnt = 0;
        elapsedTime = totalTime;

        float raysPerPixels = (float)(m_sceneCB[m_frameIndex].NumOfStartingRay * g_MaxRecursionDepth);
        float MRaysPerSecond = (SCR_WIDTH * SCR_HEIGHT * fps * raysPerPixels) / static_cast<float>(1e6);

        std::wstringstream windowText;

        windowText << std::setprecision(2) << std::fixed
            << L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
            << L"    GPU[" << AdapterID << L"]: " << AdapterName;
        SetWindowText(m_hWnd, windowText.str().c_str());
    }
}

void jRHI_DirectX12::Update()
{
    CalculateFrameStats();

	int32 prevFrameIndex = m_frameIndex - 1;
	if (prevFrameIndex < 0)
		prevFrameIndex = FrameCount - 1;

    static bool enableCameraRotation = false;

    static float elapsedTime = 0.0f;
    elapsedTime = 0.05f;
    
    // Y 축 주변으로 카메라를 회전
    if (enableCameraRotation)
	{
        float secondsToRotateAround = 24.0f;
        static float angleToRotateBy = 360.0f * (elapsedTime / secondsToRotateAround);

        XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(angleToRotateBy));
        m_eye = XMVector3Transform(m_eye, rotate);
        m_up = XMVector3Transform(m_up, rotate);
        m_at = XMVector3Transform(m_at, rotate);
        UpdateCameraMatrices();
	}

	// Y 축 주변으로 두번째 라이트를 회전
	{
        constexpr float secondsToRotateAround = 8.0f;
		float angleToRotateBy = -360.0f * (elapsedTime / secondsToRotateAround);

        static float accAngle = 0.0f;
        accAngle += angleToRotateBy;

		XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(accAngle));
        constexpr XMVECTOR rotationRadius = { 0.0f, 0.0f, 8.0f };
		m_sceneCB[m_frameIndex].lightPosition = XMVector3Transform(rotationRadius, rotate);
	}

    {
        auto frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        m_sceneCB[frameIndex].focalDistance = m_focalDistance;
        m_sceneCB[frameIndex].lensRadius = m_lensRadius;
    }
}

void jRHI_DirectX12::Render()
{
	// Prepare
	if (JFAIL(m_commandAllocator[m_frameIndex]->Reset()))
		return;
	if (JFAIL(m_commandList->Reset(m_commandAllocator[m_frameIndex].Get(), nullptr)))
		return;

    static float elapsedTime = 0.0f;
    elapsedTime = 0.01f;

    float secondsToRotateAround = 24.0f;
    float angleToRotateBy = 360.0f * (elapsedTime / secondsToRotateAround);
    static float accumulatedRotation = 0.0f;
    accumulatedRotation += angleToRotateBy;

    static float TranslationOffsetX = 0.0f;
    static bool TranslateDirRight = 1;
    if (TranslateDirRight)
    {
        if (TranslationOffsetX > 2.0f)
            TranslateDirRight = false;
        TranslationOffsetX += 0.01f;
    }
    else
    {
        if (TranslationOffsetX < -2.0f)
            TranslateDirRight = true;
        TranslationOffsetX -= 0.01f;
    }

    //if (!JASSERT(BuildTopLevelAS(TLASBuffer, true, accumulatedRotation, { TranslationOffsetX, 0.0f, 0.0f })))
    //    return;

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &barrier);
	}
	// DoRaytracing
	m_commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

	memcpy(&m_mappedConstantData[m_frameIndex].constants, &m_sceneCB[m_frameIndex], sizeof(m_sceneCB[m_frameIndex]));
	auto cbGpuAddress = m_perFrameConstants->GetGPUVirtualAddress() + m_frameIndex * sizeof(m_mappedConstantData[0]);
	m_commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);

	D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
	m_commandList->SetDescriptorHeaps(1, m_cbvHeap.GetAddressOf());
	m_commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, m_indexBufferGpuDescriptor);
    m_commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::PlaneVertexBufferSlot
        , m_vertexBufferGpuDescriptorSecondGeometry);
    m_commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot
        , m_raytracingOutputResourceUAVGpuDescriptor);
	m_commandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot
		, TLASBuffer.Result->GetGPUVirtualAddress());

	// 각 Shader table은 단 한개의 shader record를 가지기 때문에 stride가 그 사이즈와 동일함
	dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
	dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
	dispatchDesc.HitGroupTable.StrideInBytes = dispatchDesc.HitGroupTable.SizeInBytes / 2;  // 2개의 HitGroupTable이 등록되었으므로 개수 만큼 나눠줌

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

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get()
			, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &barrier);
	}

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex* m_rtvDescriptorSize);
    RenderUI(m_commandList.Get(), m_renderTargets[m_frameIndex].Get(), rtvHandle, m_imgui_SrvDescHeap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    //////////////////////////////////////////////////////////////////////////

	// Present
	if (JFAIL(m_commandList->Close()))
		return;
	ID3D12CommandList* commandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	HRESULT hr = S_OK;
	if (m_options & c_AllowTearing)
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

#ifdef _DEBUG
        char buff[64] = {};
        sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n"
            , (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_device->GetDeviceRemovedReason() : hr);
        OutputDebugStringA(buff);
#endif

        OnHandleDeviceLost();
        OnHandleDeviceRestored();
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
	m_rayGenShaderTable.Reset();
	m_missShaderTable.Reset();
	m_hitGroupShaderTable.Reset();
	m_raytracingOutput.Reset();

	m_raytracingGlobalRootSignature.Reset();
	m_raytracingLocalRootSignature.Reset();

	m_dxrDevice.Reset();
	m_dxrCommandList.Reset();
	m_dxrStateObject.Reset();

	m_cbvHeap.Reset();
	m_allocatedDescriptors = 0;
	m_raytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;
	m_indexBuffer.Reset();
	m_vertexBuffer.Reset();

	m_bottomLevelAccelerationStructure.Reset();
	// m_topLevelAccelerationStructure.Reset();
    TLASBuffer.Result.Reset();
}

void jRHI_DirectX12::OnDeviceRestored()
{
	// 7. Raytracing device and commandlist ~ 13. Raytracing Output Resouce
}

bool jRHI_DirectX12::OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized)
{
    JASSERT(InWidth > 0);
    JASSERT(InHeight > 0);

    {
        char szTemp[126];
        sprintf_s(szTemp, sizeof(szTemp), "Called OnHandleResized %d %d\n", InWidth, InHeight);
        OutputDebugStringA(szTemp);
    }

    WaitForGPU();

    for (int32 i = 0; i < FrameCount; ++i)
    {
        m_renderTargets[i].Reset();
        m_fenceValue[i] = m_fenceValue[m_frameIndex];
    }

    BackbufferFormat;

    if (ensure(m_swapChain))
    {
        HRESULT hr = m_swapChain->ResizeBuffers(FrameCount, InWidth, InHeight, BackbufferFormat
            , (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
#ifdef _DEBUG
            char buff[64] = {};
            sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n"
                , (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_device->GetDeviceRemovedReason() : hr);
            OutputDebugStringA(buff);
#endif
            if (!OnHandleDeviceLost() || !OnHandleDeviceRestored())
            {
                JASSERT(0);
            }
            return false;
        }
        else
        {
            JASSERT(0);
        }
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (int32 i = 0; i < FrameCount; ++i)
    {
        if (JFAIL(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
            return false;

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        //////////////////////////////////////////////////////////////////////////
        m_sceneCB[i].cameraPosition = m_eye;
        const float m_aspectRatio = SCR_WIDTH / (float)SCR_HEIGHT;

        const float fovAngleY = 45.0f;
        const XMMATRIX view = XMMatrixLookAtLH(m_eye, m_at, m_up);
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 125.0f);
        const XMMATRIX viewProj = view * proj;
        m_sceneCB[i].projectionToWorld = XMMatrixInverse(nullptr, viewProj);
        m_sceneCB[i].cameraDirection = XMVector3Normalize(m_at - m_eye);

        m_sceneCB[i].focalDistance = m_focalDistance;
        m_sceneCB[i].lensRadius = m_lensRadius;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    //////////////////////////////////////////////////////////////////////////
    // ReleaseWindowSizeDependentResources
    m_rayGenShaderTable.Reset();
    m_missShaderTable.Reset();
    m_hitGroupShaderTable.Reset();
    m_raytracingOutput.Reset();
    
    //////////////////////////////////////////////////////////////////////////
    // CreateWindowSizeDependentResources
    auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(BackbufferFormat
        , InWidth, InHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (JFAIL(m_device->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE
        , &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput))))
    {
        return false;
    }

    // 이미 만들어 둔 인덱스가 있어서 괜찮다.
    //D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    //auto descriptorHeapCpuBase = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
    //if (m_raytracingOutputResourceUAVDescriptorHeapIndex >= m_cbvHeap->GetDesc().NumDescriptors)
    //{
    //    m_raytracingOutputResourceUAVDescriptorHeapIndex = m_allocatedDescriptors++;
    //    uavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase
    //        , m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);
    //}

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);

    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart()
        , m_raytracingOutputResourceUAVDescriptorHeapIndex, m_cbvDescriptorSize);

    //////////////////////////////////////////////////////////////////////////
    // RecreateShaderTable
    const uint16 shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    if (JFAIL(m_dxrStateObject.As(&stateObjectProperties)))
        return false;

    void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
    void* misssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
    void* triHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_triHitGroupName);
    void* planeHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_planeHitGroupName);

    // Raygen shader table
    {
        const uint16 numShaderRecords = 1;
        const uint16 shaderRecordSize = shaderIdentifierSize;
        ShaderTable rayGenShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("RayGenShaderTable"));
        rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
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

    // Triangle Hit group shader table
    {
        struct RootArguments
        {
            CubeConstantBuffer cb;
        };
        RootArguments rootArguments;
        rootArguments.cb = m_cubeCB;

        const uint16 numShaderRecords = 2;
        const uint16 shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);       // 큰 사이즈 기준으로 2개 만듬
        ShaderTable hitGroupShaderTable(m_device.Get(), numShaderRecords, shaderRecordSize, TEXT("HitGroupShaderTable"));
        hitGroupShaderTable.push_back(ShaderRecord(triHitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));

        RootArguments planeRootArguments;
        planeRootArguments.cb = m_planeCB;
        hitGroupShaderTable.push_back(ShaderRecord(planeHitGroupShaderIdentifier, shaderIdentifierSize, &planeRootArguments, sizeof(planeRootArguments)));
        m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
    }

    return true;
}

bool jRHI_DirectX12::OnHandleDeviceLost()
{
    //////////////////////////////////////////////////////////////////////////
    // ReleaseWindowSizeDependentResources
    m_raytracingOutput.Reset();

    //////////////////////////////////////////////////////////////////////////
    // ReleaseDeviceDependentResources
    m_raytracingGlobalRootSignature.Reset();
    m_raytracingLocalRootSignature.Reset();
    m_raytracingEmptyLocalRootSignature.Reset();

    m_dxrStateObject.Reset();
    m_dxrCommandList.Reset();
    m_dxrDevice.Reset();

    m_allocatedDescriptors = 0;
    m_raytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;
    m_cbvHeap.Reset();
    m_indexBuffer.Reset();
    m_vertexBuffer.Reset();
    m_vertexBufferSecondGeometry.Reset();
    m_perFrameConstants.Reset();
    m_rayGenShaderTable.Reset();
    m_missShaderTable.Reset();
    m_hitGroupShaderTable.Reset();

    m_bottomLevelAccelerationStructure.Reset();
    m_bottomLevelAccelerationStructureSecondGeometry.Reset();

    TLASBuffer.Reset();

    return true;
}

bool jRHI_DirectX12::OnHandleDeviceRestored()
{
    Initialize();
    return true;
}

void jRHI_DirectX12::InitializeImGui()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (JFAIL(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imgui_SrvDescHeap))))
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(m_hWnd);

    ImGui_ImplDX12_Init(m_device.Get(), FrameCount,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_imgui_SrvDescHeap.Get(),
        m_imgui_SrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        m_imgui_SrvDescHeap->GetGPUDescriptorHandleForHeapStart());
}

void jRHI_DirectX12::ReleaseImGui()
{
    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    m_imgui_SrvDescHeap.Reset();
}

void jRHI_DirectX12::RenderUI(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pRenderTarget, CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle
    , ID3D12DescriptorHeap* pDescriptorHeap, D3D12_RESOURCE_STATES beforeResourceState, D3D12_RESOURCE_STATES afterResourceState)
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Vector4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

    {
        ImGui::Begin("Control Pannel");

        ImGui::SliderFloat("Focal distance", &m_focalDistance, 3.0f, 40.0f);
        ImGui::SliderFloat("Lens radius", &m_lensRadius, 0.0f, 0.2f);

        ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::SetWindowPos({10, 10}, ImGuiCond_Once);
        ImGui::SetWindowSize({350, 100}, ImGuiCond_Once);

        ImGui::End();
    }

    // Rendering UI
    ImGui::Render();

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			pRenderTarget, beforeResourceState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		pCommandList->ResourceBarrier(1, &barrier);
	}
    const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };

    //m_commandList->ClearRenderTargetView(rtvHandle, clear_color_with_alpha, 0, NULL);
    pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
    pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, afterResourceState);
		pCommandList->ResourceBarrier(1, &barrier);
	}
}
