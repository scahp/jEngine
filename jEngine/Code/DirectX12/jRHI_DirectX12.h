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
static const uint32 cbvCountPerFrame = 3;
static constexpr DXGI_FORMAT BackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

struct GlobalRootSignatureParams {
	enum Value {
		OutputViewSlot = 0,
		AccelerationStructureSlot,
		Count
	};
};

struct LocalRootSignatureParams {
	enum Value {
		ViewportConstantSlot = 0,
		Count
	};
};

struct BufferUtil
{
	static bool AllocateUploadBuffer(ID3D12Resource** OutResource, ID3D12Device* InDevice
		, void* InData, uint64 InDataSize, const wchar_t* resourceName = nullptr);

	static bool AllocateUploadBuffer(ID3D12Resource** OutResource, ID3D12Device* InDevice
		, const wchar_t* resourceName = nullptr)
	{
		return AllocateUploadBuffer(OutResource, InDevice, nullptr, 0, resourceName);
	}

	static bool AllocateUAVBuffer(ID3D12Resource** OutResource, ID3D12Device* InDevice
		, uint64 InBufferSize, D3D12_RESOURCE_STATES InInitialResourceState = D3D12_RESOURCE_STATE_COMMON
		, const wchar_t* resourceName = nullptr);
};

inline UINT Align(UINT size, UINT alignment)
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

// Shader record = {{Shader ID}, {RootArguments}}
struct ShaderRecord
{
	ShaderRecord(void* InShaderIdentifier, uint32 InShaderIdentifierSize)
		: m_shaderIdentifier(InShaderIdentifier), m_shaderIdentifierSize(InShaderIdentifierSize)
	{}

	ShaderRecord(void* InShaderIdentifier, uint32 InShaderIdentifierSize
		, void* InLocalRootArugments, uint32 InLocalRootArgumentsSize)
		: m_shaderIdentifier(InShaderIdentifier), m_shaderIdentifierSize(InShaderIdentifierSize)
		, m_localRootArguments(InLocalRootArugments), m_localRootArgumentsSize(InLocalRootArgumentsSize)
	{}

	void* m_shaderIdentifier = nullptr;
	uint32 m_shaderIdentifierSize = 0;

	void* m_localRootArguments = nullptr;
	uint32 m_localRootArgumentsSize = 0;
};

// Shader table = {{ ShaderRecord 1}, {ShaderRecord 2}, ...}
class ShaderTable
{
public:
	ShaderTable() {}
	ShaderTable(ID3D12Device* InDevice, uint32 InNumOfShaderRecords, uint32 InShaderRecordSize
		, const wchar_t* InResourceName = nullptr);

	ComPtr<ID3D12Resource> GetResource() { return m_resource; }

	void push_back(const ShaderRecord& InShaderRecord);

	// Pretty-print the shader records.
	void DebugPrint(std::unordered_map<void*, std::wstring> shaderIdToStringMap);
	
private:
	uint8* m_mappedShaderRecords = nullptr;
	uint32 m_shaderRecordSize = 0;
	std::vector<ShaderRecord> m_shaderRecords;
	ComPtr<ID3D12Resource> m_resource;

#if _DEBUG
	std::wstring m_name;
#endif
};

class jRHI_DirectX12 : public jRHI
{
public:
	static const wchar_t* jRHI_DirectX12::c_hitGroupName;
	static const wchar_t* jRHI_DirectX12::c_raygenShaderName;
	static const wchar_t* jRHI_DirectX12::c_closestHitShaderName;
	static const wchar_t* jRHI_DirectX12::c_missShaderName;

	//////////////////////////////////////////////////////////////////////////
	// 1. Device
	ComPtr<ID3D12Device> m_device;

	//////////////////////////////////////////////////////////////////////////
	// 2. Command
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator[FrameCount];
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

	//////////////////////////////////////////////////////////////////////////
	// 3. Swapchain
	int32 m_frameIndex = 0;
	ComPtr<IDXGISwapChain3> m_swapChain;

	//////////////////////////////////////////////////////////////////////////
	// 4. Heap
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
	uint32 m_rtvDescriptorSize = 0;
	uint32 m_cbvDescriptorSize = 0;

	//////////////////////////////////////////////////////////////////////////
	// 5. CommandAllocators, Commandlist, RTV for FrameCount
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

	//////////////////////////////////////////////////////////////////////////
	// 6. Create sync object
	HANDLE m_fenceEvent = nullptr;
	uint64 m_fenceValue[FrameCount]{};
	ComPtr<ID3D12Fence> m_fence;
	void WaitForGPU();

	//////////////////////////////////////////////////////////////////////////
	// 7. Raytracing device and commandlist
	ComPtr<ID3D12Device5> m_dxrDevice;
	ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList;

	//////////////////////////////////////////////////////////////////////////
	// 8. CreateRootSignature
	ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
	ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;

	struct Viewport
	{
		float left;
		float top;
		float right;
		float bottom;
	};

	struct RayGenConstantBuffer
	{
		Viewport viewport;
		Viewport stencil;
	};
	RayGenConstantBuffer m_rayGenCB;

	//////////////////////////////////////////////////////////////////////////
	// 9. DXR PipeplineStateObject
	ComPtr<ID3D12StateObject> m_dxrStateObject;

	//////////////////////////////////////////////////////////////////////////
	// 10. Create vertex and index buffer
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

	//////////////////////////////////////////////////////////////////////////
	// 11. AccelerationStructures
	ComPtr<ID3D12Resource> m_bottomLevelAccelerationStructure;
	ComPtr<ID3D12Resource> m_topLevelAccelerationStructure;

	//////////////////////////////////////////////////////////////////////////
	// 12. ShaderTable
	ComPtr<ID3D12Resource> m_rayGenShaderTable;
	ComPtr<ID3D12Resource> m_missShaderTable;
	ComPtr<ID3D12Resource> m_hitGroupShaderTable;

	//////////////////////////////////////////////////////////////////////////
	// 13. Raytracing Output Resouce
	ComPtr<ID3D12Resource> m_raytracingOutput;
	D3D12_GPU_DESCRIPTOR_HANDLE m_raytracingOutputResourceUAVGpuDescriptor;
	uint32 m_raytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;
	uint32 m_descriptorsAllocated = 0;

	void Initialize();
	void Release();

	HWND CreateMainWindow() const;

	void Render();

	void OnDeviceLost();
	void OnDeviceRestored();
};

