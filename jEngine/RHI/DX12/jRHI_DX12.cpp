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
#include "jUniformBufferBlock_DX12.h"
#include "jShaderBindingInstance_DX12.h"
#include "jShaderBindingsLayout_DX12.h"
#include "jVertexBuffer_DX12.h"
#include "jIndexBuffer_DX12.h"
#include "jShader_DX12.h"
#include "FileLoader/jFile.h"
#include "jPipelineStateInfo_DX12.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jRenderPass_DX12.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"
#include "../jRenderFrameContext.h"
#include "jRenderFrameContext_DX12.h"

#define USE_INLINE_DESCRIPTOR 0												// InlineDescriptor 를 쓸것인지? DescriptorTable 를 쓸것인지 여부
#define USE_ONE_FRAME_BUFFER_AND_DESCRIPTOR (USE_INLINE_DESCRIPTOR && 1)	// 현재 프레임에만 사용하고 버리는 임시 Descriptor 와 Buffer 를 사용할 것인지 여부

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct jSimpleConstantBuffer
{
    Matrix M;
	int32 TexIndex = 0;
};

jRHI_DX12* g_rhi_dx12 = nullptr;
robin_hood::unordered_map<size_t, jShaderBindingsLayout*> jRHI_DX12::ShaderBindingPool;
TResourcePool<jSamplerStateInfo_DX12, jMutexRWLock> jRHI_DX12::SamplerStatePool;
TResourcePool<jRasterizationStateInfo_DX12, jMutexRWLock> jRHI_DX12::RasterizationStatePool;
TResourcePool<jStencilOpStateInfo_DX12, jMutexRWLock> jRHI_DX12::StencilOpStatePool;
TResourcePool<jDepthStencilStateInfo_DX12, jMutexRWLock> jRHI_DX12::DepthStencilStatePool;
TResourcePool<jBlendingStateInfo_DX12, jMutexRWLock> jRHI_DX12::BlendingStatePool;
TResourcePool<jPipelineStateInfo_DX12, jMutexRWLock> jRHI_DX12::PipelineStatePool;
TResourcePool<jRenderPass_DX12, jMutexRWLock> jRHI_DX12::RenderPassPool;

const wchar_t* jRHI_DX12::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* jRHI_DX12::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* jRHI_DX12::c_missShaderName = L"MyMissShader";
const wchar_t* jRHI_DX12::c_triHitGroupName = L"TriHitGroup";
const wchar_t* jRHI_DX12::c_planeHitGroupName = L"PlaneHitGroup";
const wchar_t* jRHI_DX12::c_planeclosestHitShaderName = L"MyPlaneClosestHitShader";
bool jRHI_DX12::IsUsePlacedResource = true;			// PlacedResouce test

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

		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

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
	check(Swapchain);

	auto Queue = CommandBufferManager->GetCommandQueue();
    check(Queue);

	if (Queue && Swapchain)
	{
		jSwapchainImage_DX12* CurrentSwapchainImage = (jSwapchainImage_DX12*)Swapchain->GetSwapchainImage(CurrentFrameIndex);
		Swapchain->Fence->SignalWithNextFenceValue(Queue.Get(), true);
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
		if (JFAIL(factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))))
			return false;

		if (JFAIL((D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)))))
			return false;
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		const int32 ResultAdapterID = GetHardwareAdapter(factory.Get(), &hardwareAdapter);
		hardwareAdapter.As(&Adapter);

        if (JFAIL(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        {
            return false;
        }
        else
        {
            DXGI_ADAPTER_DESC desc;
			Adapter->GetDesc(&desc);
            AdapterID = ResultAdapterID;
            AdapterName = desc.Description;

#ifdef _DEBUG
            wchar_t buff[256] = {};
            swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", AdapterID, desc.VendorId, desc.DeviceId, desc.Description);
            OutputDebugStringW(buff);
#endif
        }
	}

    //////////////////////////////////////////////////////////////////////////
    // PlacedResouce test
	{
		D3D12_HEAP_DESC heapDesc;
		heapDesc.SizeInBytes = DefaultPlacedResourceHeapSize;
		heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.CreationNodeMask = 1;
		heapDesc.Properties.VisibleNodeMask = 1;
		heapDesc.Alignment = 0;
		heapDesc.Flags = D3D12_HEAP_FLAG_NONE;
		if (JFAIL(Device->CreateHeap(&heapDesc, IID_PPV_ARGS(&PlacedResourceDefaultHeap))))
			return false;
	}

	{
        D3D12_HEAP_DESC heapDesc;
        heapDesc.SizeInBytes = DefaultPlacedResourceHeapSize;
        heapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapDesc.Properties.CreationNodeMask = 1;
        heapDesc.Properties.VisibleNodeMask = 1;
        heapDesc.Alignment = 0;
        heapDesc.Flags = D3D12_HEAP_FLAG_NONE;
        if (JFAIL(Device->CreateHeap(&heapDesc, IID_PPV_ARGS(&PlacedResourceUploadHeap))))
            return false;
	}
	//////////////////////////////////////////////////////////////////////////

	// 2. Command
	CommandBufferManager = new jCommandBufferManager_DX12();
	CommandBufferManager->Initialize(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	CopyCommandBufferManager = new jCommandBufferManager_DX12();
	CopyCommandBufferManager->Initialize(Device, D3D12_COMMAND_LIST_TYPE_COPY);

    // 4. Heap	
	RTVDescriptorHeaps.Initialize(EDescriptorHeapTypeDX12::RTV);
	DSVDescriptorHeaps.Initialize(EDescriptorHeapTypeDX12::DSV);
	DescriptorHeaps.Initialize(EDescriptorHeapTypeDX12::CBV_SRV_UAV);
	SamplerDescriptorHeaps.Initialize(EDescriptorHeapTypeDX12::SAMPLER);

	OnlineDescriptorHeapBlocks.Initialize(EDescriptorHeapTypeDX12::CBV_SRV_UAV);
	OnlineSamplerDescriptorHeapBlocks.Initialize(EDescriptorHeapTypeDX12::SAMPLER);

	// 3. Swapchain
	Swapchain = new jSwapchain_DX12();
	Swapchain->Create();

	OneFrameUniformRingBuffers.resize(Swapchain->GetNumOfSwapchain());
	for(jRingBuffer_DX12*& iter : OneFrameUniformRingBuffers)
	{
		iter = new jRingBuffer_DX12();
		// iter->Create(16 * 1024 * 1024);
		iter->Create(65536);
	}

    //////////////////////////////////////////////////////////////////////////
    // 5. Initialize Camera and lighting
    {
        auto frameIndex = Swapchain->GetCurrentBackBufferIndex();

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

	// 7. Create sync object
	WaitForGPU();

	// 8. Raytracing device and commandlist
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData{};
	if (JFAIL(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))))
		return false;

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
	XMFLOAT3 vertpos[verticesCount];
	XMFLOAT3 vertnormal[verticesCount];

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

			vertpos[cnt] = vertices[cnt].position;
			vertnormal[cnt] = vertices[cnt].normal;
        }
    }

    // top
    {
        vertices[cnt].position = XMFLOAT3(0.0f, radius, 0.0f);
        Vector normal = Vector(0.0f, radius, 0.0f).GetNormalize();
        vertices[cnt].normal = XMFLOAT3(normal.x, normal.y, normal.z);

        vertpos[cnt] = vertices[cnt].position;
        vertnormal[cnt] = vertices[cnt].normal;
        ++cnt;
    }

    // down
    {    
        vertices[cnt].position = XMFLOAT3(0.0f, -radius, 0.0f);
        Vector normal = Vector(0.0f, -radius, 0.0f).GetNormalize();
        vertices[cnt].normal = XMFLOAT3(normal.x, normal.y, normal.z);

        vertpos[cnt] = vertices[cnt].position;
        vertnormal[cnt] = vertices[cnt].normal;
        ++cnt;
    }

	VertexBuffer = new jVertexBuffer_DX12();
	std::shared_ptr<jVertexStreamData> vertexStreamData = std::make_shared<jVertexStreamData>();
	{
		{
			auto streamParam = std::make_shared<jStreamParam<float>>();
			streamParam->BufferType = EBufferType::STATIC;
			streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
			streamParam->Stride = sizeof(float) * 3;
			streamParam->Name = jName("POSITION");
			streamParam->Data.resize(verticesCount * 3);
			memcpy(&streamParam->Data[0], vertpos, sizeof(vertpos));
			vertexStreamData->Params.push_back(streamParam);
		}

        {
            auto streamParam = std::make_shared<jStreamParam<float>>();
            streamParam->BufferType = EBufferType::STATIC;
            streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
            streamParam->Stride = sizeof(float) * 3;
            streamParam->Name = jName("NORMAL");
            streamParam->Data.resize(verticesCount * 3);
            memcpy(&streamParam->Data[0], vertnormal, sizeof(vertnormal));
            vertexStreamData->Params.push_back(streamParam);
        }
	}
	VertexBuffer->Initialize(vertexStreamData);
	
	IndexBuffer = new jIndexBuffer_DX12();
    auto indexStreamData = std::make_shared<jIndexStreamData>();
	{
        indexStreamData->ElementCount = _countof(indices);
         
		auto streamParam = new jStreamParam<uint32>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(uint32)));
        streamParam->Stride = sizeof(uint32) * 3;
        streamParam->Name = jName("Index");
        streamParam->Data.resize(_countof(indices));
        memcpy(&streamParam->Data[0], &indices[0], sizeof(indices));
        indexStreamData->Param = streamParam;
	}
	IndexBuffer->Initialize(indexStreamData);

	// 버택스 버퍼와 인덱스 버퍼는 디스크립터 테이블로 쉐이더로 전달됨
	// 디스크립터 힙에서 버택스 버퍼 디스크립터는 인덱스 버퍼 디스크립터에 바로다음에 있어야 함

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
	SimpleTexture[0] = (jTexture_DX12*)jImageFileLoader::GetInstance().LoadTextureFromFile(jName("Image/eye.png"), true).lock().get();
	SimpleTexture[1] = (jTexture_DX12*)jImageFileLoader::GetInstance().LoadTextureFromFile(jName("Image/bulb.png"), true).lock().get();
	SimpleTexture[2] = (jTexture_DX12*)jImageFileLoader::GetInstance().LoadTextureFromFile(jName("Image/sun.png"), true).lock().get();

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

        jCommandBuffer_DX12* commandList = g_rhi_dx12->BeginSingleTimeCopyCommands();
        jBufferUtil_DX12::CopyBufferToImage(commandList->Get(), buffer->Buffer.Get(), 0, SimpleTextureCube->Image.Get(), 6, 0);

        g_rhi_dx12->EndSingleTimeCopyCommands(commandList);
        delete buffer;

		jBufferUtil_DX12::CreateShaderResourceView(SimpleTextureCube);
	}

	WaitForGPU();

	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};

    SimpleUniformBuffer = new jUniformBufferBlock_DX12(jNameStatic("SimpleUniform"), jLifeTimeType::OneFrame);
	SimpleUniformBuffer->Init(sizeof(ConstantBuffer));	
	SimpleUniformBuffer->UpdateBufferData(&ConstantBuffer, sizeof(ConstantBuffer));

    {
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        // cbv
        ShaderBindingArray.Add(-1, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jUniformBufferResource>(SimpleUniformBuffer), true);

        //// structured buffer
        //ShaderBindingArray.Add(-1, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
        //    , ResourceInlineAllactor.Alloc<jBufferResource>(SimpleStructuredBuffer), true);

        // Descriptor 0 (1 개, BaseRegister, 이전에 들어간것들을 기반으로 자동으로 올려야 함.)
        ShaderBindingArray.Add(-1, 1, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jTextureResource>(SimpleTextureCube, nullptr));

        //// Descriptor 1 (3 개, BaseRegister)
        //ShaderBindingArray.Add(-1, 3, EShaderBindingType::TEXTURE_ARRAY_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
        //    , ResourceInlineAllactor.Alloc<jTextureArrayResource>((const jTexture**)SimpleTexture, 3));

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT>::Create();
        ShaderBindingArray.Add(-1, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jSamplerResource>(SamplerState));

        auto TestShaderBindingLayout = (jShaderBindingsLayout_DX12*)g_rhi->CreateShaderBindings(ShaderBindingArray);
		TestShaderBindingInstance = (jShaderBindingInstance_DX12*)TestShaderBindingLayout->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
    }

	{
        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

		// structured buffer
        ShaderBindingArray.Add(-1, 1, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jBufferResource>(SimpleStructuredBuffer), false);

        // Descriptor 1 (3 개, BaseRegister)
        ShaderBindingArray.Add(-1, 3, EShaderBindingType::TEXTURE_ARRAY_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jTextureArrayResource>((const jTexture**)SimpleTexture, 3));

        const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::NEAREST, ETextureFilter::NEAREST
            , ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT, ETextureAddressMode::REPEAT>::Create();
        ShaderBindingArray.Add(-1, 1, EShaderBindingType::SAMPLER, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jSamplerResource>(SamplerState));

		auto TestShaderBindingLayout2 = (jShaderBindingsLayout_DX12*)g_rhi->CreateShaderBindings(ShaderBindingArray);
		TestShaderBindingInstance2 = (jShaderBindingInstance_DX12*)TestShaderBindingLayout2->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
	}
    
	
	jRasterizationStateInfo_DX12* RasterizationStateInfo = (jRasterizationStateInfo_DX12*)TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, true, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
    jBlendingStateInfo_DX12* BlendStateInfo = (jBlendingStateInfo_DX12*)TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD
        , EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jDepthStencilStateInfo_DX12* DepthStencilState = (jDepthStencilStateInfo_DX12*)TDepthStencilStateInfo<true, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();


	jPipelineStateFixedInfo FixedInfo(RasterizationStateInfo, DepthStencilState, BlendStateInfo
		, jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), false);
	{
		{
			jShaderInfo shaderInfo(jNameStatic("TestVS"), jNameStatic("Resource/Shaders/hlsl/DXSampleHelloTriangle.hlsl")
				, jNameStatic(""), jNameStatic("VSMain"), EShaderAccessStageFlag::VERTEX);
			GraphicsPipelineShader.VertexShader = new jShader(shaderInfo);
			GraphicsPipelineShader.VertexShader->Initialize();
		}

		{
			jShaderInfo shaderInfo(jNameStatic("TestPS"), jNameStatic("Resource/Shaders/hlsl/DXSampleHelloTriangle.hlsl")
				, jNameStatic(""), jNameStatic("PSMain"), EShaderAccessStageFlag::FRAGMENT);
			GraphicsPipelineShader.PixelShader = new jShader(shaderInfo);
			GraphicsPipelineShader.PixelShader->Initialize();
		}
	}
	jVertexBufferArray VertexBufferArray;
	VertexBufferArray.Add(VertexBuffer);

	{
        jSwapchainImage* SwapchainImage = Swapchain->GetSwapchainImage(CurrentFrameIndex);
        auto SceneRT = std::make_shared<jRenderTarget>(SwapchainImage->TexturePtr);

        jRenderPassInfo renderPassInfo;
        {
            const Vector4 clearColor = { 0.0f, 0.2f, 0.4f, 1.0f };
            const Vector2 clearDepth = { 1.0f, 0.0f };

            jAttachment color = jAttachment(SceneRT, EAttachmentLoadStoreOp::CLEAR_STORE
                , EAttachmentLoadStoreOp::DONTCARE_DONTCARE, clearColor, clearDepth
                , EImageLayout::UNDEFINED, EImageLayout::COLOR_ATTACHMENT);
            renderPassInfo.Attachments.push_back(color);
        }
        RenderPass = (jRenderPass_DX12*)g_rhi_dx12->GetOrCreateRenderPass(
            renderPassInfo, Vector2i(0, 0), Vector2i(SwapchainImage->TexturePtr->Width, SwapchainImage->TexturePtr->Height));

	}

	jShaderBindingsLayoutArray ShaderBindingsLayoutArray;
	ShaderBindingsLayoutArray.Add(TestShaderBindingInstance->ShaderBindingsLayouts);
	ShaderBindingsLayoutArray.Add(TestShaderBindingInstance2->ShaderBindingsLayouts);

	jPushConstant* PushConstant = nullptr;

	PipelineStateInfo = (jPipelineStateInfo_DX12*)g_rhi->CreatePipelineStateInfo(
		&FixedInfo, GraphicsPipelineShader, VertexBufferArray, RenderPass, ShaderBindingsLayoutArray, nullptr, 0);

	//////////////////////////////////////////////////////////////////////////
    InitializeImGui();

	ShowWindow(m_hWnd, SW_SHOW);

    return true;
}

bool jRHI_DX12::Run()
{
    MSG msg = {};
	while (::IsWindow(m_hWnd))
	{
        if (g_rhi_dx12)
        {
            g_rhi_dx12->Update();
            g_rhi_dx12->Render();
        }

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
	}

    return true;
}

void jRHI_DX12::Release()
{
	WaitForGPU();
    
	if (CommandBufferManager)
		CommandBufferManager->Release();

	if (CopyCommandBufferManager)
		CopyCommandBufferManager->Release();

    ReleaseImGui();

	//////////////////////////////////////////////////////////////////////////
	// 14. Raytracing Output Resouce
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;

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
	for (int32 i = 0; i < _countof(SimpleTexture); ++i)
	{
		delete SimpleTexture[i];
		SimpleTexture[i] = nullptr;
	}

	SamplerStatePool.Release();
    
	{
        jScopeWriteLock s(&ShaderBindingPoolLock);
        for (auto& iter : ShaderBindingPool)
            delete iter.second;
        ShaderBindingPool.clear();
    }


	//////////////////////////////////////////////////////////////////////////
	// 8. Raytracing device and commandlist
	Device.Reset();

	//////////////////////////////////////////////////////////////////////////
	// 4. Heap
	RTVDescriptorHeaps.Release();
	DSVDescriptorHeaps.Release();
	DescriptorHeaps.Release();
	SamplerDescriptorHeaps.Release();

	OnlineDescriptorHeapBlocks.Release();
	OnlineSamplerDescriptorHeapBlocks.Release();

	//////////////////////////////////////////////////////////////////////////
	// 3. Swapchain
    Swapchain->Release();
    delete Swapchain;

	//////////////////////////////////////////////////////////////////////////
	// 2. Command
	delete CommandBufferManager;

	//////////////////////////////////////////////////////////////////////////
	// 1. Device
	Device.Reset();

}

void jRHI_DX12::UpdateCameraMatrices()
{
	auto frameIndex = Swapchain->GetCurrentBackBufferIndex();

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
            << L"    fps: " << fps 
			//<< L"     ~Million Primary Rays/s: " << MRaysPerSecond
            << L"    GPU[" << AdapterID << L"]: " << AdapterName;

        //////////////////////////////////////////////////////////////////////////
		// PlacedResouce test
		{
            DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo;
            Adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo);

            WCHAR usageString[20];
            const UINT64 mb = 1 << 20;
            const UINT64 kb = 1 << 10;
            if (memoryInfo.CurrentUsage > mb)
            {
                swprintf_s(usageString, L"%.1f MB", static_cast<float>(memoryInfo.CurrentUsage) / mb);
            }
            else if (memoryInfo.CurrentUsage > kb)
            {
                swprintf_s(usageString, L"%.1f KB", static_cast<float>(memoryInfo.CurrentUsage) / kb);
            }
            else
            {
                swprintf_s(usageString, L"%I64d B", memoryInfo.CurrentUsage);
            }

			windowText << L" - " << (IsUsePlacedResource ? L"[Placed]" : L"[Committed]") << L" Memory Used : " << usageString;
		}
		//////////////////////////////////////////////////////////////////////////

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
        auto frameIndex = Swapchain->GetCurrentBackBufferIndex();

        m_sceneCB[frameIndex].focalDistance = m_focalDistance;
        m_sceneCB[frameIndex].lensRadius = m_lensRadius;
    }
}

void jRHI_DX12::Render()
{
	GetOneFrameUniformRingBuffer()->Reset();

	if (std::shared_ptr<jRenderFrameContext> renderFrameContext = g_rhi->BeginRenderFrame())
	{
		// Prepare
		jCommandBuffer_DX12* CommandBuffer = (jCommandBuffer_DX12*)renderFrameContext->GetActiveCommandBuffer();
		auto GraphicsCommandList = CommandBuffer->Get();

		jSwapchainImage* SwapchainImage = Swapchain->GetSwapchainImage(CurrentFrameIndex);
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

		{
			jSimpleConstantBuffer ConstantBuffer;
			ConstantBuffer.M.SetIdentity();
			Matrix Projection = jCameraUtil::CreatePerspectiveMatrix((float)SCR_WIDTH, (float)SCR_HEIGHT, DegreeToRadian(90.0f), 0.01f, 1000.0f);
			Matrix Camera = jCameraUtil::CreateViewMatrix(Vector::FowardVector * 1.2f, Vector::ZeroVector, Vector::UpVector);
			ConstantBuffer.M = Projection * Camera * ConstantBuffer.M;
			ConstantBuffer.M.SetTranspose();

			// Dynamic indexing with constantbuffer index
			static DWORD OldTick = GetTickCount();
			static int32 TexIndex = 0;
			DWORD time = GetTickCount() - OldTick;
			if (time > 2000)
			{
				OldTick = GetTickCount();
				TexIndex = (TexIndex + 1) % _countof(SimpleTexture);
			}
			ConstantBuffer.TexIndex = TexIndex;
			SimpleUniformBuffer->UpdateBufferData(&ConstantBuffer, sizeof(ConstantBuffer));
		}

		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				SwapchainRT->Image.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			GraphicsCommandList->ResourceBarrier(1, &barrier);
		}

		RenderPass->BeginRenderPass(CommandBuffer, SwapchainRT->RTV.CPUHandle);

		PipelineStateInfo->Bind(CommandBuffer);

		//////////////////////////////////////////////////////////////////////////
		// Graphics ShaderBinding

		// 이 부분은 구조화 하게 되면, 이전에 만들어둔 것을 그대로 사용
		jShaderBindingInstanceArray ShaderBindingInstanceArray;
		ShaderBindingInstanceArray.Add(TestShaderBindingInstance);
		ShaderBindingInstanceArray.Add(TestShaderBindingInstance2);
		GraphicsCommandList->SetGraphicsRootSignature(jShaderBindingsLayout_DX12::CreateRootSignature(ShaderBindingInstanceArray));

		int32 RootParameterIndex = 0;
		bool HasDescriptor = false;
		bool HasSamplerDescriptor = false;
		for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
		{
			jShaderBindingInstance_DX12* Instance = (jShaderBindingInstance_DX12*)ShaderBindingInstanceArray[i];
			jShaderBindingsLayout_DX12* Layout = (jShaderBindingsLayout_DX12*)(Instance->ShaderBindingsLayouts);

			Instance->CopyToOnlineDescriptorHeap(CommandBuffer);
			HasDescriptor |= Instance->Descriptors.size() > 0;
			HasSamplerDescriptor |= Instance->SamplerDescriptors.size() > 0;

			Instance->BindGraphics(CommandBuffer, RootParameterIndex);
		}
		// DescriptorTable 은 항상 마지막에 바인딩
		if (HasDescriptor)
			GraphicsCommandList->SetGraphicsRootDescriptorTable(RootParameterIndex++, CommandBuffer->OnlineDescriptorHeap->GetGPUHandle());		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

		if (HasSamplerDescriptor)
			GraphicsCommandList->SetGraphicsRootDescriptorTable(RootParameterIndex++, CommandBuffer->OnlineSamplerDescriptorHeap->GetGPUHandle());	// SamplerState test
		//////////////////////////////////////////////////////////////////////////

		VertexBuffer->Bind(CommandBuffer);
		IndexBuffer->Bind(CommandBuffer);
		GraphicsCommandList->DrawIndexedInstanced(IndexBuffer->GetIndexCount(), 1, 0, 0, 0);

		RenderPass->EndRenderPass();

		RenderUI(GraphicsCommandList, SwapchainRT->Image.Get(), SwapchainRT->RTV.CPUHandle, m_imgui_SrvDescHeap.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        g_rhi->EndRenderFrame(renderFrameContext);
	}

    HRESULT hr = S_OK;
    if (Options & c_AllowTearing)
    {
        hr = Swapchain->SwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    }
    else
    {
        // 첫번째 아규먼트는 VSync가 될때까지 기다림, 어플리케이션은 다음 VSync까지 잠재운다.
        // 이렇게 하는 것은 렌더링 한 프레임 중 화면에 나오지 못하는 프레임의 cycle을 아끼기 위해서임.
        hr = Swapchain->SwapChain->Present(1, 0);
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

		CurrentFrameIndex = Swapchain->GetCurrentBackBufferIndex();
	}

	// 임시로 여기에 만들어 둠.
	g_rhi->IncrementFrameNumber();
}


void jRHI_DX12::OnDeviceLost()
{
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;

	Device.Reset();
	
	delete VertexBuffer;
	VertexBuffer = nullptr;

	delete IndexBuffer;
	IndexBuffer = nullptr;
}

void jRHI_DX12::OnDeviceRestored()
{
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

    if (ensure(Swapchain && Swapchain->SwapChain))
    {
        HRESULT hr = Swapchain->SwapChain->ResizeBuffers(MaxFrameCount, InWidth, InHeight, BackbufferFormat
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
		jSwapchainImage* SwapchainImage = Swapchain->GetSwapchainImage(i);
		jTexture_DX12* SwapchainRT = (jTexture_DX12*)SwapchainImage->TexturePtr.get();
        if (JFAIL(Swapchain->SwapChain->GetBuffer(i, IID_PPV_ARGS(&SwapchainRT->Image))))
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

    CurrentFrameIndex = Swapchain->GetCurrentBackBufferIndex();

	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;
    
    RayTacingOutputTexture = jBufferUtil_DX12::CreateImage(SCR_WIDTH, SCR_HEIGHT, 1, 1, 1, ETextureType::TEXTURE_2D, ETextureFormat::RGBA8, false, true);
    if (!ensure(RayTacingOutputTexture))
        return false;

    jBufferUtil_DX12::CreateUnorderedAccessView(RayTacingOutputTexture);

    return true;
}

bool jRHI_DX12::OnHandleDeviceLost()
{
	delete RayTacingOutputTexture;
	RayTacingOutputTexture = nullptr;

	Device.Reset();

    delete VertexBuffer;
    VertexBuffer = nullptr;

	delete IndexBuffer;
	IndexBuffer = nullptr;

    return true;
}

bool jRHI_DX12::OnHandleDeviceRestored()
{
    InitRHI();
    return true;
}

jCommandBuffer_DX12* jRHI_DX12::BeginSingleTimeCopyCommands()
{
	check(CopyCommandBufferManager);
	return CopyCommandBufferManager->GetOrCreateCommandBuffer();
}

void jRHI_DX12::EndSingleTimeCopyCommands(jCommandBuffer_DX12* commandBuffer)
{
	check(CopyCommandBufferManager);
	CopyCommandBufferManager->ExecuteCommandList(commandBuffer, true);
	CopyCommandBufferManager->ReturnCommandBuffer(commandBuffer);
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

	jCommandBuffer_DX12* commandList = g_rhi_dx12->BeginSingleTimeCopyCommands();
	jBufferUtil_DX12::CopyBufferToImage(commandList->Get(), buffer->Buffer.Get(), 0, Texture->Image.Get());
	g_rhi_dx12->EndSingleTimeCopyCommands(commandList);
	delete buffer;
	
	// Create SRV
	jBufferUtil_DX12::CreateShaderResourceView(Texture);

	return Texture;
}

jShaderBindingsLayout* jRHI_DX12::CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const
{
    size_t hash = InShaderBindingArray.GetHash();

    {
        jScopeReadLock sr(&ShaderBindingPoolLock);

        auto it_find = ShaderBindingPool.find(hash);
        if (ShaderBindingPool.end() != it_find)
            return it_find->second;
    }

    {
        jScopeWriteLock sw(&ShaderBindingPoolLock);

        // Try again, to avoid entering creation section simultanteously.
        auto it_find = ShaderBindingPool.find(hash);
        if (ShaderBindingPool.end() != it_find)
            return it_find->second;

        auto NewShaderBinding = new jShaderBindingsLayout_DX12();
        NewShaderBinding->Initialize(InShaderBindingArray);
        NewShaderBinding->Hash = hash;
        ShaderBindingPool[hash] = NewShaderBinding;

        return NewShaderBinding;
    }
}

jSamplerStateInfo* jRHI_DX12::CreateSamplerState(const jSamplerStateInfo& initializer) const
{
	return SamplerStatePool.GetOrCreate(initializer);
}

jRasterizationStateInfo* jRHI_DX12::CreateRasterizationState(const jRasterizationStateInfo& initializer) const
{
	return RasterizationStatePool.GetOrCreate(initializer);
}

jStencilOpStateInfo* jRHI_DX12::CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const
{
	return StencilOpStatePool.GetOrCreate(initializer);
}

jDepthStencilStateInfo* jRHI_DX12::CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const
{
	return DepthStencilStatePool.GetOrCreate(initializer);
}

jBlendingStateInfo* jRHI_DX12::CreateBlendingState(const jBlendingStateInfo& initializer) const
{
	return BlendingStatePool.GetOrCreate(initializer);
}

bool jRHI_DX12::CreateShaderInternal(jShader* OutShader, const jShaderInfo& shaderInfo) const
{
    jShader* shader_dx12 = OutShader;
    check(shader_dx12->GetPermutationCount());
    {
        check(!shader_dx12->CompiledShader);
        jCompiledShader_DX12* CurCompiledShader = new jCompiledShader_DX12();
        shader_dx12->CompiledShader = CurCompiledShader;

        // PermutationId 를 설정하여 컴파일을 준비함
        shader_dx12->SetPermutationId(shaderInfo.GetPermutationId());

        std::string PermutationDefines;
        shader_dx12->GetPermutationDefines(PermutationDefines);

		const wchar_t* ShadingModel = nullptr;
        switch (shaderInfo.GetShaderType())
        {
        case EShaderAccessStageFlag::VERTEX:
			ShadingModel = TEXT("vs_6_3");
            break;
        case EShaderAccessStageFlag::GEOMETRY:
			ShadingModel = TEXT("gs_6_3");
            break;
        case EShaderAccessStageFlag::FRAGMENT:
			ShadingModel = TEXT("ps_6_3");
            break;
        case EShaderAccessStageFlag::COMPUTE:
			ShadingModel = TEXT("cs_6_3");
            break;
        default:
            check(0);
            break;
        }

		{
            const bool isHLSL = !!strstr(shaderInfo.GetShaderFilepath().ToStr(), ".hlsl");

            jFile ShaderFile;
            ShaderFile.OpenFile(shaderInfo.GetShaderFilepath().ToStr(), FileType::TEXT, ReadWriteType::READ);
            ShaderFile.ReadFileToBuffer(false);
            std::string ShaderText;

            if (shaderInfo.GetPreProcessors().GetStringLength() > 0)
            {
                ShaderText += shaderInfo.GetPreProcessors().ToStr();
                ShaderText += "\r\n";
            }
            ShaderText += PermutationDefines;
            ShaderText += "\r\n";

            ShaderText += ShaderFile.GetBuffer();

            // Find relative file path
            constexpr char includePrefixString[] = "#include \"";
            constexpr int32 includePrefixLength = sizeof(includePrefixString) - 1;

            const std::filesystem::path shaderFilePath(shaderInfo.GetShaderFilepath().ToStr());
            const std::string includeShaderPath = shaderFilePath.has_parent_path() ? (shaderFilePath.parent_path().string() + "/") : "";

            std::set<std::string> AlreadyIncludedSets;
            while (1)
            {
                size_t startOfInclude = ShaderText.find(includePrefixString);
                if (startOfInclude == std::string::npos)
                    break;

                // Parse include file path
                startOfInclude += includePrefixLength;
                size_t endOfInclude = ShaderText.find("\"", startOfInclude);
                std::string includeFilepath = includeShaderPath + ShaderText.substr(startOfInclude, endOfInclude - startOfInclude);

                // Replace range '#include "filepath"' with shader file content
                const size_t ReplaceStartPos = startOfInclude - includePrefixLength;
                const size_t ReplaceCount = endOfInclude - ReplaceStartPos + 1;

                if (AlreadyIncludedSets.contains(includeFilepath))
                {
                    ShaderText.replace(ReplaceStartPos, ReplaceCount, "");
                    continue;
                }

                // If already included file, skip it.
                AlreadyIncludedSets.insert(includeFilepath);

                // Load include shader file
                jFile IncludeShaderFile;
                IncludeShaderFile.OpenFile(includeFilepath.c_str(), FileType::TEXT, ReadWriteType::READ);
                IncludeShaderFile.ReadFileToBuffer(false);
                ShaderText.replace(ReplaceStartPos, ReplaceCount, IncludeShaderFile.GetBuffer());
                IncludeShaderFile.CloseFile();
            }

            wchar_t EntryPoint[128] = { 0, };
			{
				const int32 EntryPointLength = MultiByteToWideChar(CP_ACP, 0, shaderInfo.GetEntryPoint().ToStr(), -1, NULL, NULL);
				check(EntryPointLength < 128);

				MultiByteToWideChar(CP_ACP, 0, shaderInfo.GetEntryPoint().ToStr()
					, (int32)shaderInfo.GetEntryPoint().GetStringLength(), EntryPoint, (int32)(_countof(EntryPoint) - 1));
				EntryPoint[_countof(EntryPoint) - 1] = 0;
			}
			
			CurCompiledShader->ShaderBlob = jShaderCompiler_DX12::Get().Compile(ShaderText.c_str(), (uint32)ShaderText.size()
				, ShadingModel, EntryPoint);
        }
    }
    shader_dx12->ShaderInfo = shaderInfo;

    return true;
}

jRenderPass* jRHI_DX12::GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_DX12(colorAttachments, offset, extent));
}

jRenderPass* jRHI_DX12::GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
	, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_DX12(colorAttachments, depthAttachment, offset, extent));
}

jRenderPass* jRHI_DX12::GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
	, const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_DX12(colorAttachments, depthAttachment, colorResolveAttachment, offset, extent));
}

jRenderPass* jRHI_DX12::GetOrCreateRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_DX12(renderPassInfo, offset, extent));
}

jPipelineStateInfo* jRHI_DX12::CreatePipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader
	, const jVertexBufferArray& InVertexBufferArray, const jRenderPass* InRenderPass, const jShaderBindingsLayoutArray& InShaderBindingArray
	, const jPushConstant* InPushConstant, int32 InSubpassIndex) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(InPipelineStateFixed, InShader, InVertexBufferArray
		, InRenderPass, InShaderBindingArray, InPushConstant, InSubpassIndex)));
}

jPipelineStateInfo* jRHI_DX12::CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingsLayoutArray& InShaderBindingArray
	, const jPushConstant* pushConstant) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(shader, InShaderBindingArray, pushConstant)));
}

std::shared_ptr<jRenderFrameContext> jRHI_DX12::BeginRenderFrame()
{
	SCOPE_CPU_PROFILE(BeginRenderFrame);

	//////////////////////////////////////////////////////////////////////////
	// Acquire new swapchain image
	jSwapchainImage_DX12* CurrentSwapchainImage = (jSwapchainImage_DX12*)Swapchain->GetSwapchainImage(CurrentFrameIndex);
	CurrentSwapchainImage->CommandBufferFence->WaitForFence();
	//////////////////////////////////////////////////////////////////////////

	jCommandBuffer_DX12* commandBuffer = (jCommandBuffer_DX12*)g_rhi_dx12->CommandBufferManager->GetOrCreateCommandBuffer();

    auto renderFrameContextPtr = std::make_shared<jRenderFrameContext_DX12>(commandBuffer);
    renderFrameContextPtr->UseForwardRenderer = !gOptions.UseDeferredRenderer;
    renderFrameContextPtr->FrameIndex = CurrentFrameIndex;
    renderFrameContextPtr->SceneRenderTarget = new jSceneRenderTarget();
    renderFrameContextPtr->SceneRenderTarget->Create(Swapchain->GetSwapchainImage(CurrentFrameIndex));

	return renderFrameContextPtr;
}

void jRHI_DX12::EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr)
{
	SCOPE_CPU_PROFILE(EndRenderFrame);

	jCommandBuffer_DX12* CommandBuffer = (jCommandBuffer_DX12*)renderFrameContextPtr->GetActiveCommandBuffer();
	CommandBufferManager->ExecuteCommandList(CommandBuffer);
	CommandBufferManager->ReturnCommandBuffer(CommandBuffer);

    jSwapchainImage_DX12* CurrentSwapchainImage = (jSwapchainImage_DX12*)Swapchain->GetSwapchainImage(CurrentFrameIndex);
	CurrentSwapchainImage->CommandBufferFence = CommandBuffer->Fence;
    CurrentSwapchainImage->CommandBufferFence->SignalWithNextFenceValue(CommandBufferManager->GetCommandQueue().Get());

	CurrentFrameIndex = (CurrentFrameIndex + 1) % Swapchain->Images.size();
	renderFrameContextPtr->Destroy();
}