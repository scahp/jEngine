#pragma once

#if USE_VULKAN

#include "jRHI.h"
#include "Shader/jShader.h"
#include "jFenceManager.h"
#include "jPipelineStateInfo.h"
#include "Vulkan/jRenderPass_Vulkan.h"
#include "Vulkan/jShaderBindingLayout_Vulkan.h"
#include "Vulkan/jQueryPoolTime_Vulkan.h"
#include "Vulkan/jPipelineStateInfo_Vulkan.h"
#include "Vulkan/jCommandBufferManager_Vulkan.h"
#include "Vulkan/jSwapchain_Vulkan.h"
#include "Vulkan/jShader_Vulkan.h"
#include "Vulkan/jQueryPoolOcclusion_Vulkan.h"
#include "Vulkan/jMemoryPool_Vulkan.h"
#include "jSemaphoreManager.h"

struct jRingBuffer_Vulkan;
struct jDescriptorPool_Vulkan;

#define VALIDATION_LAYER_VERBOSE 0

class jRHI_Vulkan : public jRHI
{
public:
    static robin_hood::unordered_map<size_t, VkPipelineLayout> PipelineLayoutPool;
    static robin_hood::unordered_map<size_t, jShaderBindingsLayout*> ShaderBindingPool;
    static TResourcePool<jSamplerStateInfo_Vulkan, jMutexRWLock> SamplerStatePool;
    static TResourcePool<jRasterizationStateInfo_Vulkan, jMutexRWLock> RasterizationStatePool;
    static TResourcePool<jMultisampleStateInfo_Vulkan, jMutexRWLock> MultisampleStatePool;
    static TResourcePool<jStencilOpStateInfo_Vulkan, jMutexRWLock> StencilOpStatePool;
    static TResourcePool<jDepthStencilStateInfo_Vulkan, jMutexRWLock> DepthStencilStatePool;
    static TResourcePool<jBlendingStateInfo_Vulakn, jMutexRWLock> BlendingStatePool;
    static TResourcePool<jPipelineStateInfo_Vulkan, jMutexRWLock> PipelineStatePool;
	static TResourcePool<jRenderPass_Vulkan, jMutexRWLock> RenderPassPool;

	static constexpr VkImageTiling DefaultTilingMode = VkImageTiling::VK_IMAGE_TILING_OPTIMAL;

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
	VkSurfaceKHR Surface;

	// 물리 디바이스 - 물리 그래픽 카드를 선택
	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

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
	jQueue_Vulkan PresentQueue;

    uint32 CurrentFrameIndex = 0;		// FrameIndex is swapchain number that is currently used.
	uint32 CurrentFrameNumber = 0;		// FrameNumber is just Incremented frame by frame.

	// 논리 디바이스 생성
	VkDevice Device;
    VkPipelineCache PipelineCache = nullptr;
    VkPhysicalDeviceProperties DeviceProperties;

	jSwapchain_Vulkan* Swapchain = nullptr;
	jCommandBufferManager_Vulkan* CommandBufferManager = nullptr;
    jFenceManager_Vulkan FenceManager;
	jSemaphoreManager_Vulkan SemaphoreManager;
	jMemoryPool_Vulkan* MemoryPool = nullptr;

    jQueryPoolTime_Vulkan* QueryPoolTime = nullptr;
	jQueryPoolOcclusion_Vulkan* QueryPoolOcclusion = nullptr;
	std::vector<jRingBuffer_Vulkan*> OneFrameUniformRingBuffers;
	std::vector<jRingBuffer_Vulkan*> SSBORingBuffers;
	std::vector<jDescriptorPool_Vulkan*> DescriptorPools;
	jDescriptorPool_Vulkan* DescriptorPools2 = nullptr;

	mutable jMutexRWLock ShaderBindingPoolLock;
	mutable jMutexRWLock PipelineLayoutPoolLock;
	//////////////////////////////////////////////////////////////////////////

    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;

	bool TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, uint32 mipLevels, uint32 layoutCount, VkImageLayout oldLayout, VkImageLayout newLayout) const;
	virtual bool TransitionImageLayout(jCommandBuffer* commandBuffer, jTexture* texture, EImageLayout newLayout) const override;
    virtual bool TransitionImageLayoutImmediate(jTexture* texture, EImageLayout newLayout) const override;

	//////////////////////////////////////////////////////////////////////////

	virtual bool InitRHI() override;
	virtual void ReleaseRHI() override;

	// 여기 있을 것은 아님
	void CleanupSwapChain();
	virtual void RecreateSwapChain() override;
	virtual uint32 GetMaxSwapchainCount() const override;

    FORCEINLINE const VkDevice& GetDevice() const { return Device; }

	virtual void* GetWindow() const override { return Window; }

	//////////////////////////////////////////////////////////////////////////
	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const override;
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const override;
	virtual jTexture* CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const override;
	virtual bool CreateShaderInternal(jShader* OutShader, const jShaderInfo& shaderInfo) const override;
	virtual jFrameBuffer* CreateFrameBuffer(const jFrameBufferInfo& info) const override;
	virtual std::shared_ptr<jRenderTarget> CreateRenderTarget(const jRenderTargetInfo& info) const override;

	virtual jSamplerStateInfo* CreateSamplerState(const jSamplerStateInfo& initializer) const override;
	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const override;
	virtual jMultisampleStateInfo* CreateMultisampleState(const jMultisampleStateInfo& initializer) const override;
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const override;
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const override;
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const override;

	virtual void DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const override;
	virtual void DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const override;
	virtual void DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const override;
	virtual void DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const override;
	virtual void DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const override;
	virtual void DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const override;
	virtual void DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const override;
	virtual void DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const override;

	virtual jShaderBindingsLayout* CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const override;
	virtual jShaderBindingInstance* CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const override;
	virtual void* CreatePipelineLayout(const jShaderBindingsLayoutArray& InShaderBindingLayoutArray, const jPushConstant* pushConstant) const override;

	virtual IUniformBufferBlock* CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize = 0) const override;

    virtual jQuery* CreateQueryTime() const override;
    virtual void ReleaseQueryTime(jQuery* queryTime) const override;
	virtual std::shared_ptr<jRenderFrameContext> BeginRenderFrame() override;
	virtual void EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr) override;
    virtual void QueueSubmit(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr, jSemaphore* InSignalSemaphore) override;
	jPipelineStateInfo* CreatePipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader, const jVertexBufferArray& InVertexBufferArray
		, const jRenderPass* InRenderPass, const jShaderBindingsLayoutArray& InShaderBindingArray, const jPushConstant* InPushConstant, int32 InSubpassIndex) const override;
	virtual jPipelineStateInfo* CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingsLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const override;

	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const override;
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
		, const Vector2i& offset, const Vector2i& extent) const override;
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment
		, const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent) const override;
	virtual jRenderPass* GetOrCreateRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent) const override;

	virtual jCommandBufferManager* GetCommandBufferManager() const override { return CommandBufferManager; }
	virtual EMSAASamples GetSelectedMSAASamples() const override { return SelectedMSAASamples; }
	virtual jQueryPool* GetQueryTimePool() const override { return QueryPoolTime; }
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

	virtual jMemoryPool* GetMemoryPool() const override { return MemoryPool; }

	virtual void NextSubpass(const jCommandBuffer* commandBuffer) const override;

	// Temporary VRS Texture
	jTexture* CreateSampleVRSTexture();
	FORCEINLINE jTexture* GetSampleVRSTexture() const { return SampleVRSTexture; }
	jTexture* SampleVRSTexture = nullptr;

	virtual void BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineStateLayout
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;
	virtual void BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineStateLayout
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const override;

    virtual jFenceManager* GetFenceManager() { return &FenceManager; }
    virtual jSemaphoreManager* GetSemaphoreManager() { return &SemaphoreManager; }
};

extern jRHI_Vulkan* g_rhi_vk;

#endif // USE_VULKAN