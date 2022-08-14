#pragma once
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

extern class jRHI* g_rhi;

struct jShader;
struct jShaderInfo;
struct jSamplerStateInfo;
class jSwapchain;

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

struct IUniformBufferBlock
{
	IUniformBufferBlock() = default;
	IUniformBufferBlock(const jName& name)
		: Name(name)
	{}
	virtual ~IUniformBufferBlock() {}

	jName Name;

	virtual size_t GetBufferSize() const { return 0; }
	virtual size_t GetBufferOffset() const { return 0; }

	virtual void Init(size_t size) = 0;
	virtual void Bind(const jShader* shader) const { }
	virtual void UpdateBufferData(const void* newData, size_t size) = 0;
	virtual void ClearBuffer(int32 clearValue = 0) = 0;
	
	virtual void* GetBuffer() const { return nullptr; }
	virtual void* GetBufferMemory() const { return nullptr; }
};

struct IShaderStorageBufferObject
{
	IShaderStorageBufferObject() = default;
	IShaderStorageBufferObject(const std::string& name)
		: Name(name)
	{}
	virtual ~IShaderStorageBufferObject() {}

	std::string Name;
	size_t Size = 0;

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

struct IAtomicCounterBuffer
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

struct ITransformFeedbackBuffer
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

struct jMaterialParam
{
	jMaterialParam() = default;
	jMaterialParam(const jName& name, const jTexture* texture, const jSamplerStateInfo* samplerstate)
		: Name(name), Texture(texture), SamplerState(samplerstate)
	{}
	virtual ~jMaterialParam() {}

	jName Name;
	const jTexture* Texture = nullptr;
	const jSamplerStateInfo* SamplerState = nullptr;
};

struct jMaterialData
{
	~jMaterialData()
	{
		Clear();
	}

	void AddMaterialParam(const jName& name, const jTexture* texture, const jSamplerStateInfo* samplerState = nullptr);
    void SetMaterialParam(int32 index, const jName& name, const jTexture* texture, const jSamplerStateInfo* samplerState = nullptr);
	void SetMaterialParam(int32 index, const jName& name);
	void SetMaterialParam(int32 index, const jTexture* texture);
	void SetMaterialParam(int32 index, const jSamplerStateInfo* samplerState);
	void Clear() { Params.clear(); }

	std::vector<jMaterialParam> Params;
};

class jMaterial
{
public:
	jMaterial() {}
	~jMaterial() {}
};

struct jQueryPool
{
	virtual ~jQueryPool() {}
	virtual bool Create() { return false; }
	virtual void ResetQueryPool(jCommandBuffer* pCommanBuffer = nullptr) {}
};

struct jQueryTime
{
	virtual ~jQueryTime() {}
	virtual void Init() {}

	uint64 TimeStampStartEnd[2] = { 0, 0 };
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

class jView
{
public:
	jView() = default;
	jView(jCamera* camera, jDirectionalLight* directionalLight = nullptr, jLight* pointLight = nullptr, jLight* spotLight = nullptr)
		: Camera(camera), DirectionalLight(directionalLight), PointLight(pointLight), SpotLight(spotLight)
	{}

	void SetupUniformBuffer();
	void GetShaderBindingInstance(std::vector<std::shared_ptr<jShaderBindingInstance>>& OutShaderBindingInstance);

	jCamera* Camera = nullptr;
	jDirectionalLight* DirectionalLight = nullptr;
	jLight* PointLight = nullptr;
	jLight* SpotLight = nullptr;
};

class jRHI
{
public:
	jRHI();
	virtual ~jRHI() {}

    static constexpr int32 MaxWaitingQuerySet = 3;

	virtual bool InitRHI() { return false; }
	virtual void ReleaseRHI() {}
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
	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const { return nullptr; }
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const { return nullptr; }
	virtual void UpdateVertexBuffer(jVertexBuffer* vb, const std::shared_ptr<jVertexStreamData>& streamData) const {}
	virtual void UpdateVertexBuffer(jVertexBuffer* vb, IStreamParam* streamParam, int32 streamParamIndex) const {}
	virtual void BindVertexBuffer(const jVertexBuffer* vb, const jShader* shader) const {}
	virtual void BindIndexBuffer(const jIndexBuffer* ib, const jShader* shader) const {}
	virtual void SetTextureFilter(ETextureType type, int32 sampleCount, ETextureFilterTarget target, ETextureFilter filter) const {}
	virtual void SetTextureWrap(int flag) const {}
	virtual void SetTexture(int32 index, const jTexture* texture) const {}
	virtual void DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const {}
	virtual void DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const {}
	virtual void DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const {}
	virtual void DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const {}
	virtual void DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const {}
	virtual void DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const {}
	virtual void DispatchCompute(uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const {}
	virtual void EnableDepthBias(bool enable, EPolygonMode polygonMode = EPolygonMode::FILL) const {}
	virtual void SetDepthBias(float constant, float slope) const {}
	virtual void SetShader(const jShader* shader) const {}
	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) const { return nullptr; }
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
	virtual jTexture* CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const { return nullptr; }
	virtual jTexture* CreateCubeTextureFromData(std::vector<void*> faces, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const { return nullptr; }
	virtual int32 SetMatetrial(const jMaterialData* materialData, const jShader* shader, int32 baseBindingIndex = 0) const { return baseBindingIndex; }
	virtual void EnableCullFace(bool enable) const {}
	virtual void SetFrontFace(EFrontFace frontFace) const {}
	virtual void EnableCullMode(ECullMode cullMode) const {}
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
	virtual IUniformBufferBlock* CreateUniformBufferBlock(const char* blockname, size_t size = 0) const { return nullptr; }
	virtual IShaderStorageBufferObject* CreateShaderStorageBufferObject(const char* blockname) const { return nullptr; }
	virtual IAtomicCounterBuffer* CreateAtomicCounterBuffer(const char* name, int32 bindingPoint) const { return nullptr; }
	virtual ITransformFeedbackBuffer* CreateTransformFeedbackBuffer(const char* name) const { return nullptr; }
	virtual void EnableSRGB(bool enable) const {  }
	virtual void EnableDepthClip(bool enable) const {  }
	virtual void BeginDebugEvent(const char* name) const {}
	virtual void EndDebugEvent() const {}
	virtual void GenerateMips(const jTexture* texture) const {}
	virtual jQueryTime* CreateQueryTime() const { return nullptr;  }
	virtual void ReleaseQueryTime(jQueryTime* queryTime) const {}
	virtual void QueryTimeStampStart(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jQueryTime* queryTimeStamp) const {}
	virtual void QueryTimeStampEnd(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jQueryTime* queryTimeStamp) const {}
	virtual bool IsQueryTimeStampResult(const jQueryTime* queryTimeStamp, bool isWaitUntilAvailable) const { return false; }
	virtual void GetQueryTimeStampResult(jQueryTime* queryTimeStamp) const {}
	virtual bool CanWholeQueryTimeStampResult() const { return false; }
	virtual std::vector<uint64> GetWholeQueryTimeStampResult(int32 InWatingResultIndex) const { check(0);  return std::vector<uint64>(); }
	virtual void GetQueryTimeStampResultFromWholeStampArray(jQueryTime* queryTimeStamp, int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryTimeStampArray) const {}
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
    virtual void EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr) {}

	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const { return nullptr; }
	virtual jMultisampleStateInfo* CreateMultisampleState(const jMultisampleStateInfo& initializer) const { return nullptr; }
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const { return nullptr; }
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const { return nullptr; }
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const { return nullptr; }

	virtual jPipelineStateInfo* CreatePipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader
		, const jVertexBuffer* vertexBuffer, const jRenderPass* renderPass, const std::vector<const jShaderBindingLayout*> shaderBindings) const { return nullptr; }

	virtual jPipelineStateInfo* CreateComputePipelineStateInfo(const jShader* shader, const std::vector<const jShaderBindingLayout*> shaderBindings) const { return nullptr; }

	virtual jShaderBindingLayout* CreateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings) const { check(0); return nullptr; }
	virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const std::vector<jShaderBinding>& InShaderBindings) const { check(0); return nullptr; }

	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindingLayout*>& shaderBindings) const { return nullptr; }
	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances) const { return nullptr; }

	template <typename T>
	FORCEINLINE T* CreatePipelineLayout(const std::vector<const jShaderBindingLayout*>& shaderBindings)
	{
		return (T*)CreatePipelineLayout(shaderBindings);
	}

	template <typename T>
	FORCEINLINE T* CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances)
	{
		return (T*)CreatePipelineLayout(shaderBindingInstances);
	}

	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }
	virtual jRenderPass* GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const jAttachment& colorResolveAttachment, const Vector2i& offset, const Vector2i& extent) const { return nullptr; }

	virtual jCommandBufferManager* GetCommandBufferManager() const { return nullptr; }
	virtual EMSAASamples GetSelectedMSAASamples() const { return EMSAASamples::COUNT_1; }

	virtual bool TransitionImageLayout(jCommandBuffer* commandBuffer, jTexture* texture, EImageLayout newLayout) const { return true; }
	virtual bool TransitionImageLayoutImmediate(jTexture* texture, EImageLayout newLayout) const { return true; }
	virtual jQueryPool* GetQueryPool() const { return nullptr; }
	virtual jSwapchain* GetSwapchain() const { return nullptr; }
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

class jLock
{
public:
	virtual ~jLock() {}
	virtual void Lock() {}
	virtual void Unlock() {}
};

using jEmptyLock = jLock;

class jMutexLock : public jLock
{
	void Lock()
	{
		lock.lock();
	}
	void Unlock()
	{
		lock.unlock();
	}

	std::mutex lock;
};

class jScopedLock
{
public:
	jScopedLock(jLock* InLock)
		: ScopedLock(InLock)
	{
		if (ScopedLock)
			ScopedLock->Lock();
	}
	~jScopedLock()
	{
		if (ScopedLock)
			ScopedLock->Unlock();
	}
	jLock* ScopedLock;
};

template <typename T, typename LOCK_TYPE = jEmptyLock>
class TResourcePool
{
public:
    template <typename TInitializer>
    T* GetOrCreate(const TInitializer& initializer)
    {
		jScopedLock s(&Lock);
        const size_t hash = initializer.GetHash();
        auto it_find = Pool.find(hash);
        if (Pool.end() != it_find)
        {
            return it_find->second;
        }

        auto* newResource = new T(initializer);
        newResource->Initialize();

        Pool.insert(std::make_pair(hash, newResource));
        return newResource;
    }

    std::unordered_map<size_t, T*> Pool;
	LOCK_TYPE Lock;
};

template <ETextureFilter TMinification = ETextureFilter::NEAREST, ETextureFilter TMagnification = ETextureFilter::NEAREST, ETextureAddressMode TAddressU = ETextureAddressMode::CLAMP_TO_EDGE
    , ETextureAddressMode TAddressV = ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode TAddressW = ETextureAddressMode::CLAMP_TO_EDGE, float TMipLODBias = 0.0f
    , float TMaxAnisotropy = 1.0f, Vector4 TBorderColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f), float TMinLOD = -FLT_MAX, float TMaxLOD = FLT_MAX,
    ECompareOp TComparisonFunc = ECompareOp::LESS, ETextureComparisonMode TTextureComparisonMode = ETextureComparisonMode::NONE>
struct TSamplerStateInfo
{
    FORCEINLINE static jSamplerStateInfo* Create()
    {
        jSamplerStateInfo state;
        state.Minification = TMinification;
        state.Magnification = TMagnification;
        state.AddressU = TAddressU;
        state.AddressV = TAddressV;
        state.AddressW = TAddressW;
        state.MipLODBias = TMipLODBias;
        state.MaxAnisotropy = TMaxAnisotropy;
        state.TextureComparisonMode = TTextureComparisonMode;
        state.ComparisonFunc = TComparisonFunc;
        state.BorderColor = TBorderColor;
        state.MinLOD = TMinLOD;
        state.MaxLOD = TMaxLOD;
        return g_rhi->CreateSamplerState(state);
    }
};

template <EPolygonMode TPolygonMode = EPolygonMode::FILL, ECullMode TCullMode = ECullMode::BACK, EFrontFace TFrontFace = EFrontFace::CCW,
    bool TDepthBiasEnable = false, float TDepthBiasConstantFactor = 0.0f, float TDepthBiasClamp = 0.0f, float TDepthBiasSlopeFactor = 0.0f,
    float TLineWidth = 1.0f, bool TDepthClampEnable = false, bool TRasterizerDiscardEnable = false>
struct TRasterizationStateInfo
{
    FORCEINLINE static jRasterizationStateInfo* Create()
    {
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
        return g_rhi->CreateRasterizationState(initializer);
    }
};

template <bool TSampleShadingEnable = true, float TMinSampleShading = 0.2f,
    bool TAlphaToCoverageEnable = false, bool TAlphaToOneEnable = false>
struct TMultisampleStateInfo
{
    FORCEINLINE static jMultisampleStateInfo* Create(EMSAASamples InSampleCount = EMSAASamples::COUNT_1)
    {
        jMultisampleStateInfo initializer;
        initializer.SampleCount = InSampleCount;
        initializer.SampleShadingEnable = TSampleShadingEnable;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
        initializer.MinSampleShading = TMinSampleShading;
        initializer.AlphaToCoverageEnable = TAlphaToCoverageEnable;
        initializer.AlphaToOneEnable = TAlphaToOneEnable;
        return g_rhi->CreateMultisampleState(initializer);
    }
};

template <EStencilOp TFailOp = EStencilOp::KEEP, EStencilOp TPassOp = EStencilOp::KEEP, EStencilOp TDepthFailOp = EStencilOp::KEEP,
    ECompareOp TCompareOp = ECompareOp::NEVER, uint32 TCompareMask = 0, uint32 TWriteMask = 0, uint32 TReference = 0>
struct TStencilOpStateInfo
{
    FORCEINLINE static jStencilOpStateInfo* Create()
    {
        jStencilOpStateInfo initializer;
        initializer.FailOp = TFailOp;
        initializer.PassOp = TPassOp;
        initializer.DepthFailOp = TDepthFailOp;
        initializer.CompareOp = TCompareOp;
        initializer.CompareMask = TCompareMask;
        initializer.WriteMask = TWriteMask;
        initializer.Reference = TReference;
        return g_rhi->CreateStencilOpStateInfo(initializer);
    }
};

template <bool TDepthTestEnable = false, bool TDepthWriteEnable = false, ECompareOp TDepthCompareOp = ECompareOp::LEQUAL,
    bool TDepthBoundsTestEnable = false, bool TStencilTestEnable = false,
    float TMinDepthBounds = 0.0f, float TMaxDepthBounds = 1.0f>
struct TDepthStencilStateInfo
{
    FORCEINLINE static jDepthStencilStateInfo* Create(jStencilOpStateInfo* Front = nullptr, jStencilOpStateInfo* Back = nullptr)
    {
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
        return g_rhi->CreateDepthStencilState(initializer);
    }
};

template <bool TBlendEnable = false, EBlendFactor TSrc = EBlendFactor::SRC_ALPHA, EBlendFactor TDest = EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp TBlendOp = EBlendOp::ADD,
    EBlendFactor TSrcAlpha = EBlendFactor::SRC_ALPHA, EBlendFactor TDestAlpha = EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp TAlphaBlendOp = EBlendOp::ADD,
    EColorMask TColorWriteMask = EColorMask::ALL>
struct TBlendingStateInfo
{
    FORCEINLINE static jBlendingStateInfo* Create()
    {
        jBlendingStateInfo initializer;
        initializer.BlendEnable = TBlendEnable;
        initializer.Src = TSrc;
        initializer.Dest = TDest;
        initializer.BlendOp = TBlendOp;
        initializer.SrcAlpha = TSrcAlpha;
        initializer.DestAlpha = TDestAlpha;
        initializer.AlphaBlendOp = TAlphaBlendOp;
        initializer.ColorWriteMask = TColorWriteMask;
        return g_rhi->CreateBlendingState(initializer);
    }
};
