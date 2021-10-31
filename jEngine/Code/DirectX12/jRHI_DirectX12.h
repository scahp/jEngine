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
	ComPtr<ID3D12DescriptorHeap> m_cbvHeap;

	// App resources.
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	ComPtr<ID3D12Resource> m_depthBuffer;
	ComPtr<ID3D12Resource> m_indexBuffer;
	ComPtr<ID3D12DescriptorHeap> m_dSVHeap;
	ComPtr<ID3D12Resource> m_texture;
	ComPtr<ID3D12Resource> m_textureArray[3];
	ComPtr<ID3D12DescriptorHeap> m_srvHeap;

	// Compute shader
	struct InstanceConstantBuffer
	{
		Vector4 Color = Vector4::ColorRed;
		Vector4 padding[15]; // to be aligned 256 byte.
		enum
		{
			SizeWithoutPadding = sizeof(Color),
			Size = (sizeof(Color) + sizeof(padding))
		};
	};
	struct MVPConstantBuffer
	{
		XMMATRIX MVP;
		Vector4 padding[12];	// to be aligned 256 byte.
	};
	// Data structure to match the command signature used for ExecuteIndirect.
	struct IndirectCommand
	{
		D3D12_GPU_VIRTUAL_ADDRESS cbvMVP;
		D3D12_GPU_VIRTUAL_ADDRESS cbvInstanceBuffer;
		D3D12_DRAW_INDEXED_ARGUMENTS drawArguments;
		float padding;									// align to multiple of 8
		//float padding[55];	// to be aligned 256 byte.

		//enum
		//{
		//	SizeWithoutPadding = sizeof(cbvMVP) + sizeof(cbvInstanceBuffer) + sizeof(drawArguments),
		//	Size = (sizeof(cbvMVP) + sizeof(cbvInstanceBuffer) + sizeof(drawArguments) + sizeof(padding))
		//};
	};
	ComPtr<ID3D12RootSignature> m_computeRootSignature;
	ComPtr<ID3D12PipelineState> m_computeState;
	ComPtr<ID3D12Resource> m_constantInstanceBuffer;
	std::vector<InstanceConstantBuffer> m_constantInstanceBufferData;
	ComPtr<ID3D12Resource> m_constantMVPBuffer;
	std::vector<MVPConstantBuffer> m_constantMVPBufferData;
	std::vector<IndirectCommand> commands;
	ComPtr<ID3D12Resource> m_commandBuffer;
	ComPtr<ID3D12Resource> m_processedCommandBuffers[FrameCount];
	ComPtr<ID3D12Resource> m_processedCommandBufferCounterReset;
	ComPtr<ID3D12CommandSignature> m_commandSignature;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	jCommandQueue_DirectX12 directCommandQueue;
	jCommandQueue_DirectX12 bundleCommandQueue;
	jCommandQueue_DirectX12 copyCommandQueue;
	jCommandQueue_DirectX12 computeCommandQueue;

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

	bool UpdateBufferResourceWithDesc(
		ComPtr<ID3D12Device2> InDevice
		, ComPtr<ID3D12GraphicsCommandList2> InCommandList
		, ComPtr<ID3D12Resource>& InDestinationResource
		, ComPtr<ID3D12Resource>& InIntermediateResource
		, size_t InNumOfElement, size_t InElementSize
		, const D3D12_RESOURCE_DESC& InDesc
		, const void* InData, D3D12_RESOURCE_FLAGS InFlags = D3D12_RESOURCE_FLAG_NONE)
	{
		const size_t bufferSize = InNumOfElement * InElementSize;

		if (FAILED(InDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)
			, D3D12_HEAP_FLAG_NONE
			, &InDesc
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
			subresourceData.RowPitch = InDesc.Width * InElementSize;
			subresourceData.SlicePitch = subresourceData.RowPitch * InDesc.Height;

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

