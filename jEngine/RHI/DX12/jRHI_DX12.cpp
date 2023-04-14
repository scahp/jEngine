#include "pch.h"
#include "jRHI_DX12.h"
#include "jShaderCompiler_DX12.h"
#include <iomanip>
#include "jBufferUtil_DX12.h"
#include "jRHIType_DX12.h"
#include "jTexture_DX12.h"
#include "jSwapchain_DX12.h"
#include "jBuffer_DX12.h"
#include "../jSwapchain.h"
#include "jFenceManager_DX12.h"
#include "Scene/jCamera.h"
#include "FileLoader/jImageFileLoader.h"
#include "jRingBuffer_DX12.h"

#define USE_INLINE_DESCRIPTOR 1												// InlineDescriptor 를 쓸것인지? DescriptorTable 를 쓸것인지 여부
#define USE_ONE_FRAME_BUFFER_AND_DESCRIPTOR (USE_INLINE_DESCRIPTOR && 1)	// 현재 프레임에만 사용하고 버리는 임시 Descriptor 와 Buffer 를 사용할 것인지 여부

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct jSimpleConstantBuffer
{
    Matrix M;
};

jRHI_DX12* g_rhi_dx12 = nullptr;
const wchar_t* jRHI_DX12::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* jRHI_DX12::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* jRHI_DX12::c_missShaderName = L"MyMissShader";
const wchar_t* jRHI_DX12::c_triHitGroupName = L"TriHitGroup";
const wchar_t* jRHI_DX12::c_planeHitGroupName = L"PlaneHitGroup";
const wchar_t* jRHI_DX12::c_planeclosestHitShaderName = L"MyPlaneClosestHitShader";

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

ShaderTable::ShaderTable(ID3D12Device* InDevice, uint32 InNumOfShaderRecords, uint32 InShaderRecordSize, const wchar_t* InResourceName /*= nullptr*/)
{
	m_shaderRecords.reserve(InNumOfShaderRecords);

	m_shaderRecordSize = Align(InShaderRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	const uint32 bufferSize = InNumOfShaderRecords * m_shaderRecordSize;
	Buffer = jBufferUtil_DX12::CreateBuffer(bufferSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, true, false, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, 0, InResourceName);

#if _DEBUG
	m_name = InResourceName;
#endif

	m_mappedShaderRecords = (uint8*)Buffer->Map();
}

ComPtr<ID3D12Resource> ShaderTable::GetResource() const
{
    return Buffer->Buffer;
}

void ShaderTable::push_back(const ShaderRecord& InShaderRecord)
{
    //if (ensure(m_shaderRecords.size() >= m_shaderRecords.capacity()))
    //    return;

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

void ShaderTable::DebugPrint(robin_hood::unordered_map<void*, std::wstring> shaderIdToStringMap)
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
                if (g_rhi_dx12)
					g_rhi_dx12->OnHandleResized(SCR_WIDTH, SCR_HEIGHT, IsSizeMinimize);
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
            if (g_rhi_dx12)
				g_rhi_dx12->OnHandleResized(SCR_WIDTH, SCR_HEIGHT, IsSizeMinimize);
        }
        sIsDraging = false;
    }
    return 0;
    case WM_PAINT:
        if (g_rhi_dx12)
        {
			g_rhi_dx12->Update();
			g_rhi_dx12->Render();
        }
    return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

////////////////////////////////////////////////////////////////////////////
//// jRHI_DX12::TopLevelAccelerationStructureBuffers
////////////////////////////////////////////////////////////////////////////
//void jRHI_DX12::TopLevelAccelerationStructureBuffers::Release()
//{
//    delete Scratch;
//    Scratch = nullptr;
//
//    delete Result;
//    Result = nullptr;
//
//    delete InstanceDesc;
//    InstanceDesc = nullptr;
//}

//////////////////////////////////////////////////////////////////////////
// jRHI_DX12
//////////////////////////////////////////////////////////////////////////
jRHI_DX12::jRHI_DX12()
{
	g_rhi_dx12 = this;
}

jRHI_DX12::~jRHI_DX12()
{

}

HWND jRHI_DX12::CreateMainWindow() const
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

void jRHI_DX12::WaitForGPU()
{
	check(SwapChain);

	auto Queue = GraphicsCommandQueue->GetCommandQueue();
    check(Queue);

	if (Queue && SwapChain)
	{
		jSwapchainImage_DX12* CurrentSwapchainImage = (jSwapchainImage_DX12*)SwapChain->GetSwapchainImage(CurrentFrameIndex);
		if (JFAIL(Queue->Signal((ID3D12Fence*)SwapChain->Fence->GetHandle(), CurrentSwapchainImage->FenceValue)))
			return;

		if (SwapChain->Fence->SetFenceValue(CurrentSwapchainImage->FenceValue))
			SwapChain->Fence->WaitForFence();

		++CurrentSwapchainImage->FenceValue;
	}
}

bool jRHI_DX12::InitRHI()
{
	g_rhi = this;

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

    HRESULT hr = factory.As(&Factory);
    if (SUCCEEDED(hr))
    {
        BOOL allowTearing = false;
        hr = Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));

        if (FAILED(hr) || !allowTearing)
        {
            OutputDebugStringA("WARNING: Variable refresh rate displays are not supported.\n");
            if (Options & c_RequireTearingSupport)
            {
                JASSERT(!L"Error: Sample must be run on an OS with tearing support.\n");
            }
            Options &= ~c_AllowTearing;
        }
        else
        {
            Options |= c_AllowTearing;
        }
    }

	bool UseWarpDevice = false;		// Software rasterizer 사용 여부
	if (UseWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if (JFAIL(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
			return false;

		if (JFAIL((D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)))))
			return false;
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		const int32 ResultAdapterID = GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        if (JFAIL(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
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
	GraphicsCommandQueue = new jCommandQueue_DX12();
	GraphicsCommandQueue->Initialize(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	CopyCommandQueue = new jCommandQueue_DX12();
	CopyCommandQueue->Initialize(Device, D3D12_COMMAND_LIST_TYPE_COPY);

    // 4. Heap	
    RTVDescriptorHeap.Initialize(EDescriptorHeapTypeDX12::RTV, false);
    DSVDescriptorHeap.Initialize(EDescriptorHeapTypeDX12::DSV, false);
    SRVDescriptorHeap.Initialize(EDescriptorHeapTypeDX12::CBV_SRV_UAV, true);
	SamplerDescriptorHeap.Initialize(EDescriptorHeapTypeDX12::SAMPLER, true);		// SamplerState test
    //UAVDescriptorHeap.Initialize(EDescriptorHeapTypeDX12::CBV_SRV_UAV, false);

	// 3. Swapchain
	SwapChain = new jSwapchain_DX12();
	SwapChain->Create();

	OneFrameUniformRingBuffers.resize(SwapChain->GetNumOfSwapchain());
	for(jRingBuffer_DX12*& iter : OneFrameUniformRingBuffers)
	{
		iter = new jRingBuffer_DX12();
		iter->Create(16 * 1024 * 1024);
	}

    //////////////////////////////////////////////////////////////////////////
    // 5. Initialize Camera and lighting
    {
        auto frameIndex = SwapChain->GetCurrentBackBufferIndex();

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


	//D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
	// 2 - vertex and index buffer SRV for Cube
    // 1 - vertex buffer SRV for Plane
	// 1 - ratracing output texture UAV
    // 1 - imgui

	// 6. CommandAllocators, Commandlist, RTV for FrameCount	

	// 7. Create sync object
	WaitForGPU();

	// 8. Raytracing device and commandlist
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData{};
	if (JFAIL(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))))
		return false;

	//if (!ensure(featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED))
	//	return false;

	//// 9. CreateRootSignatures
	//{
	//	// global root signature는 DispatchRays 함수 호출로 만들어지는 레이트레이싱 쉐이더의 전체에 공유됨.

	//	CD3DX12_DESCRIPTOR_RANGE ranges[3];		// 가장 빈번히 사용되는 것을 앞에 둘 수록 최적화에 좋음
	//	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);		// 1 output texture
	//	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);		// 2 static index and vertex buffer for cube
 //       ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);		// 1 vertex buffer for plane

	//	CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
	//	rootParameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]);
 //       rootParameters[GlobalRootSignatureParams::PlaneVertexBufferSlot].InitAsDescriptorTable(1, &ranges[2]);
	//	rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
	//	rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
	//	rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);

	//	CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(_countof(rootParameters), rootParameters);
	//	ComPtr<ID3DBlob> blob;
	//	ComPtr<ID3DBlob> error;
	//	if (JFAIL_E(D3D12SerializeRootSignature(&globalRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error))
	//	{
	//		if (error)
	//		{
	//			OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
	//			error->Release();
	//		}
	//		return false;
	//	}

	//	if (JFAIL(Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
	//		, IID_PPV_ARGS(&m_raytracingGlobalRootSignature))))
	//	{
	//		return false;
	//	}
	//}

//	{
//#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)
//
//		CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
//		rootParameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cubeCB), 1);
//
//		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(_countof(rootParameters), rootParameters);
//		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
//		ComPtr<ID3DBlob> blob;
//		ComPtr<ID3DBlob> error;
//		if (JFAIL_E(D3D12SerializeRootSignature(&localRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error))
//		{
//			if (error)
//			{
//				OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
//				error->Release();
//			}
//			return false;
//		}
//
//		if (JFAIL(Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
//			, IID_PPV_ARGS(&m_raytracingLocalRootSignature))))
//		{
//			return false;
//		}
//	}

//	// 10. DXR PipeplineStateObject
//	// ----------------------------------------------------
//	// 1 - DXIL Library
//	// 2 - Triangle and plane hit group
//	// 1 - Shader config
//	// 4 - Local root signature and association
//	// 1 - Global root signature
//	// 1 - Pipeline config
//	// ----------------------------------------------------
//	std::array<D3D12_STATE_SUBOBJECT, 10> subobjects;
//	uint32 index = 0;
//
//	// 1). DXIL 라이브러리 생성
//	D3D12_DXIL_LIBRARY_DESC dxilDesc{};
//	std::vector<D3D12_EXPORT_DESC> exportDesc;
//	ComPtr<IDxcBlob> ShaderBlob;
//	std::vector<std::wstring> exportName;
//	{
//		D3D12_STATE_SUBOBJECT subobject{};
//		ShaderBlob = jShaderCompiler_DX12::Get().Compile(TEXT("Resource/Shaders/hlsl/RaytracingCubeAndPlane.hlsl"), TEXT("lib_6_3"));
//		if (ShaderBlob)
//		{
//			const wchar_t* entryPoint[] = { jRHI_DX12::c_raygenShaderName, jRHI_DX12::c_closestHitShaderName, jRHI_DX12::c_missShaderName, jRHI_DX12::c_planeclosestHitShaderName };
//			subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
//			subobject.pDesc = &dxilDesc;
//
//			exportDesc.resize(_countof(entryPoint));
//			exportName.resize(_countof(entryPoint));
//			
//			dxilDesc.DXILLibrary.pShaderBytecode = ShaderBlob->GetBufferPointer();
//			dxilDesc.DXILLibrary.BytecodeLength = ShaderBlob->GetBufferSize();
//			dxilDesc.NumExports = _countof(entryPoint);
//			dxilDesc.pExports = exportDesc.data();
//
//			for (uint32 i = 0; i < _countof(entryPoint); ++i)
//			{
//				exportName[i] = entryPoint[i];
//				exportDesc[i].Name = exportName[i].c_str();
//				exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
//				exportDesc[i].ExportToRename = nullptr;
//			}
//		}
//		subobjects[index++] = subobject;
//	}
//
//	// 2). Triangle and plane hit group
//    // Triangle hit group
//	D3D12_HIT_GROUP_DESC hitgroupDesc{};
//	{
//		hitgroupDesc.AnyHitShaderImport = nullptr;
//		hitgroupDesc.ClosestHitShaderImport = jRHI_DX12::c_closestHitShaderName;
//		hitgroupDesc.HitGroupExport = jRHI_DX12::c_triHitGroupName;
//		hitgroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
//
//		D3D12_STATE_SUBOBJECT subobject{};
//		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
//		subobject.pDesc = &hitgroupDesc;
//		subobjects[index++] = subobject;
//	}
//
//    // Plane hit group
//    D3D12_HIT_GROUP_DESC planeHitGroupDesc{};
//    {
//        planeHitGroupDesc.AnyHitShaderImport = nullptr;
//        planeHitGroupDesc.ClosestHitShaderImport = jRHI_DX12::c_planeclosestHitShaderName;
//        planeHitGroupDesc.HitGroupExport = jRHI_DX12::c_planeHitGroupName;
//        planeHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
//
//        D3D12_STATE_SUBOBJECT subobject{};
//        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
//        subobject.pDesc = &planeHitGroupDesc;
//        subobjects[index++] = subobject;
//    }
//
//	// 3). Shader Config
//	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
//	{
//		shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);	// float2 barycentrics
//		shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float) + sizeof(int32);		// float4 color + float maxDepth
//
//		D3D12_STATE_SUBOBJECT subobject{};
//		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
//		subobject.pDesc = &shaderConfig;
//		subobjects[index++] = subobject;
//	}
//
//	// 4). Local root signature and association
//
//    // triangle hit root signature and association
//    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association{};
//	{
//		D3D12_STATE_SUBOBJECT subobject{};
//
//		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
//		subobject.pDesc = m_raytracingLocalRootSignature.GetAddressOf();
//		subobjects[index] = subobject;
//
//		association.NumExports = 1;
//		association.pExports = &jRHI_DX12::c_triHitGroupName;
//		association.pSubobjectToAssociate = &subobjects[index++];
//
//		D3D12_STATE_SUBOBJECT subobject2{};
//		subobject2.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
//		subobject2.pDesc = &association;
//		subobjects[index++] = subobject2;
//	}
//
//    // empty root signature and associate it with the plane hit group and miss shader
//    CD3DX12_ROOT_PARAMETER rootParametersSecondGeometry[LocalRootSignatureParams::Count];
//    rootParametersSecondGeometry[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cubeCB), 1);
//
//    CD3DX12_ROOT_SIGNATURE_DESC emptyDesc(_countof(rootParametersSecondGeometry), rootParametersSecondGeometry);
//    emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
//    
//    const WCHAR* emptyRootExport[] = { c_planeclosestHitShaderName, c_missShaderName };
//    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION emptyAssociation{};
//    {
//#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)
//
//        ComPtr<ID3DBlob> blob;
//        ComPtr<ID3DBlob> error;
//        if (JFAIL_E(D3D12SerializeRootSignature(&emptyDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error))
//        {
//            if (error)
//            {
//                OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
//                error->Release();
//            }
//            return false;
//        }
//
//        if (JFAIL(Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize()
//            , IID_PPV_ARGS(&m_raytracingEmptyLocalRootSignature))))
//        {
//            return false;
//        }
//
//        D3D12_STATE_SUBOBJECT subobject;
//        subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
//        subobject.pDesc = m_raytracingEmptyLocalRootSignature.GetAddressOf();
//        subobjects[index] = subobject;
//
//        association.NumExports = _countof(emptyRootExport);
//        association.pExports = emptyRootExport;
//        association.pSubobjectToAssociate = &subobjects[index++];
//
//        D3D12_STATE_SUBOBJECT subobject2{};
//        subobject2.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
//        subobject2.pDesc = &association;
//        subobjects[index++] = subobject2;
//    }
//
//	// 5). Global root signature
//	{
//		D3D12_STATE_SUBOBJECT subobject{};
//		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
//		subobject.pDesc = m_raytracingGlobalRootSignature.GetAddressOf();
//		subobjects[index++] = subobject;
//	}
//
//	// 6). Pipeline Config
//	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
//	{
//		// pipelineConfig.MaxTraceRecursionDepth = D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
//        pipelineConfig.MaxTraceRecursionDepth = g_MaxRecursionDepth;
//
//		D3D12_STATE_SUBOBJECT subobject{};
//		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
//		subobject.pDesc = &pipelineConfig;
//
//		subobjects[index++] = subobject;
//	}
//
//	// Create pipeline state
//	D3D12_STATE_OBJECT_DESC stateObjectDesc;
//	stateObjectDesc.NumSubobjects = index;
//	stateObjectDesc.pSubobjects = subobjects.data();
//	stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
//
//#if _DEBUG
//	PrintStateObjectDesc(&stateObjectDesc);
//#endif
//
//	if (JFAIL(Device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_dxrStateObject))))
//		return false;

	//////////////////////////////////////////////////////////////////////////
	// 11. Create vertex and index buffer
	// auto GraphicsCommandList = GraphicsCommandQueue->GetAvailableCommandList();

    const int32 slice = 100;
    const int32 verticesCount = ((slice + 1) * (slice / 2) + 2);
    const int32 verticesElementCount = ((slice + 1) * (slice / 2) + 2) / 3;

    uint32 indices[((slice) / 2 - 2) * slice * 6 + (slice * 2 * 3)];

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

 //   float AspectRatio = SCR_WIDTH / (float)SCR_HEIGHT;
 //   Vertex vertices[] =
 //   {
 //       { { 0.0f, 0.25f * AspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f } },
 //       { { 0.25f, -0.25f * AspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f } },
 //       { { -0.25f, -0.25f * AspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f } }
 //   };

	//uint32 indices[] = { 0, 1, 2 };

	VertexBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(vertices), 0, true, false, D3D12_RESOURCE_STATE_COMMON, vertices, sizeof(vertices), TEXT("SphereVB"));
	IndexBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(indices), 0, true, false, D3D12_RESOURCE_STATE_COMMON, indices, sizeof(indices), TEXT("SphereIB"));

	// 버택스 버퍼와 인덱스 버퍼는 디스크립터 테이블로 쉐이더로 전달됨
	// 디스크립터 힙에서 버택스 버퍼 디스크립터는 인덱스 버퍼 디스크립터에 바로다음에 있어야 함

	// IndexBuffer SRV
	jBufferUtil_DX12::CreateShaderResourceView(IndexBuffer, 0, sizeof(indices) / 4, ETextureFormat::R32UI);

	// VertexBuffer SRV
	jBufferUtil_DX12::CreateShaderResourceView(VertexBuffer, sizeof(vertices[0]), _countof(vertices));

	jSimpleConstantBuffer ConstantBuffer;
	ConstantBuffer.M.SetIdentity();
	Matrix Projection = jCameraUtil::CreatePerspectiveMatrix((float)SCR_WIDTH, (float)SCR_HEIGHT, DegreeToRadian(90.0f), 0.01f, 1000.0f);
	Matrix Camera = jCameraUtil::CreateViewMatrix(Vector::FowardVector * 2.0f, Vector::ZeroVector, Vector::UpVector);
	ConstantBuffer.M = Projection * Camera * ConstantBuffer.M;
    ConstantBuffer.M.SetTranspose();

	SimpleConstantBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(ConstantBuffer)
		, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, true, false, D3D12_RESOURCE_STATE_COMMON, &ConstantBuffer, sizeof(ConstantBuffer));
	jBufferUtil_DX12::CreateConstantBufferView(SimpleConstantBuffer);

    // StructuredBuffer test
    Vector4 StructuredBufferColor[3];
	StructuredBufferColor[0] = { 1.0f, 0.0f, 0.0f, 1.0f };
	StructuredBufferColor[1] = { 0.0f, 1.0f, 0.0f, 1.0f };
	StructuredBufferColor[2] = { 0.0f, 0.0f, 1.0f, 1.0f };
    SimpleStructuredBuffer = jBufferUtil_DX12::CreateBuffer(sizeof(StructuredBufferColor)
        , sizeof(Vector4), true, false, D3D12_RESOURCE_STATE_COMMON, &StructuredBufferColor, sizeof(StructuredBufferColor));
	jBufferUtil_DX12::CreateShaderResourceView(SimpleStructuredBuffer, sizeof(StructuredBufferColor[0]), _countof(StructuredBufferColor));
	//////////////////////////////////////////////////////////////////////////

	// Texture test
	SimpleTexture = (jTexture_DX12*)jImageFileLoader::GetInstance().LoadTextureFromFile(jName("Image/eye.png"), true).lock().get();

	// Cube texture test
	{
        const uint64 CubeMapRes = 128;
        const uint64 NumTexels = CubeMapRes * CubeMapRes * 6;

		std::vector<Vector4> texels;
		texels.resize(CubeMapRes* CubeMapRes * 6);

		Vector4 faceColor[6] = {
			Vector4(1.0f, 0.0f, 0.0f, 1.0f),
			Vector4(0.0f, 1.0f, 0.0f, 1.0f),
			Vector4(1.0f, 1.0f, 0.0f, 1.0f),
			Vector4(0.0f, 0.0f, 1.0f, 1.0f),
			Vector4(1.0f, 0.0f, 1.0f, 1.0f),
			Vector4(1.0f, 1.0f, 1.0f, 1.0f)
		};

        for (uint64 s = 0; s < 6; ++s)
        {
            for (uint64 y = 0; y < CubeMapRes; ++y)
            {
                for (uint64 x = 0; x < CubeMapRes; ++x)
                {
					const uint64 idx = (s * CubeMapRes * CubeMapRes) + (y * CubeMapRes) + x;
					texels[idx] = faceColor[s];
                }
            }
        }

		const ETextureFormat simpleTextureFormat = ETextureFormat::RGBA32F;
		SimpleTextureCube = jBufferUtil_DX12::CreateImage(CubeMapRes, CubeMapRes, 6, 1, 1, ETextureType::TEXTURE_CUBE, simpleTextureFormat, false, false);

        // Copy image data from buffer
        const auto Desc = SimpleTextureCube->Image->GetDesc();
        uint64 RequiredSize = 0;
        Device->GetCopyableFootprints(&Desc, 0, 6, 0, nullptr, nullptr, nullptr, &RequiredSize);

        const uint64 ImageSize = CubeMapRes * CubeMapRes * GetDX12TexturePixelSize(simpleTextureFormat) * 6;

        // todo : recycle temp buffer
        jBuffer_DX12* buffer = jBufferUtil_DX12::CreateBuffer(RequiredSize, 0, true, false, D3D12_RESOURCE_STATE_GENERIC_READ, &texels[0], ImageSize);
        check(buffer);

        ComPtr<ID3D12GraphicsCommandList4> commandList = g_rhi_dx12->BeginSingleTimeCopyCommands();
        jBufferUtil_DX12::CopyBufferToImage(commandList.Get(), buffer->Buffer.Get(), 0, SimpleTextureCube->Image.Get(), 6, 0);

        g_rhi_dx12->EndSingleTimeCopyCommands(commandList);
        delete buffer;

		jBufferUtil_DX12::CreateShaderResourceView(SimpleTextureCube);
	}

    // SamplerState test
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    SimpleSamplerState = SamplerDescriptorHeap.Alloc();
    Device->CreateSampler(&samplerDesc, SimpleSamplerState.CPUHandle);

	////////////////////////////////////////////////////////////////////////////
	//// 12. AccelerationStructures
	//D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
	//geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	//geometryDesc.Triangles.IndexBuffer = IndexBuffer->GetGPUAddress();
	//geometryDesc.Triangles.IndexCount = static_cast<uint32>(IndexBuffer->GetAllocatedSize() / sizeof(uint16));
	//geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	//geometryDesc.Triangles.Transform3x4 = 0;
	//geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	//geometryDesc.Triangles.VertexCount = static_cast<uint32>(VertexBuffer->GetAllocatedSize()) / sizeof(Vertex);
	//geometryDesc.Triangles.VertexBuffer.StartAddress = VertexBuffer->GetGPUAddress();
	//geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

	//// Opaque로 지오메트를 등록하면, 히트 쉐이더에서 더이상 쉐이더를 만들지 않을 것이므로 최적화에 좋다.
	//geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	//// Acceleration structure 에 필요한 크기를 요청함
 //   // 첫번째 지오메트리
 //   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo{};
 //   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs{};
 //   {
 //       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
 //       bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
 //       bottomLevelInputs.Flags = buildFlags;
 //       bottomLevelInputs.pGeometryDescs = &geometryDesc;
 //       bottomLevelInputs.NumDescs = 1;
 //       bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;


	//	Device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
 //       if (!ensure(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0))
 //           return false;
 //   }

 //   // Acceleration structure를 위한 리소스를 할당함
 //   // Acceleration structure는 default heap에서 생성된 리소스에만 있을 수 있음. (또는 그에 상응하는 heap)
 //   // Default heap은 CPU 읽기/쓰기 접근이 필요없기 때문에 괜찮음.
 //   // Acceleration structure를 포함하는 리소스는 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE 상태로 생성해야 함.
 //   // 그리고 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS 플래그를 가져야 함. ALLOW_UNORDERED_ACCESS는 두가지 간단한 정보를 요구함:
 //   // - 시스템은 백그라운드에서 Acceleration structure 빌드를 구현할 때 이러한 유형의 액세스를 수행할 것입니다.
 //   // - 앱의 관점에서, acceleration structure에 쓰기/읽기의 동기화는 UAV barriers를 통해서 얻어짐.
 //   D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	//BottomLevelAccelerationStructureBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, 0, false, true
	//	, initialResourceState, nullptr, 0, TEXT("BottomLevelAccelerationStructure"));
	//check(BottomLevelAccelerationStructureBuffer);

 //   // Bottom level acceleration structure desc
 //   jBuffer_DX12* ScratchResourceBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfo.ScratchDataSizeInBytes, 0, false, true
 //       , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, 0, TEXT("ScratchResourceGeometry1"));

 //   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc{};
 //   bottomLevelBuildDesc.Inputs = bottomLevelInputs;
 //   bottomLevelBuildDesc.ScratchAccelerationStructureData = ScratchResourceBuffer->GetGPUAddress();
 //   bottomLevelBuildDesc.DestAccelerationStructureData = BottomLevelAccelerationStructureBuffer->GetGPUAddress();

	//GraphicsCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
	//{
	//	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(BottomLevelAccelerationStructureBuffer->Buffer.Get());
	//	GraphicsCommandList->ResourceBarrier(1, &barrier);
	//}

    //////////////////////////////////////////////////////////////////////////

 //   // 두번째 지오메트리
 //   Vertex secondVertices[] = {
 //           XMFLOAT3(-100.0f, -1.0f, -100.0f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
 //           XMFLOAT3(100.0f, -1.0f, 100.f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
 //           XMFLOAT3(-100.0f, -1.0f, 100.0f),   XMFLOAT3(0.0f, 1.0f, 0.0f),

 //           XMFLOAT3(-100.0f, -1.0f, -100.0f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
 //           XMFLOAT3(100.0f, -1.0f, -100.0f),    XMFLOAT3(0.0f, 1.0f, 0.0f),
 //           XMFLOAT3(100.0f, -1.0f, 100.0f),   XMFLOAT3(0.0f, 1.0f, 0.0f),
 //   };

	//VertexBufferSecondGeometry = jBufferUtil_DX12::CreateBuffer(sizeof(secondVertices), 0, true, false, D3D12_RESOURCE_STATE_COMMON, secondVertices, sizeof(secondVertices), TEXT("SecondGeometryVB"));
	//check(VertexBufferSecondGeometry);

 //   // VertexBuffer SRV
	//jBufferUtil_DX12::CreateShaderResourceView(VertexBufferSecondGeometry);

 //   D3D12_RAYTRACING_GEOMETRY_DESC secondGeometryDesc{};
 //   secondGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
 //   secondGeometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
 //   secondGeometryDesc.Triangles.Transform3x4 = 0;
 //   secondGeometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
 //   secondGeometryDesc.Triangles.VertexCount = static_cast<uint32>(VertexBufferSecondGeometry->Size) / sizeof(Vertex);
	//secondGeometryDesc.Triangles.VertexBuffer.StartAddress = VertexBufferSecondGeometry->GetGPUAddress();
 //   secondGeometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);


 //   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfoSecondGeometry{};
 //   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputsSecondGeometry{};
 //   {
 //       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
 //       bottomLevelInputsSecondGeometry.Flags = buildFlags;
 //       bottomLevelInputsSecondGeometry.pGeometryDescs = &secondGeometryDesc;
 //       bottomLevelInputsSecondGeometry.NumDescs = 1;
 //       bottomLevelInputsSecondGeometry.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	//	Device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputsSecondGeometry, &bottomLevelPrebuildInfoSecondGeometry);
 //       if (!ensure(bottomLevelPrebuildInfoSecondGeometry.ResultDataMaxSizeInBytes > 0))
 //           return false;
 //   }

	//BottomLevelAccelerationStructureSecondGeometryBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfoSecondGeometry.ResultDataMaxSizeInBytes, 0, false, true
	//	, initialResourceState, nullptr, 0, TEXT("BottomLevelAccelerationStructure"));

	//jBuffer_DX12* scratchResourceSecondGeometryBuffer = nullptr;
	//scratchResourceSecondGeometryBuffer = jBufferUtil_DX12::CreateBuffer(bottomLevelPrebuildInfoSecondGeometry.ScratchDataSizeInBytes, 0, false, true
 //       , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, 0, TEXT("ScratchResourceGeometry2"));

 //   // Bottom level acceleration structure desc
 //   D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDescSecondGeometry{};
 //   bottomLevelBuildDescSecondGeometry.Inputs = bottomLevelInputsSecondGeometry;
 //   bottomLevelBuildDescSecondGeometry.ScratchAccelerationStructureData = scratchResourceSecondGeometryBuffer->GetGPUAddress();
 //   bottomLevelBuildDescSecondGeometry.DestAccelerationStructureData = BottomLevelAccelerationStructureSecondGeometryBuffer->GetGPUAddress();

	//GraphicsCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDescSecondGeometry, 0, nullptr);
	//{
	//	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(BottomLevelAccelerationStructureSecondGeometryBuffer->Buffer.Get());
	//	GraphicsCommandList->ResourceBarrier(1, &barrier);
	//}

 //   if (!ensure(BuildTopLevelAS(GraphicsCommandList, TLASBuffer, false, 0.0f, Vector::ZeroVector)))
 //       return false;

	//GraphicsCommandQueue->ExecuteCommandList(GraphicsCommandList);

	WaitForGPU();

	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};

#if USE_INLINE_DESCRIPTOR
	D3D12_ROOT_PARAMETER1 rootParameter[3];
	rootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameter[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
	rootParameter[0].Descriptor.ShaderRegister = 0;
	rootParameter[0].Descriptor.RegisterSpace = 0;

    rootParameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameter[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    rootParameter[1].Descriptor.ShaderRegister = 0;									// SRV is different register from CBV, so restarting index
    rootParameter[1].Descriptor.RegisterSpace = 0;

	// Inline SRV can only be Raw or Structurred buffers, so texture resource could be bound by using DescriptorTable
	D3D12_DESCRIPTOR_RANGE1 range[2];
    range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range[0].NumDescriptors = 1;
    range[0].BaseShaderRegister = 1;
    range[0].RegisterSpace = 0;
    range[0].OffsetInDescriptorsFromTableStart = SimpleTexture->SRV.Index;						// Texture test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

	// Cube texture test
    range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range[1].NumDescriptors = 1;
    range[1].BaseShaderRegister = 2;
    range[1].RegisterSpace = 0;
    range[1].OffsetInDescriptorsFromTableStart = SimpleTextureCube->SRV.Index;						// Texture test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

    rootParameter[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameter[2].DescriptorTable.NumDescriptorRanges = _countof(range);
	rootParameter[2].DescriptorTable.pDescriptorRanges = range;

    D3D12_STATIC_SAMPLER_DESC staticSamplerDesc = {};
    staticSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplerDesc.MinLOD = 0;
    staticSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplerDesc.MipLODBias = 0.0f;
    staticSamplerDesc.MaxAnisotropy = 1;
    staticSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	staticSamplerDesc.ShaderRegister = 0;
	staticSamplerDesc.RegisterSpace = 0;
	staticSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootSignatureDesc.NumParameters = _countof(rootParameter);
    rootSignatureDesc.pParameters = rootParameter;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &staticSamplerDesc;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#else
    D3D12_DESCRIPTOR_RANGE1 range[4];
    range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range[0].NumDescriptors = 1;
    range[0].BaseShaderRegister = 0;
    range[0].RegisterSpace = 0;
    range[0].OffsetInDescriptorsFromTableStart = SimpleConstantBuffer->CBV.Index;				// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

    // StructuredBuffer test
    range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range[1].NumDescriptors = 1;
    range[1].BaseShaderRegister = 0;
    range[1].RegisterSpace = 0;
    range[1].OffsetInDescriptorsFromTableStart = SimpleStructuredBuffer->SRV.Index;				// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

    // Texture test
    range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range[2].NumDescriptors = 1;
    range[2].BaseShaderRegister = 1;
    range[2].RegisterSpace = 0;
    range[2].OffsetInDescriptorsFromTableStart = SimpleTexture->SRV.Index;						// Texture test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

    // Cube texture test
    range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range[3].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range[3].NumDescriptors = 1;
    range[3].BaseShaderRegister = 2;
    range[3].RegisterSpace = 0;
    range[3].OffsetInDescriptorsFromTableStart = SimpleTextureCube->SRV.Index;						// Texture test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

    D3D12_ROOT_PARAMETER1 rootParameter[2];
    rootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameter[0].DescriptorTable.NumDescriptorRanges = _countof(range);
    rootParameter[0].DescriptorTable.pDescriptorRanges = range;

    // SamplerState test
    D3D12_DESCRIPTOR_RANGE1 rangeSecond[1];
    rangeSecond[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    rangeSecond[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    rangeSecond[0].NumDescriptors = 1;
    rangeSecond[0].BaseShaderRegister = 0;
    rangeSecond[0].RegisterSpace = 0;
    rangeSecond[0].OffsetInDescriptorsFromTableStart = SimpleSamplerState.Index;				// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

    rootParameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameter[1].DescriptorTable.NumDescriptorRanges = _countof(rangeSecond);
    rootParameter[1].DescriptorTable.pDescriptorRanges = rangeSecond;

    rootSignatureDesc.NumParameters = _countof(rootParameter);
    rootSignatureDesc.pParameters = rootParameter;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#endif

	//////////////////////////////////////////////////////////////////////////

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = { };
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1 = rootSignatureDesc;

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	if (JFAIL_E(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error), error))
		return false;

	if (JFAIL(Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&SimpleRootSignature))))
		return false;

	D3D12_INPUT_ELEMENT_DESC VSInputElementDesc[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "NORMAL", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 12, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	ComPtr<IDxcBlob> VSShaderBlob;
	VSShaderBlob = jShaderCompiler_DX12::Get().Compile(TEXT("Resource/Shaders/hlsl/DXSampleHelloTriangle.hlsl"), TEXT("vs_6_3"), TEXT("VSMain"));
	check(VSShaderBlob);

    ComPtr<IDxcBlob> PSShaderBlob;
	PSShaderBlob = jShaderCompiler_DX12::Get().Compile(TEXT("Resource/Shaders/hlsl/DXSampleHelloTriangle.hlsl"), TEXT("ps_6_3"), TEXT("PSMain"));
	check(PSShaderBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { .pInputElementDescs = VSInputElementDesc, .NumElements = _countof(VSInputElementDesc) };
	psoDesc.pRootSignature = SimpleRootSignature.Get();
	psoDesc.VS = { .pShaderBytecode = VSShaderBlob->GetBufferPointer(), .BytecodeLength = VSShaderBlob->GetBufferSize() };
	psoDesc.PS = { .pShaderBytecode = PSShaderBlob->GetBufferPointer(), .BytecodeLength = PSShaderBlob->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
	if (JFAIL(Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&SimplePipelineState))))
		return false;

    //delete ScratchResourceBuffer;
    //delete scratchResourceSecondGeometryBuffer;

	//// 13. ShaderTable
	//const uint16 shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	//ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
	//if (JFAIL(m_dxrStateObject.As(&stateObjectProperties)))
	//	return false;
	
	//void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
	//void* misssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
	//void* triHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_triHitGroupName);
 //   void* planeHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_planeHitGroupName);

	//// Raygen shader table
	//{
	//	const uint16 numShaderRecords = 1;
	//	const uint16 shaderRecordSize = shaderIdentifierSize;
	//	ShaderTable rayGenShaderTable(Device.Get(), numShaderRecords, shaderRecordSize, TEXT("RayGenShaderTable"));
	//	rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
	//	m_rayGenShaderTable = rayGenShaderTable.GetResource();
	//}

	//// Miss shader table
	//{
	//	const uint16 numShaderRecords = 1;
	//	const uint16 shaderRecordSize = shaderIdentifierSize;
	//	ShaderTable missShaderTable(Device.Get(), numShaderRecords, shaderRecordSize, TEXT("MissShaderTable"));
	//	missShaderTable.push_back(ShaderRecord(misssShaderIdentifier, shaderIdentifierSize));
	//	m_missShaderTable = missShaderTable.GetResource();
	//}

	//// Triangle Hit group shader table
	//{
	//	struct RootArguments
	//	{
	//		CubeConstantBuffer cb;
	//	};
	//	RootArguments rootArguments;
	//	rootArguments.cb = m_cubeCB;

	//	const uint16 numShaderRecords = 2;
	//	const uint16 shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);       // 큰 사이즈 기준으로 2개 만듬
	//	ShaderTable hitGroupShaderTable(Device.Get(), numShaderRecords, shaderRecordSize, TEXT("HitGroupShaderTable"));
	//	hitGroupShaderTable.push_back(ShaderRecord(triHitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));

 //       RootArguments planeRootArguments;
 //       planeRootArguments.cb = m_planeCB;
 //       hitGroupShaderTable.push_back(ShaderRecord(planeHitGroupShaderIdentifier, shaderIdentifierSize, &planeRootArguments, sizeof(planeRootArguments)));
	//	m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
	//}

	//////////////////////////////////////////////////////////////////////////
	// 14. Raytracing Output Resouce

	// 출력 리소스를 생성. 차원과 포맷은 swap-chain과 매치 되어야 함
	RayTacingOutputTexture = jBufferUtil_DX12::CreateImage(SCR_WIDTH, SCR_HEIGHT, 1, 1, 1, ETextureType::TEXTURE_2D, ETextureFormat::RGBA8, false, true);
	if (!ensure(RayTacingOutputTexture))
		return false;

	jBufferUtil_DX12::CreateUnorderedAccessView(RayTacingOutputTexture);

	//////////////////////////////////////////////////////////////////////////
	// 15. CreateConstantBuffers
	// 상수 버퍼 메모리를 만들고 CPU와 GPU 주소를 매핑함

    // 프레임별 상수버퍼를 만든다, 매프레임 업데이트 되기 때문
    const size_t cbSize = MaxFrameCount * sizeof(AlignedSceneConstantBuffer);
	PerFrameConstantBuffer = jBufferUtil_DX12::CreateBuffer(cbSize, 0, true, false, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, 0);
	if (!ensure(PerFrameConstantBuffer))
		return false;

	if (!PerFrameConstantBuffer->GetMappedPointer())
	{
		PerFrameConstantBuffer->Map();
	}
	
	//////////////////////////////////////////////////////////////////////////
    InitializeImGui();

	ShowWindow(m_hWnd, SW_SHOW);

    return true;
}

bool jRHI_DX12::Run()
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

void jRHI_DX12::Release()
{
	WaitForGPU();
    
    ReleaseImGui();

	//////////////////////////////////////////////////////////////////////////
	// 14. Raytracing Output Resouce
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;

	////////////////////////////////////////////////////////////////////////////
	//// 13. ShaderTable
	//m_rayGenShaderTable.Reset();
	//m_missShaderTable.Reset();
	//m_hitGroupShaderTable.Reset();

	////////////////////////////////////////////////////////////////////////////
	//// 12. AccelerationStructures
	//delete BottomLevelAccelerationStructureBuffer;
	//BottomLevelAccelerationStructureBuffer = nullptr;
	//TLASBuffer.Release();

	//////////////////////////////////////////////////////////////////////////
	// 11. Create vertex and index buffer
	delete VertexBuffer;
	VertexBuffer = nullptr;
	
	delete IndexBuffer;
	IndexBuffer = nullptr;

	delete SimpleConstantBuffer;
	SimpleConstantBuffer = nullptr;

	// StructuredBuffer test
	delete SimpleStructuredBuffer;
	SimpleStructuredBuffer = nullptr;

	// Texture test
	delete SimpleTexture;
	SimpleTexture = nullptr;

	////////////////////////////////////////////////////////////////////////////
	//// 10. DXR PipeplineStateObject
	//m_dxrStateObject.Reset();

	////////////////////////////////////////////////////////////////////////////
	//// 9. CreateRootSignature
	//m_raytracingGlobalRootSignature.Reset();
	//m_raytracingLocalRootSignature.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 8. Raytracing device and commandlist
	Device.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 7. Create sync object

	//////////////////////////////////////////////////////////////////////////
	// 6. CommandAllocators, Commandlist, RTV for FrameCount

    //////////////////////////////////////////////////////////////////////////
    // 5. Initialize Camera and lighting

	//////////////////////////////////////////////////////////////////////////
	// 4. Heap
	RTVDescriptorHeap.Release();
	DSVDescriptorHeap.Release();
	SRVDescriptorHeap.Release();
	SamplerDescriptorHeap.Release();		// SamplerState test

	//////////////////////////////////////////////////////////////////////////
	// 3. Swapchain
    SwapChain->Release();
    delete SwapChain;

	//////////////////////////////////////////////////////////////////////////
	// 2. Command
	delete GraphicsCommandQueue;

	//////////////////////////////////////////////////////////////////////////
	// 1. Device
	Device.Reset();

}

void jRHI_DX12::UpdateCameraMatrices()
{
	auto frameIndex = SwapChain->GetCurrentBackBufferIndex();

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

//bool jRHI_DX12::BuildTopLevelAS(ComPtr<ID3D12GraphicsCommandList4>& InCommandList, TopLevelAccelerationStructureBuffers& InBuffers, bool InIsUpdate, float InRotationY, Vector InTranslation)
//{
//    int32 w = 11, h = 11;
//    int32 totalCount = (w * 2 * h * 2) + 3 + 1;     // small balls, big balls, plane
//    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
//    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
//    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
//    inputs.NumDescs = totalCount;
//    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
//
//    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
//	Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
//    if (!ensure(info.ResultDataMaxSizeInBytes > 0))
//        return false;
//
//    if (InIsUpdate)
//    {
//		check(InBuffers.Result);
//
//        // Update 요청이 온다면 TLAS 는 이미 DispatchRay()의 호출에서 사용되고있다.
//        // 버퍼가 업데이트 되기 전에 UAV 베리어를 read 연산의 끝에 넣어줘야 한다.
//
//        D3D12_RESOURCE_BARRIER uavBarrier = {};
//        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
//        uavBarrier.UAV.pResource = InBuffers.Result->Buffer.Get();
//		InCommandList->ResourceBarrier(1, &uavBarrier);
//    }
//    else
//    {
//        // Update 요청이 아니면, 버퍼를 새로 만들어야 함, 그렇지 않으면 그대로 refit(이미 존재하는 TLAS를 업데이트) 될 것임.
//		InBuffers.Scratch = jBufferUtil_DX12::CreateBuffer(info.ScratchDataSizeInBytes, 0, false, true
//			, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, 0, TEXT("TLAS Scratch Buffer"));
//		InBuffers.Result = jBufferUtil_DX12::CreateBuffer(info.ResultDataMaxSizeInBytes, 0, false, true
//			, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, 0, TEXT("TLAS Result Buffer"));
//		InBuffers.InstanceDesc = jBufferUtil_DX12::CreateBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalCount, 0, true, false
//			, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, 0, TEXT("TLAS Instance Desc"));
//    }
//
//    D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = nullptr;
//	instanceDescs = (D3D12_RAYTRACING_INSTANCE_DESC*)InBuffers.InstanceDesc->Map();
//    if (!instanceDescs)
//        return false;
//
//    ZeroMemory(instanceDescs, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalCount);
//
//    int32 cnt = 0;
//
//    srand(123);
//    const float radius = 0.3f;
//    for (int32 i = -w; i < w; ++i)
//    {
//        for (int32 j = -h; j < h; ++j, ++cnt)
//        {
//            float r = radius;
//            auto s = XMMatrixScaling(r, r, r);
//            auto t = XMMatrixTranslation(
//                (float)(i * radius * 5.0f) + (radius * 4.0f * random_double())
//                , -0.7f
//                , (float)(j * radius * 5.0f) + (radius * 4.0f * random_double()));
//            auto m = XMMatrixTranspose(XMMatrixMultiply(s, t));
//
//            instanceDescs[cnt].InstanceID = cnt;
//            instanceDescs[cnt].InstanceContributionToHitGroupIndex = 0;
//            memcpy(instanceDescs[cnt].Transform, &m, sizeof(instanceDescs[cnt].Transform));
//            instanceDescs[cnt].InstanceMask = 1;
//            instanceDescs[cnt].AccelerationStructure = BottomLevelAccelerationStructureBuffer->GetGPUAddress();
//        }
//    }
//
//    for (int32 i = 0; i < 3; ++i)
//    {
//        auto s = XMMatrixScaling(1.0f, 1.0f, 1.0f);
//        auto t = XMMatrixTranslation(0.0f + i * 2, 0.0f, 0.0f + i * 2);
//        auto m = XMMatrixTranspose(XMMatrixMultiply(s, t));
//
//        instanceDescs[cnt].InstanceID = cnt;
//        instanceDescs[cnt].InstanceContributionToHitGroupIndex = 0;
//        memcpy(instanceDescs[cnt].Transform, &m, sizeof(instanceDescs[cnt].Transform));
//        instanceDescs[cnt].InstanceMask = 1;
//        instanceDescs[cnt].AccelerationStructure = BottomLevelAccelerationStructureBuffer->GetGPUAddress();
//
//        ++cnt;
//    }
//
//    auto mIdentity = XMMatrixTranspose(XMMatrixIdentity());
//
//    instanceDescs[cnt].InstanceID = cnt;
//    instanceDescs[cnt].InstanceContributionToHitGroupIndex = 1;
//    memcpy(instanceDescs[cnt].Transform, &mIdentity, sizeof(instanceDescs[cnt].Transform));
//    instanceDescs[cnt].InstanceMask = 1;
//    instanceDescs[cnt].AccelerationStructure = BottomLevelAccelerationStructureSecondGeometryBuffer->GetGPUAddress();
//    instanceDescs[cnt].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
//
//    InBuffers.InstanceDesc->Unmap();
//
//    // TLAS 생성
//    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
//    asDesc.Inputs = inputs;
//    asDesc.Inputs.InstanceDescs = InBuffers.InstanceDesc->GetGPUAddress();
//    asDesc.DestAccelerationStructureData = InBuffers.Result->GetGPUAddress();
//    asDesc.ScratchAccelerationStructureData = InBuffers.Scratch->GetGPUAddress();
//
//    // 만약 업데이트 중이라면 source buffer에 업데이트 플래그를 설정해준다.
//    if (InIsUpdate)
//    {
//        asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
//        asDesc.SourceAccelerationStructureData = InBuffers.Result->GetGPUAddress();
//    }
//
//	InCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
//
//    // 레이트레이싱 연산에서 Acceleration structure를 사용하기 전에 UAV 베리어를 추가해야 함.
//    D3D12_RESOURCE_BARRIER uavBarrier = {};
//    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
//    uavBarrier.UAV.pResource = InBuffers.Result->Buffer.Get();
//	InCommandList->ResourceBarrier(1, &uavBarrier);
//
//    return true;
//}

void jRHI_DX12::CalculateFrameStats()
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

        float raysPerPixels = (float)(m_sceneCB[CurrentFrameIndex].NumOfStartingRay * g_MaxRecursionDepth);
        float MRaysPerSecond = (SCR_WIDTH * SCR_HEIGHT * fps * raysPerPixels) / static_cast<float>(1e6);

        std::wstringstream windowText;

        windowText << std::setprecision(2) << std::fixed
            << L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
            << L"    GPU[" << AdapterID << L"]: " << AdapterName;
        SetWindowText(m_hWnd, windowText.str().c_str());
    }
}

void jRHI_DX12::Update()
{
    CalculateFrameStats();

	int32 prevFrameIndex = CurrentFrameIndex - 1;
	if (prevFrameIndex < 0)
		prevFrameIndex = MaxFrameCount - 1;

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
		m_sceneCB[CurrentFrameIndex].lightPosition = XMVector3Transform(rotationRadius, rotate);
	}

    {
        auto frameIndex = SwapChain->GetCurrentBackBufferIndex();

        m_sceneCB[frameIndex].focalDistance = m_focalDistance;
        m_sceneCB[frameIndex].lensRadius = m_lensRadius;
    }
}

void jRHI_DX12::Render()
{
	GetOneFrameUniformRingBuffer()->Reset();

	// Prepare
	auto GraphicsCommandList = GraphicsCommandQueue->GetAvailableCommandList();

 //   static float elapsedTime = 0.0f;
 //   elapsedTime = 0.01f;

 //   float secondsToRotateAround = 24.0f;
 //   float angleToRotateBy = 360.0f * (elapsedTime / secondsToRotateAround);
 //   static float accumulatedRotation = 0.0f;
 //   accumulatedRotation += angleToRotateBy;

 //   static float TranslationOffsetX = 0.0f;
 //   static bool TranslateDirRight = 1;
 //   if (TranslateDirRight)
 //   {
 //       if (TranslationOffsetX > 2.0f)
 //           TranslateDirRight = false;
 //       TranslationOffsetX += 0.01f;
 //   }
 //   else
 //   {
 //       if (TranslationOffsetX < -2.0f)
 //           TranslateDirRight = true;
 //       TranslationOffsetX -= 0.01f;
 //   }

    jSwapchainImage* SwapchainImage = SwapChain->GetSwapchainImage(CurrentFrameIndex);
    jTexture_DX12* SwapchainRT = (jTexture_DX12*)SwapchainImage->TexturePtr.get();

	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (float)SCR_WIDTH;
	viewport.Height = (float)SCR_HEIGHT;
	viewport.MinDepth = D3D12_MIN_DEPTH;
	viewport.MaxDepth = D3D12_MAX_DEPTH;

	D3D12_RECT ScissorRect;
	ScissorRect.left = 0;
	ScissorRect.right = SCR_WIDTH;
	ScissorRect.top = 0;
	ScissorRect.bottom = SCR_HEIGHT;

    GraphicsCommandList->SetGraphicsRootSignature(SimpleRootSignature.Get());

#if USE_INLINE_DESCRIPTOR
	ID3D12DescriptorHeap* ppHeaps[] = { SRVDescriptorHeap.Heap.Get() };								// SamplerState test
	GraphicsCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	#if USE_ONE_FRAME_BUFFER_AND_DESCRIPTOR
    jRingBuffer_DX12* CurrentRingBuffer = GetOneFrameUniformRingBuffer();

	// 1. Transform ConstantBuffer by using OneFrameBuffer
	{
		jSimpleConstantBuffer ConstantBuffer;
		ConstantBuffer.M.SetIdentity();
		Matrix Projection = jCameraUtil::CreatePerspectiveMatrix((float)SCR_WIDTH, (float)SCR_HEIGHT, DegreeToRadian(90.0f), 0.01f, 1000.0f);
		Matrix Camera = jCameraUtil::CreateViewMatrix(Vector::FowardVector * 2.0f, Vector::ZeroVector, Vector::UpVector);
		ConstantBuffer.M = Projection * Camera * ConstantBuffer.M;
		ConstantBuffer.M.SetTranspose();

		const uint64 TempBufferOffset = CurrentRingBuffer->Alloc(Align(sizeof(ConstantBuffer), 256));
		uint8* DestAddress = ((uint8*)CurrentRingBuffer->GetMappedPointer()) + TempBufferOffset;
		memcpy(DestAddress, &ConstantBuffer, sizeof(ConstantBuffer));

		const uint64 GPUAddress = CurrentRingBuffer->GetGPUAddress() + TempBufferOffset;
		GraphicsCommandList->SetGraphicsRootConstantBufferView(0, GPUAddress);
	}

	// 2. Transform StructuredBuffer by using OneFrameBuffer
	{
        Vector4 StructuredBufferColor[3];
        StructuredBufferColor[0] = { 1.0f, 0.0f, 0.0f, 1.0f };
        StructuredBufferColor[1] = { 0.0f, 1.0f, 0.0f, 1.0f };
        StructuredBufferColor[2] = { 0.0f, 0.0f, 1.0f, 1.0f };

		const uint64 TempBufferOffset = CurrentRingBuffer->Alloc(Align(sizeof(StructuredBufferColor), 256));
        uint8* DestAddress = ((uint8*)CurrentRingBuffer->GetMappedPointer()) + TempBufferOffset;
        memcpy(DestAddress, &StructuredBufferColor, sizeof(StructuredBufferColor));

        const uint64 GPUAddress = CurrentRingBuffer->GetGPUAddress() + TempBufferOffset;
		const uint32 stride = sizeof(StructuredBufferColor[0]);
		
        GraphicsCommandList->SetGraphicsRootShaderResourceView(1, GPUAddress);
	}
	#else
	GraphicsCommandList->SetGraphicsRootConstantBufferView(0, SimpleConstantBuffer->GetGPUAddress());
	GraphicsCommandList->SetGraphicsRootShaderResourceView(1, SimpleStructuredBuffer->GetGPUAddress());
	#endif
	GraphicsCommandList->SetGraphicsRootDescriptorTable(2, SRVDescriptorHeap.GPUHandleStart);		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap
#else
    ID3D12DescriptorHeap* ppHeaps[] = { SRVDescriptorHeap.Heap.Get(), SamplerDescriptorHeap.Heap.Get() };		// SamplerState test
    GraphicsCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    GraphicsCommandList->SetGraphicsRootDescriptorTable(0, SRVDescriptorHeap.GPUHandleStart);		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap
    GraphicsCommandList->SetGraphicsRootDescriptorTable(1, SamplerDescriptorHeap.GPUHandleStart);	// SamplerState test
#endif

	GraphicsCommandList->RSSetViewports(1, &viewport);
	GraphicsCommandList->RSSetScissorRects(1, &ScissorRect);

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			SwapchainRT->Image.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		GraphicsCommandList->ResourceBarrier(1, &barrier);
	}

	GraphicsCommandList->OMSetRenderTargets(1, &SwapchainRT->RTV.CPUHandle, false, nullptr);

	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	GraphicsCommandList->ClearRenderTargetView(SwapchainRT->RTV.CPUHandle, clearColor, 0, nullptr);
	GraphicsCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = { .BufferLocation = VertexBuffer->GetGPUAddress(), .SizeInBytes = (uint32)VertexBuffer->Size, .StrideInBytes = sizeof(Vertex) };
	GraphicsCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	D3D12_INDEX_BUFFER_VIEW indexBufferView = {.BufferLocation = IndexBuffer->GetGPUAddress(), .SizeInBytes = IndexBuffer->GetAllocatedSize(), .Format = DXGI_FORMAT_R32_UINT };
	GraphicsCommandList->IASetIndexBuffer(&indexBufferView);
	GraphicsCommandList->SetPipelineState(SimplePipelineState.Get());
	//GraphicsCommandList->DrawInstanced(3, 1, 0, 0);
	GraphicsCommandList->DrawIndexedInstanced(IndexBuffer->GetAllocatedSize() / sizeof(uint32), 1, 0, 0, 0);

	//GraphicsCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

	//AlignedSceneConstantBuffer* CurFrameConstantBuffer = (AlignedSceneConstantBuffer*)PerFrameConstantBuffer->GetMappedPointer();
	//memcpy(&CurFrameConstantBuffer[CurrentFrameIndex].constants, &m_sceneCB[CurrentFrameIndex], sizeof(m_sceneCB[CurrentFrameIndex]));
	//auto cbGpuAddress = PerFrameConstantBuffer->GetGPUAddress() + CurrentFrameIndex * sizeof(AlignedSceneConstantBuffer);
	//GraphicsCommandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);

	//D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
	//GraphicsCommandList->SetDescriptorHeaps(1, SRVDescriptorHeap.Heap.GetAddressOf());
	//GraphicsCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, IndexBuffer->SRV.GPUHandle);
	//GraphicsCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::PlaneVertexBufferSlot
 //       , VertexBufferSecondGeometry->SRV.GPUHandle);
	//GraphicsCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot
 //       , RayTacingOutputTexture->UAV.GPUHandle);
	//GraphicsCommandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot
	//	, TLASBuffer.Result->GetGPUAddress());

	//// 각 Shader table은 단 한개의 shader record를 가지기 때문에 stride가 그 사이즈와 동일함
	//dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
	//dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
	//dispatchDesc.HitGroupTable.StrideInBytes = dispatchDesc.HitGroupTable.SizeInBytes / 2;  // 2개의 HitGroupTable이 등록되었으므로 개수 만큼 나눠줌

	//dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
	//dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
	//dispatchDesc.MissShaderTable.StrideInBytes = dispatchDesc.MissShaderTable.SizeInBytes;

	//dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
	//dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;

	//dispatchDesc.Width = SCR_WIDTH;
	//dispatchDesc.Height = SCR_HEIGHT;
	//dispatchDesc.Depth = 1;

	//GraphicsCommandList->SetPipelineState1(m_dxrStateObject.Get());
	//GraphicsCommandList->DispatchRays(&dispatchDesc);

	//// CopyRaytracingOutputToBackbuffer
	//D3D12_RESOURCE_BARRIER preCopyBarriers[2];
	//preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(SwapchainRT->Image.Get()
	//	, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	//preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(RayTacingOutputTexture->Image.Get()
	//	, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	//GraphicsCommandList->ResourceBarrier(_countof(preCopyBarriers), preCopyBarriers);

	//GraphicsCommandList->CopyResource(SwapchainRT->Image.Get(), RayTacingOutputTexture->Image.Get());

	//{
	//	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(RayTacingOutputTexture->Image.Get()
	//		, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	//	GraphicsCommandList->ResourceBarrier(1, &barrier);
	//}

	// CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), FrameIndex* m_rtvDescriptorSize);
    //RenderUI(GraphicsCommandList.Get(), SwapchainRT->Image.Get(), SwapchainRT->RTV.CPUHandle, m_imgui_SrvDescHeap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    //////////////////////////////////////////////////////////////////////////

	RenderUI(GraphicsCommandList.Get(), SwapchainRT->Image.Get(), SwapchainRT->RTV.CPUHandle, m_imgui_SrvDescHeap.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// Present
	GraphicsCommandQueue->ExecuteCommandList(GraphicsCommandList);

	HRESULT hr = S_OK;
	if (Options & c_AllowTearing)
	{
		hr = SwapChain->SwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}
	else
	{
		// 첫번째 아규먼트는 VSync가 될때까지 기다림, 어플리케이션은 다음 VSync까지 잠재운다.
		// 이렇게 하는 것은 렌더링 한 프레임 중 화면에 나오지 못하는 프레임의 cycle을 아끼기 위해서임.
		hr = SwapChain->SwapChain->Present(1, 0);
	}

	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		// 디바이스 로스트 처리

#ifdef _DEBUG
        char buff[64] = {};
        sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n"
            , (hr == DXGI_ERROR_DEVICE_REMOVED) ? Device->GetDeviceRemovedReason() : hr);
        OutputDebugStringA(buff);
#endif

        OnHandleDeviceLost();
        OnHandleDeviceRestored();
	}
	else
	{
		if (JFAIL(hr))
			return;

		WaitForGPU();

		CurrentFrameIndex = SwapChain->GetCurrentBackBufferIndex();
	}
}


void jRHI_DX12::OnDeviceLost()
{
	//m_rayGenShaderTable.Reset();
	//m_missShaderTable.Reset();
	//m_hitGroupShaderTable.Reset();
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;

	//m_raytracingGlobalRootSignature.Reset();
	//m_raytracingLocalRootSignature.Reset();

	Device.Reset();
	//m_dxrStateObject.Reset();

	SRVDescriptorHeap.Release();
	SamplerDescriptorHeap.Release();		// SamplerState test
	
	delete VertexBuffer;
	VertexBuffer = nullptr;

	delete IndexBuffer;
	IndexBuffer = nullptr;

	//delete BottomLevelAccelerationStructureBuffer;
	//BottomLevelAccelerationStructureBuffer = nullptr;

	//TLASBuffer.Release();
}

void jRHI_DX12::OnDeviceRestored()
{
	// 7. Raytracing device and commandlist ~ 13. Raytracing Output Resouce
}

bool jRHI_DX12::OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized)
{
    JASSERT(InWidth > 0);
    JASSERT(InHeight > 0);

    {
        char szTemp[126];
        sprintf_s(szTemp, sizeof(szTemp), "Called OnHandleResized %d %d\n", InWidth, InHeight);
        OutputDebugStringA(szTemp);
    }

    WaitForGPU();

    if (ensure(SwapChain && SwapChain->SwapChain))
    {
        HRESULT hr = SwapChain->SwapChain->ResizeBuffers(MaxFrameCount, InWidth, InHeight, BackbufferFormat
            , (Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
#ifdef _DEBUG
            char buff[64] = {};
            sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n"
                , (hr == DXGI_ERROR_DEVICE_REMOVED) ? Device->GetDeviceRemovedReason() : hr);
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

    for (int32 i = 0; i < MaxFrameCount; ++i)
    {
		jSwapchainImage* SwapchainImage = SwapChain->GetSwapchainImage(i);
		jTexture_DX12* SwapchainRT = (jTexture_DX12*)SwapchainImage->TexturePtr.get();
        if (JFAIL(SwapChain->SwapChain->GetBuffer(i, IID_PPV_ARGS(&SwapchainRT->Image))))
            return false;

		jBufferUtil_DX12::CreateRenderTargetView(SwapchainRT);

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

    CurrentFrameIndex = SwapChain->GetCurrentBackBufferIndex();

    //////////////////////////////////////////////////////////////////////////
    // ReleaseWindowSizeDependentResources
    //m_rayGenShaderTable.Reset();
    //m_missShaderTable.Reset();
    //m_hitGroupShaderTable.Reset();
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;
    
    //////////////////////////////////////////////////////////////////////////
    // CreateWindowSizeDependentResources
    RayTacingOutputTexture = jBufferUtil_DX12::CreateImage(SCR_WIDTH, SCR_HEIGHT, 1, 1, 1, ETextureType::TEXTURE_2D, ETextureFormat::RGBA8, false, true);
    if (!ensure(RayTacingOutputTexture))
        return false;

    jBufferUtil_DX12::CreateUnorderedAccessView(RayTacingOutputTexture);

    ////////////////////////////////////////////////////////////////////////////
    //// RecreateShaderTable
    //const uint16 shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    //ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    //if (JFAIL(m_dxrStateObject.As(&stateObjectProperties)))
    //    return false;

    //void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
    //void* misssShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
    //void* triHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_triHitGroupName);
    //void* planeHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_planeHitGroupName);

    //// Raygen shader table
    //{
    //    const uint16 numShaderRecords = 1;
    //    const uint16 shaderRecordSize = shaderIdentifierSize;
    //    ShaderTable rayGenShaderTable(Device.Get(), numShaderRecords, shaderRecordSize, TEXT("RayGenShaderTable"));
    //    rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
    //    m_rayGenShaderTable = rayGenShaderTable.GetResource();
    //}

    //// Miss shader table
    //{
    //    const uint16 numShaderRecords = 1;
    //    const uint16 shaderRecordSize = shaderIdentifierSize;
    //    ShaderTable missShaderTable(Device.Get(), numShaderRecords, shaderRecordSize, TEXT("MissShaderTable"));
    //    missShaderTable.push_back(ShaderRecord(misssShaderIdentifier, shaderIdentifierSize));
    //    m_missShaderTable = missShaderTable.GetResource();
    //}

    //// Triangle Hit group shader table
    //{
    //    struct RootArguments
    //    {
    //        CubeConstantBuffer cb;
    //    };
    //    RootArguments rootArguments;
    //    rootArguments.cb = m_cubeCB;

    //    const uint16 numShaderRecords = 2;
    //    const uint16 shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);       // 큰 사이즈 기준으로 2개 만듬
    //    ShaderTable hitGroupShaderTable(Device.Get(), numShaderRecords, shaderRecordSize, TEXT("HitGroupShaderTable"));
    //    hitGroupShaderTable.push_back(ShaderRecord(triHitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));

    //    RootArguments planeRootArguments;
    //    planeRootArguments.cb = m_planeCB;
    //    hitGroupShaderTable.push_back(ShaderRecord(planeHitGroupShaderIdentifier, shaderIdentifierSize, &planeRootArguments, sizeof(planeRootArguments)));
    //    m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
    //}

    return true;
}

bool jRHI_DX12::OnHandleDeviceLost()
{
    //////////////////////////////////////////////////////////////////////////
    // ReleaseWindowSizeDependentResources
    //m_raytracingOutput.Reset();
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;

    //////////////////////////////////////////////////////////////////////////
    // ReleaseDeviceDependentResources
    //m_raytracingGlobalRootSignature.Reset();
    //m_raytracingLocalRootSignature.Reset();
    //m_raytracingEmptyLocalRootSignature.Reset();

    //m_dxrStateObject.Reset();
	Device.Reset();

	SRVDescriptorHeap.Release();
	SamplerDescriptorHeap.Release();		// SamplerState test
	
    delete VertexBuffer;
    VertexBuffer = nullptr;

	delete IndexBuffer;
	IndexBuffer = nullptr;

	//delete VertexBufferSecondGeometry;
	//VertexBufferSecondGeometry = nullptr;

	delete PerFrameConstantBuffer;
	PerFrameConstantBuffer = nullptr;

 //   m_rayGenShaderTable.Reset();
 //   m_missShaderTable.Reset();
 //   m_hitGroupShaderTable.Reset();

	//delete BottomLevelAccelerationStructureBuffer;
	//BottomLevelAccelerationStructureBuffer = nullptr;

	//delete BottomLevelAccelerationStructureSecondGeometryBuffer;
	//BottomLevelAccelerationStructureSecondGeometryBuffer = nullptr;

	//TLASBuffer.Release();

    return true;
}

bool jRHI_DX12::OnHandleDeviceRestored()
{
    InitRHI();
    return true;
}

ComPtr<ID3D12GraphicsCommandList4> jRHI_DX12::BeginSingleTimeCopyCommands()
{
	return CopyCommandQueue->GetAvailableCommandList();
}

void jRHI_DX12::EndSingleTimeCopyCommands(const ComPtr<ID3D12GraphicsCommandList4>& commandBuffer)
{
	CopyCommandQueue->ExecuteCommandList(commandBuffer);

	WaitForGPU();
}

void jRHI_DX12::InitializeImGui()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (JFAIL(Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imgui_SrvDescHeap))))
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(m_hWnd);

    ImGui_ImplDX12_Init(Device.Get(), MaxFrameCount,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_imgui_SrvDescHeap.Get(),
        m_imgui_SrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        m_imgui_SrvDescHeap->GetGPUDescriptorHandleForHeapStart());
}

void jRHI_DX12::ReleaseImGui()
{
    // Cleanup
	if (m_imgui_SrvDescHeap)
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		m_imgui_SrvDescHeap.Reset();
	}
}

void jRHI_DX12::RenderUI(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pRenderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle
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

	if (beforeResourceState != D3D12_RESOURCE_STATE_RENDER_TARGET)
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			pRenderTarget, beforeResourceState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		pCommandList->ResourceBarrier(1, &barrier);
	}
    const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };

    pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
    pCommandList->SetDescriptorHeaps(1, &pDescriptorHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);

	if (D3D12_RESOURCE_STATE_RENDER_TARGET != afterResourceState)
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, afterResourceState);
		pCommandList->ResourceBarrier(1, &barrier);
	}
}

jTexture* jRHI_DX12::CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
    , ETextureFormat textureFormat, bool createMipmap) const
{
	const uint16 MipLevels = createMipmap ? static_cast<uint32>(std::floor(std::log2(std::max<int>(width, height)))) + 1 : 1;
	jTexture_DX12* Texture = jBufferUtil_DX12::CreateImage(width, height, 1, MipLevels, 1, ETextureType::TEXTURE_2D, textureFormat, false, true);

	// Copy image data from buffer
	const auto Desc = Texture->Image->GetDesc();
	uint64 RequiredSize = 0;
	Device->GetCopyableFootprints(&Desc, 0, 1, 0, nullptr, nullptr, nullptr, &RequiredSize);

    const uint64 ImageSize = width * height * GetDX12TexturePixelSize(textureFormat);

	// todo : recycle temp buffer
	jBuffer_DX12* buffer = jBufferUtil_DX12::CreateBuffer(RequiredSize, 0, true, false, D3D12_RESOURCE_STATE_GENERIC_READ, data, ImageSize);
	check(buffer);

	ComPtr<ID3D12GraphicsCommandList4> commandList = g_rhi_dx12->BeginSingleTimeCopyCommands();
	jBufferUtil_DX12::CopyBufferToImage(commandList.Get(), buffer->Buffer.Get(), 0, Texture->Image.Get());
	g_rhi_dx12->EndSingleTimeCopyCommands(commandList);
	delete buffer;
	
	// Create SRV
	jBufferUtil_DX12::CreateShaderResourceView(Texture);

	return Texture;
}