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
};

static VertexPosColor g_Vertices[] = {
	// Cube face 0
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },

	// Cube face 1
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },

	// Cube face 2
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },

	// Cube face 3
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, 1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, 1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, 1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },

	// Cube face 4
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },

	// Cube face 5
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f), },
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

static VertexPosColor g_Quad[] = {
	{ XMFLOAT3(-1.0f, -1.0f, -7.f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
	{ XMFLOAT3(-1.0f,  1.0f, -7.), XMFLOAT3(0.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f, -7.), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f,  1.0f, -7.), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(1.0f, -1.0f, -7.), XMFLOAT3(1.0f, 1.0f, 0.0f), },
	{ XMFLOAT3(-1.0f, -1.0f, -7.f), XMFLOAT3(1.0f, 0.0f, 0.0f), },
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

	// CommandQueue를 명세하고 생성한다.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
		return;

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
		m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
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

	// 힙 생성
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
			return;
			
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
			return;

		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
		cbvHeapDesc.NumDescriptors = cbvCountPerFrame * FrameCount;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap))))
			return;

		D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
		queryHeapDesc.Count = 1;
		queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
		if (FAILED(m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap))))
			return;

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// 프레임 리소스 생성
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// RTV와 커맨드 할당자 생성
		for (uint32 i = 0; i < FrameCount; ++i)
		{
			if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
				return;

			m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);

			if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]))) )
				return;
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

void jRHI_DirectX12::Release()
{
	m_fence.Reset();
	m_commandQueue.Reset();
	for (auto& rt : m_renderTargets)
	{
		rt.Reset();
	}
	m_swapChain.Reset();
	m_device.Reset();
}

void jRHI_DirectX12::LoadContent()
{
	// 루트 시그니처 생성
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		CD3DX12_ROOT_PARAMETER1 rootParameters[1];

		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);		// CBV 1개를 만들어서
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);					// VertexShader에 전달할 예정

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error)))
			return;

		if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
			return;
	}

	// 파이프라인 스테이트 생성
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	uint32 compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32 compileFlags = 0;
#endif
		ID3DBlob* compilationMsgs = nullptr;
		if (FAILED(D3DCompileFromFile(L"Shaders/HLSL/Prediction.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &compilationMsgs)))
		{
			if (compilationMsgs)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
				compilationMsgs->Release();
			}
			return;
		}
		if (FAILED(D3DCompileFromFile(L"Shaders/HLSL/Prediction.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &compilationMsgs)))
		{
			if (compilationMsgs)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(compilationMsgs->GetBufferPointer()));
				compilationMsgs->Release();
			}
			return;
		}

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = 
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
        blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].LogicOpEnable = false;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState))))
			return;

		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_queryState))))
			return;
	}

	// 커맨드 리스트 생성
	if (m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get()
		, m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)))
	{
		return;
	}

	// ComPtr은 CPU 객체이지만 GPU에서 실행을 마칠때 까지 커맨드리스트가 레퍼런스를 가지고 있음.
	// 그래서 이 함수 끝에서 GPU를 Flush 시킴.
	ComPtr<ID3D12Resource> vertexBufferUpload;
	ComPtr<ID3D12Resource> vertexBufferUpload2;
	ComPtr<ID3D12Resource> indexBufferUpload;

	// CreateVertexBuffer
	{
		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(g_Vertices)),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer))))
		{
			return;
		}

		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(g_Vertices)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBufferUpload))))
		{
			return;
		}

		// 중간 업로드 힙에 복사하고나서 업로드 힙에서 버택스 버퍼로 복사하는 것을 스케줄링 함.
		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData = reinterpret_cast<UINT8*>(g_Vertices);
		vertexData.RowPitch = sizeof(g_Vertices);
		vertexData.SlicePitch = vertexData.RowPitch;

		UpdateSubresources<1>(m_commandList.Get(), m_vertexBuffer.Get()
			, vertexBufferUpload.Get(), 0, 0, 1, &vertexData);

		// 버택스 버퍼 뷰 초기화
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(VertexPosColor);
		m_vertexBufferView.SizeInBytes = sizeof(g_Vertices);
	}

	// CreateVertexBuffer2
	{
		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(g_Quad)),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer2))))
		{
			return;
		}

		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(g_Quad)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBufferUpload2))))
		{
			return;
		}

		// 중간 업로드 힙에 복사하고나서 업로드 힙에서 버택스 버퍼로 복사하는 것을 스케줄링 함.
		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData = reinterpret_cast<UINT8*>(g_Quad);
		vertexData.RowPitch = sizeof(g_Quad);
		vertexData.SlicePitch = vertexData.RowPitch;

		UpdateSubresources<1>(m_commandList.Get(), m_vertexBuffer2.Get()
			, vertexBufferUpload2.Get(), 0, 0, 1, &vertexData);

		// 버택스 버퍼 뷰 초기화
		m_vertexBufferView2.BufferLocation = m_vertexBuffer2->GetGPUVirtualAddress();
		m_vertexBufferView2.StrideInBytes = sizeof(VertexPosColor);
		m_vertexBufferView2.SizeInBytes = sizeof(g_Quad);
	}

	// Create IndexBuffer
	{
		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(g_Indicies)),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_indexBuffer))))
		{
			return;
		}

		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(g_Indicies)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&indexBufferUpload))))
		{
			return;
		}

		// 중간 업로드 힙에 복사하고나서 업로드 힙에서 인덱스 버퍼로 복사하는 것을 스케쥴링 함
		D3D12_SUBRESOURCE_DATA indexData = {};
		indexData.pData = reinterpret_cast<uint8*>(g_Indicies);
		indexData.RowPitch = sizeof(g_Indicies);
		indexData.SlicePitch = indexData.RowPitch;

		UpdateSubresources<1>(m_commandList.Get(), m_indexBuffer.Get()
			, indexBufferUpload.Get(), 0, 0, 1, &indexData);

		// 인덱스 버퍼 뷰 초기화
		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
		m_indexBufferView.SizeInBytes = sizeof(g_Indicies);
	}

	// constant buffer 생성
	{
		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(FrameCount * sizeof(m_constantBufferData)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffer))))
		{
			return;
		}

		// 앱 종료전 까지 unmap을 하지 않음.
		CD3DX12_RANGE readRange(0, 0);		// CPU 에서 읽지 않도록 설정
		if (FAILED(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin))))
			return;

		ZeroMemory(m_pCbvDataBegin, FrameCount * sizeof(m_constantBufferData));

		// upload buffer에 접근하기 위해서 constant buffer 생성
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_cbvHeap->GetCPUDescriptorHandleForHeapStart());
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = m_constantBuffer->GetGPUVirtualAddress();

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.SizeInBytes = sizeof(SceneConstantBuffer);

		m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		for (uint32 i = 0; i < FrameCount; ++i)
		{
			cbvDesc.BufferLocation = gpuAddress;

			m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);

			cpuHandle.Offset(m_cbvSrvDescriptorSize);
			gpuAddress += cbvDesc.SizeInBytes;
			cbvDesc.BufferLocation = gpuAddress;

			m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);

			cpuHandle.Offset(m_cbvSrvDescriptorSize);
			gpuAddress += cbvDesc.SizeInBytes;
			cbvDesc.BufferLocation = gpuAddress;

			m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);

			cpuHandle.Offset(m_cbvSrvDescriptorSize);
			gpuAddress += cbvDesc.SizeInBytes;
		}
	}

	// depth stencil view 생성
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
		depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, SCR_WIDTH, SCR_HEIGHT
				, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depthBuffer))))
		{
			return;
		}

		m_device->CreateDepthStencilView(m_depthBuffer.Get(), &depthStencilDesc
			, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// query result buffer 생성
	{
		D3D12_RESOURCE_DESC queryResultDesc = CD3DX12_RESOURCE_DESC::Buffer(8);
		if (FAILED(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&queryResultDesc,
			D3D12_RESOURCE_STATE_PREDICATION,
			nullptr,
			IID_PPV_ARGS(&m_queryResult)
		)))
		{
			return;
		}
	}

	// 커맨드 리스트를 닫고, 버택스 버퍼를 기본 힙에 복사하기 시작함.

	if (FAILED(m_commandList->Close()))
		return;

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// synchronization 객체 생성하고, 에셋이 GPU에 업로드 완료 될때까지 기다린다.
	{
		if (FAILED(m_device->CreateFence(m_fenceValue[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
			return;

		m_fenceValue[m_frameIndex]++;

		// frame synchonization 에 사용할 event handle을 생성
		m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
		if (!m_fenceEvent)
		{
			if (FAILED(HRESULT_FROM_WIN32(GetLastError())))
				return;
		}

		// 커맨드 리스트의 실행을 기다린다; 같은 커맨드 리스트를 메인루프에서 사용함
		// 계속 하기전에 설정들을 완료하려고 함.
		// Wait for GPU
		{
			if (FAILED(m_commandQueue->Signal(m_fence.Get(), m_fenceValue[m_frameIndex])))
				return;

			if (FAILED(m_fence->SetEventOnCompletion(m_fenceValue[m_frameIndex], m_fenceEvent)))
				return;

			WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);

			m_fenceValue[m_frameIndex]++;
		}
	}
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
	const float translationSpeed = 0.03f;
	const float offsetBounds = 3.0f;

	XMMATRIX VP;
	const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
	const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
	const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
	VP = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

	float aspectRatio = SCR_WIDTH / static_cast<float>(SCR_HEIGHT);
	VP = XMMatrixMultiply(VP, XMMatrixPerspectiveFovLH(XMConvertToRadians(45), aspectRatio, 0.1f, 100.0f));

	// Animate near quad
	m_constantBufferData[1].offset.x += translationSpeed;
	if (m_constantBufferData[1].offset.x > offsetBounds)
	{
		m_constantBufferData[1].offset.x = -offsetBounds;
	}

	static float t = 0.0f;
	t += 0.01f;
	float angle = static_cast<float>(t * 90.0);
	const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);

	XMMATRIX S = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	XMMATRIX R = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));
	XMMATRIX T = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	XMMATRIX ModelMatrix = XMMatrixMultiply(XMMatrixMultiply(S, R), T);
	XMMATRIX MVP = XMMatrixMultiply(ModelMatrix, VP);
	m_constantBufferData[0].MVP = MVP;			// Cube
	m_constantBufferData[1].MVP = VP;			// Quad

	S = XMMatrixScaling(1.01f, 1.01f, 1.01f);	// added 0.01f to avoid z-fighting
	ModelMatrix = XMMatrixMultiply(XMMatrixMultiply(S, R), T);
	MVP = XMMatrixMultiply(ModelMatrix, VP);
	m_constantBufferData[2].MVP = MVP;			// Cube for occlusion test

	for (int32 i = 0; i < cbvCountPerFrame; ++i)
	{
		uint32 cbvIndex = m_frameIndex * cbvCountPerFrame + i;
		uint8* destination = m_pCbvDataBegin + (cbvIndex * sizeof(SceneConstantBuffer));
		memcpy(destination, &m_constantBufferData[i], sizeof(SceneConstantBuffer));
	}
	//////////////////////////////////////////////////////////////////////////

	// PopulateCommandList

	// 커맨드 리스트 할당자는 관련된 커맨드 리스트가 모두 GPU에서 실행을 마쳤을 때 리셋 될 수 있음; 앱은 GPU의 실행되는 것을 결정하기 위해서 팬스를 써야 함.
	if (FAILED(m_commandAllocators[m_frameIndex]->Reset()))
		return;

	if (FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get())))
		return;

	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// 백버퍼가 렌더타겟으로 사용될 것이라고 해줌
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get()
		, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 커맨드 녹화
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// Quad를 그리고 오클루젼 쿼리를 수행
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvFarCube(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), m_frameIndex * cbvCountPerFrame, m_cbvSrvDescriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvNearQuad(cbvFarCube, m_cbvSrvDescriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvFarCubeForOcclusion(cbvNearQuad, m_cbvSrvDescriptorSize);

		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->IASetIndexBuffer(&m_indexBufferView);

		// far quad를 이전 프레임의 오클루젼 쿼리의 결과를 기반으로 그릴지 결정한다.
		m_commandList->SetGraphicsRootDescriptorTable(0, cbvFarCube);
		m_commandList->SetPredication(m_queryResult.Get(), 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
		m_commandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);

		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView2);
		m_commandList->IASetIndexBuffer(nullptr);

		// near quad는 predication을 꺼서 항상 그려지게 한다.
		m_commandList->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
		m_commandList->SetGraphicsRootDescriptorTable(0, cbvNearQuad);
		m_commandList->DrawInstanced(6, 1, 0, 0);

		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->IASetIndexBuffer(&m_indexBufferView);

		// quad의 바운딩 박스로 오클루전 쿼리를 실행한다.
		m_commandList->SetGraphicsRootDescriptorTable(0, cbvFarCubeForOcclusion);
		m_commandList->SetPipelineState(m_queryState.Get());
		m_commandList->BeginQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
		m_commandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);
		m_commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
	
		// 오클루전 쿼리를 마치고, 다음 프레임에 사용하기 위해서 결과를 query result buffer에 저장한다.
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_queryResult.Get()
			, D3D12_RESOURCE_STATE_PREDICATION, D3D12_RESOURCE_STATE_COPY_DEST));
		m_commandList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION
			, 0, 1, m_queryResult.Get(), 0);
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_queryResult.Get()
			, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION));
	}

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get()
		, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	if (FAILED(m_commandList->Close()))
		return;

	// 커맨들 리스트 실행
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// 스왑체인에 현재 프레임 제출
	if (FAILED(m_swapChain->Present(1, 0)))
		return;

	// 다음 프레임으로 이동
	const uint64 currentFenceValue = m_fenceValue[m_frameIndex];
	if (FAILED(m_commandQueue->Signal(m_fence.Get(), currentFenceValue)))
		return;

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (m_fence->GetCompletedValue() < m_fenceValue[m_frameIndex])
	{
		if (FAILED(m_fence->SetEventOnCompletion(m_fenceValue[m_frameIndex], m_fenceEvent)))
			return;
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
	}

	m_fenceValue[m_frameIndex] = currentFenceValue + 1;
}
