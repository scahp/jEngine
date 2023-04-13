#pragma once
#include "../jRHI.h"

#include <windows.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "jCommandQueue_DX12.h"
#include "jDescriptorHeap_DX12.h"
#include "jFenceManager_DX12.h"

class jSwapchain_DX12;
struct jBuffer_DX12;
struct jTexture_DX12;
struct jBuffer_DX12;


using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const uint32 cbvCountPerFrame = 3;
static constexpr DXGI_FORMAT BackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

struct GlobalRootSignatureParams {
	enum Value {
		OutputViewSlot = 0,
		AccelerationStructureSlot,
		SceneConstantSlot,
		VertexBuffersSlot,
        PlaneVertexBufferSlot,
		Count
	};
};

struct LocalRootSignatureParams {
	enum Value {
		CubeConstantSlot = 0,
		Count
	};
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

	ComPtr<ID3D12Resource> GetResource() const;

	void push_back(const ShaderRecord& InShaderRecord);

	// Pretty-print the shader records.
	void DebugPrint(robin_hood::unordered_map<void*, std::wstring> shaderIdToStringMap);
	
private:
	uint8* m_mappedShaderRecords = nullptr;
	uint32 m_shaderRecordSize = 0;
    jBuffer_DX12* Buffer = nullptr;
	std::vector<ShaderRecord> m_shaderRecords;

#if _DEBUG
	std::wstring m_name;
#endif
};

class jRHI_DX12 : public jRHI
{
public:
    static constexpr UINT MaxFrameCount = 3;

	static const wchar_t* c_raygenShaderName;
	static const wchar_t* c_closestHitShaderName;
	static const wchar_t* c_missShaderName;
    static const wchar_t* c_triHitGroupName;
    static const wchar_t* c_planeHitGroupName;
    static const wchar_t* c_planeclosestHitShaderName;

	jRHI_DX12();
	virtual ~jRHI_DX12();

	//////////////////////////////////////////////////////////////////////////
	// 1. Device
	ComPtr<ID3D12Device5> Device;
    uint32 Options = 0;
	ComPtr<IDXGIFactory5> Factory;

	//////////////////////////////////////////////////////////////////////////
	// 2. Command
	jCommandQueue_DX12* GraphicsCommandQueue = nullptr;
	jCommandQueue_DX12* CopyCommandQueue = nullptr;

	//////////////////////////////////////////////////////////////////////////
	// 3. Swapchain
	jSwapchain_DX12* SwapChain = nullptr;
	int32 CurrentFrameIndex = 0;

	//////////////////////////////////////////////////////////////////////////
	// 4. Heap
	jDescriptorHeap_DX12 RTVDescriptorHeap;
	jDescriptorHeap_DX12 DSVDescriptorHeap;
	jDescriptorHeap_DX12 SRVDescriptorHeap;
	jDescriptorHeap_DX12 SamplerDescriptorHeap;		// SamplerState test
	//jDescriptorHeap_DX12 UAVDescriptorHeap;

    //////////////////////////////////////////////////////////////////////////
    // 5. Initialize Camera and lighting
    struct CubeConstantBuffer
    {
        XMFLOAT4 albedo;
    };
    struct SceneConstantBuffer
    {
        XMMATRIX projectionToWorld;
        XMVECTOR cameraPosition;
        XMVECTOR lightPosition;
        XMVECTOR lightAmbientColor;
        XMVECTOR lightDiffuseColor;
        XMVECTOR cameraDirection;
        uint32 NumOfStartingRay;
        float focalDistance;
        float lensRadius;
    };
    SceneConstantBuffer m_sceneCB[MaxFrameCount];
    CubeConstantBuffer m_cubeCB;
    CubeConstantBuffer m_planeCB;

	//////////////////////////////////////////////////////////////////////////
	// 6. CommandAllocators, Commandlist, RTV for FrameCount

	//////////////////////////////////////////////////////////////////////////
	// 7. Create sync object
	jFenceManager_DX12 FenceManager;
	void WaitForGPU();

	//////////////////////////////////////////////////////////////////////////
	// 8. Raytracing device and commandlist

	////////////////////////////////////////////////////////////////////////////
	//// 9. CreateRootSignature
	//ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
	//ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;
 //   ComPtr<ID3D12RootSignature> m_raytracingEmptyLocalRootSignature;

	////////////////////////////////////////////////////////////////////////////
	//// 10. DXR PipeplineStateObject
	//ComPtr<ID3D12StateObject> m_dxrStateObject;

	//////////////////////////////////////////////////////////////////////////
	// 11. Create vertex and index buffer
	jBuffer_DX12* VertexBuffer = nullptr;
	jBuffer_DX12* IndexBuffer = nullptr;
	//jBuffer_DX12* VertexBufferSecondGeometry = nullptr;

	////////////////////////////////////////////////////////////////////////////
	//// 12. AccelerationStructures
	//jBuffer_DX12* BottomLevelAccelerationStructureBuffer = nullptr;
	//jBuffer_DX12* BottomLevelAccelerationStructureSecondGeometryBuffer = nullptr;

	////////////////////////////////////////////////////////////////////////////
	//// 13. ShaderTable
	//ComPtr<ID3D12Resource> m_rayGenShaderTable;
	//ComPtr<ID3D12Resource> m_missShaderTable;
	//ComPtr<ID3D12Resource> m_hitGroupShaderTable;

	ComPtr<ID3D12RootSignature> SimpleRootSignature;
	ComPtr<ID3D12PipelineState> SimplePipelineState;
    jDescriptor_DX12 SimpleSamplerState;					// SamplerState test

	//////////////////////////////////////////////////////////////////////////
	// 14. Raytracing Output Resouce
	jTexture_DX12* RayTacingOutputTexture = nullptr;

    HWND m_hWnd = 0;

    //////////////////////////////////////////////////////////////////////////
    // ImGui
    ComPtr<ID3D12DescriptorHeap> m_imgui_SrvDescHeap;

    void InitializeImGui();
    void ReleaseImGui();
    void RenderUI(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pRenderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle
        , ID3D12DescriptorHeap* pDescriptorHeap, D3D12_RESOURCE_STATES beforeResourceState, D3D12_RESOURCE_STATES afterResourceState);

    float m_focalDistance = 10.0f;
    float m_lensRadius = 0.2f;
    //////////////////////////////////////////////////////////////////////////

	virtual bool InitRHI() override;
    bool Run();
	void Release();
    
    uint32 AdapterID = -1;
    std::wstring AdapterName;
    void CalculateFrameStats();

	HWND CreateMainWindow() const;

	void Update();
	void Render();

	void OnDeviceLost();
	void OnDeviceRestored();

	// Raytracing scene
	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT3 normal;
	};

	XMVECTOR m_eye;
	XMVECTOR m_at;
	XMVECTOR m_up;
	void UpdateCameraMatrices();

	static_assert(sizeof(SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here");

	union AlignedSceneConstantBuffer
	{
		SceneConstantBuffer constants;
		uint8 alignedPadding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
	};
	jBuffer_DX12* PerFrameConstantBuffer = nullptr;

  //  struct TopLevelAccelerationStructureBuffers
  //  {
		//jBuffer_DX12* Scratch = nullptr;
		//jBuffer_DX12* Result = nullptr;
		//jBuffer_DX12* InstanceDesc = nullptr;    // Used only for top-level AS

		//void Release();
  //  };
  //  TopLevelAccelerationStructureBuffers TLASBuffer;

  //  bool BuildTopLevelAS(ComPtr<ID3D12GraphicsCommandList4>& InCommandList, TopLevelAccelerationStructureBuffers& InBuffers, bool InIsUpdate, float InRotationY, Vector InTranslation);

	jBuffer_DX12* SimpleConstantBuffer = nullptr;
	jBuffer_DX12* SimpleStructuredBuffer = nullptr;			// StructuredBuffer test
	jTexture_DX12* SimpleTexture = nullptr;					// Texture test
	jTexture_DX12* SimpleTextureCube = nullptr;				// Cube texture test

    bool OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized);
    bool OnHandleDeviceLost();
    bool OnHandleDeviceRestored();

	ComPtr<ID3D12GraphicsCommandList4> BeginSingleTimeCopyCommands();
    void EndSingleTimeCopyCommands(const ComPtr<ID3D12GraphicsCommandList4>& commandBuffer);

	virtual jTexture* CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const override;
	virtual jFenceManager* GetFenceManager() override { return &FenceManager; }
};

extern jRHI_DX12* g_rhi_dx12;
