#pragma once

#include "RHI/jRHI.h"
#include "RHI/jPipelineStateInfo.h"
#include "RHI/jSemaphoreManager.h"
#include "Shader/jShader.h"
#include "jRenderPass_Vulkan.h"
#include "jShaderBindingLayout_Vulkan.h"
#include "jQueryPoolTime_Vulkan.h"
#include "jPipelineStateInfo_Vulkan.h"
#include "jCommandBufferManager_Vulkan.h"
#include "jSwapchain_Vulkan.h"
#include "jShader_Vulkan.h"
#include "jQueryPoolOcclusion_Vulkan.h"
#include "jMemoryPool_Vulkan.h"
#include "jFenceManager_Vulkan.h"

struct jRingBuffer_Vulkan;
struct jDescriptorPool_Vulkan;

#define VALIDATION_LAYER_VERBOSE 0

// For delaying destroy for standalone resource
struct jStandaloneResourceVulkan
{
	static void ReleaseBufferResource(VkBuffer InBuffer, VkDeviceMemory InMemory, const VkAllocationCallbacks* InAllocatorCallback = nullptr);
	static void ReleaseImageResource(VkImage InImage, VkDeviceMemory InMemory, const std::vector<VkImageView>& InViews, const VkAllocationCallbacks* InAllocatorCallback = nullptr);

	VkBuffer Buffer = nullptr;
	VkDeviceMemory Memory = nullptr;

	VkImage Image = nullptr;
	std::vector<VkImageView> Views;

	const VkAllocationCallbacks* AllocatorCallback = nullptr;
};
using jDeallocatorMultiFrameStandalonResource = jDeallocatorMultiFrameResource<std::shared_ptr<jStandaloneResourceVulkan>>;
using jDeallocatorMultiFrameRenderPass = jDeallocatorMultiFrameResource<jRenderPass_Vulkan*>;

class jRHI_Vulkan : public jRHI
{
public:
    static robin_hood::unordered_map<size_t, jShaderBindingLayout*> ShaderBindingPool;
    static TResourcePool<jSamplerStateInfo_Vulkan, jMutexRWLock> SamplerStatePool;
    static TResourcePool<jRasterizationStateInfo_Vulkan, jMutexRWLock> RasterizationStatePool;
    static TResourcePool<jStencilOpStateInfo_Vulkan, jMutexRWLock> StencilOpStatePool;
    static TResourcePool<jDepthStencilStateInfo_Vulkan, jMutexRWLock> DepthStencilStatePool;
    static TResourcePool<jBlendingStateInfo_Vulkan, jMutexRWLock> BlendingStatePool;
    static TResourcePool<jPipelineStateInfo_Vulkan, jMutexRWLock> PipelineStatePool;
	static TResourcePool<jRenderPass_Vulkan, jMutexRWLock> RenderPassPool;

	jRHI_Vulkan();
	virtual ~jRHI_Vulkan();

	GLFWwindow* Window = nullptr;
	bool FramebufferResized = false;

	VkInstance Instance;		// Instance는 Application과 Vulkan Library를 연결시켜줌, 드라이버에 어플리케이션 정보를 전달하기도 한다.

	// What validation layers do?
	// 1. 명세와는 다른 값의 파라메터를 사용하는 것을 감지
	// 2. 오브젝트들의 생성과 소멸을 추적하여 리소스 Leak을 감지
	// 3. Calls을 호출한 스레드를 추적하여, 스레드 안정성을 체크
	// 4. 모든 calls를 로깅하고, 그의 파라메터를 standard output으로 보냄
	// 5. 프로파일링과 리플레잉을 위해서 Vulkan calls를 추적
	//
	// Validation layer for LunarG
	// 1. Instance specific : instance 같은 global vulkan object와 관련된 calls 를 체크
	// 2. Device specific(deprecated) : 특정 GPU Device와 관련된 calls 를 체크.
	VkDebugUtilsMessengerEXT DebugMessenger = nullptr;

	// Surface
	// VkInstance를 만들고 바로 Surface를 만들어야 한다. 물리 디바이스 선택에 영향을 미치기 때문
	// Window에 렌더링할 필요 없으면 그냥 만들지 않고 Offscreen rendering만 해도 됨. (OpenGL은 보이지 않는 창을 만들어야만 함)
	// 플랫폼 독립적인 구조이지만 Window의 Surface와 연결하려면 HWND or HMODULE 등을 사용해야 하므로 VK_KHR_win32_surface Extension을 사용해서 처리함.
	VkSurfaceKHR Surface = nullptr;

	// 물리 디바이스 - 물리 그래픽 카드를 선택
	VkPhysicalDevice PhysicalDevice = nullptr;

	EMSAASamples SelectedMSAASamples = EMSAASamples::COUNT_1;

	// Queue Families
	// 여러종류의 Queue type이 있을 수 있다. (ex. Compute or memory transfer related commands 만 만듬)
	// - 논리 디바이스(VkDevice) 생성시에 함께 생성시킴
	// - 논리 디바이스가 소멸될때 함께 알아서 소멸됨, 그래서 Cleanup 해줄필요가 없음.
	struct jQueue_Vulkan // base 없음
	{
		uint32 QueueIndex = 0;
		VkQueue Queue = nullptr;
	};
	jQueue_Vulkan GraphicsQueue;
	jQueue_Vulkan ComputeQueue;
	jQueue_Vulkan CopyQueue;
	jQueue_Vulkan PresentQueue;

    uint32 CurrentFrameIndex = 0;		// FrameIndex is swapchain number that is currently used.
	uint32 CurrentFrameNumber = 0;		// FrameNumber is just Incremented frame by frame.

	// 논리 디바이스 생성
	VkDevice Device;
    VkPipelineCache PipelineCache = nullptr;
	VkPhysicalDeviceProperties2 DeviceProperties{};
	const VkPhysicalDeviceLimits& GetDevicePropertyLimits() const { return DeviceProperties.properties.limits; }
	VkPhysicalDeviceFeatures2 DeviceFeatures2{};
	
	// Raytracing Features
	VkPhysicalDeviceAccelerationStructureFeaturesKHR AccelerationStructureFeatures{};
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR  RayTracingPipelineProperties{};
	//////////////////////////////////////////////////////////////////////////

	jSwapchain_Vulkan* Swapchain = nullptr;
	jCommandBufferManager_Vulkan* GraphicsCommandBufferManager = nullptr;
	jCommandBufferManager_Vulkan* ComputeCommandBufferManager = nullptr;
	jCommandBufferManager_Vulkan* CopyCommandBufferManager = nullptr;
    mutable jFenceManager_Vulkan FenceManager;
	jSemaphoreManager_Vulkan SemaphoreManager;
	jMemoryPool_Vulkan* MemoryPool = nullptr;

    jQueryPoolTime_Vulkan* QueryPoolTime[(int32)ECommandBufferType::MAX] = { nullptr, };
	jQueryPoolOcclusion_Vulkan* QueryPoolOcclusion = nullptr;
	std::vector<jRingBuffer_Vulkan*> OneFrameUniformRingBuffers;
	std::vector<jRingBuffer_Vulkan*> SSBORingBuffers;
	std::vector<jDescriptorPool_Vulkan*> DescriptorPools;
	jDescriptorPool_Vulkan* DescriptorPools2 = nullptr;

	mutable jMutexRWLock ShaderBindingPoolLock;
	//////////////////////////////////////////////////////////////////////////

	virtual jCommandBuffer_Vulkan* BeginSingleTimeCommands() const override;
	virtual void EndSingleTimeCommands(jCommandBuffer* commandBuffer) const override;
	jCommandBuffer_Vulkan* BeginSingleTimeComputeCommands() const;
    void EndSingleTimeComputeCommands(jCommandBuffer* commandBuffer) const;
	jCommandBuffer_Vulkan* BeginSingleTimeCopyCommands() const;
    void EndSingleTimeCopyCommands(jCommandBuffer_Vulkan* commandBuffer, bool bWaitUntilExecuteComplete = false) const;

	// Resource Barrier
	virtual void TransitionLayout(jCommandBuffer* commandBuffer, jTexture* texture, EResourceLayout newLayout) const override;
    virtual void TransitionLayout(jCommandBuffer* commandBuffer, jBuffer* buffer, EResourceLayout newLayout) const override;
    virtual void UAVBarrier(jCommandBuffer* commandBuffer, jTexture* /*texture*/) const override;
    virtual void UAVBarrier(jCommandBuffer* commandBuffer, jBuffer* /*buffer*/) const override;

    virtual void TransitionLayout(jTexture* texture, EResourceLayout newLayout) const override;
    virtual void TransitionLayout(jBuffer* buffer, EResourceLayout newLayout) const override; 
	virtual void UAVBarrier(jTexture* /*texture*/) const override;
    virtual void UAVBarrier(jBuffer* /*buffer*/) const override;
	//////////////////////////////////////////////////////////////////////////

    virtual jName GetRHIName() override
    {
        return jNameStatic("Vulkan");
    }

	virtual bool InitRHI() override;
	virtual void ReleaseRHI() override;

	// 여기 있을 것은 아님
	void CleanupSwapChain();
	virtual void RecreateSwapChain() override;
	virtual uint32 GetMaxSwapchainCount() const override;

    FORCEINLINE const VkDevice& GetDevice() const { return Device; }

	virtual void* GetWindow() const override { return Window; }

	//////////////////////////////////////////////////////////////////////////
	virtual std::shared_ptr<jVertexBuffer> CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const override;
	virtual std::shared_ptr<jIndexBuffer> CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const override;
	virtual std::shared_ptr<jTexture> CreateTextureFromData(const jImageData* InImageData) const override;
	virtual bool CreateShaderInternal(jShader* OutShader, const jShaderInfo& shaderInfo) const override;
	virtual jFrameBuffer* CreateFrameBuffer(const jFrameBufferInfo& info) const override;
	virtual std::shared_ptr<jRenderTarget> CreateRenderTarget(const jRenderTargetInfo& info) const override;

	virtual jSamplerStateInfo* CreateSamplerState(const jSamplerStateInfo& initializer) const override;
	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const override;
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const override;
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const override;
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const override;

	virtual void DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const override;
	virtual void DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const override;
	virtual void DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount) const override;
	virtual void DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 instanceCount) const override;
	virtual void DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex) const override;
	virtual void DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex, int32 instanceCount) const override;
	virtual void DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const override;
	virtual void DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const override;
	virtual void DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const override;
	virtual void DispatchRay(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jRaytracingDispatchData& InDispatchData) const override;

	virtual jShaderBindingLayout* CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const override;
	virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const override;

    virtual jQuery* CreateQueryTime(ECommandBufferType InCmdBufferType) const override;
    virtual void ReleaseQueryTime(jQuery* queryTime) const override;
	virtual std::shared_ptr<jRenderFrameContext> BeginRenderFrame() override;
	virtual void EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr) override;
	std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> QueueSubmit(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr, jSemaphore* InSignalSemaphore);
	jPipelineStateInfo* CreatePipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader, const jVertexBufferArray& InVertexBufferArray
		, const jRenderPass* InRenderPass, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* InPushConstant, int32 InSubpassIndex) const override;
	virtual jPipelineStateInfo* CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const override;
	virtual jPipelineStateInfo* CreateRaytracingPipelineStateInfo(const std::vector<jRaytracingPipelineShader>& InShaders, const jRaytracingPipelineData& InRaytracingData
		, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const override;
    void RemovePipelineStateInfo(size_t InHash);

	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const override;
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
		, const Vector2i& offset, const Vector2i& extent) const override;
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
		, const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent) const override;
	virtual jRenderPass* GetOrCreateRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent) const override;

	virtual jCommandBufferManager_Vulkan* GetGraphicsCommandBufferManager() const override { return GraphicsCommandBufferManager; }
    virtual jCommandBufferManager_Vulkan* GetComputeCommandBufferManager() const override { return ComputeCommandBufferManager; }
    virtual jCommandBufferManager_Vulkan* GetCopyCommandBufferManager() const override { return CopyCommandBufferManager; }
	FORCEINLINE const jQueue_Vulkan& GetQueue(ECommandBufferType InType) const
	{
		switch(InType)
		{
		case ECommandBufferType::GRAPHICS:	return GraphicsQueue;
		case ECommandBufferType::COMPUTE:	return ComputeQueue;
		case ECommandBufferType::COPY:		return CopyQueue;
		default:
			check(0);
			break;
		}
		static jQueue_Vulkan Empty;
		return Empty;
	}

	virtual EMSAASamples GetSelectedMSAASamples() const override { return SelectedMSAASamples; }
	virtual jQueryPool* GetQueryTimePool(ECommandBufferType InType) const override { return QueryPoolTime[(int32)InType]; }
	virtual jQueryPool* GetQueryOcclusionPool() const override { return QueryPoolOcclusion; }
	virtual jSwapchain* GetSwapchain() const override { return Swapchain; }
	virtual uint32 GetCurrentFrameIndex() const override { return CurrentFrameIndex; }
	virtual uint32 GetCurrentFrameNumber() const override { return CurrentFrameNumber; }
	virtual void IncrementFrameNumber() { ++CurrentFrameNumber; }

	jRingBuffer_Vulkan* GetOneFrameUniformRingBuffer() const { return OneFrameUniformRingBuffers[CurrentFrameIndex]; }
	jRingBuffer_Vulkan* GetSSBORingBuffer() const { return SSBORingBuffers[CurrentFrameIndex]; }
	jDescriptorPool_Vulkan* GetDescriptorPoolForSingleFrame() const { return DescriptorPools[CurrentFrameIndex]; }
	jDescriptorPool_Vulkan* GetDescriptorPoolMultiFrame() const { return DescriptorPools2; }

	virtual void Flush() const override;
	virtual void Finish() const override;

	// VRS
	PFN_vkCmdBindShadingRateImageNV vkCmdBindShadingRateImageNV = nullptr;		// Function pointer of VRS
	virtual void BindShadingRateImage(jCommandBuffer* commandBuffer, jTexture* vrstexture) const override;
	
	// DebugMarker
    PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTag = nullptr;
    PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName = nullptr;
    PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBegin = nullptr;
    PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEnd = nullptr;
    PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsert = nullptr;

    // Raytracing
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;

	virtual jMemoryPool* GetMemoryPool() const override { return MemoryPool; }

	virtual void NextSubpass(const jCommandBuffer* commandBuffer) const override;

	// Temporary VRS Texture
	jTexture* CreateSampleVRSTexture();
	FORCEINLINE jTexture* GetSampleVRSTexture() const { return SampleVRSTexturePtr.get(); }
	std::shared_ptr<jTexture_Vulkan> SampleVRSTexturePtr;

	virtual void BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;
	virtual void BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;
	virtual void BindRaytracingShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;

    virtual jFenceManager* GetFenceManager() override { return &FenceManager; }
    virtual jSemaphoreManager* GetSemaphoreManager() override { return &SemaphoreManager; }
	virtual jSwapchainImage* GetSwapchainImage(int32 InIndex) const override { return Swapchain->Images[InIndex]; }

	virtual void BeginDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor = Vector4::ColorGreen) const override;
	virtual void EndDebugEvent(jCommandBuffer* InCommandBuffer) const override;
	virtual bool OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized) override;
	virtual jRaytracingScene* CreateRaytracingScene() const;

	// Create Buffers
	std::shared_ptr<jBuffer> CreateBufferInternal(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
		, EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const;
    virtual std::shared_ptr<jBuffer> CreateStructuredBuffer(uint64 InSize, uint64 InAlignment, uint64 InStride, EBufferCreateFlag InBufferCreateFlag
        , EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const override;
    virtual std::shared_ptr<jBuffer> CreateRawBuffer(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
        , EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const override;
    virtual std::shared_ptr<jBuffer> CreateFormattedBuffer(uint64 InSize, uint64 InAlignment, ETextureFormat InFormat, EBufferCreateFlag InBufferCreateFlag
        , EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const override;
    virtual std::shared_ptr<IUniformBufferBlock> CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize = 0) const override;

    // Create Images
	VkImageUsageFlags GetImageUsageFlags(ETextureCreateFlag InTextureCreateFlag) const;
	VkMemoryPropertyFlagBits GetMemoryPropertyFlagBits(ETextureCreateFlag InTextureCreateFlag) const;

    virtual std::shared_ptr<jTexture> Create2DTexture(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
        , EResourceLayout InImageLayout = EResourceLayout::UNDEFINED, const jImageBulkData& InImageBulkData = {}, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr) const override;

    virtual std::shared_ptr<jTexture> CreateCubeTexture(uint32 InWidth, uint32 InHeight, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
        , EResourceLayout InImageLayout = EResourceLayout::UNDEFINED, const jImageBulkData& InImageBulkData = {}, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr) const override;
    //////////////////////////////////////////////////////////////////////////

	virtual bool IsSupportVSync() const override;

	jDeallocatorMultiFrameStandalonResource DeallocatorMultiFrameStandaloneResource;
	jDeallocatorMultiFrameRenderPass DeallocatorMultiFrameRenderPass;

	// To release renderpasses that have relation with rendertarget which will be released.
	virtual void RemoveRenderPassByHash(const std::vector<uint64>& InRelatedRenderPassHashes) const override;

	virtual float GetCurrentMonitorDPIScale() const override;

	jResourceBarrierBatcher_Vulkan* BarrierBatcher = nullptr;
	virtual jResourceBarrierBatcher* GetGlobalBarrierBatcher() const override { return BarrierBatcher; }
};

extern jRHI_Vulkan* g_rhi_vk;

