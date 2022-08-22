#pragma once

#include <type_traits>
#include "Core/jName.h"
#include "Math/Vector.h"

#define DECLARE_ENUM_BIT_OPERATORS(ENUM_TYPE)\
FORCEINLINE constexpr ENUM_TYPE operator | (ENUM_TYPE lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return static_cast<ENUM_TYPE>(static_cast<T>(lhs) | static_cast<T>(rhs));\
}\
FORCEINLINE constexpr ENUM_TYPE operator & (ENUM_TYPE lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return static_cast<ENUM_TYPE>(static_cast<T>(lhs) & static_cast<T>(rhs));\
}\
FORCEINLINE constexpr ENUM_TYPE operator ^ (ENUM_TYPE lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return static_cast<ENUM_TYPE>(static_cast<T>(lhs) ^ static_cast<T>(rhs));\
}\
FORCEINLINE constexpr ENUM_TYPE& operator |= (ENUM_TYPE& lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	lhs = static_cast<ENUM_TYPE>(static_cast<T>(lhs) | static_cast<T>(rhs));\
	return lhs;\
}\
FORCEINLINE constexpr ENUM_TYPE& operator &= (ENUM_TYPE& lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	lhs = static_cast<ENUM_TYPE>(static_cast<T>(lhs) & static_cast<T>(rhs));\
	return lhs;\
}\
FORCEINLINE constexpr ENUM_TYPE& operator ^= (ENUM_TYPE& lhs, ENUM_TYPE rhs)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	lhs = static_cast<ENUM_TYPE>(static_cast<T>(lhs) ^ static_cast<T>(rhs));\
	return lhs;\
}\
FORCEINLINE constexpr bool operator ! (ENUM_TYPE value)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return !static_cast<T>(value);\
}\
FORCEINLINE constexpr ENUM_TYPE operator ~ (ENUM_TYPE value)\
{\
	using T = std::underlying_type<ENUM_TYPE>::type;\
	return static_cast<ENUM_TYPE>(~static_cast<T>(value));\
}

//////////////////////////////////////////////////////////////////////////
// Auto generate type conversion code
template <typename T1, typename T2>
using ConversionTypePair = std::pair<T1, T2>;

// Parameter Packs 의 첫번째 타입을 얻어줌
template<typename T, typename... T1>
using PacksType = T;

template <typename... T1>
constexpr auto GenerateConversionTypeArray(T1... args)
{
	// std::pair 로 구성된 Parameter packs 의 첫번째 타입의 second_tpype 을 얻음
    using value_type = typename PacksType<T1...>::second_type;
    
	std::array<value_type, sizeof...(args)> newArray;
    auto AddElementFunc = [&newArray](const auto& arg)
    {
        newArray[(int64)arg.first] = arg.second;
    };

    int dummy[] = { 0, (AddElementFunc(args), 0)... };
    return newArray;
}

template <typename... T1>
constexpr auto GenerateConversionTypeMap(T1... args)
{
    // std::pair 로 구성된 Parameter packs 의 첫번째 타입의 first_type과 second_tpype 을 얻음
    using key_type = typename PacksType<T1...>::first_type;
	using value_type = typename PacksType<T1...>::second_type;

	std::unordered_map<key_type, value_type> newMap;
    auto AddElementFunc = [&newMap](const auto& arg)
    {
		newMap.insert(arg);
    };

    int dummy[] = { 0, (AddElementFunc(args), 0)... };
    return newMap;
}

template <typename... T1>
constexpr auto GenerateInverseConversionTypeMap(T1... args)
{
    // std::pair 로 구성된 Parameter packs 의 첫번째 타입의 first_type과 second_tpype 을 얻음
    using key_type = typename PacksType<T1...>::second_type;
    using value_type = typename PacksType<T1...>::first_type;

    std::unordered_map<key_type, value_type> newMap;
    auto AddElementFunc = [&newMap](const auto& arg)
    {
        newMap.insert(std::make_pair(arg.second, arg.first));
    };

    int dummy[] = { 0, (AddElementFunc(args), 0)... };
    return newMap;
}

#define CONVERSION_TYPE_ELEMENT(x, y) ConversionTypePair<decltype(x), decltype(y)>(x, y)

// 입력한 Elment를 Array 에 넣어줌. Engine type -> API type 전환시 사용함. Engine type 0~N 모든 정수 값을 사용하고, enum 범위가 크지 않기 때문에 배열로 사용하기 좋음.
#define GENERATE_STATIC_CONVERSION_ARRAY(...) {static auto _TypeArray_ = GenerateConversionTypeArray(__VA_ARGS__); return _TypeArray_[(int64)type];}

// 입력한 Element 를 Map 에 넣어줌. API type -> Engine type 전환시 사용함. Engine type의 경우 여러 enum 들이 섞여 있거나 해서 enum 범위가 커서 배열로 하는 경우 메모리 낭비가 큼.
#define GENERATE_STATIC_CONVERSION_MAP(...) {static auto _TypeMap_ = GenerateConversionTypeMap(__VA_ARGS__); return _TypeMap_[type];}
#define GENERATE_STATIC_INVERSECONVERSION_MAP(...) {static auto _TypeMap_ = GenerateInverseConversionTypeMap(__VA_ARGS__); return _TypeMap_[type];}

// Variadic macro 의 첫번째 항목 얻는 매크로
#define DEDUCE_FIRST(First, ...) First

// Engine type <-> API type 전환 함수 만들어주는 매크로
#define GENERATE_CONVERSION_FUNCTION(FunctionName, ...) \
using FunctionName##key_type = typename decltype(DEDUCE_FIRST(__VA_ARGS__))::first_type; \
using FunctionName##value_type = typename decltype(DEDUCE_FIRST(__VA_ARGS__))::second_type; \
FORCEINLINE auto FunctionName(FunctionName##key_type type) { \
	GENERATE_STATIC_CONVERSION_ARRAY(__VA_ARGS__)\
}\
FORCEINLINE auto FunctionName(FunctionName##value_type type) { \
	GENERATE_STATIC_INVERSECONVERSION_MAP(__VA_ARGS__)\
}

//////////////////////////////////////////////////////////////////////////

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
	UINT16,
	UINT32,
	FLOAT,
	MAX,
};

struct IStreamParam : public std::enable_shared_from_this<IStreamParam>
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
	virtual ~jVertexStreamData()
	{
		Params.clear();
	}

	std::vector<std::shared_ptr<IStreamParam>> Params;
	EPrimitiveType PrimitiveType;
	int32 ElementCount = 0;
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

DECLARE_ENUM_BIT_OPERATORS(ERenderBufferType)

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
	NONE = 0,
	BACK,
	FRONT,
	FRONT_AND_BACK,
	MAX
};

enum class EMSAASamples : uint32
{
	COUNT_1 =	0x00000001,
	COUNT_2 =	0x00000010,
	COUNT_4 =	0x00000100,
	COUNT_8 =	0x00001000,
	COUNT_16 =	0x00010000,
	COUNT_32 =	0x00100000,
	COUNT_64 =	0x01000000
};
DECLARE_ENUM_BIT_OPERATORS(EMSAASamples)

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
DECLARE_ENUM_BIT_OPERATORS(EShaderAccessStageFlag)

// BindingType 별 사용법 문서
// https://github.com/KhronosGroup/Vulkan-Guide/blob/master/chapters/mapping_data_to_shaders.adoc#storage-texel-buffer
enum class EShaderBindingType
{
	UNIFORMBUFFER = 0,
	UNIFROMBUFFER_DYNAMIC,			// 동적버퍼에 대한 설명 https://github.com/SaschaWillems/Vulkan/blob/master/examples/dynamicuniformbuffer/README.md
	TEXTURE_SAMPLER_SRV,
	TEXTURE_SRV,
	TEXTURE_UAV,
	SAMPLER,
	BUFFER_UAV,
	BUFFER_UAV_DYNAMIC,
	BUFFER_TEXEL_SRV,
	BUFFER_TEXEL_UAV,
	SUBPASS_INPUT_ATTACHMENT,
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
DECLARE_ENUM_BIT_OPERATORS(EColorMask)

enum class EImageLayout : uint8
{
    UNDEFINED = 0,
    GENERAL,
    COLOR_ATTACHMENT,
    DEPTH_STENCIL_ATTACHMENT,
    DEPTH_STENCIL_READ_ONLY,
    SHADER_READ_ONLY,
    TRANSFER_SRC,
    TRANSFER_DST,
    PREINITIALIZED,
    DEPTH_READ_ONLY_STENCIL_ATTACHMENT,
    DEPTH_ATTACHMENT_STENCIL_READ_ONLY,
    DEPTH_ATTACHMENT,
    DEPTH_READ_ONLY,
    STENCIL_ATTACHMENT,
    STENCIL_READ_ONLY,
    PRESENT_SRC,
    SHARED_PRESENT,
    SHADING_RATE_NV,
    FRAGMENT_DENSITY_MAP_EXT,
    READ_ONLY,
    ATTACHMENT,
};

struct jBuffer : std::enable_shared_from_this<jBuffer>
{
    virtual void Release() = 0;

	virtual void* GetMappedPointer() const = 0;
    virtual void* Map(uint64 offset, uint64 size) = 0;
	virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual void UpdateBuffer(const void* data, uint64 size) = 0;

    virtual void* GetHandle() const = 0;
    virtual void* GetMemoryHandle() const = 0;
	virtual size_t GetAllocatedSize() const = 0;
};
