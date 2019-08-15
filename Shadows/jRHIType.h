#pragma once

enum class EPrimitiveType
{
	LINES,
	TRIANGLES,
	TRIANGLE_STRIP,
	MAX
};

enum class EBufferType
{
	STATIC,
	DYNAMIC
};

enum class EBufferElementType
{
	BYTE,
	INT,
	FLOAT,
	MAX,
};

struct IStreamParam
{
	IStreamParam() = default;
	IStreamParam(const std::string& name, const EBufferType& bufferType, int stride, EBufferElementType elementType, int elementTypeSize)
		: Name(name), BufferType(bufferType), Stride(stride), ElementType(elementType), ElementTypeSize(elementTypeSize)
	{}
	virtual ~IStreamParam() {}

	EBufferType BufferType = EBufferType::STATIC;
	int Stride = 0;
	EBufferElementType ElementType = EBufferElementType::BYTE;
	int ElementTypeSize = 0;
	std::string Name;

	virtual const void* GetBufferData() const = 0;
	virtual size_t GetBufferSize() const { return 0; }
	virtual size_t GetElementSize() const { return 0; }
};

template <typename T>
struct jStreamParam : public IStreamParam
{
	static constexpr int ElementSize = sizeof(T);

	std::vector<T> Data;

	virtual const void* GetBufferData() const override { return &Data[0]; }
	virtual size_t GetBufferSize() const override { return Data.size() * ElementSize; }
	virtual size_t GetElementSize() const override { return ElementSize; }
};

struct jVertexStreamData : std::enable_shared_from_this<jVertexStreamData>
{
	std::vector<IStreamParam*> Params;
	EPrimitiveType PrimitiveType;
	int ElementCount = 0;
};

struct jIndexStreamData : std::enable_shared_from_this<jIndexStreamData>
{
	IStreamParam* Param;
	int ElementCount = 0;
};

enum class ETextureFilterTarget
{
	MINIFICATION = 0,
	MAGNIFICATION,
};

enum class ETextureFilter
{
	NEAREST = 0,
	LINEAR,
	NEAREST_MIPMAP_NEAREST,
	LINEAR_MIPMAP_NEAREST,
	NEAREST_MIPMAP_LINEAR,
	LINEAR_MIPMAP_LINEAR,
};

enum class ETextureType
{
	TEXTURE_2D = 0,
	TEXTURE_2D_ARRAY,
	TEXTURE_CUBE,
	TEXTURE_2D_ARRAY_OMNISHADOW,
	MAX,
};

enum class ETextureFormat
{
	RGB = 0,
	RGBA = 1,
	RG,
	R,
	R32F,
	RG32F,
	RGBA16F,
	RGBA32F,
	R11G11B10F,
};

enum class EFormatType
{
	BYTE = 0,
	INT,
	FLOAT,
};

enum class EBlendSrc
{
	ZERO = 0,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
	CONSTANT_COLOR,
	ONE_MINUS_CONSTANT_COLOR,
	CONSTANT_ALPHA,
	ONE_MINUS_CONSTANT_ALPHA,
	SRC_ALPHA_SATURATE,
	MAX
};

enum class EBlendDest
{
	ZERO = 0,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
	CONSTANT_COLOR,
	ONE_MINUS_CONSTANT_COLOR,
	CONSTANT_ALPHA,
	ONE_MINUS_CONSTANT_ALPHA,
	MAX
};

enum class EFace
{
	FRONT = 0,
	BACK,
	FRONT_AND_BACK,
	MAX,
};

enum class EStencilOp
{
	KEEP = 0,
	ZERO,
	REPLACE,
	INCR,
	INCR_WRAP,
	DECR,
	DECR_WRAP,
	INVERT,
	MAX
};

enum class EDepthStencilFunc
{
	NEVER = 0,
	LESS,
	EQUAL,
	LEQUAL,
	GREATER,
	NOTEQUAL,
	GEQUAL,
	ALWAYS
};

enum class ERenderBufferType : uint32
{
	NONE = 0,
	COLOR = 0x00000001,
	DEPTH = 0x00000002,
	STENCIL = 0x00000004,
	MAX = 0xffffffff
};

enum class EDrawBufferType
{
	COLOR_ATTACHMENT0 = 0,
	COLOR_ATTACHMENT1,
	COLOR_ATTACHMENT2,
	COLOR_ATTACHMENT3,
	COLOR_ATTACHMENT4,
	COLOR_ATTACHMENT5,
	COLOR_ATTACHMENT6,
	MAX
};

enum class EDepthBufferType
{
	DEPTH = 0,
	DEPTH_STENCIL,
	MAX,
};