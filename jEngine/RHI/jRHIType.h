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

	robin_hood::unordered_map<key_type, value_type> newMap;
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

	robin_hood::unordered_map<key_type, value_type> newMap;
    auto AddElementFunc = [&newMap](const auto& arg)
    {
		newMap[arg.second] = arg.first;
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

template <typename EnumType>
FORCEINLINE std::array<std::string, static_cast<size_t>(EnumType::MAX) + 1> split(const std::string& s, char delim) 
{
	std::array<std::string, static_cast<size_t>(EnumType::MAX) + 1> result;
    std::stringstream ss(s);
    std::string item;

	int32 Count = 0;
    while (getline(ss, item, delim)) 
	{
		result[Count++] = item;
    }

    return result;
}

#define MAKE_ENUM_TO_STRING_CONT(EnumType, N, EnumListString) \
static std::array<const char*, N> EnumType##Strings = split(EnumListString, ',');

// Enum class 선언과 Enum 에서 string 으로 변환할 수 있는 함수를 생성하는 매크로
#define DECLARE_ENUM_WITH_CONVERT_TO_STRING(EnumType, UnderlyingType, ...) \
enum class EnumType : UnderlyingType \
{ \
__VA_ARGS__ \
}; \
static std::array<std::string, static_cast<size_t>(EnumType::MAX) + 1> EnumType##Strings = split<EnumType>(#__VA_ARGS__, ',');\
FORCEINLINE const char* EnumToString(EnumType value) { \
    return EnumType##Strings[static_cast<size_t>(value)].c_str(); \
}
//\
//FORCEINLINE const wchar_t* EnumToWchar(EnumType value) { \
//    static const std::array<const wchar_t*, static_cast<size_t>(EnumType::MAX) + 1> EnumType##WStrings = { \
//        L#__VA_ARGS__ \
//    }; \
//    return EnumType##WStrings[static_cast<size_t>(value)]; \
//}

//////////////////////////////////////////////////////////////////////////

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EPrimitiveType, uint8,
	POINTS,
	LINES,
	LINES_ADJACENCY,
	LINE_STRIP_ADJACENCY,
	TRIANGLES,
	TRIANGLE_STRIP,
	TRIANGLES_ADJACENCY,
	TRIANGLE_STRIP_ADJACENCY,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EVertexInputRate, uint8,
	VERTEX,
	INSTANCE,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EBufferType, uint8,
	STATIC,
	DYNAMIC,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EBufferElementType, uint8,
	BYTE,
	BYTE_UNORM,
	UINT16,
	UINT32,
	FLOAT,
	MAX,
);

struct IStreamParam : public std::enable_shared_from_this<IStreamParam>
{
    struct jAttribute
    {
		jAttribute() = default;
		jAttribute(EBufferElementType InElementType, int32 InStride) : UnderlyingType(InElementType), Stride(InStride) {}
		jAttribute(jName InName, EBufferElementType InElementType, int32 InStride) : Name(InName), UnderlyingType(InElementType), Stride(InStride) {}

		jName Name;
        EBufferElementType UnderlyingType = EBufferElementType::BYTE;
		int32 Stride = 0;
    };

	IStreamParam() = default;
	IStreamParam(const jName& name, const EBufferType& bufferType, int32 stride, std::vector<jAttribute> elements, int32 instanceDivisor = 0)
		: Name(name), BufferType(bufferType), Stride(stride), Attributes(elements), InstanceDivisor(instanceDivisor)
	{}
	virtual ~IStreamParam() {}

	jName Name;
	EBufferType BufferType = EBufferType::STATIC;
	int32 Stride = 0;
	std::vector<jAttribute> Attributes;
	int32 InstanceDivisor = 0;

	virtual const void* GetBufferData() const = 0;
	virtual size_t GetBufferSize() const { return 0; }
	virtual size_t GetElementSize() const { return 0; }
    virtual size_t GetStride() const { return 0; }
    virtual size_t GetNumOfElement() const { return 0; }
};

template <typename T>
struct jStreamParam : public IStreamParam
{
	static constexpr int ElementSize = sizeof(T);

	std::vector<T> Data;

	virtual const void* GetBufferData() const override { return &Data[0]; }
	virtual size_t GetBufferSize() const override { return Data.size() * ElementSize; }
	virtual size_t GetElementSize() const override { return ElementSize; }
	virtual size_t GetStride() const override { return ElementSize; }
	virtual size_t GetNumOfElement() const override { return Data.size(); }
};

struct jVertexStreamData : public std::enable_shared_from_this<jVertexStreamData>
{
	virtual ~jVertexStreamData()
	{
		Params.clear();
	}

	virtual int32 GetEndLocation() const
	{
		check(ElementCount);

		int32 endLocation = StartLocation;
		for (const std::shared_ptr<IStreamParam>& iter : Params)
		{
			endLocation += (int32)iter->Attributes.size();
		}
		return endLocation;
	}

	std::vector<std::shared_ptr<IStreamParam>> Params;
	EPrimitiveType PrimitiveType = EPrimitiveType::TRIANGLES;
	EVertexInputRate VertexInputRate = EVertexInputRate::VERTEX;
	int32 ElementCount = 0;
	int32 BindingIndex = 0;
	int32 StartLocation = 0;
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

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ETextureFilter, uint8,
	NEAREST,
	LINEAR,
	NEAREST_MIPMAP_NEAREST,
	LINEAR_MIPMAP_NEAREST,
	NEAREST_MIPMAP_LINEAR,
	LINEAR_MIPMAP_LINEAR,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ETextureType, uint8,
	TEXTURE_1D,
	TEXTURE_2D,
	TEXTURE_2D_ARRAY,
	TEXTURE_3D,
	TEXTURE_3D_ARRAY,
	TEXTURE_CUBE,
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ETextureFormat, uint8,
	// color
	RGB8,
	RGB16F,
	RGB32F,
	R11G11B10F,

	RGBA8,
	RGBA16F,
	RGBA32F,
	RGBA8SI,
	RGBA8UI,

	BGRA8,

	R8,
	R16F,
	R32F,
	R8UI,
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

	BC1_UNORM,
	BC2_UNORM,
    BC3_UNORM,
    BC4_UNORM,
    BC4_SNORM,
    BC5_UNORM,
    BC5_SNORM,
    BC6H_UF16,
    BC6H_SF16,
    BC7_UNORM,
	MAX,
);
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

static bool IsDepthOnlyFormat(ETextureFormat format)
{
    switch (format)
    {
    case ETextureFormat::D16:
    case ETextureFormat::D24:
    case ETextureFormat::D32:
        return true;
    }

    return false;
}

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EFormatType, uint8,
	BYTE,
	UNSIGNED_BYTE,
	SHORT_INT,
	INT,
	UNSIGNED_INT,
	HALF,
	FLOAT,
	MAX
);



GENERATE_CONVERSION_FUNCTION(GetTexturePixelType,
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB8, EFormatType::UNSIGNED_BYTE),          // not support rgb8 -> rgba8
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, EFormatType::HALF),                 // not support rgb32 -> rgba32
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, EFormatType::HALF),                 // not support rgb16 -> rgba16
    CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, EFormatType::HALF),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, EFormatType::HALF),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, EFormatType::FLOAT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8SI, EFormatType::INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, EFormatType::INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::R8, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::R16F, EFormatType::HALF),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, EFormatType::FLOAT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::R8UI, EFormatType::INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, EFormatType::INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RG8, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RG16F, EFormatType::HALF),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, EFormatType::FLOAT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::D16, EFormatType::SHORT_INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::D16_S8, EFormatType::INT),                  // not support d16_s8 -> d24_s8
    CONVERSION_TYPE_ELEMENT(ETextureFormat::D24, EFormatType::INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, EFormatType::INT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, EFormatType::FLOAT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, EFormatType::FLOAT),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC1_UNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC2_UNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC3_UNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC4_UNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC4_SNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC5_UNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC5_SNORM, EFormatType::UNSIGNED_BYTE),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC6H_UF16, EFormatType::HALF),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC6H_SF16, EFormatType::HALF),
    CONVERSION_TYPE_ELEMENT(ETextureFormat::BC7_UNORM, EFormatType::HALF)
)


DECLARE_ENUM_WITH_CONVERT_TO_STRING(EBlendFactor, uint8,
	ZERO,
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
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EBlendOp, uint8,
	ADD,
	SUBTRACT,
	REVERSE_SUBTRACT,
	MIN_VALUE,
	MAX_VALUE,
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EFace, uint8,
	FRONT,
	BACK,
	FRONT_AND_BACK,
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EStencilOp, uint8,
	KEEP,
	ZERO,
	REPLACE,
	INCR,
	INCR_WRAP,
	DECR,
	DECR_WRAP,
	INVERT,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ECompareOp, uint8,
	NEVER,
	LESS,
	EQUAL,
	LEQUAL,
	GREATER,
	NOTEQUAL,
	GEQUAL,
	ALWAYS,
	MAX
);

enum class ERenderBufferType : uint32
{
	NONE = 0,
	COLOR = 0x00000001,
	DEPTH = 0x00000002,
	STENCIL = 0x00000004,
	MAX = 0xffffffff
};

DECLARE_ENUM_BIT_OPERATORS(ERenderBufferType)

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EDrawBufferType, uint8,
	COLOR_ATTACHMENT0,
	COLOR_ATTACHMENT1,
	COLOR_ATTACHMENT2,
	COLOR_ATTACHMENT3,
	COLOR_ATTACHMENT4,
	COLOR_ATTACHMENT5,
	COLOR_ATTACHMENT6,
	COLOR_ATTACHMENT7,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EDepthBufferType, uint8,
	NONE,
	DEPTH16,
	DEPTH16_STENCIL8,
	DEPTH24,
	DEPTH24_STENCIL8,
	DEPTH32,
	DEPTH32_STENCIL8,
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ETextureAddressMode, uint8,
	REPEAT,
	MIRRORED_REPEAT,
	CLAMP_TO_EDGE,
	CLAMP_TO_BORDER,
	MIRROR_CLAMP_TO_EDGE,
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ETextureComparisonMode, uint8,
	NONE,
	COMPARE_REF_TO_TEXTURE,					// to use PCF filtering by using samplerXXShadow series.
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EPolygonMode, uint8,
	POINT,
	LINE,
	FILL,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EImageTextureAccessType, uint8,
	NONE,
	READ_ONLY,
	WRITE_ONLY,
	READ_WRITE,
	MAX,
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EFrontFace, uint8,
	CW,
	CCW,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ECullMode, uint8,
	NONE,
	BACK,
	FRONT,
	FRONT_AND_BACK,
	MAX
);

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

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EAttachmentLoadStoreOp, uint8,
    LOAD_STORE,
	LOAD_DONTCARE,
    CLEAR_STORE,
	CLEAR_DONTCARE,
    DONTCARE_STORE,
	DONTCARE_DONTCARE,
    MAX
);

enum class EShaderAccessStageFlag : uint32
{
    VERTEX = 0x00000001,
    TESSELLATION_CONTROL = 0x00000002,
    TESSELLATION_EVALUATION = 0x00000004,
    GEOMETRY = 0x00000008,
    FRAGMENT = 0x00000010,
    COMPUTE = 0x00000020,
	RAYTRACING = 0x00000040,
	RAYTRACING_RAYGEN = 0x00000080,
	RAYTRACING_MISS = 0x00000100,
	RAYTRACING_CLOSESTHIT = 0x00000200,
	RAYTRACING_ANYHIT = 0x00000400,
	ALL_RAYTRACING = RAYTRACING_RAYGEN | RAYTRACING_MISS | RAYTRACING_CLOSESTHIT | RAYTRACING_ANYHIT,
    ALL_GRAPHICS = 0x0000001F,
    ALL = 0x7FFFFFFF
};
DECLARE_ENUM_BIT_OPERATORS(EShaderAccessStageFlag)

// BindingType 별 사용법 문서
// https://github.com/KhronosGroup/Vulkan-Guide/blob/master/chapters/mapping_data_to_shaders.adoc#storage-texel-buffer
DECLARE_ENUM_WITH_CONVERT_TO_STRING(EShaderBindingType, uint32,
	UNIFORMBUFFER,
	UNIFORMBUFFER_DYNAMIC,			// 동적버퍼에 대한 설명 https://github.com/SaschaWillems/Vulkan/blob/master/examples/dynamicuniformbuffer/README.md
	TEXTURE_SAMPLER_SRV,
	TEXTURE_SRV,
	TEXTURE_UAV,
	TEXTURE_ARRAY_SRV,
	SAMPLER,
	BUFFER_SRV,						// SSBO or StructuredBuffer
	BUFFER_UAV,
	BUFFER_UAV_DYNAMIC,
	BUFFER_TEXEL_SRV,
	BUFFER_TEXEL_UAV,
	ACCELERATION_STRUCTURE_SRV,
	SUBPASS_INPUT_ATTACHMENT,
	MAX
);

enum class EVulkanBufferBits : uint32
{
    TRANSFER_SRC = 0x00000001,
    TRANSFER_DST = 0x00000002,
    UNIFORM_TEXEL_BUFFER = 0x00000004,
    STORAGE_TEXEL_BUFFER = 0x00000008,
    UNIFORM_BUFFER = 0x00000010,
    STORAGE_BUFFER = 0x00000020,
    INDEX_BUFFER = 0x00000040,
    VERTEX_BUFFER = 0x00000080,
    INDIRECT_BUFFER = 0x00000100,
    SHADER_DEVICE_ADDRESS = 0x00020000,
    TRANSFORM_FEEDBACK_BUFFER = 0x00000800,
    TRANSFORM_FEEDBACK_COUNTER_BUFFER = 0x00001000,
    CONDITIONAL_RENDERING = 0x00000200,
    ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY = 0x00080000,
    ACCELERATION_STRUCTURE_STORAGE = 0x00100000,
    SHADER_BINDING_TABLE = 0x00000400,
};
DECLARE_ENUM_BIT_OPERATORS(EVulkanBufferBits)

enum class EVulkanMemoryBits : uint32
{
	DEVICE_LOCAL = 0x00000001,
	HOST_VISIBLE = 0x00000002,
	HOST_COHERENT = 0x00000004,
	HOST_CACHED = 0x00000008,
	LAZILY_ALLOCATED = 0x00000010,
	PROTECTED = 0x00000020,
	DEVICE_COHERENT_AMD = 0x00000040,
	DEVICE_UNCACHED_AMD = 0x00000080,
	RDMA_CAPABLE_NV = 0x00000100,
	FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
};
DECLARE_ENUM_BIT_OPERATORS(EVulkanMemoryBits)

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

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EImageLayout, uint8,
    UNDEFINED,
    GENERAL,
	UAV,
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
	ACCELERATION_STRUCTURE,
	MAX
);

struct jShaderBindableResource
{
	jShaderBindableResource() = default;
	jShaderBindableResource(const jName& InName) : ResourceName(InName) {}
	virtual ~jShaderBindableResource() {}

    jName ResourceName;
};

struct jBuffer : public jShaderBindableResource, public std::enable_shared_from_this<jBuffer>
{
	virtual ~jBuffer() {}
    virtual void Release() = 0;

	virtual void* GetMappedPointer() const = 0;
    virtual void* Map(uint64 offset, uint64 size) = 0;
	virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual void UpdateBuffer(const void* data, uint64 size) = 0;

    virtual void* GetHandle() const = 0;
	virtual uint64 GetOffset() const = 0;
	virtual uint64 GetAllocatedSize() const = 0;				// AllocatedSize from memory pool
	virtual uint64 GetBufferSize() const = 0;					// RequstedSize
};

enum class EPipelineStageMask : uint32
{
    NONE = 0,
    TOP_OF_PIPE_BIT = 0x00000001,
    DRAW_INDIRECT_BIT = 0x00000002,
    VERTEX_INPUT_BIT = 0x00000004,
    VERTEX_SHADER_BIT = 0x00000008,
    TESSELLATION_CONTROL_SHADER_BIT = 0x00000010,
    TESSELLATION_EVALUATION_SHADER_BIT = 0x00000020,
    GEOMETRY_SHADER_BIT = 0x00000040,
    FRAGMENT_SHADER_BIT = 0x00000080,
    EARLY_FRAGMENT_TESTS_BIT = 0x00000100,
    LATE_FRAGMENT_TESTS_BIT = 0x00000200,
    COLOR_ATTACHMENT_OUTPUT_BIT = 0x00000400,
    COMPUTE_SHADER_BIT = 0x00000800,
    TRANSFER_BIT = 0x00001000,
    BOTTOM_OF_PIPE_BIT = 0x00002000,
    HOST_BIT = 0x00004000,
    ALL_GRAPHICS_BIT = 0x00008000,
    ALL_COMMANDS_BIT = 0x00010000,
};
DECLARE_ENUM_BIT_OPERATORS(EPipelineStageMask)

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EPipelineDynamicState, uint8,
    VIEWPORT,
    SCISSOR,
    LINE_WIDTH,
    DEPTH_BIAS,
    BLEND_CONSTANTS,
    DEPTH_BOUNDS,
    STENCIL_COMPARE_MASK,
    STENCIL_WRITE_MASK,
    STENCIL_REFERENCE,
    CULL_MODE,
    FRONT_FACE,
    PRIMITIVE_TOPOLOGY,
    VIEWPORT_WITH_COUNT,
    SCISSOR_WITH_COUNT,
    VERTEX_INPUT_BINDING_STRIDE,
    DEPTH_TEST_ENABLE,
    DEPTH_WRITE_ENABLE,
    DEPTH_COMPARE_OP,
    DEPTH_BOUNDS_TEST_ENABLE,
    STENCIL_TEST_ENABLE,
    STENCIL_OP,
    RASTERIZER_DISCARD_ENABLE,
    DEPTH_BIAS_ENABLE,
    PRIMITIVE_RESTART_ENABLE,
    VIEWPORT_W_SCALING_NV,
    DISCARD_RECTANGLE_EXT,
    SAMPLE_LOCATIONS_EXT,
    RAY_TRACING_PIPELINE_STACK_SIZE_KHR,
    VIEWPORT_SHADING_RATE_PALETTE_NV,
    VIEWPORT_COARSE_SAMPLE_ORDER_NV,
    EXCLUSIVE_SCISSOR_NV,
    FRAGMENT_SHADING_RATE_KHR,
    LINE_STIPPLE_EXT,
    VERTEX_INPUT_EXT,
    PATCH_CONTROL_POINTS_EXT,
    LOGIC_OP_EXT,
    COLOR_WRITE_ENABLE_EXT,
	MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(EDescriptorHeapTypeDX12, uint8,
    CBV_SRV_UAV,
    SAMPLER,
    RTV,
    DSV,
    MAX
);

DECLARE_ENUM_WITH_CONVERT_TO_STRING(ERTClearType, uint8,
    None,
    Color,
    DepthStencil,
	MAX
);

enum class EBufferCreateFlag : uint32
{
    NONE = 0,
    CPUAccess = 0x00000001,
    UAV = 0x00000002,
    Readback = 0x00000004,
	AccelerationStructureBuildInput = 0x00000008,
	VertexBuffer = 0x00000010,
	IndexBuffer = 0x00000020,
	IndirectCommand = 0x00000040,
	ShaderBindingTable = 0x00000080,
	AccelerationStructure = 0x00000100,
};
DECLARE_ENUM_BIT_OPERATORS(EBufferCreateFlag)

enum class ETextureCreateFlag : uint32
{
    NONE = 0,
    RTV = 0x00000001,
    UAV = 0x00000002,
    CPUAccess = 0x00000004,
	TransferDst = 0x00000008,
	TransferSrc = 0x00000010,
	ShadingRate = 0x00000020,
	DSV = 0x00000040,
	SubpassInput = 0x00000040,
	Memoryless = 0x00000080,
};
DECLARE_ENUM_BIT_OPERATORS(ETextureCreateFlag)

struct jDepthStencilClearType
{
    float Depth;
    uint32 Stencil;
};

class jRTClearValue
{
public:
    static const jRTClearValue Invalid;

	union jClearValueType
	{
		float Color[4];
		jDepthStencilClearType DepthStencil;
	};

	constexpr jRTClearValue() = default;
    constexpr jRTClearValue(const Vector4& InColor)
        : Type(ERTClearType::Color)
    {
        ClearValue.Color[0] = InColor.x;
        ClearValue.Color[1] = InColor.y;
        ClearValue.Color[2] = InColor.z;
        ClearValue.Color[3] = InColor.w;
    }
	constexpr jRTClearValue(float InR, float InG, float InB, float InA)
		: Type(ERTClearType::Color)
	{
		ClearValue.Color[0] = InR;
		ClearValue.Color[1] = InG;
		ClearValue.Color[2] = InB;
		ClearValue.Color[3] = InA;
	}
    constexpr jRTClearValue(float InDepth, uint32 InStencil)
        : Type(ERTClearType::DepthStencil)
    {
        ClearValue.DepthStencil.Depth = InDepth;
		ClearValue.DepthStencil.Stencil = InStencil;
    }

    void SetColor(const Vector4& InColor)
    {
        Type = ERTClearType::Color;
        ClearValue.Color[0] = InColor.x;
        ClearValue.Color[1] = InColor.y;
        ClearValue.Color[2] = InColor.z;
        ClearValue.Color[3] = InColor.w;
    }

    void SetDepthStencil(float InDepth, uint8 InStencil)
    {
		Type = ERTClearType::DepthStencil;
        ClearValue.DepthStencil.Depth = InDepth;
        ClearValue.DepthStencil.Stencil = InStencil;
    }

	const float* GetCleraColor() const { return &ClearValue.Color[0]; }
	jDepthStencilClearType GetCleraDepthStencil() const { return ClearValue.DepthStencil; }
	float GetCleraDepth() const { return ClearValue.DepthStencil.Depth; }
	uint32 GetCleraStencil() const { return ClearValue.DepthStencil.Stencil; }
	jClearValueType GetClearValue() const { return ClearValue; }

	void ResetToNoneType() { Type = ERTClearType::None; }
	ERTClearType GetType() const { return Type; }

	size_t GetHash() const
	{
		size_t result = CityHash64((uint64)Type);
		return CityHash64WithSeed((const char*)&ClearValue.Color, sizeof(ClearValue.Color), result);		// safe : Color is 16 byte aligned
	}

private:
	ERTClearType Type = ERTClearType::None;
	jClearValueType ClearValue;
};

struct jBaseVertex
{
    Vector Pos = Vector::ZeroVector;
    Vector Normal = Vector::ZeroVector;
    Vector Tangent = Vector::ZeroVector;
    Vector Bitangent = Vector::ZeroVector;
    Vector2 TexCoord = Vector2::ZeroVector;
};

struct jPositionOnlyVertex
{
    Vector Pos = Vector::ZeroVector;
};
