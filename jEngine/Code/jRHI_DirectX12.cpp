
#include "pch.h"
#include "jRHI_DirectX12.h"

#include <queue>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxguid.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

CD3DX12_VIEWPORT m_viewport(0.0f, 0.0f, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));
CD3DX12_RECT m_scissorRect(0, 0, static_cast<LONG>(SCR_WIDTH), static_cast<LONG>(SCR_HEIGHT));

void WaitForPreviousFrame();


// Vertex data for a colored cube.
struct VertexPosColor
{
	XMFLOAT3 Position;
	XMFLOAT3 Color;
};

static VertexPosColor g_Vertices[8] = {
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
	{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
};

static WORD g_Indicies[36] =
{
	0, 1, 2, 0, 2, 3,
	4, 6, 5, 4, 7, 6,
	4, 5, 1, 4, 1, 0,
	3, 2, 6, 3, 6, 7,
	1, 5, 6, 1, 6, 2,
	4, 0, 3, 4, 3, 7
};

void jRHI_DirectX12::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	m_commandAllocator->Reset();

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->DrawInstanced(3, 1, 0, 0);

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	m_commandList->Close();
}

void jRHI_DirectX12::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	m_commandQueue->Signal(m_fence.Get(), fence);
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		m_fence->SetEventOnCompletion(fence, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
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

	//// Describe and create the command queue.
	//D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	//queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	//queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	//m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));

	D3D12_COMMAND_QUEUE_DESC directQueueDesc = {};
	directQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	directQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	directQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	directQueueDesc.NodeMask = 0;

	m_device->CreateCommandQueue(&directQueueDesc, IID_PPV_ARGS(&directCommandQueue));

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
		directCommandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
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

	//m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
	//LoadAssets();

//	// 3. 루트 시그니쳐를 생성함
//	// Create an empty root signature.
//	{
//		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
//		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
//		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
//		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
//
//		// Allow input layout and deny uneccessary access to certain pipeline stages.
//		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
//			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
//			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
//			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
//			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
//			D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
//
//		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
//		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);
//
//		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
//
//		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
//		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
//		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
//			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
//
//		ComPtr<ID3DBlob> signature;
//		ComPtr<ID3DBlob> error;
//		D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
//		m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
//	}
//
//	// 4. PSO 생성
//	//		1). VertexShader, PixelShader 생성
//	//		2). pipelinestate 생성
//	//		3). InputLayout 생성
//	//		4). RasterizerState 생성
//	// Create the pipeline state, which includes compiling and loading shaders.
//	{
//		ComPtr<ID3DBlob> vertexShader;
//		ComPtr<ID3DBlob> pixelShader;
//
//#if defined(_DEBUG)
//		// Enable better shader debugging with the graphics debugging tools.
//		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
//#else
//		UINT compileFlags = 0;
//#endif
//
//		D3DCompileFromFile(L"Shaders/HLSL/shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr);
//		D3DCompileFromFile(L"Shaders/HLSL/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr);
//
//		// Define the vertex input layout.
//		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
//		{
//			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
//			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
//		};
//
//		// Describe and create the graphics pipeline state object (PSO).
//		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
//		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
//		psoDesc.pRootSignature = m_rootSignature.Get();
//		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
//		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
//		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
//		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
//		psoDesc.DepthStencilState.DepthEnable = FALSE;
//		psoDesc.DepthStencilState.StencilEnable = FALSE;
//		psoDesc.SampleMask = UINT_MAX;
//		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
//		psoDesc.NumRenderTargets = 1;
//		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
//		psoDesc.SampleDesc.Count = 1;
//		m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
//	}
//
//	// 5. 커맨드리스트 생성
//	// Create the command list.
//	m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList));
//
//	// Command lists are created in the recording state, but there is nothing
//	// to record yet. The main loop expects it to be closed, so close it now.
//	m_commandList->Close();
//
//	// 6. 지오메트리 버퍼들 생성 (버택스, 인덱스)
//	// Create the vertex buffer.
//	{
//		struct Vertex
//		{
//			XMFLOAT3 position;
//			XMFLOAT4 color;
//		};
//
//		float m_aspectRatio = static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT);
//
//		// Define the geometry for a triangle.
//		Vertex triangleVertices[] =
//		{
//			{ { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
//			{ { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
//			{ { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
//		};
//
//		const UINT vertexBufferSize = sizeof(triangleVertices);
//
//		// Note: using upload heaps to transfer static data like vert buffers is not 
//		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
//		// over. Please read up on Default Heap usage. An upload heap is used here for 
//		// code simplicity and because there are very few verts to actually transfer.
//		m_device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&m_vertexBuffer));
//
//		// Copy the triangle data to the vertex buffer.
//		UINT8* pVertexDataBegin;
//		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
//		m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
//		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
//		m_vertexBuffer->Unmap(0, nullptr);
//
//		// Initialize the vertex buffer view.
//		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
//		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
//		m_vertexBufferView.SizeInBytes = vertexBufferSize;
//	}
//
//	// 7. 상수 버퍼 생성 및 데이터 채움
//	// Create the constant buffer.
//
//	// Describe and create a constant buffer view (CBV) descriptor heap.
//	// Flags indicate that this descriptor heap can be bound to the pipeline 
//	// and that descriptors contained in it can be referenced by a root table.
//	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
//	cbvHeapDesc.NumDescriptors = 1;
//	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
//	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
//	m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap));
//
//	struct SceneConstantBuffer
//	{
//		XMFLOAT4 offset;
//		float padding[60]; // Padding so the constant buffer is 256-byte aligned.
//	};
//	SceneConstantBuffer m_constantBufferData;
//	m_constantBufferData.offset.x = 0.4f;
//	m_constantBufferData.offset.y = 0.6f;
//	m_constantBufferData.offset.z = 0.7f;
//	m_constantBufferData.offset.w = 0.0f;
//	{
//		const UINT constantBufferSize = sizeof(SceneConstantBuffer);    // CB size is required to be 256-byte aligned.
//
//		m_device->CreateCommittedResource(
//			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//			D3D12_HEAP_FLAG_NONE,
//			&CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
//			D3D12_RESOURCE_STATE_GENERIC_READ,
//			nullptr,
//			IID_PPV_ARGS(&m_constantBuffer));
//
//		// Describe and create a constant buffer view.
//		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
//		cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
//		cbvDesc.SizeInBytes = constantBufferSize;
//		m_device->CreateConstantBufferView(&cbvDesc, m_cbvHeap->GetCPUDescriptorHandleForHeapStart());
//
//		// Map and initialize the constant buffer. We don't unmap this until the
//		// app closes. Keeping things mapped for the lifetime of the resource is okay.
//		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
//		UINT8* m_pCbvDataBegin = nullptr;
//		m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin));
//		memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
//	}
//
//	// 8. 동기화 오브젝트들 생성
//	// Create synchronization objects and wait until assets have been uploaded to the GPU.
//	{
//		m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
//		m_fenceValue = 1;
//
//		// Create an event handle to use for frame synchronization.
//		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
//		if (m_fenceEvent == nullptr)
//		{
//			HRESULT_FROM_WIN32(GetLastError());
//		}
//
//		// Wait for the command list to execute; we are reusing the same command 
//		// list in our main loop but for now, we just want to wait for setup to 
//		// complete before continuing.
//	}
//	// 9. 렌더링할 백버퍼를 얻어옴
//	WaitForPreviousFrame();

	LoadContent();

	m_device->CreateFence(directFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&directFence));
	directFenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

	m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&directCommandAllocator));
	m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, directCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&directCommandList));
	directCommandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), directCommandAllocator.Get());

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
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12GraphicsCommandList2> commandList;
	ComPtr<ID3D12Fence> fence;
	uint64_t fenceValue = 0;
	HANDLE fenceEvent;
	{
		D3D12_COMMAND_QUEUE_DESC copyqueueDesc = {};
		copyqueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
		copyqueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		copyqueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		copyqueueDesc.NodeMask = 0;
		m_device->CreateCommandQueue(&copyqueueDesc, IID_PPV_ARGS(&m_copyCommandQueue));
		m_device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

		m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&commandAllocator));
		m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	}

	// Upload vertex buffer data
	ComPtr<ID3D12Resource> intermediateVertexBuffer;

	{
		size_t numOfElements = _countof(g_Vertices);
		size_t elementSize = sizeof(VertexPosColor);
		const void* bufferData = g_Vertices;
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

		size_t bufferSize = numOfElements * elementSize;

		ID3D12Resource** pDestinationResource = &m_vertexBuffer;
		ID3D12Resource** pIntermediateResource = &intermediateVertexBuffer;

		m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(pDestinationResource));

		if (bufferData)
		{
			m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(pIntermediateResource));

			D3D12_SUBRESOURCE_DATA subresourceData = {};
			subresourceData.pData = bufferData;
			subresourceData.RowPitch = bufferSize;
			subresourceData.SlicePitch = subresourceData.RowPitch;

			UpdateSubresources(commandList.Get(), *pDestinationResource, *pIntermediateResource, 0, 0, 1, &subresourceData);
		}
	}

	// Create the vertex buffer view.
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.SizeInBytes = sizeof(g_Vertices);
	m_vertexBufferView.StrideInBytes = sizeof(VertexPosColor);

	// Upload index buffer data.
	ComPtr<ID3D12Resource> intermediateIndexBuffer;
	{
		size_t numOfElements = _countof(g_Indicies);
		size_t elementSize = sizeof(DWORD);
		const void* bufferData = g_Indicies;
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

		size_t bufferSize = numOfElements * elementSize;

		ID3D12Resource** pDestinationResource = &m_indexBuffer;
		ID3D12Resource** pIntermediateResource = &intermediateIndexBuffer;

		m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(pDestinationResource));

		if (bufferData)
		{
			m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(pIntermediateResource));

			D3D12_SUBRESOURCE_DATA subresourceData = {};
			subresourceData.pData = bufferData;
			subresourceData.RowPitch = bufferSize;
			subresourceData.SlicePitch = subresourceData.RowPitch;

			UpdateSubresources(commandList.Get(), *pDestinationResource, *pIntermediateResource, 0, 0, 1, &subresourceData);
		}
	}

	m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	m_indexBufferView.SizeInBytes = sizeof(g_Indicies);

	// Create the descriptor heap for the depth-stencil view.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dSVHeap));

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
	D3DCompileFromFile(L"Shaders/HLSL/CubeVertexShader.hlsl", nullptr, nullptr, "main", "vs_5_1", compileFlags, 0, &vertexShader, &compilationMsgs);
	if (compilationMsgs)
	{
		OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
		compilationMsgs->Release();
	}
	D3DCompileFromFile(L"Shaders/HLSL/CubePixelShader.hlsl", nullptr, nullptr, "main", "ps_5_1", compileFlags, 0, &pixelShader, &compilationMsgs);
	if (compilationMsgs)
	{
		OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
		compilationMsgs->Release();
	}

	// Create the vertex input layout
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	// A single 32-bit constant root parameter that is used by the vertex shader
	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

	// Serialize the root signature
	ComPtr<ID3DBlob> rootSignatureBlob;
	ComPtr<ID3DBlob> errorBlob;
	D3DX12SerializeVersionedRootSignature(&rootSignatureDescription, featureData.HighestVersion, &rootSignatureBlob, &errorBlob);

	// Create the root signature
	m_device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

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
	m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState));

	// fenceValue = m_copyCommandQueue->ExecuteCommandList(commandList);
	uint64_t curFenceValue = 0;
	{
		//// Keep track of command allocators that are "in-flight"
		//struct CommandAllocatorEntry
		//{
		//	uint64_t fenceValue;
		//	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
		//};

		//using CommandAllocatorQueue = std::queue<CommandAllocatorEntry>;
		//using CommandListQueue = std::queue< Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> >;

		//std::queue<CommandAllocatorEntry> commandAllocatorQueue;
		//CommandListQueue commandListQueue;

		commandList->Close();

		//ID3D12CommandAllocator* commandAllocator;
		//UINT dataSize = sizeof(commandAllocator);
		//commandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator);

		ID3D12CommandList* const ppCommandLists[] = { commandList.Get() };

		m_copyCommandQueue->ExecuteCommandLists(1, ppCommandLists);
		curFenceValue = ++fenceValue;
		m_copyCommandQueue->Signal(fence.Get(), fenceValue);

		//commandAllocatorQueue.emplace(CommandAllocatorEntry{ fenceValue, commandAllocator });
		//commandListQueue.push(commandList);

		//commandList->Release();
		//commandAllocator->Release();
	}
	if (fence->GetCompletedValue() >= curFenceValue)		// IsFenceComplete
	{
		fence->SetEventOnCompletion(curFenceValue, fenceEvent);
		::WaitForSingleObject(fenceEvent, DWORD_MAX);
	}

	// Flush any GPU commands that might be referencing the depth buffer
	// m_copyCommandQueue->Flush();
	// 위에있는 fence 기다리는것과 동일

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

void jRHI_DirectX12::RenderTriangleTest()
{
	// Record all the commands we need to render the scene into the command list.
//PopulateCommandList();
	{
		// [1]. 커맨드 생성기 초기화
		// Command list allocators can only be reset when the associated 
		// command lists have finished execution on the GPU; apps should use 
		// fences to determine GPU execution progress.
		m_commandAllocator->Reset();

		// [2]. PSO 바인딩
		// However, when ExecuteCommandList() is called on a particular command 
		// list, that command list can then be reset at any time and must be before 
		// re-recording.
		m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

		// [3]. 루트 시그니쳐 바인딩
		m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
		m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());

		// [4]. 렌더타겟 설정
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		// [5]. 기타 상태 설정
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);

		// [6]. 백버퍼 리소스 베리어 전환
		// Indicate that the back buffer will be used as a render target.
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		// [7]. 렌더타겟과 뎁스버퍼 클리어
		// Record commands.
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

		// [8]. 지오메트리 버퍼(버택스, 인덱스) 및 Primitive 타입 바인딩
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

		// [9]. 상수버퍼 바인딩

		// [10]. 렌더
		m_commandList->DrawInstanced(3, 1, 0, 0);

		// Indicate that the back buffer will now be used to present.
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

		m_commandList->Close();
	}

	// [11]. 커맨드버퍼 실행
	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// [12]. Present
	// Present the frame.
	m_swapChain->Present(1, 0);

	WaitForPreviousFrame();
}

void jRHI_DirectX12::RenderCubeTest()
{
	{
		static bool enable = 1;

		// Signal and increment the fence value.
		{
			const UINT64 fence = directFenceValue;
			directCommandQueue->Signal(directFence.Get(), fence);
			directFenceValue++;

			// Wait until the previous frame is finished.
			if (directFence->GetCompletedValue() < fence)
			{
				directFence->SetEventOnCompletion(fence, directFenceEvent);
				::WaitForSingleObject(directFenceEvent, INFINITE);
			}
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
		ComPtr<ID3D12Resource> currentBackbuffer = m_renderTargets[m_frameIndex];
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);

		// Clear the render targets
		if (enable)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				currentBackbuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			directCommandList->ResourceBarrier(1, &barrier);

			FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

			directCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			directCommandList->ClearDepthStencilView(m_dSVHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		if (enable)
		{
			directCommandList->SetPipelineState(m_pipelineState.Get());
			directCommandList->SetGraphicsRootSignature(m_rootSignature.Get());

			directCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			directCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
			directCommandList->IASetIndexBuffer(&m_indexBufferView);

			directCommandList->RSSetViewports(1, &m_viewport);
			directCommandList->RSSetScissorRects(1, &m_scissorRect);

			directCommandList->OMSetRenderTargets(1, &rtvHandle, false, &m_dSVHeap->GetCPUDescriptorHandleForHeapStart());
		}

		// Update the MVP matrix
		if (enable)
		{
			XMMATRIX m_ModelMatrix;
			XMMATRIX m_ViewMatrix;
			XMMATRIX m_ProjectionMatrix;

			static float t = 0.0f;
			t += 0.01f;

			// Update the model matrix.
			float angle = static_cast<float>(t * 90.0);
			const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
			m_ModelMatrix = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));

			// Update the view matrix.
			const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
			const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
			const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
			m_ViewMatrix = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

			// Update the projection matrix.
			float aspectRatio = SCR_WIDTH / static_cast<float>(SCR_HEIGHT);
			m_ProjectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(45), aspectRatio, 0.1f, 100.0f);

			XMMATRIX mvpMatrix = XMMatrixMultiply(m_ModelMatrix, m_ViewMatrix);
			mvpMatrix = XMMatrixMultiply(mvpMatrix, m_ProjectionMatrix);
			directCommandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvpMatrix, 0);

			directCommandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);
		}

		// Present
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				currentBackbuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			directCommandList->ResourceBarrier(1, &barrier);

			auto r = directCommandList->Close();

			ID3D12CommandAllocator* commandAllocator;
			UINT dataSize = sizeof(commandAllocator);
			directCommandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator);

			ID3D12CommandList* const ppCommandLists[] = {
				directCommandList.Get()
			};

			directCommandQueue->ExecuteCommandLists(1, ppCommandLists);
			uint64_t fenceValue = ++directFenceValue;
			auto r2 = directCommandQueue->Signal(directFence.Get(), fenceValue);
			//directCommandAllocator->Release();

			// Present
			{
				UINT syncInterval = 1;	// vsyn
				UINT presentFlags = 0;
				HRESULT r = m_swapChain->Present(syncInterval, presentFlags);
				UINT currentBackbufferIndex = m_swapChain->GetCurrentBackBufferIndex();

				if (directFence->GetCompletedValue() < fenceValue)
				{
					directFence->SetEventOnCompletion(directFenceValue, directFenceEvent);
					::WaitForSingleObject(directFenceEvent, DWORD_MAX);
				}
			}
		}
		directCommandAllocator->Reset();
		directCommandList->Reset(directCommandAllocator.Get(), NULL);
		directCommandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), directCommandAllocator.Get());
	}
}
