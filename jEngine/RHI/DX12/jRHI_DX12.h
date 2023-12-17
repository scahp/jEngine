#pragma once
#include "../jRHI.h"

#include <windows.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "jCommandBufferManager_DX12.h"
#include "jDescriptorHeap_DX12.h"
#include "jFenceManager_DX12.h"
#include "jUniformBufferBlock_DX12.h"
#include "jPipelineStateInfo_DX12.h"
#include "Renderer/jDrawCommand.h"
#include "jSwapchain_DX12.h"
#include "jQueryPoolTime_DX12.h"
#include "jRenderPass_DX12.h"

class jSwapchain_DX12;
struct jBuffer_DX12;
struct jTexture_DX12;
struct jBuffer_DX12;
struct jRingBuffer_DX12;
struct jVertexBuffer_DX12;
struct jIndexBuffer_DX12;
struct jPipelineStateInfo_DX12;

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

struct jPlacedResource
{
    bool IsValid() const { return Size > 0 && PlacedSubResource.Get(); }
    ComPtr<ID3D12Resource> PlacedSubResource;
    size_t Size = 0;
	bool IsUploadResource = false;
};

// Resuse for PlacedResource for DX12
struct jPlacedResourcePool
{
    enum class EPoolSizeType : uint8
    {
        E128,
        E256,
        E512,
        E1K,
        E2K,
        E4K,
        E8K,
        E16K,
		E1M,
		E10M,
		E100M,
        MAX
    };

    constexpr static uint64 MemorySize[(int32)EPoolSizeType::MAX] =
    {
        128,					// E128
        256,					// E256
        512,					// E512
        1024,					// E1K 
        2048,					// E2K 
        4096,					// E4K 
        8192,					// E8K 
        16 * 1024,				// E16K
		1024 * 1024,			// E1M
		10 * 1024 * 1024,		// E10M
		100 * 1024 * 1024,      // E100M
    };

    void Init();
    void Release();

    const jPlacedResource Alloc(size_t InRequestedSize, bool InIsUploadResource)
    {
        jScopedLock s(&Lock);

		auto& PendingList = GetPendingPlacedResources(InIsUploadResource, InRequestedSize);
        for (int32 i = 0; i < (int32)PendingList.size(); ++i)
        {
            if (PendingList[i].Size >= InRequestedSize)
            {
                jPlacedResource resource = PendingList[i];
				PendingList.erase(PendingList.begin() + i);
				UsingPlacedResources.insert(std::make_pair(resource.PlacedSubResource.Get(), resource));
                return resource;
            }
        }

        return jPlacedResource();
    }

    void Free(const ComPtr<ID3D12Resource>& InData);

    void AddUsingPlacedResource(const jPlacedResource InPlacedResource)
    {
		check(InPlacedResource.IsValid());
		{
			jScopedLock s(&Lock);
			UsingPlacedResources.insert(std::make_pair(InPlacedResource.PlacedSubResource.Get(), InPlacedResource));
		}
    }

    // This will be called from 'jDeallocatorMultiFrameUniformBufferBlock'
    void FreedFromPendingDelegate(std::shared_ptr<IUniformBufferBlock> InData)
    {
        jUniformBufferBlock_DX12* UniformBufferBlock_DX12 = (jUniformBufferBlock_DX12*)InData.get();
        check(UniformBufferBlock_DX12);
        check(UniformBufferBlock_DX12->LifeType == jLifeTimeType::MultiFrame);

        jBuffer_DX12* Buffer = UniformBufferBlock_DX12->Buffer;
        check(Buffer);
        {
			Free(Buffer->Buffer);
        }
    }

	std::vector<jPlacedResource>& GetPendingPlacedResources(bool InIsUploadPlacedResource, size_t InSize)
	{
		const int32 Index = (int32)GetPoolSizeType(InSize);
		check(Index != (int32)EPoolSizeType::MAX);
		return InIsUploadPlacedResource ? PendingUploadPlacedResources[Index] : PendingPlacedResources[Index];
	}

    // 적절한 PoolSize 선택 함수
    EPoolSizeType GetPoolSizeType(uint64 InSize) const
    {
        for (int32 i = 0; i < (int32)EPoolSizeType::MAX; ++i)
        {
            if (MemorySize[i] > InSize)
            {
                return (EPoolSizeType)i;
            }
        }
        return EPoolSizeType::MAX;
    }

    jMutexLock Lock;
    std::map<ID3D12Resource*, jPlacedResource> UsingPlacedResources;
    std::vector<jPlacedResource> PendingPlacedResources[(int32)EPoolSizeType::MAX];
    std::vector<jPlacedResource> PendingUploadPlacedResources[(int32)EPoolSizeType::MAX];
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
	ComPtr<IDXGIAdapter3> Adapter;
	ComPtr<ID3D12Device5> Device;
    uint32 Options = 0;
	ComPtr<IDXGIFactory5> Factory;

	//////////////////////////////////////////////////////////////////////////
	// 2. Command
	jCommandBufferManager_DX12* CommandBufferManager = nullptr;
	jCommandBufferManager_DX12* CopyCommandBufferManager = nullptr;

	//////////////////////////////////////////////////////////////////////////
	// 3. Swapchain
	jSwapchain_DX12* Swapchain = nullptr;
	uint32 CurrentFrameIndex = 0;

	//////////////////////////////////////////////////////////////////////////
	// 4. Heap
	jOfflineDescriptorHeap_DX12 RTVDescriptorHeaps;
	jOfflineDescriptorHeap_DX12 DSVDescriptorHeaps;
    jOfflineDescriptorHeap_DX12 DescriptorHeaps;
    jOfflineDescriptorHeap_DX12 SamplerDescriptorHeaps;

	jOnlineDescriptorManager OnlineDescriptorHeapManager;

	// 7. Create sync object
	jFenceManager_DX12 FenceManager;
	void WaitForGPU() const;

    HWND m_hWnd = 0;

    float m_focalDistance = 10.0f;
    float m_lensRadius = 0.2f;
    //////////////////////////////////////////////////////////////////////////

	virtual jName GetRHIName() override
	{
		return jNameStatic("DirectX12");
	}

	virtual bool InitRHI() override;
	virtual void ReleaseRHI() override;
    
    uint32 AdapterID = -1;
    std::wstring AdapterName;
    void CalculateFrameStats();

	HWND CreateMainWindow() const;

	//////////////////////////////////////////////////////////////////////////
	// PlacedResouce test
	ComPtr<ID3D12Heap> PlacedResourceDefaultHeap;
	uint64 PlacedResourceDefaultHeapOffset = 0;
	jMutexLock PlacedPlacedResourceDefaultHeapOffsetLock;

    ComPtr<ID3D12Heap> PlacedResourceUploadHeap;
    uint64 PlacedResourceDefaultUploadOffset = 0;
	jMutexLock PlacedResourceDefaultUploadOffsetLock;

    static constexpr uint64 DefaultPlacedResourceHeapSize = 256 * 1024 * 1024;
	static constexpr uint64 PlacedResourceSizeThreshold = 512 * 512 * 4;
	static bool IsUsePlacedResource;

	jPlacedResourcePool PlacedResourcePool;

	template <typename T>
	ComPtr<ID3D12Resource> CreateResource(T&& InDesc, D3D12_RESOURCE_STATES InResourceState, D3D12_CLEAR_VALUE* InClearValue = nullptr)
	{
		check(Device);

        const D3D12_RESOURCE_ALLOCATION_INFO info = Device->GetResourceAllocationInfo(0, 1, InDesc);
		if (IsUsePlacedResource)
		{
			jPlacedResource ReusePlacedResource = PlacedResourcePool.Alloc(info.SizeInBytes, false);
			if (ReusePlacedResource.IsValid())
			{
				return ReusePlacedResource.PlacedSubResource;
			}
			else
			{
				jScopedLock s(&PlacedPlacedResourceDefaultHeapOffsetLock);
				const bool IsAvailableCreatePlacedResource = IsUsePlacedResource && (info.SizeInBytes <= PlacedResourceSizeThreshold)
					&& ((PlacedResourceDefaultHeapOffset + info.SizeInBytes) <= DefaultPlacedResourceHeapSize);

				if (IsAvailableCreatePlacedResource)
				{
					check(PlacedResourceDefaultHeap);

                    ComPtr<ID3D12Resource> NewResource;
					JFAIL(Device->CreatePlacedResource(PlacedResourceDefaultHeap.Get(), PlacedResourceDefaultHeapOffset
						, std::forward<T>(InDesc), InResourceState, InClearValue, IID_PPV_ARGS(&NewResource)));

					PlacedResourceDefaultHeapOffset += info.SizeInBytes;

                    jPlacedResource NewPlacedResource;
                    NewPlacedResource.IsUploadResource = false;
                    NewPlacedResource.PlacedSubResource = NewResource;
                    NewPlacedResource.Size = info.SizeInBytes;
                    PlacedResourcePool.AddUsingPlacedResource(NewPlacedResource);

					return NewResource;
				}
			}
		}

		const CD3DX12_HEAP_PROPERTIES& HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> NewResource;
		JFAIL(Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE
			, std::forward<T>(InDesc), InResourceState, InClearValue, IID_PPV_ARGS(&NewResource)));
		return NewResource;
	}

    template <typename T>
    ComPtr<ID3D12Resource> CreateUploadResource(T&& InDesc, D3D12_RESOURCE_STATES InResourceState, D3D12_CLEAR_VALUE* InClearValue = nullptr)
    {
        check(Device);

        const D3D12_RESOURCE_ALLOCATION_INFO info = Device->GetResourceAllocationInfo(0, 1, InDesc);
		if (IsUsePlacedResource)
		{
			jPlacedResource ReusePlacedUploadResource = PlacedResourcePool.Alloc(info.SizeInBytes, true);
			if (ReusePlacedUploadResource.IsValid())
			{
				return ReusePlacedUploadResource.PlacedSubResource;
			}
			else
			{
				jScopedLock s(&PlacedResourceDefaultUploadOffsetLock);
				const bool IsAvailablePlacedResource = IsUsePlacedResource && (info.SizeInBytes <= PlacedResourceSizeThreshold)
					&& ((PlacedResourceDefaultUploadOffset + info.SizeInBytes) <= DefaultPlacedResourceHeapSize);

				if (IsAvailablePlacedResource)
				{
					check(PlacedResourceUploadHeap);

                    ComPtr<ID3D12Resource> NewResource;
					JFAIL(Device->CreatePlacedResource(PlacedResourceUploadHeap.Get(), PlacedResourceDefaultUploadOffset
						, std::forward<T>(InDesc), InResourceState, InClearValue, IID_PPV_ARGS(&NewResource)));

					PlacedResourceDefaultUploadOffset += info.SizeInBytes;

					jPlacedResource NewPlacedResource;
					NewPlacedResource.IsUploadResource = true;
					NewPlacedResource.PlacedSubResource = NewResource;
					NewPlacedResource.Size = info.SizeInBytes;
					PlacedResourcePool.AddUsingPlacedResource(NewPlacedResource);

					return NewResource;
				}
			}
		}

        const CD3DX12_HEAP_PROPERTIES& HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        ComPtr<ID3D12Resource> NewResource;
        JFAIL(Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE
            , std::forward<T>(InDesc), InResourceState, InClearValue, IID_PPV_ARGS(&NewResource)));
        return NewResource;
    }
	//////////////////////////////////////////////////////////////////////////

	virtual bool OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized) override;

    jCommandBuffer_DX12* BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(jCommandBuffer_DX12* commandBuffer) const;

	jCommandBuffer_DX12* BeginSingleTimeCopyCommands() const;
    void EndSingleTimeCopyCommands(jCommandBuffer_DX12* commandBuffer) const;

	virtual jTexture* CreateTextureFromData(const jImageData* InImageData) const override;
	virtual jFenceManager* GetFenceManager() override { return &FenceManager; }

	std::vector<jRingBuffer_DX12*> OneFrameUniformRingBuffers;
	jRingBuffer_DX12* GetOneFrameUniformRingBuffer() const { return OneFrameUniformRingBuffers[CurrentFrameIndex]; }

	virtual jShaderBindingLayout* CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const override;

	virtual jSamplerStateInfo* CreateSamplerState(const jSamplerStateInfo& initializer) const override;
    virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const override;
    virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const override;
    virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const override;
    virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const override;

	uint32 CurrentFrameNumber = 0;		// FrameNumber is just Incremented frame by frame.
    virtual void IncrementFrameNumber() { ++CurrentFrameNumber; }
    virtual uint32 GetCurrentFrameNumber() const override { return CurrentFrameNumber; }
	virtual uint32 GetCurrentFrameIndex() const { return CurrentFrameIndex; }

	static robin_hood::unordered_map<size_t, jShaderBindingLayout*> ShaderBindingPool;
	mutable jMutexRWLock ShaderBindingPoolLock;

    static TResourcePool<jSamplerStateInfo_DX12, jMutexRWLock> SamplerStatePool;
    static TResourcePool<jRasterizationStateInfo_DX12, jMutexRWLock> RasterizationStatePool;
    static TResourcePool<jStencilOpStateInfo_DX12, jMutexRWLock> StencilOpStatePool;
    static TResourcePool<jDepthStencilStateInfo_DX12, jMutexRWLock> DepthStencilStatePool;
    static TResourcePool<jBlendingStateInfo_DX12, jMutexRWLock> BlendingStatePool;
    static TResourcePool<jPipelineStateInfo_DX12, jMutexRWLock> PipelineStatePool;
    static TResourcePool<jRenderPass_DX12, jMutexRWLock> RenderPassPool;

	virtual bool CreateShaderInternal(jShader* OutShader, const jShaderInfo& shaderInfo) const override;

    virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const override;
    virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
        , const Vector2i& offset, const Vector2i& extent) const override;
    virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
        , const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent) const override;
    virtual jRenderPass* GetOrCreateRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent) const override;

    virtual jPipelineStateInfo* CreatePipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader, const jVertexBufferArray& InVertexBufferArray
        , const jRenderPass* InRenderPass, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* InPushConstant, int32 InSubpassIndex) const override;
    virtual jPipelineStateInfo* CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const override;
	virtual void RemovePipelineStateInfo(size_t InHash) override;

	virtual std::shared_ptr<jRenderFrameContext> BeginRenderFrame() override;
	virtual void EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr) override;
	virtual jCommandBufferManager_DX12* GetCommandBufferManager() const override { return CommandBufferManager; }
	virtual jCommandBufferManager_DX12* GetCopyCommandBufferManager() const { return CopyCommandBufferManager; }

	virtual IUniformBufferBlock* CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize = 0) const override;
    virtual void BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineStateLayout
        , const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;
	virtual void BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineStateLayout
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;

	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const override;
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const override;
	virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const override;

	virtual void DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const override;
	virtual void DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const override;
    virtual void DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount) const override;
    virtual void DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 instanceCount) const override;
    virtual void DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex) const override;
    virtual void DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex, int32 instanceCount) const override;
    virtual void DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const override;
    virtual void DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const override;
	virtual void DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const override;

	virtual void* GetWindow() const override { return m_hWnd; }
	virtual std::shared_ptr<jRenderTarget> CreateRenderTarget(const jRenderTargetInfo& info) const override;

	virtual jQuery* CreateQueryTime() const override;
	virtual void ReleaseQueryTime(jQuery* queryTime) const override;

	virtual bool TransitionImageLayout(jCommandBuffer* commandBuffer, jTexture* texture, EImageLayout newLayout) const override;
	virtual bool TransitionImageLayoutImmediate(jTexture* texture, EImageLayout newLayout) const override;

    virtual jSwapchain* GetSwapchain() const override { return Swapchain; }
    virtual jSwapchainImage* GetSwapchainImage(int32 InIndex) const override { return Swapchain->GetSwapchainImage(InIndex); }

    virtual void BeginDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor = Vector4::ColorGreen) const override;
    virtual void EndDebugEvent(jCommandBuffer* InCommandBuffer) const override;

	jQueryPoolTime_DX12* QueryPoolTime = nullptr;
	virtual jQueryPool* GetQueryTimePool() const override { return QueryPoolTime; }

	virtual void Flush() const override;
	virtual void Finish() const override;

	jMutexLock MultiFrameShaderBindingInstanceLock;
	jDeallocatorMultiFrameShaderBindingInstance DeallocatorMultiFrameShaderBindingInstance;
	jDeallocatorMultiFrameUniformBufferBlock DeallocatorMultiFrameUniformBufferBlock;
};

extern jRHI_DX12* g_rhi_dx12;
