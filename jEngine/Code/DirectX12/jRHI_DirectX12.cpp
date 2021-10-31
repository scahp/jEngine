#include "pch.h"
#include "jRHI_DirectX12.h"
#include "jImageFileLoader.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

CD3DX12_VIEWPORT m_viewport(0.0f, 0.0f, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));
CD3DX12_RECT m_scissorRect(0, 0, static_cast<LONG>(SCR_WIDTH), static_cast<LONG>(SCR_HEIGHT));

// Vertex data for a colored cube.
struct VertexPosColor
{
	XMFLOAT3 Position;
	XMFLOAT3 Color;
	XMFLOAT2 TexCoord;
};

static VertexPosColor g_Vertices[] = {
	// Cube face 0
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

	// Cube face 1
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },

	// Cube face 2
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },

	// Cube face 3
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, 1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
	{ XMFLOAT3(1.0f, 1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f, 1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

	// Cube face 4
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },

	// Cube face 5
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
};

static WORD g_Indicies[] =
{
	0, 1, 2, 3, 4, 5,
	6, 7, 8, 9, 10, 11,
	12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29,
	30, 31, 32, 33, 34, 35
};

struct MaterialConstants
{
	uint32 MaterialIndex = 0;
};

enum HeapOffset
{
	CBV_SRV = 0,
	Command = 2,
	UAV = 3,
	TextureSRV = 4,
	TextureSRV_Unbound = 5,
	TextureSRV_Unbound_End = 7,
	Num,
};

// Compute shader
struct ComputeRootConstant
{
	float CommandCount;
};

const int32 CubeCount = 49;
uint32 GraphicsShaderCBVInstanceBufferForIndirectCommand = 0;
uint32 GraphicsShaderCBVMVPForIndirectCommand = 0;

const uint32 ComputeShaderSrvUavRootParameterIndex = 0;
const uint32 ComputeShaderRootConstantRootParameterIndex = 1;

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

void GetHardwareAdapter(
	IDXGIFactory1* pFactory,
	IDXGIAdapter1** ppAdapter,
	bool requestHighPerformanceAdapter = false)
{
	*ppAdapter = nullptr;

	ComPtr<IDXGIAdapter1> adapter;

	ComPtr<IDXGIFactory6> factory6;
	if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
	{
		for (
			UINT adapterIndex = 0;
			DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(
				adapterIndex,
				requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
				IID_PPV_ARGS(&adapter));
			++adapterIndex)
		{
			if (!adapter)
				continue;

			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
	}
	else
	{
		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see whether the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
	}

	*ppAdapter = adapter.Detach();
}

void jRHI_DirectX12::Initialize()
{
	HWND hWnd = CreateMainWindow();

	//////////////////////////////////////////////////////////////////////////
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	bool m_useWarpDevice = false;

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));

		D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		);
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		);
	}

	directCommandQueue.Initialize(m_device);
	bundleCommandQueue.Initialize(m_device, D3D12_COMMAND_LIST_TYPE_BUNDLE);
	computeCommandQueue.Initialize(m_device, D3D12_COMMAND_LIST_TYPE_COMPUTE);

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = SCR_WIDTH;
	swapChainDesc.Height = SCR_HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	factory->CreateSwapChainForHwnd(
		directCommandQueue.GetCommandQueue().Get(),        // Swap chain needs the queue so that it can force a flush on it.
		hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	);

	// This sample does not support fullscreen transitions.
	factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

	swapChain.As(&m_swapChain);
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// [여기서 부터 렌더링에 필요한 리소스 생성]
	// 1. 렌더타겟 뷰를 생성할 디스크립터 힙을 생성함
	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	{
		// Create Heap desc
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = HeapOffset::Num * FrameCount;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap));
	}

	// 2. 백버퍼 렌더타겟의 뷰를 만듬
	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n]));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}

	LoadContent();

	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

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

		RenderCubeTest();
	}
}

void jRHI_DirectX12::LoadContent()
{
	//ComPtr<ID3D12CommandAllocator> commandAllocator;
	//ComPtr<ID3D12GraphicsCommandList2> commandList;
	//ComPtr<ID3D12Fence> fence;
	//uint64_t fenceValue = 0;
	//HANDLE fenceEvent;
	//{
	//	D3D12_COMMAND_QUEUE_DESC copyqueueDesc = {};
	//	copyqueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	//	copyqueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	//	copyqueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	//	copyqueueDesc.NodeMask = 0;
	//	m_device->CreateCommandQueue(&copyqueueDesc, IID_PPV_ARGS(&m_copyCommandQueue));
	//	m_device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	//	fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

	//	m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&commandAllocator));
	//	m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	//}
    ComPtr<ID3D12GraphicsCommandList2> directCommandList = directCommandQueue.GetAvailableCommandList();

	copyCommandQueue.Initialize(m_device, D3D12_COMMAND_LIST_TYPE_COPY);
	{
		ComPtr<ID3D12GraphicsCommandList2> copyCommandList = copyCommandQueue.GetAvailableCommandList();

		// Upload vertex buffer data
		ComPtr<ID3D12Resource> intermediateVertexBuffer;
		UpdateBufferResource(m_device, copyCommandList, m_vertexBuffer, intermediateVertexBuffer
			, _countof(g_Vertices), sizeof(VertexPosColor), g_Vertices, D3D12_RESOURCE_FLAG_NONE);

		// Create the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.SizeInBytes = sizeof(g_Vertices);
		m_vertexBufferView.StrideInBytes = sizeof(VertexPosColor);

		// Upload index buffer data.
		ComPtr<ID3D12Resource> intermediateIndexBuffer;
		UpdateBufferResource(m_device, copyCommandList, m_indexBuffer, intermediateIndexBuffer
			, _countof(g_Indicies), sizeof(DWORD), g_Indicies, D3D12_RESOURCE_FLAG_NONE);

        uint64 executeFenceValue = copyCommandQueue.ExecuteCommandList(copyCommandList);
        copyCommandQueue.WaitForFenceValue(executeFenceValue);
	}

	// Texture
	ComPtr<ID3D12Resource> intermediateTextureUploadBuffer;
	std::weak_ptr<jImageData> ImageData = jImageFileLoader::GetInstance().LoadImageDataFromFile("Image/sun.png");
	std::shared_ptr<jImageData> imageDataPtr = ImageData.lock();

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = imageDataPtr->Width;
	textureDesc.Height = imageDataPtr->Height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	{
		ComPtr<ID3D12GraphicsCommandList2> copyCommandList = copyCommandQueue.GetAvailableCommandList();

		const size_t textureSize = imageDataPtr->Width * imageDataPtr->Height;
		UpdateBufferResourceWithDesc(m_device, copyCommandList, m_texture, intermediateTextureUploadBuffer
			, textureSize, 4, textureDesc, &imageDataPtr->ImageData[0], D3D12_RESOURCE_FLAG_NONE);

		directCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		uint64 executeFenceValue = copyCommandQueue.ExecuteCommandList(copyCommandList);
		copyCommandQueue.WaitForFenceValue(executeFenceValue);
    }

    int32 m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	{
		// Describe and create a SRV for the texture
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		for (uint32 frame = 0; frame < FrameCount; ++frame)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), HeapOffset::TextureSRV + frame * HeapOffset::Num, m_cbvSrvDescriptorSize);
			m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, srvHandle);
			//srvHandle.Offset(HeapOffset::Num, m_cbvSrvDescriptorSize);
		}
	}
	//////////////////////////////////////////////////////////////////////////\

	// Texture Array
	const char* ImagePathArray[] = 
	{
        "Image/bulb.png",
        "Image/spot.png",
		"Image/sun.png",
	};
	for (int32 i = 0; i < _countof(ImagePathArray); ++i)
	{
        std::weak_ptr<jImageData> ImageData = jImageFileLoader::GetInstance().LoadImageDataFromFile(ImagePathArray[i]);
        std::shared_ptr<jImageData> imageDataPtr = ImageData.lock();

        ComPtr<ID3D12GraphicsCommandList2> copyCommandList = copyCommandQueue.GetAvailableCommandList();
        
		const size_t textureSize = imageDataPtr->Width * imageDataPtr->Height;
        UpdateBufferResourceWithDesc(m_device, copyCommandList, m_textureArray[i], intermediateTextureUploadBuffer
            , textureSize, 4, textureDesc, &imageDataPtr->ImageData[0], D3D12_RESOURCE_FLAG_NONE);

		directCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_textureArray[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        uint64 executeFenceValue = copyCommandQueue.ExecuteCommandList(copyCommandList);
        copyCommandQueue.WaitForFenceValue(executeFenceValue);
	}

	{
		for (uint32 frame = 0; frame < FrameCount; ++frame)
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), HeapOffset::TextureSRV_Unbound + frame * HeapOffset::Num, m_cbvSrvDescriptorSize);
			for (int32 i = 0; i < _countof(m_textureArray); ++i)
			{
				// Create SRV for the ImageArray
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Format = textureDesc.Format;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = 1;
				m_device->CreateShaderResourceView(m_textureArray[i].Get(), &srvDesc, srvHandle);

				srvHandle.Offset(m_cbvSrvDescriptorSize);
			}
			//srvHandle.Offset(HeapOffset::Num, m_cbvSrvDescriptorSize);
		}
	}
	//////////////////////////////////////////////////////////////////////////

	m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	m_indexBufferView.SizeInBytes = sizeof(g_Indicies);

	// Create the descriptor heap for the depth-stencil view.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dSVHeap))))
		return;

	// Load the shaders
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif
	ID3DBlob* compilationMsgs = nullptr;

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;
	D3DCompileFromFile(L"Shaders/HLSL/CubeVertexShader_IndirectCommand.hlsl", nullptr, nullptr, "main", "vs_5_1", compileFlags, 0, &vertexShader, &compilationMsgs);
	if (compilationMsgs)
	{
		OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
		compilationMsgs->Release();
	}
	D3DCompileFromFile(L"Shaders/HLSL/CubePixelShader_IndirectCommand.hlsl", nullptr, nullptr, "main", "ps_5_1"
		, compileFlags | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES, 0, &pixelShader, &compilationMsgs);
	if (compilationMsgs)
	{
		OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
		compilationMsgs->Release();
	}

	// Create the vertex input layout
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// Create a root signature
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	// Allow input layout and deny unnecessary accesss to certain pipeline stages
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;// |
		//D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// A single 32-bit constant root parameter that is used by the vertex shader
	CD3DX12_ROOT_PARAMETER1 rootParameters[5];
	int32 rootParameter = 0;

	GraphicsShaderCBVInstanceBufferForIndirectCommand = rootParameter++;
	rootParameters[GraphicsShaderCBVInstanceBufferForIndirectCommand].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
	//rootParameters[rootParameter++].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	rootParameters[rootParameter++].InitAsConstants(sizeof(MaterialConstants) / 4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

	// rootParameters[2].InitAsConstants(InstanceConstantBuffer::SizeWithoutPadding / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	GraphicsShaderCBVMVPForIndirectCommand = rootParameter++;
	rootParameters[GraphicsShaderCBVMVPForIndirectCommand].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_PIXEL);

	// Texture
	CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	//ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, _countof(m_textureArray), 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);		// unbounded 라서 descriptor 수를 1개로 해도 됨.

	rootParameters[rootParameter++].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);		//	* rootParameters[1] 의 1을 SetGraphicsRootDescriptorTable에 Index로 넘겨야함. *
	rootParameters[rootParameter++].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);		//	* rootParameters[2] 의 2을 SetGraphicsRootDescriptorTable에 Index로 넘겨야함. *

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	//////////////////////////////////////////////////////////////////////////


	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);

	// Serialize the root signature
	ComPtr<ID3DBlob> rootSignatureBlob;
	ComPtr<ID3DBlob> errorBlob;
	if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription, featureData.HighestVersion, &rootSignatureBlob, &errorBlob)))
	{
		if (errorBlob)
		{
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		return;
	}

	// Create the root signature
	if (FAILED(m_device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
		return;

	struct PipelineStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	} pipelineStateStream;

	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 1;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	pipelineStateStream.pRootSignature = m_rootSignature.Get();
	pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipelineStateStream.RTVFormats = rtvFormats;

	D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
		sizeof(PipelineStateStream), &pipelineStateStream
	};
	if (FAILED(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState))))
		return;

	// Resize/Create the depth buffer
	D3D12_CLEAR_VALUE optimizedClearValue = {};
	optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	optimizedClearValue.DepthStencil = { 1.0f, 0 };

	m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, SCR_WIDTH, SCR_HEIGHT,
			1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&optimizedClearValue,
		IID_PPV_ARGS(&m_depthBuffer));

	// Update the depth-stencil view.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsv.Texture2D.MipSlice = 0;
	dsv.Flags = D3D12_DSV_FLAG_NONE;

	m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dSVHeap->GetCPUDescriptorHandleForHeapStart());

	//////////////////////////////////////////////////////////////////////////
	// Compute shader
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

		// Create compute signature.
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

		CD3DX12_ROOT_PARAMETER1 computeRootParameters[2];
		computeRootParameters[ComputeShaderSrvUavRootParameterIndex].InitAsDescriptorTable(2, ranges);
		computeRootParameters[ComputeShaderRootConstantRootParameterIndex].InitAsConstants(sizeof(ComputeRootConstant) / 4, 0);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
		computeRootSignatureDesc.Init_1_1(_countof(computeRootParameters), computeRootParameters);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		if (FAILED(D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, featureData.HighestVersion, &signature, &error)))
		{
			if (errorBlob)
			{
				OutputDebugStringA((char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}
			return;
		}

		if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature))))
			return;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif
		ID3DBlob* compilationMsgs = nullptr;
		ComPtr<ID3DBlob> computeShader;
		D3DCompileFromFile(L"Shaders/HLSL/IndirectCommandCompute.hlsl", nullptr, nullptr, "main", "cs_5_0", compileFlags, 0, &computeShader, &compilationMsgs);
		if (compilationMsgs)
		{
			OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
			compilationMsgs->Release();
		}

		// Describe and create the compute pipeline state object (PSO)
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
		computePsoDesc.pRootSignature = m_computeRootSignature.Get();
		computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		if (FAILED(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computeState))))
			return;

		// Create the buffers
		{
			//////////////////////////////////////////////////////////////////////////
			// Create constant instance buffer
			m_constantInstanceBufferData.resize(CubeCount * FrameCount);

			// Initialize the constant buffers for each of the triangles.
			const int32 Interval = (255 * 3) / CubeCount;
			for (int32 frame = 0; frame < FrameCount; ++frame)
			{
				Vector4 ColorTemp = Vector4::ColorBlack;
				for (int32 i = 0; i < CubeCount; ++i)
				{
					if (ColorTemp.x + Interval <= 255)
						ColorTemp.x += Interval;
					else if (ColorTemp.y + Interval <= 255)
						ColorTemp.y += Interval;
					else if (ColorTemp.z + Interval <= 255)
						ColorTemp.z += Interval;
					m_constantInstanceBufferData[i + CubeCount * frame].Color = ColorTemp / 255.0f;
				}
			}

			{
				ComPtr<ID3D12GraphicsCommandList2> copyCommandList = copyCommandQueue.GetAvailableCommandList();

				// Upload constant buffer data
				ComPtr<ID3D12Resource> intermediateConstantBuffer;
				UpdateBufferResource(m_device, copyCommandList, m_constantInstanceBuffer, intermediateConstantBuffer
					, m_constantInstanceBufferData.size(), sizeof(InstanceConstantBuffer), m_constantInstanceBufferData.data(), D3D12_RESOURCE_FLAG_NONE);

				uint64 executeFenceValue = copyCommandQueue.ExecuteCommandList(copyCommandList);
				copyCommandQueue.WaitForFenceValue(executeFenceValue);
			}

			// Create shader resource views (SRV) of the constant instance buffers for the compute shader to read from.
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Buffer.NumElements = CubeCount;
				srvDesc.Buffer.StructureByteStride = sizeof(InstanceConstantBuffer);
				srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

				CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), HeapOffset::CBV_SRV, m_cbvSrvDescriptorSize);
				for (uint32 frame = 0; frame < FrameCount; ++frame)
				{
					srvDesc.Buffer.FirstElement = frame * CubeCount;
					m_device->CreateShaderResourceView(m_constantInstanceBuffer.Get(), &srvDesc, srvHandle);
					srvHandle.Offset(HeapOffset::Num, m_cbvSrvDescriptorSize);
				}
			}
			//////////////////////////////////////////////////////////////////////////

			//////////////////////////////////////////////////////////////////////////
			// Create constant MVP buffer
			XMMATRIX VP;

			// Update the view matrix.
			static float t = 0.0f;
			m_constantMVPBufferData.resize(CubeCount * FrameCount);
			const int32 MaxXorY = sqrt(CubeCount);
			for (int32 frame = 0; frame < FrameCount; ++frame)
			{
				const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
				const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
				const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
				VP = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

				float aspectRatio = SCR_WIDTH / static_cast<float>(SCR_HEIGHT);
				VP = XMMatrixMultiply(VP, XMMatrixPerspectiveFovLH(XMConvertToRadians(45), aspectRatio, 0.1f, 100.0f));
				t = 0.0f;

				for (int32 i = 0; i < CubeCount; ++i)
				{
					t += 0.01f;
					float angle = static_cast<float>(t * 90.0);
					const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);

					float x = static_cast<float>(i % MaxXorY) - MaxXorY / 2;
					float y = static_cast<float>(i / MaxXorY) - MaxXorY / 2;
					XMMATRIX S = XMMatrixScaling(0.3f, 0.3f, 0.3f);
					XMMATRIX R = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));
					XMMATRIX T = XMMatrixTranslation(x, y, 0.0f);
					XMMATRIX ModelMatrix = XMMatrixMultiply(XMMatrixMultiply(S, R), T);
					m_constantMVPBufferData[i + CubeCount * frame].MVP = XMMatrixMultiply(ModelMatrix, VP);
				}
			}

			{
				ComPtr<ID3D12GraphicsCommandList2> copyCommandList = copyCommandQueue.GetAvailableCommandList();

				// Upload constant buffer data
				ComPtr<ID3D12Resource> intermeidateConstanceBuffer;
				UpdateBufferResource(m_device, copyCommandList, m_constantMVPBuffer, intermeidateConstanceBuffer
					, m_constantMVPBufferData.size(), sizeof(MVPConstantBuffer), m_constantMVPBufferData.data(), D3D12_RESOURCE_FLAG_NONE);

				uint64 executeFenceValue = copyCommandQueue.ExecuteCommandList(copyCommandList);
				copyCommandQueue.WaitForFenceValue(executeFenceValue);
			}

			// Create shader resource views (SRV) of the constant MVP buffers for the compute shader to read from.
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Buffer.NumElements = CubeCount;
				srvDesc.Buffer.StructureByteStride = sizeof(MVPConstantBuffer);
				srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

				CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), HeapOffset::CBV_SRV + 1, m_cbvSrvDescriptorSize);
				for (uint32 frame = 0; frame < FrameCount; ++frame)
				{
					srvDesc.Buffer.FirstElement = frame * CubeCount;
					m_device->CreateShaderResourceView(m_constantMVPBuffer.Get(), &srvDesc, srvHandle);
					srvHandle.Offset(HeapOffset::Num, m_cbvSrvDescriptorSize);
				}
			}
			//////////////////////////////////////////////////////////////////////////

			//////////////////////////////////////////////////////////////////////////
			// Create command buffer
			commands.resize(CubeCount * FrameCount);

			D3D12_GPU_VIRTUAL_ADDRESS instanceBufferGpuAddress = m_constantInstanceBuffer->GetGPUVirtualAddress();
			D3D12_GPU_VIRTUAL_ADDRESS MVPBufferGpuAddress = m_constantMVPBuffer->GetGPUVirtualAddress();
			int32 commandIndex = 0;

			for (int32 frame = 0; frame < FrameCount; ++frame)
			{
				for (int32 i = 0; i < CubeCount; ++i)
				{
					commands[commandIndex].cbvInstanceBuffer = instanceBufferGpuAddress;
					commands[commandIndex].cbvMVP = MVPBufferGpuAddress;
					commands[commandIndex].drawArguments.IndexCountPerInstance = 36;
					commands[commandIndex].drawArguments.InstanceCount = 1;
					commands[commandIndex].drawArguments.StartIndexLocation = 0;
					commands[commandIndex].drawArguments.StartInstanceLocation = 0;
					commands[commandIndex].drawArguments.BaseVertexLocation = 0;

					++commandIndex;
					instanceBufferGpuAddress += sizeof(InstanceConstantBuffer);
					MVPBufferGpuAddress += sizeof(MVPConstantBuffer);
				}
			}
			{
				ComPtr<ID3D12GraphicsCommandList2> copyCommandList = copyCommandQueue.GetAvailableCommandList();

				// Upload command buffer data
				ComPtr<ID3D12Resource> intermediateCommandBuffer;
				UpdateBufferResource(m_device, copyCommandList, m_commandBuffer, intermediateCommandBuffer
					, commands.size(), sizeof(IndirectCommand), commands.data(), D3D12_RESOURCE_FLAG_NONE);

				uint64 executeFenceValue = copyCommandQueue.ExecuteCommandList(copyCommandList);
				copyCommandQueue.WaitForFenceValue(executeFenceValue);
			}

			{
				// Create SRVs for the command buffers.
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Buffer.NumElements = CubeCount;
				srvDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
				srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

				CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), HeapOffset::Command, m_cbvSrvDescriptorSize);
				for (int32 frame = 0; frame < FrameCount; ++frame)
				{
					srvDesc.Buffer.FirstElement = frame * CubeCount;
					m_device->CreateShaderResourceView(m_commandBuffer.Get(), &srvDesc, srvHandle);
					srvHandle.Offset(HeapOffset::Num, m_cbvSrvDescriptorSize);
				}
			}
			//////////////////////////////////////////////////////////////////////////

			//////////////////////////////////////////////////////////////////////////
			// Create the unordered access views (UAVs) that store the results of the compute work
			{
				// We pack the UAV counter into the same buffer as the commands rather than create
				// a separate 64K resource/heap for it. The counter must be aligned on 4K boundaries,
				// so we pad the command buffer (if necessary) such that the counter will be placed
				// at a valid location in the buffer.
				auto AlignForUavCounter = [](uint32 bufferSize)->uint32
				{
					const uint32 alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
					return (bufferSize + (alignment - 1)) & ~(alignment - 1);
				};

				const int32 CommandSizePerFrame = CubeCount * sizeof(IndirectCommand);
				const int32 CommandBufferCounterOffset = AlignForUavCounter(CommandSizePerFrame);

				D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(CommandBufferCounterOffset + sizeof(uint32), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

				CD3DX12_CPU_DESCRIPTOR_HANDLE processedCommandsHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart(), HeapOffset::UAV, m_cbvSrvDescriptorSize);
				for (int32 frame = 0; frame < FrameCount; ++frame)
				{
					m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE
						, &commandBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_processedCommandBuffers[frame]));

					D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
					uavDesc.Format = DXGI_FORMAT_UNKNOWN;
					uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					uavDesc.Buffer.FirstElement = 0;
					uavDesc.Buffer.NumElements = CubeCount;
					uavDesc.Buffer.StructureByteStride = sizeof(IndirectCommand);
					uavDesc.Buffer.CounterOffsetInBytes = CommandBufferCounterOffset;
					uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

					m_device->CreateUnorderedAccessView(
						m_processedCommandBuffers[frame].Get(),
						m_processedCommandBuffers[frame].Get(),
						&uavDesc,
						processedCommandsHandle);

					processedCommandsHandle.Offset(HeapOffset::Num, m_cbvSrvDescriptorSize);
				}

				// Allocate a buffer that can be used to reset the UAV counters and initialize it to 0.
				if (FAILED(m_device->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
					D3D12_HEAP_FLAG_NONE,
					&CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32)),
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(&m_processedCommandBufferCounterReset))))
				{
					return;
				}

				UINT8* pMappedCounterReset = nullptr;
				CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
				if (!FAILED(m_processedCommandBufferCounterReset->Map(0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset))))
				{
					ZeroMemory(pMappedCounterReset, sizeof(uint32));
					m_processedCommandBufferCounterReset->Unmap(0, nullptr);
				}
			}
			//////////////////////////////////////////////////////////////////////////
		}

		// Create the command signature used for indirect drawing
		{
			// Each command consists of a CBV update and a DrawInstanced cell
			D3D12_INDIRECT_ARGUMENT_DESC argDescs[3] = {};
			argDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
			argDescs[0].ConstantBufferView.RootParameterIndex = GraphicsShaderCBVInstanceBufferForIndirectCommand;
			argDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
			argDescs[1].ConstantBufferView.RootParameterIndex = GraphicsShaderCBVMVPForIndirectCommand;
			argDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

			D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
			commandSignatureDesc.pArgumentDescs = argDescs;
			commandSignatureDesc.NumArgumentDescs = _countof(argDescs);
			commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

			if (FAILED(m_device->CreateCommandSignature(&commandSignatureDesc, m_rootSignature.Get(), IID_PPV_ARGS(&m_commandSignature))))
				return;
		}
	}
	//////////////////////////////////////////////////////////////////////////
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

void jRHI_DirectX12::RenderCubeTest()
{
	directCommandQueue.WaitForFenceValue();

	static bool IsUseBundleCommandList = false;
	static bool IsBundleCommandListRecorded = false;

	ComPtr<ID3D12GraphicsCommandList2> direcCommandList = directCommandQueue.GetAvailableCommandList();
	ComPtr<ID3D12GraphicsCommandList2> commandList = IsUseBundleCommandList ? bundleCommandQueue.GetAvailableCommandList() : direcCommandList;
	// static ComPtr<ID3D12GraphicsCommandList2> bundleCommandList = bundleCommandQueue.GetAvailableCommandList();

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	ComPtr<ID3D12Resource> currentBackbuffer = m_renderTargets[m_frameIndex];
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);

	// We pack the UAV counter into the same buffer as the commands rather than create
	// a separate 64K resource/heap for it. The counter must be aligned on 4K boundaries,
	// so we pad the command buffer (if necessary) such that the counter will be placed
	// at a valid location in the buffer.
	auto AlignForUavCounter = [](uint32 bufferSize) -> uint32
	{
		const uint32 alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
		return (bufferSize + (alignment - 1)) & ~(alignment - 1);
	};
	const uint32 CommandBufferCounterOffset = AlignForUavCounter(CubeCount * sizeof(IndirectCommand));

	// Record the compute commands that will cull cube and prevent them from being processed by vertex shader
	{
		ComPtr<ID3D12GraphicsCommandList2> computeCommandList = computeCommandQueue.GetAvailableCommandList();

		const int32 m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		computeCommandList->SetPipelineState(m_computeState.Get());
		computeCommandList->SetComputeRootSignature(m_computeRootSignature.Get());

		ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
		computeCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		computeCommandList->SetComputeRootDescriptorTable(ComputeShaderSrvUavRootParameterIndex
			, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), HeapOffset::Num * m_frameIndex + HeapOffset::CBV_SRV, m_cbvSrvDescriptorSize));

		// Root constants for the compute shader.
		struct RootConstants
		{
			float commandCount;
		};

		static RootConstants rootConstant{ std::min(100, CubeCount) };

		computeCommandList->CopyBufferRegion(m_processedCommandBuffers[m_frameIndex].Get(), CommandBufferCounterOffset
			, m_processedCommandBufferCounterReset.Get(), 0, sizeof(uint32));

		computeCommandList->SetComputeRoot32BitConstants(ComputeShaderRootConstantRootParameterIndex
			, sizeof(rootConstant) / 4, reinterpret_cast<void*>(&rootConstant), 0);

		{
			D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_processedCommandBuffers[m_frameIndex].Get()
				, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			computeCommandList->ResourceBarrier(1, &barrier);
		}

		static const uint32 ComputeThreadBlockSize = 128;
		const uint32 NumGroupX = static_cast<uint32>(ceil(rootConstant.commandCount / float(ComputeThreadBlockSize)));
		computeCommandList->Dispatch(NumGroupX, 1, 1);

		{
			D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_processedCommandBuffers[m_frameIndex].Get()
				, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			computeCommandList->ResourceBarrier(1, &barrier);
		}

		auto fenceValue = computeCommandQueue.ExecuteCommandList(computeCommandList);
		computeCommandQueue.WaitForFenceValue(fenceValue);
	}

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			currentBackbuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		direcCommandList->ResourceBarrier(1, &barrier);
	}

	// Clear the render targets
	{
		{
			FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

			direcCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			direcCommandList->ClearDepthStencilView(m_dSVHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		const bool ShouldRecordBundleCommandList = IsUseBundleCommandList && !IsBundleCommandListRecorded;
		const bool ShouldRecordCommandList = !IsUseBundleCommandList || ShouldRecordBundleCommandList;
		if (ShouldRecordCommandList)
		{
			commandList->SetPipelineState(m_pipelineState.Get());

			commandList->SetGraphicsRootSignature(m_rootSignature.Get());

			ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
			commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

			int32 m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle(m_srvHeap->GetGPUDescriptorHandleForHeapStart(), HeapOffset::Num * m_frameIndex + HeapOffset::TextureSRV, m_cbvSrvDescriptorSize);

			commandList->SetGraphicsRootDescriptorTable(3, cbvSrvHandle);	// rootParameters에서 SRV가 어떤 Index에 들어간지 RootParameterIndex를 설정해야 함
			cbvSrvHandle.Offset(m_cbvSrvDescriptorSize);
			commandList->SetGraphicsRootDescriptorTable(4, cbvSrvHandle);

			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
			commandList->IASetIndexBuffer(&m_indexBufferView);

			commandList->RSSetViewports(1, &m_viewport);
			commandList->RSSetScissorRects(1, &m_scissorRect);

			commandList->OMSetRenderTargets(1, &rtvHandle, false, &m_dSVHeap->GetCPUDescriptorHandleForHeapStart());
		}

		// Update the MVP matrix
		{
			static MaterialConstants materialConstants;

			static uint32 PrevTickCount = GetTickCount();
			static uint32 AccumulatedTickCount = 0;
			AccumulatedTickCount += GetTickCount() - PrevTickCount;
			PrevTickCount = GetTickCount();

			if (AccumulatedTickCount > 1000)
			{
				AccumulatedTickCount = 0;
				materialConstants.MaterialIndex = (++materialConstants.MaterialIndex) % _countof(m_textureArray);
			}

			commandList->SetGraphicsRoot32BitConstants(1, sizeof(MaterialConstants) / 4, &materialConstants, 0);

			// Draw all of the triangles.
			uint32 CommandBufferOffset = 0;
			const uint32 CommandSizePerFrame = CubeCount * sizeof(IndirectCommand);
			commandList->ExecuteIndirect(m_commandSignature.Get(), CubeCount, m_processedCommandBuffers[m_frameIndex].Get()
				, 0, m_processedCommandBuffers[m_frameIndex].Get(), CommandBufferOffset);

			if (ShouldRecordBundleCommandList)
				commandList->Close();
		}

		if (ShouldRecordBundleCommandList)
		{
			direcCommandList->ExecuteBundle(commandList.Get());
		}
	}

	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_processedCommandBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);

		commandList->ResourceBarrier(1, &barrier);
	}

	// Present
	{
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				currentBackbuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			direcCommandList->ResourceBarrier(1, &barrier);
		}

		uint64 executedFenceValue = directCommandQueue.ExecuteCommandList(direcCommandList);
		directCommandQueue.WaitForFenceValue(executedFenceValue);
		// Present
		{
			UINT syncInterval = 1;	// vsyn
			UINT presentFlags = 0;
			HRESULT r = m_swapChain->Present(syncInterval, presentFlags);
			UINT currentBackbufferIndex = m_swapChain->GetCurrentBackBufferIndex();
		}
	}
}
