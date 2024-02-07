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
#include "jShaderBindingLayout_DX12.h"
#include "jVertexBuffer_DX12.h"
#include "jIndexBuffer_DX12.h"
#include "jShader_DX12.h"
#include "FileLoader/jFile.h"
#include "jPipelineStateInfo_DX12.h"
#include "Renderer/jSceneRenderTargets.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"
#include "../jRenderFrameContext.h"
#include "jRenderFrameContext_DX12.h"
#include "jPrimitiveUtil.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jLight.h"
#include "jQueryPoolTime_DX12.h"
#include "jImGui_DX12.h"
#include "jEngine.h"
#include "jRaytracingScene_DX12.h"
#include "jResourceBarrierBatcher_DX12.h"

#define DX12_ENABLE_DEBUG_LAYER 0

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
robin_hood::unordered_map<size_t, jShaderBindingLayout*> jRHI_DX12::ShaderBindingPool;
TResourcePool<jSamplerStateInfo_DX12, jMutexRWLock> jRHI_DX12::SamplerStatePool;
TResourcePool<jRasterizationStateInfo_DX12, jMutexRWLock> jRHI_DX12::RasterizationStatePool;
TResourcePool<jStencilOpStateInfo_DX12, jMutexRWLock> jRHI_DX12::StencilOpStatePool;
TResourcePool<jDepthStencilStateInfo_DX12, jMutexRWLock> jRHI_DX12::DepthStencilStatePool;
TResourcePool<jBlendingStateInfo_DX12, jMutexRWLock> jRHI_DX12::BlendingStatePool;
TResourcePool<jPipelineStateInfo_DX12, jMutexRWLock> jRHI_DX12::PipelineStatePool;
TResourcePool<jRenderPass_DX12, jMutexRWLock> jRHI_DX12::RenderPassPool;

inline float random_double() 
{
    // Returns a random real in [0,1).
    return rand() / (RAND_MAX + 1.0f);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

	switch (message)
	{
	case WM_CREATE:
	{
		// Save the DXSample* passed in to CreateWindow.
		LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        return 0;
    }
	case WM_KEYDOWN:
    {
        if (!ImGui::IsAnyItemActive())
            g_KeyState[(int32)wParam] = true;
        return 0;
    }
	case WM_KEYUP:
    {
        g_KeyState[(int32)wParam] = false;
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        EMouseButtonType buttonType;
        if (WM_RBUTTONDOWN == message)
            buttonType = EMouseButtonType::RIGHT;
        else if (WM_LBUTTONDOWN == message)
            buttonType = EMouseButtonType::LEFT;
        else if (WM_MBUTTONDOWN == message)
            buttonType = EMouseButtonType::MIDDLE;
        else
            return 0;

        bool buttonDown = false;
        const bool IsCapturedButtonInputOnUI = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        if (!IsCapturedButtonInputOnUI)
        {
            buttonDown = true;
        }
        g_MouseState[buttonType] = buttonDown;
        g_Engine->OnMouseButton();
        
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
        EMouseButtonType buttonType;
        if (WM_RBUTTONUP == message)
            buttonType = EMouseButtonType::RIGHT;
        else if (WM_LBUTTONUP == message)
            buttonType = EMouseButtonType::LEFT;
        else if (WM_MBUTTONUP == message)
            buttonType = EMouseButtonType::MIDDLE;
        else
            return 0;

        g_MouseState[buttonType] = false;
        g_Engine->OnMouseButton();

        return 0;
    }
    case WM_MOUSEMOVE:
    {
        const int32 x = ((int)(short)LOWORD(lParam));
        const int32 y = ((int)(short)HIWORD(lParam));

        static int32 xOld = -1;
        static int32 yOld = -1;

        if (-1 == xOld)
            xOld = x;

        if (-1 == yOld)
            yOld = y;

        g_Engine->OnMouseMove((int32)(x - xOld), (int32)(y - yOld));

        xOld = x;
        yOld = y;

        return 0;
    }
    case WM_KILLFOCUS:
    {
        // 포커스를 잃은 경우 입력 상태를 모두 릴리즈 시킴.
        g_KeyState.clear();
        g_MouseState.clear();
        return 0;
    }
    case WM_SIZE:
    {
        if (g_Engine)
        {
            RECT clientRect = {};
            GetClientRect(hWnd, &clientRect);

            uint32 width = clientRect.right - clientRect.left;
            uint32 height = clientRect.bottom - clientRect.top;
            // const bool isSizeMininized = wParam == SIZE_MINIMIZED;

            g_Engine->Resize(width, height);
        }
    }
    return 0;
    case WM_ENTERSIZEMOVE:
        break;
    case WM_EXITSIZEMOVE:
        break;
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

void jRHI_DX12::WaitForGPU() const
{
	check(Swapchain);

	auto Queue = CommandBufferManager->GetCommandQueue();
    check(Queue);

    if (CommandBufferManager && CommandBufferManager->Fence)
        CommandBufferManager->Fence->SignalWithNextFenceValue(Queue.Get(), true);
}

bool jRHI_DX12::InitRHI()
{
	g_rhi = this;

    m_hWnd = CreateMainWindow();

	// 1. Device
	uint32 dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// 디버그 레이어 켬 ("optional feature" 그래픽 툴을 요구함)
	// 주의 : 디버그 레이어를 device 생성 후에 하면 활성화된 device를 무효화 됨.
    if (gOptions.EnableDebuggerLayer)
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
            GRHISupportVsync = false;
        }
        else
        {
            GRHISupportVsync = true;
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

    PlacedResourcePool.Init();
    DeallocatorMultiFrameStandaloneResource.FreeDelegate = [](ComPtr<ID3D12Resource> InData)
    {
        InData.Reset();
    };

    //////////////////////////////////////////////////////////////////////////
    // PlacedResouce test
	{
		D3D12_HEAP_DESC heapDesc;
		heapDesc.SizeInBytes = GDefaultPlacedResourceHeapSize;
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
        heapDesc.SizeInBytes = GDefaultPlacedResourceHeapSize;
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

    BarrierBatcher = new jResourceBarrierBatcher_DX12();

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

	// 3. Swapchain
	Swapchain = new jSwapchain_DX12();
	Swapchain->Create();
    CurrentFrameIndex = Swapchain->GetCurrentBackBufferIndex();

	OneFrameUniformRingBuffers.resize(Swapchain->GetNumOfSwapchain());
	for(jRingBuffer_DX12*& iter : OneFrameUniformRingBuffers)
	{
		iter = new jRingBuffer_DX12();
		iter->Create(65536);
	}

	// 7. Create sync object
	WaitForGPU();

	// 8. Raytracing device and commandlist
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData{};
	if (JFAIL(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))))
		return false;

#if SUPPORT_RAYTRACING
    check(featureSupportData.RaytracingTier > D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
    if (featureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
        return false;
#endif

    // Create InstanceVertexBuffer for cube map six face
    // todo : It will be removed, when I have a system that can generate per instance data for visible faces
    {
        struct jFaceInstanceData
        {
            uint32 LayerIndex = 0;
        };
        jFaceInstanceData instanceData[6];
        for (int32 i = 0; i < _countof(instanceData); ++i)
        {
            instanceData[i].LayerIndex = i;
        }

        auto streamParam = std::make_shared<jStreamParam<jFaceInstanceData>>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(uint32)));
        streamParam->Stride = sizeof(jFaceInstanceData);
        //streamParam->Name = jName("LayerIndex");
        streamParam->Name = jName("BLENDINDICES");
        streamParam->Data.resize(6);
        memcpy(&streamParam->Data[0], instanceData, sizeof(instanceData));

        auto VertexStream_InstanceData = std::make_shared<jVertexStreamData>();
        VertexStream_InstanceData->ElementCount = _countof(instanceData);
        VertexStream_InstanceData->StartLocation = 1;
        VertexStream_InstanceData->BindingIndex = 1;
        VertexStream_InstanceData->VertexInputRate = EVertexInputRate::INSTANCE;
        VertexStream_InstanceData->Params.push_back(streamParam);
        CubeMapInstanceDataForSixFace = g_rhi->CreateVertexBuffer(VertexStream_InstanceData);
    }

    QueryPoolTime = new jQueryPoolTime_DX12();
    QueryPoolTime->Create();

    g_ImGUI = new jImGUI_DX12();
    g_ImGUI->Initialize((float)SCR_WIDTH, (float)SCR_HEIGHT);

	//////////////////////////////////////////////////////////////////////////

	ShowWindow(m_hWnd, SW_SHOW);

#if SUPPORT_RAYTRACING
    RaytracingScene = CreateRaytracingScene();
#endif // SUPPORT_RAYTRACING

    return true;
}

void jRHI_DX12::ReleaseRHI()
{
	WaitForGPU();
    
    jRHI::ReleaseRHI();

#if SUPPORT_RAYTRACING
    delete RaytracingScene;
    RaytracingScene = nullptr;
#endif // SUPPORT_RAYTRACING

    for(auto it : ShaderBindingPool)
    {
        delete it.second;
    }
    ShaderBindingPool.clear();

    SamplerStatePool.ReleaseAll();
    RasterizationStatePool.ReleaseAll();
    StencilOpStatePool.ReleaseAll();
    DepthStencilStatePool.ReleaseAll();
    BlendingStatePool.ReleaseAll();
    PipelineStatePool.ReleaseAll();
    RenderPassPool.ReleaseAll();

    for (jRingBuffer_DX12* iter : OneFrameUniformRingBuffers)
    {
        delete iter;
    }
    OneFrameUniformRingBuffers.clear();

    CubeMapInstanceDataForSixFace.reset();

    delete g_ImGUI;
    g_ImGUI = nullptr;
    
    delete QueryPoolTime;
    QueryPoolTime = nullptr;

    if (CommandBufferManager)
    {
        CommandBufferManager->Release();
        delete CommandBufferManager;
        CommandBufferManager = nullptr;
    }

    if (CopyCommandBufferManager)
    {
        CopyCommandBufferManager->Release();
        delete CopyCommandBufferManager;
        CopyCommandBufferManager = nullptr;
    }
    FenceManager.Release();
	SamplerStatePool.ReleaseAll();
    
    DeallocatorMultiFrameStandaloneResource.Release();
    DeallocatorMultiFramePlacedResource.Release();
    DeallocatorMultiFrameShaderBindingInstance.Release();
    PlacedResourceDefaultHeap.Reset();
    PlacedResourceUploadHeap.Reset();
    PlacedResourcePool.Release();

	{
        jScopeWriteLock s(&ShaderBindingPoolLock);
        for (auto& iter : ShaderBindingPool)
            delete iter.second;
        ShaderBindingPool.clear();
    }

	RTVDescriptorHeaps.Release();
	DSVDescriptorHeaps.Release();
	DescriptorHeaps.Release();
	SamplerDescriptorHeaps.Release();

    OnlineDescriptorHeapManager.Release();

    Swapchain->Release();
    delete Swapchain;
    Swapchain = nullptr;

    delete BarrierBatcher;

    Adapter.Reset();
    Factory.Reset();
    Device.Reset();
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

        std::wstringstream windowText;

        windowText << std::setprecision(2) << std::fixed
            << L"    fps: " << fps 
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

			windowText << L" - " << (GIsUsePlacedResource ? L"[Placed]" : L"[Committed]") << L" Memory Used : " << usageString;
		}
		//////////////////////////////////////////////////////////////////////////

        SetWindowText(m_hWnd, windowText.str().c_str());
    }
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

    Swapchain->Resize(InWidth, InHeight);
    CurrentFrameIndex = Swapchain->GetCurrentBackBufferIndex();

    return true;
}

jCommandBuffer_DX12* jRHI_DX12::BeginSingleTimeCommands() const
{
    check(CommandBufferManager);
    return CommandBufferManager->GetOrCreateCommandBuffer();
}

void jRHI_DX12::EndSingleTimeCommands(jCommandBuffer* commandBuffer) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)commandBuffer;

    check(CommandBufferManager);
    CommandBufferManager->ExecuteCommandList(CommandBuffer_DX12, true);
    CommandBufferManager->ReturnCommandBuffer(CommandBuffer_DX12);
}

jCommandBuffer_DX12* jRHI_DX12::BeginSingleTimeCopyCommands() const
{
	check(CopyCommandBufferManager);
	return CopyCommandBufferManager->GetOrCreateCommandBuffer();
}

void jRHI_DX12::EndSingleTimeCopyCommands(jCommandBuffer_DX12* commandBuffer) const
{
	check(CopyCommandBufferManager);
	CopyCommandBufferManager->ExecuteCommandList(commandBuffer, true);
	CopyCommandBufferManager->ReturnCommandBuffer(commandBuffer);
}

std::shared_ptr<jTexture> jRHI_DX12::CreateTextureFromData(const jImageData* InImageData) const
{
    check(InImageData);

    const int32 MipLevel = InImageData->MipLevel;
    const EResourceLayout Layout = EResourceLayout::GENERAL;
    
    std::shared_ptr<jTexture_DX12> TexturePtr;
    if (InImageData->TextureType == ETextureType::TEXTURE_CUBE)
    {
        TexturePtr = g_rhi->CreateCubeTexture<jTexture_DX12>(InImageData->Width, InImageData->Height, MipLevel, InImageData->Format, ETextureCreateFlag::UAV, Layout, InImageData->ImageBulkData);
    }
    else
    {
        TexturePtr = g_rhi->Create2DTexture<jTexture_DX12>(InImageData->Width, InImageData->Height, InImageData->LayerCount, MipLevel, InImageData->Format, ETextureCreateFlag::UAV, Layout, InImageData->ImageBulkData);
    }
    TexturePtr->sRGB = InImageData->sRGB;
	return TexturePtr;
}

jShaderBindingLayout* jRHI_DX12::CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const
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

        auto NewShaderBinding = new jShaderBindingLayout_DX12();
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
    std::vector<jName> IncludeFilePaths;
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
#if SUPPORT_RAYTRACING
            ShadingModel = TEXT("vs_6_6");
#else
			ShadingModel = TEXT("vs_6_5");
#endif
            break;
        case EShaderAccessStageFlag::GEOMETRY:
#if SUPPORT_RAYTRACING
            ShadingModel = TEXT("gs_6_6");
#else
            ShadingModel = TEXT("gs_6_5");
#endif
            break;
        case EShaderAccessStageFlag::FRAGMENT:
#if SUPPORT_RAYTRACING
            ShadingModel = TEXT("ps_6_6");
#else
            ShadingModel = TEXT("ps_6_5");
#endif
            break;
        case EShaderAccessStageFlag::COMPUTE:
#if SUPPORT_RAYTRACING
            ShadingModel = TEXT("cs_6_6");
#else
            ShadingModel = TEXT("cs_6_5");
#endif
            break;
        case EShaderAccessStageFlag::RAYTRACING:
        case EShaderAccessStageFlag::RAYTRACING_RAYGEN:
        case EShaderAccessStageFlag::RAYTRACING_MISS:
        case EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT:
        case EShaderAccessStageFlag::RAYTRACING_ANYHIT:
#if SUPPORT_RAYTRACING
            ShadingModel = TEXT("lib_6_6");
#else
            ShadingModel = TEXT("lib_6_5");
#endif
            break;
        default:
            check(0);
            break;
        }

		{
            const bool isHLSL = !!strstr(shaderInfo.GetShaderFilepath().ToStr(), ".hlsl");

            jFile ShaderFile;
            if (!ShaderFile.OpenFile(shaderInfo.GetShaderFilepath().ToStr(), FileType::TEXT, ReadWriteType::READ))
                return false;
            ShaderFile.ReadFileToBuffer(false);
            std::string ShaderText;

            shaderInfo.GetShaderTypeDefines(ShaderText, shaderInfo.GetShaderType());

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
                IncludeFilePaths.push_back(jName(includeFilepath.c_str()));

                // Load include shader file
                jFile IncludeShaderFile;
                if (!IncludeShaderFile.OpenFile(includeFilepath.c_str(), FileType::TEXT, ReadWriteType::READ))
                    return false;
                IncludeShaderFile.ReadFileToBuffer(false);
                ShaderText.replace(ReplaceStartPos, ReplaceCount, IncludeShaderFile.GetBuffer());
                IncludeShaderFile.CloseFile();
            }

            const std::wstring EntryPoint = ConvertToWchar(shaderInfo.GetEntryPoint());
			
			CurCompiledShader->ShaderBlob = jShaderCompiler_DX12::Get().Compile(ShaderText.c_str(), (uint32)ShaderText.size()
				, ShadingModel, EntryPoint.c_str());
            if (!CurCompiledShader->ShaderBlob)
                return false;
        }
    }
    shader_dx12->ShaderInfo = shaderInfo;
    shader_dx12->ShaderInfo.SetIncludeShaderFilePaths(IncludeFilePaths);

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
	, const jVertexBufferArray& InVertexBufferArray, const jRenderPass* InRenderPass, const jShaderBindingLayoutArray& InShaderBindingArray
	, const jPushConstant* InPushConstant, int32 InSubpassIndex) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(InPipelineStateFixed, InShader, InVertexBufferArray
		, InRenderPass, InShaderBindingArray, InPushConstant, InSubpassIndex)));
}

jPipelineStateInfo* jRHI_DX12::CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingLayoutArray& InShaderBindingArray
	, const jPushConstant* pushConstant) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(shader, InShaderBindingArray, pushConstant)));
}

jPipelineStateInfo* jRHI_DX12::CreateRaytracingPipelineStateInfo(const std::vector<jRaytracingPipelineShader>& InShaders, const jRaytracingPipelineData& InRaytracingData
    , const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const
{
    return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(InShaders, InRaytracingData, InShaderBindingArray, pushConstant)));
}

void jRHI_DX12::RemovePipelineStateInfo(size_t InHash)
{
    PipelineStatePool.Release(InHash);
}

std::shared_ptr<jRenderFrameContext> jRHI_DX12::BeginRenderFrame()
{
	SCOPE_CPU_PROFILE(BeginRenderFrame);

	//////////////////////////////////////////////////////////////////////////
	// Acquire new swapchain image
	jSwapchainImage_DX12* CurrentSwapchainImage = (jSwapchainImage_DX12*)Swapchain->GetCurrentSwapchainImage();
    check(CommandBufferManager);
    CommandBufferManager->Fence->WaitForFenceValue(CurrentSwapchainImage->FenceValue);
	//////////////////////////////////////////////////////////////////////////

    GetOneFrameUniformRingBuffer()->Reset();

	jCommandBuffer_DX12* commandBuffer = (jCommandBuffer_DX12*)CommandBufferManager->GetOrCreateCommandBuffer();

    auto renderFrameContextPtr = std::make_shared<jRenderFrameContext_DX12>(commandBuffer);
    renderFrameContextPtr->RaytracingScene = g_rhi->RaytracingScene;
    renderFrameContextPtr->UseForwardRenderer = !gOptions.UseDeferredRenderer;
    renderFrameContextPtr->FrameIndex = CurrentFrameIndex;
    renderFrameContextPtr->SceneRenderTargetPtr = std::make_shared<jSceneRenderTarget>();
    renderFrameContextPtr->SceneRenderTargetPtr->Create(CurrentSwapchainImage, &jLight::GetLights());

	return renderFrameContextPtr;
}

void jRHI_DX12::EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
	SCOPE_CPU_PROFILE(EndRenderFrame);

	jCommandBuffer_DX12* CommandBuffer = (jCommandBuffer_DX12*)InRenderFrameContextPtr->GetActiveCommandBuffer();
    jSwapchainImage_DX12* CurrentSwapchainImage = (jSwapchainImage_DX12*)Swapchain->GetCurrentSwapchainImage();

    g_rhi->TransitionLayout(CommandBuffer, CurrentSwapchainImage->TexturePtr.get(), EResourceLayout::PRESENT_SRC);
	CommandBufferManager->ExecuteCommandList(CommandBuffer);

    CurrentSwapchainImage->FenceValue = CommandBuffer->Owner->Fence->SignalWithNextFenceValue(CommandBufferManager->GetCommandQueue().Get());

    HRESULT hr = S_OK;
    if (g_rhi->IsSupportVSync())
    {
        // 첫번째 아규먼트는 VSync가 될때까지 기다림, 어플리케이션은 다음 VSync까지 잠재운다.
        // 이렇게 하는 것은 렌더링 한 프레임 중 화면에 나오지 못하는 프레임의 cycle을 아끼기 위해서임.
        hr = Swapchain->SwapChain->Present(1, 0);
    }
    else
	{
		hr = Swapchain->SwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}

    ensure(hr == S_OK || hr == DXGI_STATUS_OCCLUDED || hr == DXGI_STATUS_CLIPPED);

	// CurrentFrameIndex = (CurrentFrameIndex + 1) % Swapchain->Images.size();
    CurrentFrameIndex = Swapchain->GetCurrentBackBufferIndex();
	InRenderFrameContextPtr->Destroy();
}

std::shared_ptr<IUniformBufferBlock> jRHI_DX12::CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize /*= 0*/) const
{
    auto uniformBufferBlockPtr = std::make_shared<jUniformBufferBlock_DX12>(InName, InLifeTimeType);
    uniformBufferBlockPtr->Init(InSize);
    return uniformBufferBlockPtr;
}

void jRHI_DX12::BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
	, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
    // 이 부분은 구조화 하게 되면, 이전에 만들어둔 것을 그대로 사용
	if (InShaderBindingInstanceCombiner.ShaderBindingInstanceArray)
	{
        auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InCommandBuffer;
        check(CommandBuffer_DX12);

		const jShaderBindingInstanceArray& ShaderBindingInstanceArray = *(InShaderBindingInstanceCombiner.ShaderBindingInstanceArray);
		CommandBuffer_DX12->CommandList->SetGraphicsRootSignature(jShaderBindingLayout_DX12::CreateRootSignature(ShaderBindingInstanceArray, EShaderAccessStageFlag::ALL_GRAPHICS));

		int32 RootParameterIndex = 0;
        int32 NumOfDescriptor = 0;
        int32 NumOfSamplerDescriptor = 0;

        // Check current online descriptor is enough to allocate descriptors, if not, allocate descriptor blocks for current commandlist
        {
            for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
            {
                jShaderBindingInstance_DX12* Instance = (jShaderBindingInstance_DX12*)ShaderBindingInstanceArray[i];
                NumOfDescriptor += (int32)Instance->Descriptors.size();
                NumOfSamplerDescriptor += (int32)Instance->SamplerDescriptors.size();
            }

            check(Device);

            // Descriptor 가 충분한지 확인 함. 모자라면 새로 할당함.
            bool NeedSetDescriptorHeapsAgain = false;            
            if (!CommandBuffer_DX12->OnlineDescriptorHeap->CanAllocate(NumOfDescriptor))
            {
                const ID3D12DescriptorHeap* PrevDescriptorHeap = CommandBuffer_DX12->OnlineDescriptorHeap->GetHeap();
                
                CommandBuffer_DX12->OnlineDescriptorHeap->Release();
                CommandBuffer_DX12->OnlineDescriptorHeap = ((jRHI_DX12*)this)->OnlineDescriptorHeapManager.Alloc(EDescriptorHeapTypeDX12::CBV_SRV_UAV);
                check(CommandBuffer_DX12->OnlineDescriptorHeap->CanAllocate(NumOfDescriptor));

                if (PrevDescriptorHeap != CommandBuffer_DX12->OnlineDescriptorHeap->GetHeap())
                {
                    NeedSetDescriptorHeapsAgain = true;
                }
            }

            // SamplerDescriptor 가 충분한지 확인 함. 모자라면 새로 할당함.
            const ID3D12DescriptorHeap* PrevOnlineSamplerDescriptorHeap = CommandBuffer_DX12->OnlineDescriptorHeap->GetHeap();
            if (!CommandBuffer_DX12->OnlineSamplerDescriptorHeap->CanAllocate(NumOfSamplerDescriptor))
            {
                const ID3D12DescriptorHeap* PrevDescriptorHeap = CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetHeap();

                CommandBuffer_DX12->OnlineSamplerDescriptorHeap->Release();
                CommandBuffer_DX12->OnlineSamplerDescriptorHeap = ((jRHI_DX12*)this)->OnlineDescriptorHeapManager.Alloc(EDescriptorHeapTypeDX12::SAMPLER);
                check(CommandBuffer_DX12->OnlineSamplerDescriptorHeap->CanAllocate(NumOfSamplerDescriptor));

                if (PrevDescriptorHeap != CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetHeap())
                {
                    NeedSetDescriptorHeapsAgain = true;
                }
            }

            // Descriptor 새로 할당했으면 SetDescriptorHeaps 로 OnlineDescriptorHeap 을 교체해준다.
            if (NeedSetDescriptorHeapsAgain)
            {
                ensure(CommandBuffer_DX12->OnlineDescriptorHeap && CommandBuffer_DX12->OnlineSamplerDescriptorHeap
                    || (!CommandBuffer_DX12->OnlineDescriptorHeap && !CommandBuffer_DX12->OnlineSamplerDescriptorHeap));
                if (CommandBuffer_DX12->OnlineDescriptorHeap && CommandBuffer_DX12->OnlineSamplerDescriptorHeap)
                {
                    ID3D12DescriptorHeap* ppHeaps[] =
                    {
                        CommandBuffer_DX12->OnlineDescriptorHeap->GetHeap(),
                        CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetHeap()
                    };
                    CommandBuffer_DX12->CommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
                }
            }
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE FirstGPUDescriptorHandle
            = CommandBuffer_DX12->OnlineDescriptorHeap->GetGPUHandle(CommandBuffer_DX12->OnlineDescriptorHeap->GetNumOfAllocated());
        const D3D12_GPU_DESCRIPTOR_HANDLE FirstGPUSamplerDescriptorHandle
            = CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetGPUHandle(CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetNumOfAllocated());

		for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
		{
			jShaderBindingInstance_DX12* Instance = (jShaderBindingInstance_DX12*)ShaderBindingInstanceArray[i];
			jShaderBindingLayout_DX12* Layout = (jShaderBindingLayout_DX12*)(Instance->ShaderBindingsLayouts);

			Instance->CopyToOnlineDescriptorHeap(CommandBuffer_DX12);
			Instance->BindGraphics(CommandBuffer_DX12, RootParameterIndex);
		}

		// DescriptorTable 은 항상 마지막에 바인딩
		if (NumOfDescriptor > 0)
			CommandBuffer_DX12->CommandList->SetGraphicsRootDescriptorTable(RootParameterIndex++, FirstGPUDescriptorHandle);		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

		if (NumOfSamplerDescriptor > 0)
			CommandBuffer_DX12->CommandList->SetGraphicsRootDescriptorTable(RootParameterIndex++, FirstGPUSamplerDescriptorHandle);	// SamplerState test
	}
}

void jRHI_DX12::BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
    // 이 부분은 구조화 하게 되면, 이전에 만들어둔 것을 그대로 사용
    if (InShaderBindingInstanceCombiner.ShaderBindingInstanceArray)
    {
        auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InCommandBuffer;
        check(CommandBuffer_DX12);

        const jShaderBindingInstanceArray& ShaderBindingInstanceArray = *(InShaderBindingInstanceCombiner.ShaderBindingInstanceArray);
        CommandBuffer_DX12->CommandList->SetComputeRootSignature(jShaderBindingLayout_DX12::CreateRootSignature(ShaderBindingInstanceArray, EShaderAccessStageFlag::COMPUTE | EShaderAccessStageFlag::ALL_RAYTRACING));

        int32 RootParameterIndex = 0;
        bool HasDescriptor = false;
        bool HasSamplerDescriptor = false;

        const D3D12_GPU_DESCRIPTOR_HANDLE FirstGPUDescriptorHandle
            = CommandBuffer_DX12->OnlineDescriptorHeap->GetGPUHandle(CommandBuffer_DX12->OnlineDescriptorHeap->GetNumOfAllocated());
        const D3D12_GPU_DESCRIPTOR_HANDLE FirstGPUSamplerDescriptorHandle
            = CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetGPUHandle(CommandBuffer_DX12->OnlineSamplerDescriptorHeap->GetNumOfAllocated());

        for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
        {
            jShaderBindingInstance_DX12* Instance = (jShaderBindingInstance_DX12*)ShaderBindingInstanceArray[i];
            jShaderBindingLayout_DX12* Layout = (jShaderBindingLayout_DX12*)(Instance->ShaderBindingsLayouts);

            Instance->CopyToOnlineDescriptorHeap(CommandBuffer_DX12);

            HasDescriptor |= Instance->Descriptors.size() > 0;
            HasSamplerDescriptor |= Instance->SamplerDescriptors.size() > 0;

            Instance->BindCompute(CommandBuffer_DX12, RootParameterIndex);
        }

        // DescriptorTable 은 항상 마지막에 바인딩
        if (HasDescriptor)
            CommandBuffer_DX12->CommandList->SetComputeRootDescriptorTable(RootParameterIndex++, FirstGPUDescriptorHandle);		// StructuredBuffer test, I will use descriptor index based on GPU handle start of SRVDescriptorHeap

        if (HasSamplerDescriptor)
            CommandBuffer_DX12->CommandList->SetComputeRootDescriptorTable(RootParameterIndex++, FirstGPUSamplerDescriptorHandle);	// SamplerState test
    }
}

void jRHI_DX12::BindRaytracingShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
    , const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
    BindComputeShaderBindingInstances(InCommandBuffer, InPiplineState, InShaderBindingInstanceCombiner, InFirstSet);
}

std::shared_ptr<jVertexBuffer> jRHI_DX12::CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const
{
    if (!streamData)
        return nullptr;

	auto vertexBufferPtr = std::make_shared<jVertexBuffer_DX12>();
    vertexBufferPtr->Initialize(streamData);
    return vertexBufferPtr;
}

std::shared_ptr<jIndexBuffer> jRHI_DX12::CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const
{
    if (!streamData)
        return nullptr;

    check(streamData);
    check(streamData->Param);

    auto indexBufferPtr = std::make_shared<jIndexBuffer_DX12>();
    indexBufferPtr->Initialize(streamData);
    return indexBufferPtr;
}

std::shared_ptr<jTexture> jRHI_DX12::Create2DTexture(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
    , EResourceLayout InImageLayout, const jImageBulkData& InImageBulkData, const jRTClearValue& InClearValue, const wchar_t* InResourceName) const
{
    auto TexturePtr = jBufferUtil_DX12::CreateTexture(InWidth, InHeight, InArrayLayers, InMipLevels, 1, ETextureType::TEXTURE_2D, InFormat, InTextureCreateFlag, InImageLayout, InClearValue, InResourceName);
    if (InImageBulkData.ImageData.size() > 0)
    {
        // todo : recycle temp buffer
        auto BufferPtr = jBufferUtil_DX12::CreateBuffer(InImageBulkData.ImageData.size(), 0
            , EBufferCreateFlag::CPUAccess, EResourceLayout::READ_ONLY, &InImageBulkData.ImageData[0], InImageBulkData.ImageData.size());
        check(BufferPtr);

        jCommandBuffer_DX12* commandList = BeginSingleTimeCopyCommands();
        if (InImageBulkData.SubresourceFootprints.size() > 0)
        {
            jBufferUtil_DX12::CopyBufferToTexture(commandList->Get(), BufferPtr->Buffer->Get(), TexturePtr->Texture->Get(), InImageBulkData.SubresourceFootprints);
        }
        else
        {
            jBufferUtil_DX12::CopyBufferToTexture(commandList->Get(), BufferPtr->Buffer->Get(), 0, TexturePtr->Texture->Get());
        }

        EndSingleTimeCopyCommands(commandList);
    }
    return TexturePtr;
}

std::shared_ptr<jTexture> jRHI_DX12::CreateCubeTexture(uint32 InWidth, uint32 InHeight, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
    , EResourceLayout InImageLayout, const jImageBulkData& InImageBulkData, const jRTClearValue& InClearValue, const wchar_t* InResourceName) const
{
    auto TexturePtr = jBufferUtil_DX12::CreateTexture(InWidth, InHeight, 6, InMipLevels, 1, ETextureType::TEXTURE_CUBE, InFormat, InTextureCreateFlag, InImageLayout, InClearValue, InResourceName);
    if (InImageBulkData.ImageData.size() > 0)
    {
        // todo : recycle temp buffer
        auto BufferPtr = jBufferUtil_DX12::CreateBuffer(InImageBulkData.ImageData.size(), 0
            , EBufferCreateFlag::CPUAccess, EResourceLayout::READ_ONLY, &InImageBulkData.ImageData[0], InImageBulkData.ImageData.size());
        check(BufferPtr);

        jCommandBuffer_DX12* commandList = BeginSingleTimeCopyCommands();
        if (InImageBulkData.SubresourceFootprints.size() > 0)
        {
            jBufferUtil_DX12::CopyBufferToTexture(commandList->Get(), BufferPtr->Buffer->Get(), TexturePtr->Texture->Get(), InImageBulkData.SubresourceFootprints);
        }
        else
        {
            jBufferUtil_DX12::CopyBufferToTexture(commandList->Get(), BufferPtr->Buffer->Get(), 0, TexturePtr->Texture->Get());
        }

        EndSingleTimeCopyCommands(commandList);
    }
    return TexturePtr;
}

std::shared_ptr<jShaderBindingInstance> jRHI_DX12::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
    auto shaderBindingsLayout = CreateShaderBindings(InShaderBindingArray);
    check(shaderBindingsLayout);
    return shaderBindingsLayout->CreateShaderBindingInstance(InShaderBindingArray, InType);
}

void jRHI_DX12::DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	CommandBuffer_DX12->CommandList->DrawInstanced(vertCount, 1, vertStartIndex, 0);
}

void jRHI_DX12::DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type
	, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	CommandBuffer_DX12->CommandList->DrawInstanced(vertCount, instanceCount, vertStartIndex, 0);
}

void jRHI_DX12::DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type
	, int32 elementSize, int32 startIndex, int32 indexCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	CommandBuffer_DX12->CommandList->DrawIndexedInstanced(indexCount, 1, startIndex, 0, 0);
}

void jRHI_DX12::DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type
	, int32 elementSize, int32 startIndex, int32 indexCount, int32 instanceCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    CommandBuffer_DX12->CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, 0, 0);
}

void jRHI_DX12::DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type
	, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    CommandBuffer_DX12->CommandList->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertexIndex, 0);
}

void jRHI_DX12::DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
	, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex, int32 instanceCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    CommandBuffer_DX12->CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertexIndex, 0);
}

void jRHI_DX12::DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type
	, jBuffer* buffer, int32 startIndex, int32 drawCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	check(0);
}

void jRHI_DX12::DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type
	, jBuffer* buffer, int32 startIndex, int32 drawCount) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	check(0);
}

void jRHI_DX12::DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);
    check(CommandBuffer_DX12->CommandList);
    check(numGroupsX * numGroupsY * numGroupsZ > 0);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    CommandBuffer_DX12->CommandList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void jRHI_DX12::DispatchRay(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jRaytracingDispatchData& InDispatchData) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    CommandBuffer_DX12->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    D3D12_DISPATCH_RAYS_DESC dispatchDesc{};
    
    auto PipelineState = (jPipelineStateInfo_DX12*)InDispatchData.PipelineState;
    check(PipelineState);

    check(PipelineState->HitGroupBuffer);
    dispatchDesc.HitGroupTable.StartAddress = PipelineState->HitGroupBuffer->GetGPUAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = PipelineState->HitGroupBuffer->Size;
    dispatchDesc.HitGroupTable.StrideInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

    check(PipelineState->MissBuffer);
    dispatchDesc.MissShaderTable.StartAddress = PipelineState->MissBuffer->GetGPUAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = PipelineState->MissBuffer->Size;
    dispatchDesc.MissShaderTable.StrideInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

    check(PipelineState->RaygenBuffer);
    dispatchDesc.RayGenerationShaderRecord.StartAddress = PipelineState->RaygenBuffer->GetGPUAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = PipelineState->RaygenBuffer->Size;

    dispatchDesc.Width = InDispatchData.Width;
    dispatchDesc.Height = InDispatchData.Height;
    dispatchDesc.Depth = InDispatchData.Depth;

    CommandBuffer_DX12->CommandList->DispatchRays(&dispatchDesc);
}

std::shared_ptr<jRenderTarget> jRHI_DX12::CreateRenderTarget(const jRenderTargetInfo& info) const
{
    const uint16 MipLevels = info.IsGenerateMipmap ? static_cast<uint32>(std::floor(std::log2(std::max<int>(info.Width, info.Height)))) + 1 : 1;
    
    auto TexturePtr = jBufferUtil_DX12::CreateTexture(info.Width, info.Height, info.LayerCount, MipLevels, (uint32)info.SampleCount, info.Type, info.Format
        , (ETextureCreateFlag::RTV | info.TextureCreateFlag), EResourceLayout::UNDEFINED, info.RTClearValue, info.ResourceName);

    auto RenderTargetPtr = std::make_shared<jRenderTarget>();
    check(RenderTargetPtr);
    RenderTargetPtr->Info = info;
    RenderTargetPtr->TexturePtr = TexturePtr;

    return RenderTargetPtr;
}

jQuery* jRHI_DX12::CreateQueryTime() const
{
    auto queryTime = new jQueryTime_DX12();
    return queryTime;
}

void jRHI_DX12::ReleaseQueryTime(jQuery* queryTime) const
{
    auto queryTime_gl = static_cast<jQuery*>(queryTime);
    delete queryTime_gl;
}

void jRHI_DX12::TransitionLayout(jCommandBuffer* commandBuffer, jTexture* texture, EResourceLayout newLayout) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddTransition(texture, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::TransitionLayout(jTexture* texture, EResourceLayout newLayout) const
{
    check(BarrierBatcher);
    BarrierBatcher->AddTransition(texture, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::UAVBarrier(jCommandBuffer* commandBuffer, jTexture* texture) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddUAV(texture);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::UAVBarrier(jCommandBuffer* commandBuffer, jBuffer* buffer) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddUAV(buffer);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::UAVBarrier(jTexture* texture) const
{
    check(BarrierBatcher);
    BarrierBatcher->AddUAV(texture);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
}
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::UAVBarrier(jBuffer* buffer) const
{
    check(BarrierBatcher);
    BarrierBatcher->AddUAV(buffer);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::TransitionLayout(jCommandBuffer* commandBuffer, jBuffer* buffer, EResourceLayout newLayout) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddTransition(buffer, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::TransitionLayout(jBuffer* buffer, EResourceLayout newLayout) const
{
    check(BarrierBatcher);
    BarrierBatcher->AddTransition(buffer, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_DX12::BeginDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor /*= Vector4::ColorGreen*/) const
{
    jCommandBuffer_DX12* CommandList = (jCommandBuffer_DX12*)InCommandBuffer;
    check(CommandList);
    check(!CommandList->IsClosed);

    PIXBeginEvent(CommandList->Get(), PIX_COLOR((BYTE)(255 * InColor.x), (BYTE)(255 * InColor.y), (BYTE)(255 * InColor.z)), InName);
}

void jRHI_DX12::EndDebugEvent(jCommandBuffer* InCommandBuffer) const
{
    jCommandBuffer_DX12* CommandList = (jCommandBuffer_DX12*)InCommandBuffer;
    check(CommandList);
    check(!CommandList->IsClosed);

    PIXEndEvent(CommandList->Get());
}

void jRHI_DX12::Flush() const
{
    WaitForGPU();
}

void jRHI_DX12::Finish() const
{
    WaitForGPU();
}

jRaytracingScene* jRHI_DX12::CreateRaytracingScene() const
{
    return new jRaytracingScene_DX12();
}

std::shared_ptr<jBuffer> jRHI_DX12::CreateStructuredBuffer(uint64 InSize, uint64 InAlignment, uint64 InStride, EBufferCreateFlag InBufferCreateFlag
    , EResourceLayout InInitialState, const void* InData, uint64 InDataSize, const wchar_t* InResourceName) const
{
    auto BufferPtr = jBufferUtil_DX12::CreateBuffer(InSize, InStride, InBufferCreateFlag, InInitialState
        , InData, InDataSize, InResourceName);

    jBufferUtil_DX12::CreateShaderResourceView_StructuredBuffer(BufferPtr.get(), (uint32)InStride, (uint32)(InSize / InStride));

    if (!!(EBufferCreateFlag::UAV & InBufferCreateFlag))
    {
        jBufferUtil_DX12::CreateUnorderedAccessView_StructuredBuffer(BufferPtr.get(), (uint32)InStride, (uint32)(InSize / InStride));
    }

    return std::shared_ptr<jBuffer>(BufferPtr);
}

std::shared_ptr<jBuffer> jRHI_DX12::CreateRawBuffer(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
    , EResourceLayout InInitialState, const void* InData, uint64 InDataSize, const wchar_t* InResourceName) const
{
    auto BufferPtr = jBufferUtil_DX12::CreateBuffer(InSize, InAlignment, InBufferCreateFlag, InInitialState
        , InData, InDataSize, InResourceName);

    jBufferUtil_DX12::CreateShaderResourceView_Raw(BufferPtr.get(), (uint32)InSize);

    if (!!(EBufferCreateFlag::UAV & InBufferCreateFlag))
    {
        jBufferUtil_DX12::CreateUnorderedAccessView_Raw(BufferPtr.get(), (uint32)InSize);
    }

    return BufferPtr;
}

std::shared_ptr<jBuffer> jRHI_DX12::CreateFormattedBuffer(uint64 InSize, uint64 InAlignment, ETextureFormat InFormat, EBufferCreateFlag InBufferCreateFlag
    , EResourceLayout InInitialState, const void* InData, uint64 InDataSize, const wchar_t* InResourceName) const
{
    auto BufferPtr = jBufferUtil_DX12::CreateBuffer(InSize, InAlignment, InBufferCreateFlag, InInitialState
        , InData, InDataSize, InResourceName);

    jBufferUtil_DX12::CreateShaderResourceView_Formatted(BufferPtr.get(), InFormat, (uint32)InSize);

    if (!!(EBufferCreateFlag::UAV & InBufferCreateFlag))
    {
        jBufferUtil_DX12::CreateUnorderedAccessView_Formatted(BufferPtr.get(), InFormat, (uint32)InSize);
    }

    return BufferPtr;
}

bool jRHI_DX12::IsSupportVSync() const
{
    return GRHISupportVsync && GUseVsync;
}

//////////////////////////////////////////////////////////////////////////
// jPlacedResourcePool
void jPlacedResourcePool::Init()
{
    // The allocator should be able to allocate memory larger than the PlacedResourceSizeThreshold. 
    check(g_rhi_dx12->GPlacedResourceSizeThreshold <= MemorySize[(int32)EPoolSizeType::MAX - 1]);

    check(g_rhi_dx12);
    g_rhi_dx12->DeallocatorMultiFramePlacedResource.FreeDelegate
        = std::bind(&jPlacedResourcePool::FreedFromPendingDelegateForCreatedResource, this, std::placeholders::_1);
}

void jPlacedResourcePool::Release()
{
    check(g_rhi_dx12);
    g_rhi_dx12->DeallocatorMultiFramePlacedResource.FreeDelegate = nullptr;

    for (int32 i = 0; i < (int32)EPoolSizeType::MAX; ++i)
    {
        PendingPlacedResources[i].clear();
        PendingUploadPlacedResources[i].clear();
    }
}

void jPlacedResourcePool::Free(const ComPtr<ID3D12Resource>& InData)
{
    if (!InData)
        return;

    if (g_rhi_dx12->GIsUsePlacedResource)
    {
        jScopedLock s(&Lock);

        auto it_find = UsingPlacedResources.find(InData.Get());
        if (UsingPlacedResources.end() != it_find)
        {
            auto& PendingList = GetPendingPlacedResources(it_find->second.IsUploadResource, it_find->second.Size);
            PendingList.push_back(it_find->second);
            UsingPlacedResources.erase(it_find);
            return;
        }

        check(0);	// can't reach here.
    }
}
