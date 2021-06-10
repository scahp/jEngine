#pragma once
#include "jRHI.h"

#include <windows.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>


using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const UINT FrameCount = 2;

class jRHI_DirectX12 : public jRHI
{
public:
	void Initialize();
	void LoadContent();

	HWND CreateMainWindow() const;

	// TriangleTest function
	void PopulateCommandList();
	void WaitForPreviousFrame();
	void RenderTriangleTest();
	//////////////////////////////////////////////////////////////////////////

	// CubeTest
	void RenderCubeTest();
	//////////////////////////////////////////////////////////////////////////

	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device2> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandQueue> m_copyCommandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12GraphicsCommandList> m_bundle;
	UINT m_rtvDescriptorSize;
	ComPtr<ID3D12Resource> m_constantBuffer;
	ComPtr<ID3D12DescriptorHeap> m_cbvHeap;

	// App resources.
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	ComPtr<ID3D12Resource> m_depthBuffer;
	ComPtr<ID3D12Resource> m_indexBuffer;
	ComPtr<ID3D12DescriptorHeap> m_dSVHeap;


	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;



	HANDLE directFenceEvent;
	ComPtr<ID3D12Fence> directFence;
	UINT64 directFenceValue = 0;
	ComPtr<ID3D12CommandQueue> directCommandQueue;
	ComPtr<ID3D12CommandAllocator> directCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList2> directCommandList;
};

