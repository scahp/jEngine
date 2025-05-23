﻿#pragma once
#include "Math\Matrix.h"
#include "Core\jName.h"
#include "Math\Vector.h"
#include "jPipelineStateInfo.h"
#include "jFrameBuffer.h"
#include "jRenderTarget.h"
#include "jShaderBindingLayout.h"
#include "jBuffer.h"
#include "jCommandBufferManager.h"
#include "jRenderPass.h"
#include "jRenderFrameContext.h"
#include "Core/TResourcePool.h"

class jMaterial;

extern class jRHI* g_rhi;

extern std::shared_ptr<jTexture> GWhiteTexture;
extern std::shared_ptr<jTexture> GBlackTexture;
extern std::shared_ptr<jTexture> GWhiteCubeTexture;
extern std::shared_ptr<jTexture> GNormalTexture;
extern std::shared_ptr<jTexture> GRoughnessMetalicTexture;
extern std::shared_ptr<jTexture> GNoiseTexture;
extern std::shared_ptr<jMaterial> GDefaultMaterial;

struct jShader;
struct jShaderInfo;
struct jSamplerStateInfo;
class jSwapchain;
class jMemoryPool;
class jFenceManager;
class jSemaphoreManager;
struct jImageData;
class jRaytracingScene;

enum class EUniformType
{
	NONE = 0,
	MATRIX,
	BOOL,
	INT,
	FLOAT,
	VECTOR2,
	VECTOR3,
	VECTOR4,
	VECTOR2I,
	VECTOR3I,
	VECTOR4I, 
	MAX,
};

template <typename T> struct ConvertUniformType { };
template <> struct ConvertUniformType<Matrix> { operator EUniformType () { return EUniformType::MATRIX; } };
template <> struct ConvertUniformType<bool> { operator EUniformType () { return EUniformType::BOOL; } };
template <> struct ConvertUniformType<int> { operator EUniformType () { return EUniformType::INT; } };
template <> struct ConvertUniformType<float> { operator EUniformType () { return EUniformType::FLOAT; } };
template <> struct ConvertUniformType<Vector2> { operator EUniformType () { return EUniformType::VECTOR2; } };
template <> struct ConvertUniformType<Vector> { operator EUniformType () { return EUniformType::VECTOR3; } };
template <> struct ConvertUniformType<Vector4> { operator EUniformType () { return EUniformType::VECTOR4; } };

struct IUniformBuffer
{
	IUniformBuffer() = default;
	IUniformBuffer(const jName& name)
		: Name(name)
	{}
	virtual ~IUniformBuffer() {}
	jName Name;

	virtual jName GetName() const { return Name; }
	virtual EUniformType GetType() const { return EUniformType::NONE; }
	virtual void SetUniformbuffer(const jShader* /*InShader*/) const {}
	virtual void Bind(const jShader* shader) const;
};

template <typename T>
struct jUniformBuffer : public IUniformBuffer
{
	T Data;
};

static int32 GetBindPoint()
{
	static int32 s_index = 0;
	return s_index++;
}

enum class jLifeTimeType : uint8
{
    OneFrame = 0,
    MultiFrame,
    MAX
};

struct IUniformBufferBlock : public jShaderBindableResource, public std::enable_shared_from_this<IUniformBufferBlock>
{
	IUniformBufferBlock() = default;
	IUniformBufferBlock(const jName& InName, jLifeTimeType InLifeType)
		: jShaderBindableResource(InName), LifeType(InLifeType)
	{}
	virtual ~IUniformBufferBlock() {}

	const jLifeTimeType LifeType = jLifeTimeType::MultiFrame;

	virtual bool IsUseRingBuffer() const { return (LifeType == jLifeTimeType::OneFrame); }

	virtual size_t GetBufferSize() const { return 0; }
	virtual size_t GetBufferOffset() const { return 0; }

	virtual void Init(size_t size) = 0;
	virtual void Release() = 0;
	virtual void Bind(const jShader* shader) const { }
	virtual void UpdateBufferData(const void* newData, size_t size) = 0;
	virtual void ClearBuffer(int32 clearValue = 0) = 0;
	
	virtual void* GetLowLevelResource() const { return nullptr; }
	virtual void* GetLowLevelMemory() const { return nullptr; }		// Vulkan only
};

struct IAtomicCounterBuffer : public jShaderBindableResource
{
	IAtomicCounterBuffer() = default;
	IAtomicCounterBuffer(const std::string& name, uint32 bindingPoint)
		: Name(name), BindingPoint(bindingPoint)
	{}
	virtual ~IAtomicCounterBuffer() {}

	std::string Name;
	size_t Size = 0;
	uint32 BindingPoint = -1;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const {}
	virtual void UpdateBufferData(void* newData, size_t size) = 0;
	virtual void GetBufferData(void* newData, size_t size) = 0;
	virtual void ClearBuffer(int32 clearValue) = 0;

	template <typename T>
	void GetBufferData(typename std::remove_reference<T>::type& out)
	{
		GetBufferData(&out, sizeof(T));
	}
};

struct ITransformFeedbackBuffer : public jShaderBindableResource
{
	ITransformFeedbackBuffer() = default;
	ITransformFeedbackBuffer(const std::string& name)
		: Name(name)
	{}
	virtual ~ITransformFeedbackBuffer() {}

	std::string Name;
	size_t Size = 0;
	std::vector<std::string> Varyings;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const {}
	virtual void UpdateBufferData(void* newData, size_t size) = 0;
	virtual void GetBufferData(void* newData, size_t size) = 0;
	virtual void ClearBuffer(int32 clearValue) = 0;
	virtual void UpdateVaryingsToShader(const std::vector<std::string>& varyings, const jShader* shader) = 0;
	virtual void ClearVaryingsToShader(const jShader* shader) = 0;
	virtual void Begin(EPrimitiveType type) = 0;
	virtual void End() = 0;
	virtual void Pause() = 0;

	template <typename T>
	void GetBufferData(typename std::remove_reference<T>::type& out)
	{
		GetBufferData(&out, sizeof(T));
	}
};

struct jQuery;

struct jQueryPool
{
	virtual ~jQueryPool() {}
	virtual bool Create() { return false; }
	virtual void ResetQueryPool(jCommandBuffer* pCommanBuffer = nullptr) {}
	virtual void Release() {}
	virtual void* GetHandle() const { return nullptr; }
	virtual int32 GetUsedQueryCount(int32 InFrameIndex) const { return -1; }

    virtual bool CanWholeQueryResult() const { return false; }
    virtual std::vector<uint64> GetWholeQueryResult(int32 InFrameIndex, int32 InCount) const { check(0);  return std::vector<uint64>(); }
	virtual std::vector<uint64> GetWholeQueryResult(int32 InFrameIndex) const { check(0);  return std::vector<uint64>(); }
};

struct jQuery
{
	virtual ~jQuery() {}
	virtual void Init() {}

	virtual void BeginQuery(const jCommandBuffer* commandBuffer) const {}
	virtual void EndQuery(const jCommandBuffer* commandBuffer) const {}
	
	virtual bool IsQueryTimeStampResult(bool isWaitUntilAvailable) const { return false; }
	virtual void GetQueryResult() const {}
	virtual void GetQueryResultFromQueryArray(int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryArray) const {}

	virtual uint64 GetElpasedTime() const { return 0; }
	virtual ECommandBufferType GetCommandBufferType() const { return ECommandBufferType::GRAPHICS; }
};

struct jQueryPrimitiveGenerated
{
	virtual ~jQueryPrimitiveGenerated() {}

	uint64 NumOfGeneratedPrimitives = 0;

	void Begin() const;
	void End() const;
	uint64 GetResult();
};

class jCamera;
class jLight;
class jDirectionalLight;
struct jShaderBindingInstance;

// View 에서 사용할 라이트 정보
class jViewLight
{
public:
    jViewLight() = default;
    jViewLight(jLight* InLight)
        : Light(InLight)
    { }

    jLight* Light = nullptr;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance;
    std::shared_ptr<jRenderTarget> ShadowMapPtr;
};

class jView
{
public:
	jView() = default;
	jView(const jCamera* camera, const std::vector<jLight*>& InLights);

	void PrepareViewUniformBufferShaderBindingInstance();
	void GetShaderBindingInstance(jShaderBindingInstanceArray& OutShaderBindingInstanceArray, bool InIsForwardRenderer = false) const;
	void GetShaderBindingLayout(jShaderBindingLayoutArray& OutShaderBindingsLayoutArray, bool InIsForwardRenderer = false) const;

	const jCamera* Camera = nullptr;
	std::vector<jViewLight> Lights;
	std::vector<jViewLight> ShadowCasterLights;
	std::shared_ptr<IUniformBufferBlock> ViewUniformBufferPtr;
	std::shared_ptr<jShaderBindingInstance> ViewUniformBufferShaderBindingInstance;
};

// BindGraphicsShaderBindingInstances 에서 바인딩 할 ShaderBindingInstance 배열을 얻어오는데 사용하는 구조체
struct jShaderBindingInstanceCombiner
{
	const jShaderBindingInstanceArray* ShaderBindingInstanceArray = nullptr;

    jResourceContainer<void*> DescriptorSetHandles;
    jResourceContainer<uint32> DynamicOffsets;
};

struct jImageSubResourceData
{
    int32 Format = 0;
    uint32 Width = 0;
    uint32 Height = 0;
	uint32 MipLevel = 0;
    uint32 Depth = 0;
    uint32 RowPitch = 0;
	uint64 Offset = 0;
};

struct jImageBulkData
{
    std::vector<unsigned char> ImageData;
    std::vector<jImageSubResourceData> SubresourceFootprints;
};

struct jRaytracingDispatchData
{
	int32 Width = 0;
	int32 Height = 0;
	int32 Depth = 0;
	jPipelineStateInfo* PipelineState = nullptr;
};

class jRHI
{
public:
	jRHI();
	virtual ~jRHI() {}

    static TResourcePool<jShader, jMutexRWLock> ShaderPool;
    static constexpr int32 MaxWaitingQuerySet = 4;
    static std::shared_ptr<jVertexBuffer> CubeMapInstanceDataForSixFace;

	template <typename T = jShader>
	T* CreateShader(const jShaderInfo& InShaderInfo) const
	{
		return (T*)ShaderPool.GetOrCreate<jShaderInfo, T>(InShaderInfo);
	}
	void AddShader(const jShaderInfo& InShaderInfo, jShader* InShader)
	{
		return ShaderPool.Add(InShaderInfo, InShader);
	}
    void ReleaseShader(const jShaderInfo& InShaderInfo)
    {
        ShaderPool.Release(InShaderInfo);
    }
	std::vector<jShader*> GetAllShaders() 
	{
		std::vector<jShader*> Out;
		ShaderPool.GetAllResource(Out);
		return Out;
	}

	virtual jName GetRHIName() { return jName::Invalid; }
	virtual bool InitRHI();
	virtual void OnInitRHI();
	virtual void ReleaseRHI();
	virtual void* GetWindow() const { return nullptr; }
	virtual jSamplerStateInfo* CreateSamplerState(const jSamplerStateInfo& info) const { return nullptr; }
	virtual void ReleaseSamplerState(jSamplerStateInfo* samplerState) const {}
	virtual void BindSamplerState(int32 index, const jSamplerStateInfo* samplerState) const {}
	virtual void SetClear(ERenderBufferType typeBit) const {}
	virtual void SetClearColor(float r, float g, float b, float a) const {}
	virtual void SetClearColor(Vector4 rgba) const {}
	virtual void SetClearBuffer(ERenderBufferType typeBit, const float* value, int32 bufferIndex) const {}
	virtual void SetClearBuffer(ERenderBufferType typeBit, const int32* value, int32 bufferIndex) const {}
	virtual void SetFrameBuffer(const jFrameBuffer* rt, int32 index = 0, bool mrt = false) const {}
	virtual void SetDrawBuffers(const std::initializer_list<EDrawBufferType>& list) const {}
	virtual void SetTextureFilter(ETextureType type, int32 sampleCount, ETextureFilterTarget target, ETextureFilter filter) const {}
	virtual void SetTextureWrap(int flag) const {}
	virtual void SetTexture(int32 index, const jTexture* texture) const {}
	virtual void DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const {}
	virtual void DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const {}
	virtual void DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount) const {}
	virtual void DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 instanceCount) const {}
	virtual void DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex) const {}
	virtual void DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex, int32 instanceCount) const {}
	virtual void DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const {}
	virtual void DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const {}
	virtual void DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const {}
	virtual void DispatchRay(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jRaytracingDispatchData& InDispatchData) const {}
	virtual void EnableDepthBias(bool enable, EPolygonMode polygonMode = EPolygonMode::FILL) const {}
	virtual void SetDepthBias(float constant, float slope) const {}
    virtual bool CreateShaderInternal(jShader* OutShader, const jShaderInfo& shaderInfo) const { return false; }
	virtual void ReleaseShader(jShader* shader) const {}
	virtual void SetViewport(int32 x, int32 y, int32 width, int32 height) const {}
	virtual void SetViewport(const jViewport& viewport) const {}
	virtual void SetViewportIndexed(int32 index, float x, float y, float width, float height) const {}
	virtual void SetViewportIndexed(int32 index, const jViewport& viewport) const {}
	virtual void SetViewportIndexedArray(int32 startIndex, int32 count, const jViewport* viewports) const {}
	virtual bool SetUniformbuffer(const jName& name, const Matrix& InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const int InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const uint32 InData, const jShader* InShader) const { return false; }
	FORCEINLINE virtual bool SetUniformbuffer(jName name, const bool InData, const jShader* InShader) const { return SetUniformbuffer(name, (int32)InData, InShader); }
	virtual bool SetUniformbuffer(const jName& name, const float InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const Vector2& InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const Vector& InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const Vector4& InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const Vector2i& InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const Vector3i& InData, const jShader* InShader) const { return false; }
	virtual bool SetUniformbuffer(const jName& name, const Vector4i& InData, const jShader* InShader) const { return false; }
	virtual bool GetUniformbuffer(Matrix& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(int& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(uint32& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(float& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(Vector2& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(Vector& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(Vector4& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(Vector2i& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(Vector3i& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual bool GetUniformbuffer(Vector4i& outResult, const jName& name, const jShader* shader) const { return false; }
	virtual jTexture* CreateNullTexture() const { return nullptr; }
	virtual std::shared_ptr<jTexture> CreateTextureFromData(const jImageData* InImageData) const { return nullptr; }
	virtual jTexture* CreateCubeTextureFromData(std::vector<void*> faces, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const { return nullptr; }
	virtual jFrameBuffer* CreateFrameBuffer(const jFrameBufferInfo& info) const { return nullptr; }
	virtual std::shared_ptr<jRenderTarget> CreateRenderTarget(const jRenderTargetInfo& info) const { return nullptr; }
	virtual void EnableDepthTest(bool enable) const {}
	virtual void EnableBlend(bool enable) const {}
	virtual void SetBlendFunc(EBlendFactor src, EBlendFactor dest) const {}
	virtual void SetBlendFuncRT(EBlendFactor src, EBlendFactor dest, int32 rtIndex = 0) const {}
	virtual void SetBlendEquation(EBlendOp func) const {}
	virtual void SetBlendEquation(EBlendOp func, int32 rtIndex) const {}
	virtual void SetBlendColor(float r, float g, float b, float a) const {}
	virtual void EnableStencil(bool enable) const {}
	virtual void SetStencilOpSeparate(EFace face, EStencilOp sFail, EStencilOp dpFail, EStencilOp dpPass) const {}
	virtual void SetStencilFunc(ECompareOp func, int32 ref, uint32 mask) const {}
	virtual void SetDepthFunc(ECompareOp func) const {}
	virtual void SetDepthMask(bool enable) const {}
	virtual void SetColorMask(bool r, bool g, bool b, bool a) const {}
	virtual void EnableSRGB(bool enable) const {  }
	virtual void EnableDepthClip(bool enable) const {  }
	virtual void BeginDebugEvent(const char* name) const {}
	virtual void EndDebugEvent() const {}
    virtual void BeginDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor = Vector4::ColorGreen) const {}
    virtual void EndDebugEvent(jCommandBuffer* InCommandBuffer) const {}
	virtual void GenerateMips(const jTexture* texture) const {}
	virtual jQuery* CreateQueryTime(ECommandBufferType InCmdBufferType) const { return nullptr;  }
	virtual void ReleaseQueryTime(jQuery* queryTime) const {}
	virtual void EnableWireframe(bool enable) const {}
	virtual void SetImageTexture(int32 index, const jTexture* texture, EImageTextureAccessType type) const {}
	virtual void SetPolygonMode(EFace face, EPolygonMode mode = EPolygonMode::FILL) {}
	virtual jQueryPrimitiveGenerated* CreateQueryPrimitiveGenerated() const { return nullptr; }
	virtual void ReleaseQueryPrimitiveGenerated(jQueryPrimitiveGenerated* query) const {}
	virtual void BeginQueryPrimitiveGenerated(const jQueryPrimitiveGenerated* query) const {}
	virtual void EndQueryPrimitiveGenerated() const {}
	virtual void GetQueryPrimitiveGeneratedResult(jQueryPrimitiveGenerated* query) const {}
	virtual void EnableRasterizerDiscard(bool enable) const {}
	virtual void SetTextureMipmapLevelLimit(ETextureType type, int32 sampleCount, int32 baseLevel, int32 maxLevel) const {}
	virtual void EnableMultisample(bool enable) const {}
	virtual void SetCubeMapSeamless(bool enable) const {}
	virtual void SetLineWidth(float width) const {}
	virtual void Flush() const {}
	virtual void Finish() const {}
	virtual std::shared_ptr<jRenderFrameContext> BeginRenderFrame() { return nullptr; }
    virtual void EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr) {}

	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const { return nullptr; }
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const { return nullptr; }
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const { return nullptr; }
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const { return nullptr; }

	virtual jPipelineStateInfo* CreatePipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jGraphicsPipelineShader shader
		, const jVertexBufferArray& InVertexBufferArray, const jRenderPass* renderPass, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* InPushConstant, int32 InSubpassIndex) const { return nullptr; }
	virtual jPipelineStateInfo* CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const { return nullptr; }
	virtual jPipelineStateInfo* CreateRaytracingPipelineStateInfo(const std::vector<jRaytracingPipelineShader>& InShaders, const jRaytracingPipelineData& InRaytracingData
		, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const { return nullptr; }
	virtual void RemovePipelineStateInfo(size_t InHash) {}

	virtual jShaderBindingLayout* CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const { check(0); return nullptr; }
	virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const { check(0); return nullptr; }

	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }
    virtual jRenderPass* GetOrCreateRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }

	FORCEINLINE virtual jCommandBufferManager* GetCommandBufferManager(ECommandBufferType InType) const
	{ 
		switch(InType)
		{
		case ECommandBufferType::GRAPHICS:	return GetGraphicsCommandBufferManager();
		case ECommandBufferType::COMPUTE:	return GetComputeCommandBufferManager();
		case ECommandBufferType::COPY:		return GetCopyCommandBufferManager();
		default:
			break;
		}
		return nullptr; 
	}
	virtual jCommandBufferManager* GetGraphicsCommandBufferManager() const { return nullptr; }
	virtual jCommandBufferManager* GetComputeCommandBufferManager() const { return nullptr; }
	virtual jCommandBufferManager* GetCopyCommandBufferManager() const { return nullptr; }
	virtual EMSAASamples GetSelectedMSAASamples() const { return EMSAASamples::COUNT_1; }

	// ResourceBarrier
	virtual void TransitionLayout(jCommandBuffer* commandBuffer, jTexture* texture, EResourceLayout newLayout) const { }
    virtual void TransitionLayout(jCommandBuffer* commandBuffer, jBuffer* buffer, EResourceLayout newLayout) const { }
    virtual void UAVBarrier(jCommandBuffer* commandBuffer, jTexture* texture) const { }
    virtual void UAVBarrier(jCommandBuffer* commandBuffer, jBuffer* buffer) const { }

	virtual void TransitionLayout(jTexture* texture, EResourceLayout newLayout) const { }
	virtual void TransitionLayout(jBuffer* buffer, EResourceLayout newLayout) const { }
	virtual void UAVBarrier(jTexture* texture) const { }
    virtual void UAVBarrier(jBuffer* buffer) const { }
	//////////////////////////////////////////////////////////////////////////

	virtual jQueryPool* GetQueryTimePool(ECommandBufferType InType) const { return nullptr; }
	virtual jSwapchain* GetSwapchain() const { return nullptr; }
	virtual class jSwapchainImage* GetSwapchainImage(int32 InIndex) const { return nullptr; }
	virtual void RecreateSwapChain() {}
	virtual uint32 GetMaxSwapchainCount() const { return 0; }

    virtual jQueryPool* GetQueryOcclusionPool() const { return nullptr; }
	virtual void BindShadingRateImage(jCommandBuffer* commandBuffer, jTexture* vrstexture) const {}
	virtual jMemoryPool* GetMemoryPool() const { return nullptr; }

	virtual void NextSubpass(const jCommandBuffer* commandBuffer) const {}
	virtual void BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const {}
	virtual void BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
		, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const {}
    virtual void BindRaytracingShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
        , const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const {}

	virtual jFenceManager* GetFenceManager() { return nullptr; }
	virtual jSemaphoreManager* GetSemaphoreManager() { return nullptr; }
	virtual uint32 GetCurrentFrameIndex() const { return 0; }
	virtual uint32 GetCurrentFrameNumber() const { return 0; }
	virtual void IncrementFrameNumber() {}
	virtual bool IsSupportVSync() const { return false; }
	virtual bool OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized) { return false; }
	virtual jRaytracingScene* CreateRaytracingScene() const { return nullptr; }
	virtual jCommandBuffer* BeginSingleTimeCommands() const { return nullptr; }
    virtual void EndSingleTimeCommands(jCommandBuffer* commandBuffer) const { }

	jRaytracingScene* RaytracingScene = nullptr;

	// CreateBuffers
	virtual std::shared_ptr<jBuffer> CreateStructuredBuffer(uint64 InSize, uint64 InAlignment, uint64 InStride, EBufferCreateFlag InBufferCreateFlag
		, EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const { return nullptr; }
	virtual std::shared_ptr<jBuffer> CreateRawBuffer(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
		, EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const { return nullptr; }
	virtual std::shared_ptr<jBuffer> CreateFormattedBuffer(uint64 InSize, uint64 InAlignment, ETextureFormat InFormat, EBufferCreateFlag InBufferCreateFlag
		, EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const { return nullptr; }
    virtual std::shared_ptr<IUniformBufferBlock> CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize = 0) const { return nullptr; }

	virtual IAtomicCounterBuffer* CreateAtomicCounterBuffer(const char* name, int32 bindingPoint) const { return nullptr; }
    virtual ITransformFeedbackBuffer* CreateTransformFeedbackBuffer(const char* name) const { return nullptr; }
    virtual std::shared_ptr<jVertexBuffer> CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const { return nullptr; }
    virtual std::shared_ptr<jIndexBuffer> CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const { return nullptr; }

	template <typename T = jBuffer>
	FORCEINLINE std::shared_ptr<T> CreateStructuredBuffer(uint64 InSize, uint64 InAlignment, uint64 InStride, EBufferCreateFlag InBufferCreateFlag
		, EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const 
	{
		return std::static_pointer_cast<T>(CreateStructuredBuffer(InSize, InAlignment, InStride, InBufferCreateFlag
			, InInitialState, InData, InDataSize, InResourceName));
	}

	template <typename T = jBuffer>
    FORCEINLINE std::shared_ptr<T> CreateRawBuffer(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
        , EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const 
	{
		return std::static_pointer_cast<T>(CreateRawBuffer(InSize, InAlignment, InBufferCreateFlag
			, InInitialState, InData, InDataSize, InResourceName));
    }

	template <typename T = jBuffer>
    FORCEINLINE std::shared_ptr<T> CreateFormattedBuffer(uint64 InSize, uint64 InAlignment, ETextureFormat InFormat, EBufferCreateFlag InBufferCreateFlag
        , EResourceLayout InInitialState, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr) const 
	{
		return std::static_pointer_cast<T>(CreateFormattedBuffer(InSize, InAlignment, InFormat, InBufferCreateFlag
			, InInitialState, InData, InDataSize, InResourceName));
    }

    template <typename T = IUniformBufferBlock>
    FORCEINLINE std::shared_ptr<T> CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize = 0) const
    {
        return std::static_pointer_cast<T>(CreateUniformBufferBlock(InName, InLifeTimeType, InSize));
    }
	//////////////////////////////////////////////////////////////////////////

	// Create Images
    virtual std::shared_ptr<jTexture> Create2DTexture(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
		, EResourceLayout InImageLayout = EResourceLayout::UNDEFINED, const jImageBulkData& InImageBulkData = {}, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr) const { return nullptr; }

	virtual std::shared_ptr<jTexture> CreateCubeTexture(uint32 InWidth, uint32 InHeight, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
		, EResourceLayout InImageLayout = EResourceLayout::UNDEFINED, const jImageBulkData& InImageBulkData = {}, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr) const { return nullptr; }

	template <typename T>
    std::shared_ptr<T> Create2DTexture(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
		, EResourceLayout InImageLayout = EResourceLayout::UNDEFINED, const jImageBulkData& InImageCopyData = {}, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr) const
	{
		return std::static_pointer_cast<T>(Create2DTexture(InWidth, InHeight, InArrayLayers, InMipLevels, InFormat, InTextureCreateFlag, InImageLayout, InImageCopyData, InClearValue, InResourceName));
    }

	template <typename T>
    std::shared_ptr<T> CreateCubeTexture(uint32 InWidth, uint32 InHeight, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
		, EResourceLayout InImageLayout = EResourceLayout::UNDEFINED, const jImageBulkData& InImageCopyData = {}, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr) const
	{
		return std::static_pointer_cast<T>(CreateCubeTexture(InWidth, InHeight, InMipLevels, InFormat, InTextureCreateFlag, InImageLayout, InImageCopyData, InClearValue, InResourceName));
    }
	//////////////////////////////////////////////////////////////////////////

	// To release renderpasses that have relation with rendertarget which will be released.
	virtual void RemoveRenderPassByHash(const std::vector<uint64>& InRelatedRenderPassHashes) const {}
	
	virtual float GetCurrentMonitorDPIScale() const { return 1.0f; }

	virtual jResourceBarrierBatcher* GetGlobalBarrierBatcher() const { return nullptr; }
};

// Not thred safe
#define SET_UNIFORM_BUFFER_STATIC(Name, CurrentData, Shader) \
{\
	static jUniformBuffer<typename std::decay<decltype(CurrentData)>::type> temp(jName(Name), CurrentData);\
	temp.Data = CurrentData;\
	temp.SetUniformbuffer(Shader);\
}

// Not thread safe
#define GET_UNIFORM_BUFFER_STATIC(ResultData, Name, Shader) \
{\
	static jName tempName(Name);\
	EUniformType UniformType = ConvertUniformType<typename std::decay<decltype(ResultData)>::type>();\
	JASSERT(UniformType != EUniformType::NONE);\
	g_rhi->GetUniformbuffer(ResultData, tempName, Shader);\
}

struct jScopeDebugEvent final
{
	jScopeDebugEvent(const jRHI* rhi, const char* name)
		: RHI(rhi)
	{
		JASSERT(RHI);
		RHI->BeginDebugEvent(name);
	}
	~jScopeDebugEvent()
	{
		RHI->EndDebugEvent();
	}

	const jRHI* RHI = nullptr;
};
#define SCOPE_DEBUG_EVENT(rhi, name) jScopeDebugEvent scope_debug_event(rhi, name);

//////////////////////////////////////////////////////////////////////////
#define DECLARE_UNIFORMBUFFER(EnumType, DataType) \
template <> struct jUniformBuffer<DataType> : public IUniformBuffer\
{\
	jUniformBuffer() = default;\
	jUniformBuffer(const jName& name, const DataType& data)\
		: IUniformBuffer(name), Data(data)\
	{}\
	static constexpr EUniformType Type = EnumType;\
	FORCEINLINE virtual EUniformType GetType() const { return Type; }\
	FORCEINLINE virtual void SetUniformbuffer(const jShader* InShader) const { g_rhi->SetUniformbuffer(Name, Data, InShader); }\
	DataType Data;\
};

DECLARE_UNIFORMBUFFER(EUniformType::MATRIX, Matrix);
DECLARE_UNIFORMBUFFER(EUniformType::BOOL, bool);
DECLARE_UNIFORMBUFFER(EUniformType::INT, int);
DECLARE_UNIFORMBUFFER(EUniformType::FLOAT, float);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR2, Vector2);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR3, Vector);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR4, Vector4);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR2I, Vector2i);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR3I, Vector3i);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR4I, Vector4i);

jName GetCommonTextureName(int32 index);
jName GetCommonTextureSRGBName(int32 index);

//////////////////////////////////////////////////////////////////////////

template <ETextureFilter TMinification = ETextureFilter::NEAREST, ETextureFilter TMagnification = ETextureFilter::NEAREST, ETextureAddressMode TAddressU = ETextureAddressMode::CLAMP_TO_EDGE
    , ETextureAddressMode TAddressV = ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode TAddressW = ETextureAddressMode::CLAMP_TO_EDGE, float TMipLODBias = 0.0f
    , float TMaxAnisotropy = 1.0f, Vector4 TBorderColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f), bool TIsEnableComparisonMode = false, ECompareOp TComparisonFunc = ECompareOp::LESS, float TMinLOD = -FLT_MAX, float TMaxLOD = FLT_MAX,
    ETextureComparisonMode TTextureComparisonMode = ETextureComparisonMode::NONE>
struct TSamplerStateInfo
{
    FORCEINLINE static jSamplerStateInfo* Create()
    {
        static jSamplerStateInfo* CachedInfo = nullptr;
        if (CachedInfo)
            return CachedInfo;

        jSamplerStateInfo initializer;
        initializer.Minification = TMinification;
        initializer.Magnification = TMagnification;
        initializer.AddressU = TAddressU;
        initializer.AddressV = TAddressV;
        initializer.AddressW = TAddressW;
        initializer.MipLODBias = TMipLODBias;
        initializer.MaxAnisotropy = TMaxAnisotropy;
		initializer.IsEnableComparisonMode = TIsEnableComparisonMode;
        initializer.TextureComparisonMode = TTextureComparisonMode;
        initializer.ComparisonFunc = TComparisonFunc;
        initializer.BorderColor = TBorderColor;
        initializer.MinLOD = TMinLOD;
        initializer.MaxLOD = TMaxLOD;
		initializer.GetHash();
		CachedInfo = g_rhi->CreateSamplerState(initializer);
		return CachedInfo;
    }
};

template <EPolygonMode TPolygonMode = EPolygonMode::FILL, ECullMode TCullMode = ECullMode::BACK, EFrontFace TFrontFace = EFrontFace::CCW,
    bool TDepthBiasEnable = false, float TDepthBiasConstantFactor = 0.0f, float TDepthBiasClamp = 0.0f, float TDepthBiasSlopeFactor = 0.0f,
    float TLineWidth = 1.0f, bool TDepthClampEnable = false, bool TRasterizerDiscardEnable = false, EMSAASamples TSampleCount = EMSAASamples::COUNT_1, 
	bool TSampleShadingEnable = true, float TMinSampleShading = 0.2f, bool TAlphaToCoverageEnable = false, bool TAlphaToOneEnable = false>
struct TRasterizationStateInfo
{
    FORCEINLINE static jRasterizationStateInfo* Create()
    {
        static jRasterizationStateInfo* CachedInfo = nullptr;
        if (CachedInfo)
            return CachedInfo;

        jRasterizationStateInfo initializer;
        initializer.PolygonMode = TPolygonMode;
        initializer.CullMode = TCullMode;
        initializer.FrontFace = TFrontFace;
        initializer.DepthBiasEnable = TDepthBiasEnable;
        initializer.DepthBiasConstantFactor = TDepthBiasConstantFactor;
        initializer.DepthBiasClamp = TDepthBiasClamp;
        initializer.DepthBiasSlopeFactor = TDepthBiasSlopeFactor;
        initializer.LineWidth = TLineWidth;
        initializer.DepthClampEnable = TDepthClampEnable;
        initializer.RasterizerDiscardEnable = TRasterizerDiscardEnable;

		initializer.SampleCount = TSampleCount;
		initializer.SampleShadingEnable = TSampleShadingEnable;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
		initializer.MinSampleShading = TMinSampleShading;
		initializer.AlphaToCoverageEnable = TAlphaToCoverageEnable;
		initializer.AlphaToOneEnable = TAlphaToOneEnable;

		initializer.GetHash();
		CachedInfo = g_rhi->CreateRasterizationState(initializer);
		return CachedInfo;
    }
};

//template <bool TSampleShadingEnable = true, float TMinSampleShading = 0.2f,
//    bool TAlphaToCoverageEnable = false, bool TAlphaToOneEnable = false>
//struct TMultisampleStateInfo
//{
//    FORCEINLINE static jMultisampleStateInfo* Create(EMSAASamples InSampleCount = EMSAASamples::COUNT_1)
//    {
//		static jMultisampleStateInfo* CachedInfo = nullptr;
//		if (CachedInfo)
//			return CachedInfo;
//
//        jMultisampleStateInfo initializer;
//        initializer.SampleCount = InSampleCount;
//        initializer.SampleShadingEnable = TSampleShadingEnable;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
//        initializer.MinSampleShading = TMinSampleShading;
//        initializer.AlphaToCoverageEnable = TAlphaToCoverageEnable;
//        initializer.AlphaToOneEnable = TAlphaToOneEnable;
//		initializer.GetHash();
//		CachedInfo = g_rhi->CreateMultisampleState(initializer);
//		return CachedInfo;
//    }
//};

template <EStencilOp TFailOp = EStencilOp::KEEP, EStencilOp TPassOp = EStencilOp::KEEP, EStencilOp TDepthFailOp = EStencilOp::KEEP,
    ECompareOp TCompareOp = ECompareOp::NEVER, uint32 TCompareMask = 0, uint32 TWriteMask = 0, uint32 TReference = 0>
struct TStencilOpStateInfo
{
    FORCEINLINE static jStencilOpStateInfo* Create()
    {
        static jStencilOpStateInfo* CachedInfo = nullptr;
        if (CachedInfo)
            return CachedInfo;

		jStencilOpStateInfo initializer;
        initializer.FailOp = TFailOp;
        initializer.PassOp = TPassOp;
        initializer.DepthFailOp = TDepthFailOp;
        initializer.CompareOp = TCompareOp;
        initializer.CompareMask = TCompareMask;
        initializer.WriteMask = TWriteMask;
        initializer.Reference = TReference;
		initializer.GetHash();
		CachedInfo = g_rhi->CreateStencilOpStateInfo(initializer);
		return CachedInfo;
    }
};

template <bool TDepthTestEnable = false, bool TDepthWriteEnable = false, ECompareOp TDepthCompareOp = ECompareOp::LEQUAL,
    bool TDepthBoundsTestEnable = false, bool TStencilTestEnable = false,
    float TMinDepthBounds = 0.0f, float TMaxDepthBounds = 1.0f>
struct TDepthStencilStateInfo
{
    FORCEINLINE static jDepthStencilStateInfo* Create(jStencilOpStateInfo* Front = nullptr, jStencilOpStateInfo* Back = nullptr)
    {
        static jDepthStencilStateInfo* CachedInfo = nullptr;
        if (CachedInfo)
            return CachedInfo;

        jDepthStencilStateInfo initializer;
        initializer.DepthTestEnable = TDepthTestEnable;
        initializer.DepthWriteEnable = TDepthWriteEnable;
        initializer.DepthCompareOp = TDepthCompareOp;
        initializer.DepthBoundsTestEnable = TDepthBoundsTestEnable;
        initializer.StencilTestEnable = TStencilTestEnable;
        initializer.Front = Front;
        initializer.Back = Back;
        initializer.MinDepthBounds = TMinDepthBounds;
        initializer.MaxDepthBounds = TMaxDepthBounds;
		initializer.GetHash();
		CachedInfo = g_rhi->CreateDepthStencilState(initializer);
		return CachedInfo;
    }
};

template <bool TBlendEnable = false, EBlendFactor TSrc = EBlendFactor::SRC_ALPHA, EBlendFactor TDest = EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp TBlendOp = EBlendOp::ADD,
    EBlendFactor TSrcAlpha = EBlendFactor::SRC_ALPHA, EBlendFactor TDestAlpha = EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp TAlphaBlendOp = EBlendOp::ADD,
    EColorMask TColorWriteMask = EColorMask::ALL>
struct TBlendingStateInfo
{
    FORCEINLINE static jBlendingStateInfo* Create()
    {
        static jBlendingStateInfo* CachedInfo = nullptr;
        if (CachedInfo)
            return CachedInfo;

        jBlendingStateInfo initializer;
        initializer.BlendEnable = TBlendEnable;
        initializer.Src = TSrc;
        initializer.Dest = TDest;
        initializer.BlendOp = TBlendOp;
        initializer.SrcAlpha = TSrcAlpha;
        initializer.DestAlpha = TDestAlpha;
        initializer.AlphaBlendOp = TAlphaBlendOp;
        initializer.ColorWriteMask = TColorWriteMask;
		initializer.GetHash();
		CachedInfo =  g_rhi->CreateBlendingState(initializer);
		return CachedInfo;
    }
};

class jGPUDebugEvent
{
public:
	jGPUDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName);
	jGPUDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor);
	~jGPUDebugEvent();
    jCommandBuffer* CommandBuffer = nullptr;
};

#define DEBUG_EVENT_NAME(Name, Line) Name##Line
#define DEBUG_EVENT(RenderFrameContextPtr, Name) jGPUDebugEvent DEBUG_EVENT_NAME(DebugEvent_, __LINE__)(RenderFrameContextPtr->GetActiveCommandBuffer(), Name);
#define DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, Name, ColorVec4) jGPUDebugEvent DEBUG_EVENT_NAME(DebugEvent_, __LINE__)(RenderFrameContextPtr->GetActiveCommandBuffer(), Name, ColorVec4);

