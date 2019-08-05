#pragma once
#include "jRHIType.h"
#include "Math\Matrix.h"

extern class jRHI* g_rhi;

struct jShader;
struct jShaderInfo;

struct IBuffer
{
	virtual ~IBuffer() {}

	virtual const char* GetName() const { return ""; }
	virtual void Bind(const jShader* shader) const = 0;
};

struct jVertexBuffer : public IBuffer
{
	std::weak_ptr<jVertexStreamData> VertexStreamData;

	virtual void Bind(const jShader* shader) const {}
};

struct jIndexBuffer : public IBuffer
{
	std::weak_ptr<jIndexStreamData> IndexStreamData;

	virtual void Bind(const jShader* shader) const {}
};

struct jTexture
{

};

struct jViewport
{
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

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
	MAX,
};

struct IUniformBuffer : public IBuffer
{
	IUniformBuffer() = default;
	IUniformBuffer(const std::string& name)
		: Name(name)
	{}
	virtual ~IUniformBuffer() {}
	std::string Name;
	virtual EUniformType GetType() const { return EUniformType::NONE; }

	virtual void Bind(const jShader* shader) const override;
};

template <typename T>
struct jUniformBuffer : public IUniformBuffer
{
	T Data;
};

#define DECLARE_UNIFORMBUFFER(EnumType, DataType) \
template <> struct jUniformBuffer<DataType> : public IUniformBuffer\
{\
	jUniformBuffer() = default;\
	jUniformBuffer(const std::string& name, const DataType& data)\
		: IUniformBuffer(name), Data(data)\
	{}\
	static constexpr EUniformType Type = EnumType;\
	virtual EUniformType GetType() const { return Type; }\
	DataType Data;\
};

DECLARE_UNIFORMBUFFER(EUniformType::MATRIX, Matrix);
DECLARE_UNIFORMBUFFER(EUniformType::BOOL, bool);
DECLARE_UNIFORMBUFFER(EUniformType::INT, int);
DECLARE_UNIFORMBUFFER(EUniformType::FLOAT, float);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR2, Vector2);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR3, Vector);
DECLARE_UNIFORMBUFFER(EUniformType::VECTOR4, Vector4);

//template <>
//struct jUniformBuffer<Matrix> : public IUniformBuffer
//{
//	jUniformBuffer<Matrix>() = default;
//	jUniformBuffer<Matrix>(const std::string& name, const Matrix& data)
//		: IUniformBuffer(name), Data(data)
//	{}
//
//	static constexpr EUniformType Type = EUniformType::MATRIX;
//	virtual EUniformType GetType() const { return Type; }
//	Matrix Data;
//};

static int32 GetBindPoint()
{
	static int32 s_index = 0;
	return s_index++;
}

struct IUniformBufferBlock : public IBuffer
{
	IUniformBufferBlock() = default;
	IUniformBufferBlock(const std::string& name)
		: Name(name)
	{}
	virtual ~IUniformBufferBlock() {}

	std::string Name;
	int32 Size = 0;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const override { }
	virtual void UpdateBufferData(const void* newData, int32 size) = 0;
	virtual void ClearBuffer(int32 clearValue = 0) = 0;
};

struct IShaderStorageBufferObject : public IBuffer
{
	IShaderStorageBufferObject() = default;
	IShaderStorageBufferObject(const std::string& name)
		: Name(name)
	{}
	virtual ~IShaderStorageBufferObject() {}

	std::string Name;
	int32 Size = 0;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const override {}
	virtual void UpdateBufferData(void* newData, int32 size) = 0;
	virtual void GetBufferData(void* newData, int32 size) = 0;
	virtual void ClearBuffer(int32 clearValue) = 0;

	template <typename T>
	void GetBufferData(typename std::remove_reference<T>::type& out)
	{
		GetBufferData(&out, sizeof(T));
	}
};

struct IAtomicCounterBuffer : public IBuffer
{
	IAtomicCounterBuffer() = default;
	IAtomicCounterBuffer(const std::string& name, uint32 bindingPoint)
		: Name(name), BindingPoint(bindingPoint)
	{}
	virtual ~IAtomicCounterBuffer() {}

	std::string Name;
	int32 Size = 0;
	uint32 BindingPoint = -1;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const override {}
	virtual void UpdateBufferData(void* newData, int32 size) = 0;
	virtual void GetBufferData(void* newData, int32 size) = 0;
	virtual void ClearBuffer(int32 clearValue) = 0;

	template <typename T>
	void GetBufferData(typename std::remove_reference<T>::type& out)
	{
		GetBufferData(&out, sizeof(T));
	}
};

struct jMaterialParam
{
	virtual ~jMaterialParam() {}

	std::string Name;
	const jTexture* Texture = nullptr;
	ETextureFilter Minification = ETextureFilter::NEAREST;
	ETextureFilter Magnification = ETextureFilter::NEAREST;
};

struct jMaterialData
{
	~jMaterialData()
	{
		for (auto& iter : Params)
			delete iter;
		Params.clear();
	}
	std::vector<jMaterialParam*> Params;
};

class jMaterial
{
public:
	jMaterial() {}
	~jMaterial() {}
};

//////////////////////////////////////////////////////////////////////////

struct jRenderTargetInfo
{
	size_t GetHash() const
	{
		size_t result = 0;
		hash_combine(result, TextureType);
		hash_combine(result, InternalFormat);
		hash_combine(result, Format);
		hash_combine(result, FormatType);
		hash_combine(result, Width);
		hash_combine(result, Height);
		hash_combine(result, Height);
		hash_combine(result, TextureCount);
		return result;
	}

	ETextureType TextureType = ETextureType::TEXTURE_2D;
	EFormat InternalFormat = EFormat::RGB;
	EFormat Format = EFormat::RGB;
	EFormatType FormatType = EFormatType::BYTE;
	EDepthBufferType DepthBufferType = EDepthBufferType::DEPTH;
	int32 Width = 0;
	int32 Height = 0;
	int32 TextureCount = 1;
};

struct jRenderTarget : public std::enable_shared_from_this<jRenderTarget>
{
	virtual ~jRenderTarget() {}

	virtual jTexture* GetTexture(int32 index = 0) const { return Textures[index]; }
	virtual ETextureType GetTextureType() const { return Info.TextureType; }

	virtual bool Begin(int index = 0, bool mrt = false) const { return true; };
	virtual void End() const {}

	jRenderTargetInfo Info;
	std::vector<jTexture*> Textures;
};

class jRHI
{
public:
	jRHI();
	virtual ~jRHI() {}

	virtual void SetClear(ERenderBufferType typeBit) const {}
	virtual void SetClearColor(float r, float g, float b, float a) const {}
	virtual void SetClearColor(Vector4 rgba) const {}
	virtual void SetRenderTarget(const jRenderTarget* rt, int32 index = 0, bool mrt = false) const {}
	virtual void SetDrawBuffers(const std::initializer_list<EDrawBufferType>& list) const {}
	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const { return nullptr; }
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const { return nullptr; }
	virtual void UpdateVertexBuffer(jVertexBuffer* vb, const std::shared_ptr<jVertexStreamData>& streamData) const {}
	virtual void UpdateVertexBuffer(jVertexBuffer* vb, IStreamParam* streamParam, int32 streamParamIndex) const {}
	virtual void BindVertexBuffer(const jVertexBuffer* vb, const jShader* shader) const {}
	virtual void BindIndexBuffer(const jIndexBuffer* ib, const jShader* shader) const {}
	virtual void MapBufferdata(IBuffer* buffer);
	virtual void SetTextureFilter(ETextureType type, ETextureFilterTarget target, ETextureFilter filter) const {}
	virtual void SetTextureWrap(int flag) const {}
	virtual void SetTexture(int32 index, const jTexture* texture) const {}
	virtual void DrawArray(EPrimitiveType type, int vertStartIndex, int vertCount) const {}
	virtual void DrawElement(EPrimitiveType type, int elementSize, int32 startIndex = -1, int32 count = -1) const {}
	virtual void DrawElementBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const {}
	virtual void DispatchCompute(uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const {}
	virtual void EnableDepthBias(bool enable) const {}
	virtual void SetDepthBias(float constant, float slope) const {}
	virtual void SetShader(const jShader* shader) const {}
	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) const { return nullptr; }
	virtual void SetViewport(int32 x, int32 y, int32 width, int32 height) const {}
	virtual void SetViewport(const jViewport& viewport) const {}
	virtual void SetViewportIndexed(int32 index, float x, float y, float width, float height) const {}
	virtual void SetViewportIndexed(int32 index, const jViewport& viewport) const {}
	virtual void SetViewportIndexedArray(int32 startIndex, int32 count, const jViewport* viewports) const {}
	virtual bool SetUniformbuffer(const IUniformBuffer* buffer, const jShader* shader) const { return false; }
	virtual jTexture* CreateNullTexture() const { return nullptr; }
	virtual jTexture* CreateTextureFromData(unsigned char* data, int32 width, int32 height) const { return nullptr; }
	virtual void SetMatetrial(jMaterialData* materialData, const jShader* shader, int32 baseBindingIndex = 0) const {}
	virtual void EnableCullFace(bool enable) const {}
	virtual jRenderTarget* CreateRenderTarget(const jRenderTargetInfo& info) const { return nullptr; }
	virtual void EnableDepthTest(bool enable) const {}
	virtual void EnableBlend(bool enable) const {}
	virtual void SetBlendFunc(EBlendSrc src, EBlendDest dest) const {}
	virtual void EnableStencil(bool enable) const {}
	virtual void SetStencilOpSeparate(EFace face, EStencilOp sFail, EStencilOp dpFail, EStencilOp dpPass) const {}
	virtual void SetStencilFunc(EDepthStencilFunc func, int32 ref, uint32 mask) const {}
	virtual void SetDepthFunc(EDepthStencilFunc func) const {}
	virtual void SetDepthMask(bool enable) const {}
	virtual void SetColorMask(bool r, bool g, bool b, bool a) const {}
	virtual IUniformBufferBlock* CreateUniformBufferBlock(const char* blockname) const { return nullptr; }
	virtual IShaderStorageBufferObject* CreateShaderStorageBufferObject(const char* blockname) const { return nullptr; }
	virtual IAtomicCounterBuffer* CreateAtomicCounterBuffer(const char* name, int32 bindingPoint) const { return nullptr; }
	virtual void EnableSRGB(bool enable) const {  }
	virtual void EnableDepthClip(bool enable) const {  }
	virtual void BeginDebugEvent(const char* name) const {}
	virtual void EndDebugEvent() const {}
};

// Not thred safe
#define SET_UNIFORM_BUFFER_STATIC(Type, Name, CurrentData, Shader) \
{\
	static jUniformBuffer<Type> temp(Name, CurrentData);\
	temp.Data = CurrentData;\
	g_rhi->SetUniformbuffer(&temp, Shader);\
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