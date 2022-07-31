#pragma once

#if USE_VULKAN

#include "jRHI.h"
#include "Shader/jShader.h"
#include "jFenceManager.h"
#include "jPipelineStateInfo.h"
#include "jCommandBufferManager.h"
#include "Vulkan/jRenderPass_Vulkan.h"
#include "Vulkan/jShaderBindings_Vulkan.h"
#include "Vulkan/jQueryPool_Vulkan.h"
#include "Vulkan/jPipelineStateInfo_Vulkan.h"
#include "Vulkan/jImage_Vulkan.h"

#define VALIDATION_LAYER_VERBOSE 0

struct jSwapchainImage_Vulkan : public jImage_Vulkan
{
    VkFence CommandBufferFence = nullptr;

    // Semaphore 는 GPU - GPU 간의 동기화를 맞춰줌.여러개의 프레임이 동시에 만들어질 수 있게 함.
    // Semaphores 는 커맨드 Queue 내부 혹은 Queue 들 사이에 명령어 동기화를 위해서 설계됨
    VkSemaphore Available = nullptr;		// 이미지를 획득해서 렌더링 준비가 완료된 경우 Signal(Lock 이 풀리는) 되는 것
    VkSemaphore RenderFinished = nullptr;	// 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것
};

// Swapchain
struct jSwapchain_Vulkan
{
    VkSwapchainKHR SwapChain = nullptr;
	ETextureFormat Format = ETextureFormat::RGB8;
    Vector2i Extent;
    std::vector<jSwapchainImage_Vulkan> Images;
};

class jRHI_Vulkan : public jRHI
{
public:
    static std::unordered_map<size_t, VkPipelineLayout> PipelineLayoutPool;
    static std::unordered_map<size_t, jShaderBindings*> ShaderBindingPool;
    static TResourcePool<jSamplerStateInfo_Vulkan> SamplerStatePool;
    static TResourcePool<jRasterizationStateInfo_Vulkan> RasterizationStatePool;
    static TResourcePool<jMultisampleStateInfo_Vulkan> MultisampleStatePool;
    static TResourcePool<jStencilOpStateInfo_Vulkan> StencilOpStatePool;
    static TResourcePool<jDepthStencilStateInfo_Vulkan> DepthStencilStatePool;
    static TResourcePool<jBlendingStateInfo_Vulakn> BlendingStatePool;
    static TResourcePool<jPipelineStateInfo_Vulkan> PipelineStatePool;

	jRHI_Vulkan();
	virtual ~jRHI_Vulkan();

	GLFWwindow* Window = nullptr;
	bool framebufferResized = false;

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
	VkDebugUtilsMessengerEXT DebugMessenger;

	// Surface
	// VkInstance를 만들고 바로 Surface를 만들어야 한다. 물리 디바이스 선택에 영향을 미치기 때문
	// Window에 렌더링할 필요 없으면 그냥 만들지 않고 Offscreen rendering만 해도 됨. (OpenGL은 보이지 않는 창을 만들어야만 함)
	// 플랫폼 독립적인 구조이지만 Window의 Surface와 연결하려면 HWND or HMODULE 등을 사용해야 하므로 VK_KHR_win32_surface Extension을 사용해서 처리함.
	VkSurfaceKHR Surface;

	// 물리 디바이스 - 물리 그래픽 카드를 선택
	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

	VkSampleCountFlagBits MsaaSamples = VK_SAMPLE_COUNT_1_BIT;

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
	jQueue_Vulkan PresentQueue;

	// 논리 디바이스 생성
	VkDevice Device;

	jSwapchain_Vulkan Swapchain_Vulkan;

	jCommandBufferManager_Vulkan CommandBuffersManager;

	size_t CurrenFrameIndex = 0;

    jCommandBufferManager_Vulkan CommandBufferManager;
    VkPipelineCache PipelineCache = nullptr;

    jShaderBindingsManager_Vulkan ShaderBindingsManager;
    jFenceManager_Vulkan FenceManager;

    VkPhysicalDeviceProperties DeviceProperties;

    std::vector<jRenderPass_Vulkan*> RenderPasses;
    jCommandBuffer_Vulkan* CurrentCommandBuffer = nullptr;
    jPipelineStateFixedInfo CurrentPipelineStateFixed_Shadow;
    jPipelineStateFixedInfo CurrentPipelineStateFixed;

    jQueryPool_Vulkan QueryPool;
	//////////////////////////////////////////////////////////////////////////

    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    bool TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32 mipLevels) const;

	//////////////////////////////////////////////////////////////////////////

	virtual bool InitRHI() override;
	virtual void ReleaseRHI() override;

	// 여기 있을 것은 아님
	void CleanupSwapChain();
	void RecreateSwapChain();

    FORCEINLINE const VkDevice& GetDevice() const { return Device; }

	virtual void* GetWindow() const override { return Window; }

	//////////////////////////////////////////////////////////////////////////
	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const override;
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const override;
	virtual jTexture* CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const override;
	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) const override;
	virtual bool CreateShader(jShader* OutShader, const jShaderInfo& shaderInfo) const override;
	virtual jFrameBuffer* CreateFrameBuffer(const jFrameBufferInfo& info) const override;
	virtual std::shared_ptr<jRenderTarget> CreateRenderTarget(const jRenderTargetInfo& info) const override;

	virtual jSamplerStateInfo* CreateSamplerState(const jSamplerStateInfo& initializer) const override;
	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const override;
	virtual jMultisampleStateInfo* CreateMultisampleState(const jMultisampleStateInfo& initializer) const override;
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const override;
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const override;
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const override;

	virtual void DrawArrays(EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const override;
	virtual void DrawArraysInstanced(EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const override;
	virtual void DrawElements(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const override;
	virtual void DrawElementsInstanced(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const override;
	virtual void DrawElementsBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const override;
	virtual void DrawElementsInstancedBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const override;

	virtual jShaderBindings* CreateShaderBindings(const std::vector<jShaderBinding>& InUniformBindings, const std::vector<jShaderBinding>& InTextureBindings) const override;
	virtual jShaderBindingInstance* CreateShaderBindingInstance(const std::vector<TShaderBinding<IUniformBufferBlock*>>& InUniformBuffers, const std::vector<TShaderBinding<jTextureBindings>>& InTextures) const override;
	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindings*>& shaderBindings) const override;
	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances) const override;

	virtual IUniformBufferBlock* CreateUniformBufferBlock(const char* blockname, size_t size = 0) const override;

    virtual jQueryTime* CreateQueryTime() const override;
    virtual void ReleaseQueryTime(jQueryTime* queryTime) const override;
	virtual void QueryTimeStampStart(const jQueryTime* queryTimeStamp) const override;
	virtual void QueryTimeStampEnd(const jQueryTime* queryTimeStamp) const override;
    virtual bool IsQueryTimeStampResult(const jQueryTime* queryTimeStamp, bool isWaitUntilAvailable) const override;
    virtual void GetQueryTimeStampResult(jQueryTime* queryTimeStamp) const override;
	virtual bool CanWholeQueryTimeStampResult() const override { return true; }
	virtual std::vector<uint64> GetWholeQueryTimeStampResult(int32 InWatingResultIndex) const override;
	virtual void GetQueryTimeStampResultFromWholeStampArray(jQueryTime* queryTimeStamp, int32 InWatingResultIndex
		, const std::vector<uint64>& wholeQueryTimeStampArray) const override;
	virtual int32 BeginRenderFrame(jCommandBuffer* commandBuffer) override;
	virtual void EndRenderFrame(jCommandBuffer* commandBuffer) override;
	jPipelineStateInfo* CreatePipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader
		, const jVertexBuffer* vertexBuffer, const jRenderPass* renderPass, const std::vector<const jShaderBindings*> shaderBindings) const override;
};

extern jRHI_Vulkan* g_rhi_vk;

#endif // USE_VULKAN