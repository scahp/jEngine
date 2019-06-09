#pragma once
#include "jRHIType.h"
#include "Math\Matrix.h"

extern class jRHI* g_rhi;

struct jShader;

struct jBuffer
{
	virtual ~jBuffer() {}

	virtual const char* GetName() const { return ""; }
};

struct jVertexBuffer : public jBuffer
{
	std::weak_ptr<jVertexStreamData> VertexStreamData;

	virtual void Bind(jShader* shader) {}
};

struct jIndexBuffer : public jBuffer
{
	std::weak_ptr<jIndexStreamData> IndexStreamData;

	virtual void Bind(jShader* shader) {}
};

struct jTexture
{

};

struct jShaderInfo
{
	size_t CreateShaderHash() const
	{
		return std::hash<std::string>{}(vs+vsPreProcessor+fs+fsPreProcessor);
	}
	std::string vs;
	std::string fs;
	std::string vsPreProcessor;
	std::string fsPreProcessor;
};

struct jShader
{
	static std::map<size_t, jShader*> ShaderMap;
	static jShader* GetShader(size_t hashCode);
	static jShader* CreateShader(const jShaderInfo& shaderInfo);
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

struct IUniformBuffer
{
	IUniformBuffer() = default;
	IUniformBuffer(const std::string& name)
		: Name(name)
	{}
	virtual ~IUniformBuffer() {}
	std::string Name;
	virtual EUniformType GetType() const { return EUniformType::NONE; }
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

struct IMaterialParam
{
	virtual ~IMaterialParam() {}
};

struct jMaterialData
{
	std::vector<IMaterialParam*> Params;
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
		return result;
	}

	ETextureType TextureType = ETextureType::TEXTURE_2D;
	EFormat InternalFormat = EFormat::RGB;
	EFormat Format = EFormat::RGB;
	EFormatType FormatType = EFormatType::BYTE;
	int32 Width = 0;
	int32 Height = 0;
};

struct jRenderTarget
{
	virtual ~jRenderTarget() {}

	virtual jTexture* GetTexture(int32 index = 0) const { return Texture[0]; }
	virtual ETextureType GetTextureType() const { return Info.TextureType; }

	virtual bool Begin(int index = 0, bool mrt = false) { return true; };
	virtual void End() {}

	jRenderTargetInfo Info;
	jTexture* Texture[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
};

struct jPipeline
{
};


class jRHI
{
public:
	jRHI();
	virtual ~jRHI() {}

	virtual void SetClear(ERenderBufferType typeBit) {}
	virtual void SetClearColor(float r, float g, float b, float a);

	virtual void SetRenderTarget(jRenderTarget* rt, int32 index = 0, bool mrt = false) {}
	virtual void SetDrawBuffers(const std::initializer_list<EDrawBufferType>& list) {}
	
	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) { return nullptr; }
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) { return nullptr; }

	virtual void UpdateVertexBuffer(jVertexBuffer* vb, const std::shared_ptr<jVertexStreamData>& streamData) {}
	virtual void UpdateVertexBuffer(jVertexBuffer* vb, IStreamParam* streamParam, int32 streamParamIndex) {}

	virtual void BindVertexBuffer(jVertexBuffer* vb, jShader* shader) {}
	virtual void BindIndexBuffer(jIndexBuffer* ib, jShader* shader) {}

	virtual void MapBufferdata(jBuffer* buffer);

	virtual void SetTextureFilter(ETextureType type, ETextureFilterTarget target, ETextureFilter filter) {}
	virtual void SetTextureWrap(int flag) {}

	virtual void SetTexture(int32 index, jTexture* texture) {}
	
	virtual void DrawArray(EPrimitiveType type, int vertStartIndex, int vertCount);
	virtual void DrawElement(EPrimitiveType type, int elementCount, int elementSize);

	virtual void SetShader(jShader* shader) {}
	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) { return nullptr; }

	virtual bool SetUniformbuffer(IUniformBuffer* buffer, jShader* shader) { return false; }

	virtual jTexture* CreateNullTexture() const { return nullptr; }

	virtual jTexture* CreateTextureFromData(unsigned char* data, int32 width, int32 height) { return nullptr; }

	virtual void SetMatetrial(jMaterialData* materialData, jShader* shader) {}

	virtual void EnableCullFace(bool enable) {}

	virtual jRenderTarget* CreateRenderTarget(const jRenderTargetInfo& info) { return nullptr; }

	virtual void EnableDepthTest(bool enable) {}
	
	virtual void EnableBlend(bool enable) {}

	virtual void SetBlendFunc(EBlendSrc src, EBlendDest dest) {}

	virtual void EnableStencil(bool enable) {}

	virtual void SetStencilOpSeparate(EFace face, EStencilOp sFail, EStencilOp dpFail, EStencilOp dpPass) {}

	virtual void SetStencilFunc(EDepthStencilFunc func, int32 ref, uint32 mask) {}

	virtual void SetDepthFunc(EDepthStencilFunc func) {}
	virtual void SetDepthMask(bool enable) {}
	virtual void SetColorMask(bool r, bool g, bool b, bool a) {}
};

