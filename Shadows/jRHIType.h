#pragma once

#include <type_traits>
#include "Core/jName.h"
#include "Math/Vector.h"

#define DECLARE_ENUM_BIT_OPERATORS(ENUM_TYPE)\
FORCEINLINE ENUM_TYPE operator | (ENUM_TYPE lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return static_cast<ENUM_TYPE>(static_cast<T>(lhs) | static_cast<T>(rhs));\
}\
FORCEINLINE ENUM_TYPE operator & (ENUM_TYPE lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return static_cast<ENUM_TYPE>(static_cast<T>(lhs) & static_cast<T>(rhs));\
}\
FORCEINLINE decltype(auto) operator ! (ENUM_TYPE value)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return !static_cast<T>(value);\
}

enum class EPrimitiveType : uint8
{
	POINTS,
	LINES,
	LINES_ADJACENCY,
	LINE_STRIP_ADJACENCY,
	TRIANGLES,
	TRIANGLE_STRIP,
	TRIANGLES_ADJACENCY,
	TRIANGLE_STRIP_ADJACENCY,
	MAX
};

enum class EBufferType : uint8
{
	STATIC,
	DYNAMIC
};

enum class EBufferElementType : uint8
{
	BYTE,
	UNSIGNED_INT,
	FLOAT,
	MAX,
};

struct IStreamParam
{
	IStreamParam() = default;
	IStreamParam(const jName& name, const EBufferType& bufferType, int32 stride, EBufferElementType elementType, int32 elementTypeSize, int32 instanceDivisor = 0)
		: Name(name), BufferType(bufferType), Stride(stride), ElementType(elementType), ElementTypeSize(elementTypeSize), InstanceDivisor(instanceDivisor)
	{}
	virtual ~IStreamParam() {}

	jName Name;
	EBufferType BufferType = EBufferType::STATIC;
	int32 Stride = 0;
	EBufferElementType ElementType = EBufferElementType::BYTE;
	int32 ElementTypeSize = 0;
	int32 InstanceDivisor = 0;

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

struct jVertexStreamData : public std::enable_shared_from_this<jVertexStreamData>
{
	~jVertexStreamData()
	{
		for (auto param : Params)
			delete param;
		Params.clear();
	}

	std::vector<IStreamParam*> Params;
	EPrimitiveType PrimitiveType;
	int ElementCount = 0;
};

struct jIndexStreamData : public std::enable_shared_from_this<jIndexStreamData>
{
	~jIndexStreamData()
	{
		delete Param;
		Param = nullptr;
	}

	IStreamParam* Param = nullptr;
	uint32 ElementCount = 0;
};

enum class ETextureFilterTarget : uint8
{
	MINIFICATION = 0,
	MAGNIFICATION,
	MAX
};

enum class ETextureFilter : uint8
{
	NEAREST = 0,
	LINEAR,
	NEAREST_MIPMAP_NEAREST,
	LINEAR_MIPMAP_NEAREST,
	NEAREST_MIPMAP_LINEAR,
	LINEAR_MIPMAP_LINEAR,
	MAX
};

enum class ETextureType : uint8
{
	TEXTURE_2D = 0,
	TEXTURE_2D_ARRAY,
	TEXTURE_CUBE,
	MAX,
};

enum class ETextureFormat
{
	// color
	RGB8 = 0,
	RGB16F,
	RGB32F,
	R11G11B10F,

	RGBA8,
	RGBA16F,
	RGBA32F,
	RGBA8I,
	RGBA8UI,

	BGRA8,

	R8,
	R16F,
	R32F,
	R32UI,

	RG8,
	RG16F,
	RG32F,

	// depth
	D16,
	D16_S8,
	D24,
	D24_S8,
	D32,
	D32_S8,

	MAX,
};

static bool IsDepthFormat(ETextureFormat format)
{
	switch (format)
	{
		case ETextureFormat::D16:
		case ETextureFormat::D16_S8:
		case ETextureFormat::D24:
		case ETextureFormat::D24_S8:
		case ETextureFormat::D32:
		case ETextureFormat::D32_S8:
			return true;
	}

	return false;
}

enum class EFormatType : uint8
{
	BYTE = 0,
	UNSIGNED_BYTE,
	INT,
	UNSIGNED_INT,
	HALF,
	FLOAT,
	MAX
};

enum class EBlendFactor : uint8
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

enum class EBlendOp : uint8
{
	ADD = 0,
	SUBTRACT,
	REVERSE_SUBTRACT,
	MIN_VALUE,
	MAX_VALUE,
	MAX,
};

enum class EFace : uint8
{
	FRONT = 0,
	BACK,
	FRONT_AND_BACK,
	MAX,
};

enum class EStencilOp : uint8
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

enum class ECompareOp : uint8
{
	NEVER = 0,
	LESS,
	EQUAL,
	LEQUAL,
	GREATER,
	NOTEQUAL,
	GEQUAL,
	ALWAYS,
	MAX
};

enum class ERenderBufferType : uint32
{
	NONE = 0,
	COLOR = 0x00000001,
	DEPTH = 0x00000002,
	STENCIL = 0x00000004,
	MAX = 0xffffffff
};

DECLARE_ENUM_BIT_OPERATORS(ERenderBufferType);

enum class EDrawBufferType : uint8
{
	COLOR_ATTACHMENT0 = 0,
	COLOR_ATTACHMENT1,
	COLOR_ATTACHMENT2,
	COLOR_ATTACHMENT3,
	COLOR_ATTACHMENT4,
	COLOR_ATTACHMENT5,
	COLOR_ATTACHMENT6,
	COLOR_ATTACHMENT7,
	MAX
};

enum class EDepthBufferType : uint8
{
	NONE = 0,
	DEPTH16,
	DEPTH16_STENCIL8,
	DEPTH24,
	DEPTH24_STENCIL8,
	DEPTH32,
	DEPTH32_STENCIL8,
	MAX,
};

enum class ETextureAddressMode : uint8
{
	REPEAT = 0,
	MIRRORED_REPEAT,
	CLAMP_TO_EDGE,
	CLAMP_TO_BORDER,
	MIRROR_CLAMP_TO_EDGE,
	MAX,
};

enum class ETextureComparisonMode : uint8
{
	NONE = 0,
	COMPARE_REF_TO_TEXTURE,					// to use PCF filtering by using samplerXXShadow series.
	MAX,
};

enum class EPolygonMode : uint8
{
	POINT = 0,
	LINE,
	FILL,
	MAX
};

enum class EImageTextureAccessType : uint8
{
	NONE = 0,
	READ_ONLY,
	WRITE_ONLY,
	READ_WRITE,
	MAX,
};

enum class EFrontFace : uint8
{
	CW = 0,
	CCW = 1,
	MAX,
};

enum class ECullMode : uint8
{
	BACK = 0,
	FRONT,
	FRONT_AND_BACK,
	MAX
};

enum class EMSAASamples : uint8
{
	COUNT_1 = 1,
	COUNT_2 = 2,
	COUNT_4 = 4,
	COUNT_8 = 8,
	COUNT_16 = 16,
	COUNT_32 = 32,
	COUNT_64 = 64
};

enum class EAttachmentLoadStoreOp : uint8
{
    LOAD_STORE = 0,
	LOAD_DONTCARE,
    CLEAR_STORE,
	CLEAR_DONTCARE,
    DONTCARE_STORE,
	DONTCARE_DONTCARE,
    MAX
};

enum class EShaderAccessStageFlag : uint32
{
    VERTEX = 0x00000001,
    TESSELLATION_CONTROL = 0x00000002,
    TESSELLATION_EVALUATION = 0x00000004,
    GEOMETRY = 0x00000008,
    FRAGMENT = 0x00000010,
    COMPUTE = 0x00000020,
    ALL_GRAPHICS = 0x0000001F,
    ALL = 0x7FFFFFFF
};

enum class EColorMask : uint8
{
	NONE = 0,
	R = 0x01,
	G = 0x02,
	B = 0x04,
	A = 0x08,
	ALL = 0x0F
};

