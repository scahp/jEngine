#pragma once
#include "jRHI.h"

#include <windows.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "jCommandQueue_DirectX12.h"


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
	void RenderTriangleTest();
	//////////////////////////////////////////////////////////////////////////

	// CubeTest
	void RenderCubeTest();
	//////////////////////////////////////////////////////////////////////////

	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device2> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
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

	jCommandQueue_DirectX12 directCommandQueue;
	jCommandQueue_DirectX12 copyCommandQueue;

	bool UpdateBufferResource(
		ComPtr<ID3D12Device2> InDevice
		, ComPtr<ID3D12GraphicsCommandList2> InCommandList
		, ComPtr<ID3D12Resource>& InDestinationResource
		, ComPtr<ID3D12Resource>& InIntermediateResource
		, size_t InNumOfElement, size_t InElementSize
		, const void* InData, D3D12_RESOURCE_FLAGS InFlags = D3D12_RESOURCE_FLAG_NONE)
	{
		const size_t bufferSize = InNumOfElement * InElementSize;

		if (FAILED(InDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)
			, D3D12_HEAP_FLAG_NONE
			, &CD3DX12_RESOURCE_DESC::Buffer(bufferSize, InFlags)
			, D3D12_RESOURCE_STATE_COPY_DEST
			, nullptr
			, IID_PPV_ARGS(&InDestinationResource))))
		{
			return false;
		}

		if (InData)
		{
			if (FAILED(InDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)
				, D3D12_HEAP_FLAG_NONE
				, &CD3DX12_RESOURCE_DESC::Buffer(bufferSize)
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&InIntermediateResource))))
			{
				return false;
			}

			D3D12_SUBRESOURCE_DATA subresourceData = {};
			subresourceData.pData = InData;
			subresourceData.RowPitch = bufferSize;
			subresourceData.SlicePitch = subresourceData.RowPitch;

			if (0 == UpdateSubresources(InCommandList.Get()
				, InDestinationResource.Get(), InIntermediateResource.Get()
				, 0, 0, 1, &subresourceData))
			{
				return false;
			}
		}

		return true;
	}
};

