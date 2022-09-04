#include "pch.h"
#include "jRHI_OpenGL.h"

#if USE_OPENGL
#include "FileLoader/jFile.h"
#include "Shader/jShader.h"

jRHI_OpenGL* g_rhi_gl = nullptr;

//////////////////////////////////////////////////////////////////////////
// Auto generate type conversion code
template <typename T>
using ConversionTypePair = std::pair<T, uint32>;

template <typename... T1>
constexpr auto GenerateConversionTypeArrayGL(T1... args)
{
	std::array<uint32, sizeof...(args)> newArray;
	auto AddElementFunc = [&newArray](const auto& arg)
	{
		newArray[(int32)arg.first] = arg.second;
	};

	int dummy[] = { 0, (AddElementFunc(args), 0)... };
	return newArray;
}

#define CONVERSION_TYPE_ELEMENT(x, y) ConversionTypePair<decltype(x)>(x, y)
#define GENERATE_STATIC_CONVERSION_ARRAY(...) {static auto TypeArrayGL = GenerateConversionTypeArrayGL(__VA_ARGS__); return TypeArrayGL[(int32)type];}

//////////////////////////////////////////////////////////////////////////
// OpenGL utility functions
FORCEINLINE uint32 GetPrimitiveType(EPrimitiveType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::POINTS, GL_POINTS),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::LINES, GL_LINES),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::LINES_ADJACENCY, GL_LINES_ADJACENCY),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::LINE_STRIP_ADJACENCY, GL_LINE_STRIP_ADJACENCY),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLES, GL_TRIANGLES),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLE_STRIP, GL_TRIANGLE_STRIP),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLES_ADJACENCY, GL_TRIANGLES_ADJACENCY),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLE_STRIP_ADJACENCY, GL_TRIANGLE_STRIP_ADJACENCY)
		);
}

FORCEINLINE uint32 GetOpenGLTextureType(ETextureType type, int32 sampleCount)
{
	uint32 result = 0;
	switch (type)
	{
	case ETextureType::TEXTURE_2D:
		result = (sampleCount > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		result = (sampleCount > 1) ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
		break;
	case ETextureType::TEXTURE_CUBE:
		if (sampleCount > 1)
		{
			check(0);			// 지원 안함
		}
		result = GL_TEXTURE_CUBE_MAP;
		break;
	default:
		check(0);
		break;
	}
	return result;
}

FORCEINLINE uint32 GetOpenGLFilterTargetType(ETextureFilterTarget type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFilterTarget::MINIFICATION, GL_TEXTURE_MIN_FILTER),
		CONVERSION_TYPE_ELEMENT(ETextureFilterTarget::MAGNIFICATION, GL_TEXTURE_MAG_FILTER)
	);
}

FORCEINLINE uint32 GetOpenGLTextureInternalFormat(ETextureFormat type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB8, GL_RGB8),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, GL_RGB16F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, GL_RGB32F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, GL_R11F_G11F_B10F),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, GL_RGBA8),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, GL_RGBA16F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, GL_RGBA32F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, GL_RGBA8I),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, GL_RGBA8UI),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, GL_BGRA),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::R8, GL_R8),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R16F, GL_R16F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, GL_R32F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, GL_R32UI),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG8, GL_RG8),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG16F, GL_RG16F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, GL_RG32F),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16, GL_DEPTH_COMPONENT16),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16_S8, GL_DEPTH_COMPONENT16),		// OpenGL 지원 안함, Depth16 포맷으로 변경
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24, GL_DEPTH_COMPONENT24),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, GL_DEPTH24_STENCIL8),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, GL_DEPTH_COMPONENT32F),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, GL_DEPTH32F_STENCIL8)
	);
}

FORCEINLINE uint32 GetOpenGLTextureFormat(ETextureFormat type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB8, GL_RGB),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, GL_RGB),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, GL_RGB),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, GL_RGB),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, GL_RGBA),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, GL_RGBA),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, GL_RGBA),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, GL_RGBA),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, GL_RGBA),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, GL_BGRA),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::R8, GL_RED),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R16F, GL_RED),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, GL_RED),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, GL_RED),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG8, GL_RG),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG16F, GL_RG),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, GL_RG),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16, GL_DEPTH_COMPONENT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16_S8, GL_DEPTH_COMPONENT),		// OpenGL 지원 안함, Depth16 포맷으로 변경
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24, GL_DEPTH_COMPONENT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, GL_DEPTH_STENCIL),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, GL_DEPTH_COMPONENT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, GL_DEPTH_STENCIL)
	);
}

FORCEINLINE uint32 GetOpenGLPixelFormat(ETextureFormat type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB8, GL_UNSIGNED_BYTE),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, GL_FLOAT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, GL_UNSIGNED_BYTE),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, GL_INT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, GL_UNSIGNED_INT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, GL_UNSIGNED_BYTE),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::R8, GL_UNSIGNED_BYTE),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R16F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, GL_UNSIGNED_INT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG8, GL_UNSIGNED_BYTE),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG16F, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, GL_FLOAT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16, GL_UNSIGNED_SHORT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16_S8, GL_UNSIGNED_SHORT),		// OpenGL 지원 안함, Depth16 포맷으로 변경
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24, GL_UNSIGNED_INT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, GL_UNSIGNED_INT_24_8),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, GL_FLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, GL_FLOAT_32_UNSIGNED_INT_24_8_REV)
	);
}

FORCEINLINE uint32 GetOpenGLTextureFilterType(ETextureFilter type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST, GL_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR, GL_LINEAR),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST_MIPMAP_LINEAR, GL_NEAREST_MIPMAP_LINEAR),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR)
	);
}

FORCEINLINE uint32 GetOpenGLTextureAddressMode(ETextureAddressMode type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::REPEAT, GL_REPEAT),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::MIRRORED_REPEAT, GL_MIRRORED_REPEAT),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER)
	);
}

FORCEINLINE uint32 GetOpenGLComparisonFunc(ECompareOp type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ECompareOp::NEVER, GL_NEVER),
		CONVERSION_TYPE_ELEMENT(ECompareOp::LESS, GL_LESS),
		CONVERSION_TYPE_ELEMENT(ECompareOp::EQUAL, GL_EQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::LEQUAL, GL_LEQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::GREATER, GL_GREATER),
		CONVERSION_TYPE_ELEMENT(ECompareOp::NOTEQUAL, GL_NOTEQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::GEQUAL, GL_GEQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::ALWAYS, GL_ALWAYS)
	);
}

FORCEINLINE uint32 GetOpenGLTextureComparisonMode(ETextureComparisonMode type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureComparisonMode::NONE, GL_NONE),
		CONVERSION_TYPE_ELEMENT(ETextureComparisonMode::COMPARE_REF_TO_TEXTURE, GL_COMPARE_REF_TO_TEXTURE)
	);
}

FORCEINLINE uint32 GetOpenGLImageTextureAccessType(EImageTextureAccessType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EImageTextureAccessType::READ_ONLY, GL_READ_ONLY),
		CONVERSION_TYPE_ELEMENT(EImageTextureAccessType::WRITE_ONLY, GL_WRITE_ONLY),
		CONVERSION_TYPE_ELEMENT(EImageTextureAccessType::READ_WRITE, GL_READ_WRITE)
	);
}

FORCEINLINE uint32 GetOpenGLPolygonMode(EPolygonMode type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EPolygonMode::POINT, GL_POINT),
		CONVERSION_TYPE_ELEMENT(EPolygonMode::LINE, GL_LINE),
		CONVERSION_TYPE_ELEMENT(EPolygonMode::FILL, GL_FILL)
	);
}

FORCEINLINE uint32 GetOpenGLFrontFaceType(EFrontFace type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EFrontFace::CW, GL_CW),
		CONVERSION_TYPE_ELEMENT(EFrontFace::CCW, GL_CCW)
	);
}

FORCEINLINE uint32 GetOpenGLFaceType(EFace type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EFace::BACK, GL_BACK),
		CONVERSION_TYPE_ELEMENT(EFace::FRONT, GL_FRONT),
		CONVERSION_TYPE_ELEMENT(EFace::FRONT_AND_BACK, GL_FRONT_AND_BACK)
	);
}

FORCEINLINE uint32 GetOpenGLCullMode(ECullMode type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ECullMode::BACK, GL_NONE),
		CONVERSION_TYPE_ELEMENT(ECullMode::BACK, GL_BACK),
		CONVERSION_TYPE_ELEMENT(ECullMode::FRONT, GL_FRONT),
		CONVERSION_TYPE_ELEMENT(ECullMode::FRONT_AND_BACK, GL_FRONT_AND_BACK)
	);
}

FORCEINLINE uint32 GetOpenGLDepthBufferType(EDepthBufferType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::NONE, 0),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH16, GL_DEPTH_ATTACHMENT),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH24, GL_DEPTH_ATTACHMENT),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH32, GL_DEPTH_ATTACHMENT),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT)
	);
}

FORCEINLINE uint32 GetOpenGLDepthBufferFormat(EDepthBufferType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::NONE, 0),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH16, GL_DEPTH_COMPONENT16),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH24, GL_DEPTH_COMPONENT24),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH32, GL_DEPTH_COMPONENT32),
		CONVERSION_TYPE_ELEMENT(EDepthBufferType::DEPTH24_STENCIL8, GL_DEPTH24_STENCIL8)
	);
}

FORCEINLINE uint32 GetOpenGLBlendSrc(EBlendFactor type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ZERO, GL_ZERO),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE, GL_ONE),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_COLOR, GL_SRC_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::DST_COLOR, GL_DST_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_DST_COLOR, GL_ONE_MINUS_DST_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_ALPHA, GL_SRC_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::DST_ALPHA, GL_DST_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::CONSTANT_COLOR, GL_CONSTANT_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::CONSTANT_ALPHA, GL_CONSTANT_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_ALPHA_SATURATE, GL_SRC_ALPHA_SATURATE)
	);
}

FORCEINLINE uint32 GetOpenGLBlendDest(EBlendFactor type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ZERO, GL_ZERO),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE, GL_ONE),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_COLOR, GL_SRC_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::DST_COLOR, GL_DST_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_DST_COLOR, GL_ONE_MINUS_DST_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_ALPHA, GL_SRC_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::DST_ALPHA, GL_DST_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::CONSTANT_COLOR, GL_CONSTANT_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::CONSTANT_ALPHA, GL_CONSTANT_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA)
	);
}

FORCEINLINE uint32 GetOpenGLBlendEquation(EBlendOp type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBlendOp::ADD, GL_FUNC_ADD),
		CONVERSION_TYPE_ELEMENT(EBlendOp::SUBTRACT, GL_FUNC_SUBTRACT),
		CONVERSION_TYPE_ELEMENT(EBlendOp::REVERSE_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT),
		CONVERSION_TYPE_ELEMENT(EBlendOp::MIN_VALUE, GL_MIN),
		CONVERSION_TYPE_ELEMENT(EBlendOp::MAX_VALUE, GL_MAX)
	);
}

FORCEINLINE uint32 GetOpenGLBufferType(EBufferType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBufferType::STATIC, GL_STATIC_DRAW),
		CONVERSION_TYPE_ELEMENT(EBufferType::DYNAMIC, GL_DYNAMIC_DRAW)
	);
}

FORCEINLINE uint32 GetOpenGLBufferElementType(EBufferElementType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBufferElementType::BYTE, GL_UNSIGNED_BYTE),
		CONVERSION_TYPE_ELEMENT(EBufferElementType::UINT16, GL_UNSIGNED_SHORT),
		CONVERSION_TYPE_ELEMENT(EBufferElementType::UINT32, GL_UNSIGNED_INT),
		CONVERSION_TYPE_ELEMENT(EBufferElementType::FLOAT, GL_FLOAT)
	);
}

FORCEINLINE uint32 GetOpenGLDrawBufferType(EDrawBufferType type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT0),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT1),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT2),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT3),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT4),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT5),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT6),
		CONVERSION_TYPE_ELEMENT(EDrawBufferType::COLOR_ATTACHMENT7, GL_COLOR_ATTACHMENT7)
	);
}

FORCEINLINE uint32 GetStencilOp(EStencilOp type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EStencilOp::KEEP, GL_KEEP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::ZERO, GL_ZERO),
		CONVERSION_TYPE_ELEMENT(EStencilOp::REPLACE, GL_REPLACE),
		CONVERSION_TYPE_ELEMENT(EStencilOp::INCR, GL_INCR),
		CONVERSION_TYPE_ELEMENT(EStencilOp::INCR_WRAP, GL_INCR_WRAP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::DECR, GL_DECR),
		CONVERSION_TYPE_ELEMENT(EStencilOp::DECR_WRAP, GL_DECR_WRAP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::INVERT, GL_INVERT)
	);
}

FORCEINLINE uint32 GetPolygonOffsetPrimitiveType(EPolygonMode type)
{
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EPolygonMode::POINT, GL_POLYGON_OFFSET_POINT),
		CONVERSION_TYPE_ELEMENT(EPolygonMode::LINE, GL_POLYGON_OFFSET_LINE),
		CONVERSION_TYPE_ELEMENT(EPolygonMode::FILL, GL_POLYGON_OFFSET_FILL)
	);
}

uint32 GetOpenGLClearBufferBit(ERenderBufferType typeBit)
{
	uint32 clearBufferBit = 0;
	if (!!(ERenderBufferType::COLOR & typeBit))
		clearBufferBit |= GL_COLOR;
	else if (!!(ERenderBufferType::DEPTH & typeBit) && !!(ERenderBufferType::STENCIL & typeBit))
		clearBufferBit |= GL_DEPTH_STENCIL;
	else if (!!(ERenderBufferType::DEPTH & typeBit))
		clearBufferBit |= GL_DEPTH;
	else if (!!(ERenderBufferType::STENCIL & typeBit))
		clearBufferBit |= GL_STENCIL;
	return clearBufferBit;
}

//////////////////////////////////////////////////////////////////////////
// jRHI_OpenGL
jRHI_OpenGL::jRHI_OpenGL()
{
	g_rhi_gl = this;
}

jRHI_OpenGL::~jRHI_OpenGL()
{
}

bool jRHI_OpenGL::InitRHI()
{
    // glfw: initialize and configure
// ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if (DEBUG_OUTPUT_ON == 1)
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif // DEBUG_OUTPUT_ON

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

    // glfw window creation
    // --------------------
	window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "OpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

	glfwMakeContextCurrent(window);

    // glad: load all OpenGL function pointers
	// ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return false;
    }

	return true;
}

jSamplerStateInfo* jRHI_OpenGL::CreateSamplerState(const jSamplerStateInfo& info) const
{
	auto samplerState = new jSamplerStateInfo_OpenGL(info);
	glGenSamplers(1, &samplerState->SamplerId);
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_MIN_FILTER, GetOpenGLTextureFilterType(info.Minification));
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_MAG_FILTER, GetOpenGLTextureFilterType(info.Magnification));
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_WRAP_S, GetOpenGLTextureAddressMode(info.AddressU));
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_WRAP_T, GetOpenGLTextureAddressMode(info.AddressV));
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_WRAP_R, GetOpenGLTextureAddressMode(info.AddressW));
	glSamplerParameterf(samplerState->SamplerId, GL_TEXTURE_LOD_BIAS, info.MipLODBias);
	glSamplerParameterf(samplerState->SamplerId, GL_TEXTURE_MAX_ANISOTROPY, info.MaxAnisotropy);
	glSamplerParameterfv(samplerState->SamplerId, GL_TEXTURE_BORDER_COLOR, info.BorderColor.v);
	glSamplerParameterf(samplerState->SamplerId, GL_TEXTURE_MIN_LOD, info.MinLOD);
	glSamplerParameterf(samplerState->SamplerId, GL_TEXTURE_MAX_LOD, info.MaxLOD);
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_COMPARE_MODE, GetOpenGLTextureComparisonMode(info.TextureComparisonMode));
	glSamplerParameteri(samplerState->SamplerId, GL_TEXTURE_COMPARE_FUNC, GetOpenGLComparisonFunc(info.ComparisonFunc));

	return samplerState;
}

void jRHI_OpenGL::ReleaseSamplerState(jSamplerStateInfo* samplerState) const
{
	auto samplerState_gl = static_cast<jSamplerStateInfo_OpenGL*>(samplerState);
	glDeleteSamplers(1, &samplerState_gl->SamplerId);
	delete samplerState_gl;
}

void jRHI_OpenGL::BindSamplerState(int32 index, const jSamplerStateInfo* samplerState) const
{
	if (samplerState)
	{
		auto samplerState_gl = static_cast<const jSamplerStateInfo_OpenGL*>(samplerState);
		glBindSampler(index, samplerState_gl->SamplerId);
	}
	else
	{
		glBindSampler(index, 0);
	}
}

jVertexBuffer* jRHI_OpenGL::CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	jVertexBuffer_OpenGL* vertexBuffer = new jVertexBuffer_OpenGL();
	vertexBuffer->VertexStreamData = streamData;
	glGenVertexArrays(1, &vertexBuffer->VAO);
	glBindVertexArray(vertexBuffer->VAO);
	std::list<uint32> buffers;
	for (const auto& iter : streamData->Params)
	{
		if (iter->Stride <= 0)
			continue;

		const uint32 bufferType = GetOpenGLBufferType(iter->BufferType);

		jVertexStream_OpenGL stream;
		glGenBuffers(1, &stream.BufferID);
		glBindBuffer(GL_ARRAY_BUFFER, stream.BufferID);
		glBufferData(GL_ARRAY_BUFFER, iter->GetBufferSize(), iter->GetBufferData(), bufferType);
		stream.Name = iter->Name;
		stream.Count = iter->Stride / iter->ElementTypeSize;
		stream.BufferType = EBufferType::STATIC;
		stream.ElementType = iter->ElementType;
		stream.Stride = iter->Stride;
		stream.Offset = 0;
		stream.InstanceDivisor = iter->InstanceDivisor;

		vertexBuffer->Streams.emplace_back(stream);
	}

	return vertexBuffer;
}

jIndexBuffer* jRHI_OpenGL::CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	const uint32 bufferType = GetOpenGLBufferType(streamData->Param->BufferType);

	jIndexBuffer_OpenGL* indexBuffer = new jIndexBuffer_OpenGL();
	indexBuffer->IndexStreamData = streamData;
	std::list<uint32> buffers;
	glGenBuffers(1, &indexBuffer->BufferID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer->BufferID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, streamData->Param->GetBufferSize(), streamData->Param->GetBufferData(), bufferType);

	return indexBuffer;
}

void jRHI_OpenGL::BindVertexBuffer(const jVertexBuffer* vb, const jShader* shader) const
{
	auto vb_gl = static_cast<const jVertexBuffer_OpenGL*>(vb);
	glBindVertexArray(vb_gl->VAO);
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	for (const auto& iter : vb_gl->Streams)
	{
		auto loc = shader_gl->TryGetAttributeLocation(iter.Name);
		if (loc != -1)
		{
			const uint32 elementType = GetOpenGLBufferElementType(iter.ElementType);

			glBindBuffer(GL_ARRAY_BUFFER, iter.BufferID);

			if (iter.Count <= 4)
			{
				glVertexAttribPointer(loc, iter.Count, elementType, iter.Normalized, iter.Stride, reinterpret_cast<const void*>(iter.Offset));
				glEnableVertexAttribArray(loc);
				glVertexAttribDivisor(loc, iter.InstanceDivisor);

			}
			else
			{
				const int cnt = iter.Count / 4;
				int32 remainSize = iter.Count;
				const int32 step = sizeof(float) * 4;
				for (int32 i = 0; i < cnt; ++i, remainSize -= 4)
				{
					glVertexAttribPointer(loc + i, Min(4, remainSize), elementType, iter.Normalized, iter.Stride, reinterpret_cast<const void*>(iter.Offset + step * i));
					glEnableVertexAttribArray(loc + i);
					glVertexAttribDivisor(loc + i, iter.InstanceDivisor);
				}
			}
		}
	}
}

void jRHI_OpenGL::BindIndexBuffer(const jIndexBuffer* ib, const jShader* shader) const
{
	auto ib_gl = static_cast<const jIndexBuffer_OpenGL*>(ib);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib_gl->BufferID);
}

void jRHI_OpenGL::DrawArrays(EPrimitiveType type, int vertStartIndex, int vertCount) const
{
	glDrawArrays(GetPrimitiveType(type), vertStartIndex, vertCount);
}

void jRHI_OpenGL::DrawArraysInstanced(EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const
{
	glDrawArraysInstanced(GetPrimitiveType(type), vertStartIndex, vertCount, instanceCount);
}

void jRHI_OpenGL::DrawElements(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const
{
	union jOffset
	{
		int64 offset;
		void* offset_pointer;
	} u_offset;
	u_offset.offset = startIndex * elementSize;
	const auto elementType = (elementSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;	
	glDrawElements(GetPrimitiveType(type), count, elementType, u_offset.offset_pointer);
}

void jRHI_OpenGL::DrawElementsInstanced(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const
{
	union jOffset
	{
		int64 offset;
		void* offset_pointer;
	} u_offset;
	u_offset.offset = startIndex * elementSize;
	const auto elementType = (elementSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
	glDrawElementsInstanced(GetPrimitiveType(type), count, elementType, u_offset.offset_pointer, instanceCount);
}

void jRHI_OpenGL::DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const
{
	union jOffset
	{
		int64 offset;
		void* offset_pointer;
	} u_offset;
	u_offset.offset = startIndex * elementSize;
	const auto elementType = (elementSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
	glDrawElementsBaseVertex(GetPrimitiveType(type), count, elementType, u_offset.offset_pointer, baseVertexIndex);
}

void jRHI_OpenGL::DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const
{
	union jOffset
	{
		int64 offset;
		void* offset_pointer;
	} u_offset;
	u_offset.offset = startIndex * elementSize;
	const auto elementType = (elementSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
	glDrawElementsInstancedBaseVertex(GetPrimitiveType(type), count, elementType, u_offset.offset_pointer, instanceCount, baseVertexIndex);
}

void jRHI_OpenGL::DispatchCompute(uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const
{
	glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
}

void jRHI_OpenGL::EnableDepthBias(bool enable, EPolygonMode polygonMode) const
{
	if (enable)
		glEnable(GetPolygonOffsetPrimitiveType(polygonMode));
	else
		glDisable(GetPolygonOffsetPrimitiveType(polygonMode));
}

void jRHI_OpenGL::SetDepthBias(float constant, float slope) const
{
	glPolygonOffset(slope, constant);
}

void jRHI_OpenGL::EnableSRGB(bool enable) const
{
	if (enable)
		glEnable(GL_FRAMEBUFFER_SRGB);
	else
		glDisable(GL_FRAMEBUFFER_SRGB);
}

IShaderStorageBufferObject* jRHI_OpenGL::CreateShaderStorageBufferObject(const char* blockname) const
{
	auto ssbo = new jShaderStorageBufferObject_OpenGL(blockname);
	ssbo->Init();
	return ssbo;
}

IAtomicCounterBuffer* jRHI_OpenGL::CreateAtomicCounterBuffer(const char* name, int32 bindingPoint) const
{
	auto acbo = new jAtomicCounterBuffer_OpenGL(name, bindingPoint);
	acbo->Init();
	return acbo;
}

ITransformFeedbackBuffer* jRHI_OpenGL::CreateTransformFeedbackBuffer(const char* name) const
{
	auto tfbo = new jTransformFeedbackBuffer_OpenGL(name);
	tfbo->Init();
	return tfbo;
}

void jRHI_OpenGL::SetViewport(int32 x, int32 y, int32 width, int32 height) const
{
	glViewport(x, y, width, height);
}

void jRHI_OpenGL::SetViewport(const jViewport& viewport) const
{
	glViewport(static_cast<int32>(viewport.X), static_cast<int32>(viewport.Y), static_cast<int32>(viewport.Width), static_cast<int32>(viewport.Height));
}

void jRHI_OpenGL::SetViewportIndexed(int32 index, float x, float y, float width, float height) const
{
	glViewportIndexedf(index, x, y, width, height);
}

void jRHI_OpenGL::SetViewportIndexed(int32 index, const jViewport& viewport) const
{
	glViewportIndexedfv(index, reinterpret_cast<const float*>(&viewport));
}

void jRHI_OpenGL::SetViewportIndexedArray(int32 startIndex, int32 count, const jViewport* viewports) const
{
	glViewportArrayv(startIndex, count, reinterpret_cast<const float*>(viewports));
}

void jRHI_OpenGL::EnableDepthClip(bool enable) const
{
	// Enabling GL_DEPTH_CLAMP means that it disabling depth clip.
	if (enable)
		glDisable(GL_DEPTH_CLAMP);
	else
		glEnable(GL_DEPTH_CLAMP);
}

void jRHI_OpenGL::BeginDebugEvent(const char* name) const
{
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<int32>(strlen(name)), name);
}

void jRHI_OpenGL::EndDebugEvent() const
{
	glPopDebugGroup();
}

void jRHI_OpenGL::GenerateMips(const jTexture* texture) const
{
	const auto texture_gl = static_cast<const jTexture_OpenGL*>(texture);
	glActiveTexture(GL_TEXTURE0);
	const auto textureType = GetOpenGLTextureType(texture_gl->Type, texture_gl->SampleCount);
	glBindTexture(textureType, texture_gl->TextureID);
	glGenerateMipmap(textureType);
	glBindTexture(textureType, 0);
}

jQuery* jRHI_OpenGL::CreateQueryTime() const
{
	auto queryTime = new jQueryTime_OpenGL();
	glGenQueries(2, &queryTime->QueryId[0]);
	return queryTime;
}

void jRHI_OpenGL::ReleaseQueryTime(jQuery* queryTime) const 
{
	auto queryTime_gl = static_cast<jQueryTime_OpenGL*>(queryTime);
	glDeleteQueries(2, &queryTime_gl->QueryId[0]);
	queryTime_gl->QueryId[0] = 0;
	queryTime_gl->QueryId[1] = 0;
	delete queryTime_gl;
}

void jRHI_OpenGL::QueryTimeStampStart(const jQuery* queryTimeStamp) const
{
    const auto queryTimeStamp_gl = static_cast<const jQueryTime_OpenGL*>(queryTimeStamp);
    JASSERT(queryTimeStamp_gl->QueryId[0] > 0);
    glQueryCounter(queryTimeStamp_gl->QueryId[0], GL_TIMESTAMP);
}

void jRHI_OpenGL::QueryTimeStampEnd(const jQuery* queryTimeStamp) const
{
    const auto queryTimeStamp_gl = static_cast<const jQueryTime_OpenGL*>(queryTimeStamp);
    JASSERT(queryTimeStamp_gl->QueryId[1] > 0);
    glQueryCounter(queryTimeStamp_gl->QueryId[1], GL_TIMESTAMP);
}

bool jRHI_OpenGL::IsQueryTimeStampResult(const jQuery* queryTimeStamp, bool isWaitUntilAvailable) const
{
	auto queryTimeStamp_gl = static_cast<const jQueryTime_OpenGL*>(queryTimeStamp);
	JASSERT(queryTimeStamp_gl->QueryId[0] > 0);
	JASSERT(queryTimeStamp_gl->QueryId[1] > 0);

    uint32 availableStart = 0;
    uint32 availableEnd = 0;
	if (isWaitUntilAvailable)
	{
		while (!availableStart)
			glGetQueryObjectuiv(queryTimeStamp_gl->QueryId[0], GL_QUERY_RESULT_AVAILABLE, &availableStart);

        while (!availableEnd)
            glGetQueryObjectuiv(queryTimeStamp_gl->QueryId[1], GL_QUERY_RESULT_AVAILABLE, &availableEnd);
	}
	else
	{
		glGetQueryObjectuiv(queryTimeStamp_gl->QueryId[0], GL_QUERY_RESULT_AVAILABLE, &availableStart);
		glGetQueryObjectuiv(queryTimeStamp_gl->QueryId[1], GL_QUERY_RESULT_AVAILABLE, &availableEnd);
	}
	return (availableStart && availableEnd);
}

void jRHI_OpenGL::GetQueryTimeStampResult(jQuery* queryTimeStamp) const
{
	auto queryTimeStamp_gl = static_cast<jQueryTime_OpenGL*>(queryTimeStamp);
	JASSERT(queryTimeStamp_gl->QueryId[0] > 0);
	JASSERT(queryTimeStamp_gl->QueryId[1] > 0);
	glGetQueryObjectui64v(queryTimeStamp_gl->QueryId[0], GL_QUERY_RESULT, &queryTimeStamp_gl->TimeStampStartEnd[0]);
	glGetQueryObjectui64v(queryTimeStamp_gl->QueryId[1], GL_QUERY_RESULT, &queryTimeStamp_gl->TimeStampStartEnd[1]);
}

void jRHI_OpenGL::SetPolygonMode(EFace face, EPolygonMode mode)
{
	glPolygonMode(GetOpenGLFaceType(face), GetOpenGLPolygonMode(mode));
}

jQueryPrimitiveGenerated* jRHI_OpenGL::CreateQueryPrimitiveGenerated() const
{
	auto query = new jQueryPrimitiveGenerated_OpenGL();
	glGenQueries(1, &query->QueryId);
	return query;
}

void jRHI_OpenGL::ReleaseQueryPrimitiveGenerated(jQueryPrimitiveGenerated* query) const
{
	auto query_gl = static_cast<jQueryPrimitiveGenerated_OpenGL*>(query);
	glDeleteQueries(1, &query_gl->QueryId);
	query_gl->QueryId = 0;
	delete query_gl;
}

static int32 DepthCheck_BeginQueryPrimitiveGenerated = 0;

void jRHI_OpenGL::BeginQueryPrimitiveGenerated(const jQueryPrimitiveGenerated* query) const
{
	verify(1 == ++DepthCheck_BeginQueryPrimitiveGenerated);

	auto query_gl = static_cast<const jQueryPrimitiveGenerated_OpenGL*>(query);
	glBeginQuery(GL_PRIMITIVES_GENERATED, query_gl->QueryId);
}

void jRHI_OpenGL::EndQueryPrimitiveGenerated() const 
{
	verify(0 == --DepthCheck_BeginQueryPrimitiveGenerated);
	glEndQuery(GL_PRIMITIVES_GENERATED);
}

void jRHI_OpenGL::GetQueryPrimitiveGeneratedResult(jQueryPrimitiveGenerated* query) const
{
	auto query_gl = static_cast<jQueryPrimitiveGenerated_OpenGL*>(query);
	glGetQueryObjectui64v(query_gl->QueryId, GL_QUERY_RESULT, &query_gl->NumOfGeneratedPrimitives);
}

void jRHI_OpenGL::EnableRasterizerDiscard(bool enable) const
{
	if (enable)
		glEnable(GL_RASTERIZER_DISCARD);
	else
		glDisable(GL_RASTERIZER_DISCARD);
}

void jRHI_OpenGL::SetTextureMipmapLevelLimit(ETextureType type, int32 sampleCount, int32 baseLevel, int32 maxLevel) const
{
	const uint32 textureType = GetOpenGLTextureType(type, sampleCount);
	glTexParameteri(textureType, GL_TEXTURE_BASE_LEVEL, baseLevel);
	glTexParameteri(textureType, GL_TEXTURE_MAX_LEVEL, maxLevel);
}

void jRHI_OpenGL::EnableMultisample(bool enable) const
{
	if (enable)
		glEnable(GL_MULTISAMPLE);
	else
		glDisable(GL_MULTISAMPLE);
}

void jRHI_OpenGL::SetCubeMapSeamless(bool enable) const
{
	if (enable)
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	else
		glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}


void jRHI_OpenGL::SetLineWidth(float width) const
{
	glLineWidth(width);
}

void jRHI_OpenGL::Flush() const
{
	glFlush();
}

void jRHI_OpenGL::Finish() const
{
	glFinish();
}

std::shared_ptr<jRenderTarget> jRHI_OpenGL::CreateRenderTarget(const jRenderTargetInfo& info) const
{
	const uint32 textureFormat = GetOpenGLTextureFormat(info.Format);
	const uint32 textureInternalFormat = GetOpenGLTextureInternalFormat(info.Format);
	const uint32 formatType = GetOpenGLPixelFormat(info.Format);
	const uint32 textureType = GetOpenGLTextureType(info.Type, info.SampleCount);

	const bool hasDepthAttachment = IsDepthFormat(info.Format);

	uint32 error = 0;
	uint32 tbo = 0;
	if (info.Type == ETextureType::TEXTURE_2D)
	{
		glGenTextures(1, &tbo);
		glBindTexture(textureType, tbo);
		error = glGetError();

		if (info.SampleCount > 1)
			glTexImage2DMultisample(textureType, info.SampleCount, textureInternalFormat, info.Width, info.Height, true);
		else
			glTexImage2D(textureType, 0, textureInternalFormat, info.Width, info.Height, 0, textureFormat, formatType, nullptr);
		
	}
	else if (info.Type == ETextureType::TEXTURE_2D_ARRAY)
	{
		glGenTextures(1, &tbo);
		glBindTexture(textureType, tbo);
		if (info.SampleCount > 1)
			glTexImage3DMultisample(textureType, info.SampleCount, textureInternalFormat, info.Width, info.Height, info.LayerCount, true);
		else
			glTexImage3D(textureType, 0, textureInternalFormat, info.Width, info.Height, info.LayerCount, 0, textureFormat, formatType, nullptr);
	}
	else if (info.Type == ETextureType::TEXTURE_CUBE)
	{
		glGenTextures(1, &tbo);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tbo);

		for (int32 i = 0; i < 6; ++i)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, textureInternalFormat, info.Width, info.Height, 0, textureFormat, formatType, nullptr);
	}
	else
	{
		check(0);
	}
	
	auto t = glGetError();
	check(!t);

	if (info.IsGenerateMipmap)
		glGenerateMipmap(textureType);

	auto tex_gl = jTexture_OpenGL();
	tex_gl->Type = info.Type;
	tex_gl->Format = info.Format;
	tex_gl->Width = info.Width;
	tex_gl->Height = info.Height;
	tex_gl->LayerCount = info.LayerCount;
	tex_gl->SampleCount = info.SampleCount;
	tex_gl->TextureID = tbo;
	tex_gl->MipLevel = info.IsGenerateMipmap ? jTexture::GetMipLevels(info.Width, info.Height) : 1;

	auto rt = new jRenderTarget();
	rt->Info = info;
	rt->TexturePtr = std::shared_ptr<jTexture>(tex_gl);

	return std::shared_ptr<jRenderTarget>(rt);
}

void jRHI_OpenGL::EnableWireframe(bool enable) const
{
	if (enable)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void jRHI_OpenGL::SetImageTexture(int32 index, const jTexture* texture, EImageTextureAccessType type) const
{
	const auto texture_gl = static_cast<const jTexture_OpenGL*>(texture);
	const auto textureType = GetOpenGLTextureFormat(texture_gl->Format);
	const auto accesstype_gl = GetOpenGLImageTextureAccessType(type);
	glBindImageTexture(index, texture_gl->TextureID, 0, GL_FALSE, 0, accesstype_gl, textureType);
}

int32 jRHI_OpenGL::GetUniformLocation(uint32 InProgram, const char* name) const
{
	return glGetUniformLocation(InProgram, name);
}

int32 jRHI_OpenGL::GetAttributeLocation(uint32 InProgram, const char* name) const
{
	return glGetAttribLocation(InProgram, name);
}

void jRHI_OpenGL::SetClear(ERenderBufferType typeBit) const
{
	uint32 clearBufferBit = 0;
	if (!!(ERenderBufferType::COLOR & typeBit))
		clearBufferBit |= GL_COLOR_BUFFER_BIT;
	if (!!(ERenderBufferType::DEPTH & typeBit))
		clearBufferBit |= GL_DEPTH_BUFFER_BIT;
	if (!!(ERenderBufferType::STENCIL & typeBit))
		clearBufferBit |= GL_STENCIL_BUFFER_BIT;

	glClear(clearBufferBit);
}

void jRHI_OpenGL::SetClearColor(float r, float g, float b, float a) const
{
	glClearColor(r, g, b, a);
}

void jRHI_OpenGL::SetClearColor(Vector4 rgba) const
{
	glClearColor(rgba.x, rgba.y, rgba.z, rgba.w);
}

void jRHI_OpenGL::SetClearBuffer(ERenderBufferType typeBit, const float* value, int32 bufferIndex) const
{
	const uint32 clearBufferBit = GetOpenGLClearBufferBit(typeBit);
	glClearBufferfv(clearBufferBit, bufferIndex, value);
}

void jRHI_OpenGL::SetClearBuffer(ERenderBufferType typeBit, const int32* value, int32 bufferIndex) const
{
	const uint32 clearBufferBit = GetOpenGLClearBufferBit(typeBit);
	glClearBufferiv(clearBufferBit, bufferIndex, value);
}

void jRHI_OpenGL::SetShader(const jShader* shader) const
{
	JASSERT(shader);
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	glUseProgram(shader_gl->program);
}

jShader* jRHI_OpenGL::CreateShader(const jShaderInfo& shaderInfo) const
{
	auto shader = new jShader_OpenGL();
	CreateShader(shader, shaderInfo);
	return shader;
}

bool jRHI_OpenGL::CreateShader(jShader* OutShader, const jShaderInfo& shaderInfo) const
{
	JASSERT(OutShader);

	// https://stackoverflow.com/questions/5878775/how-to-find-and-replace-string
	auto ReplaceString = [](std::string subject, const std::string& search, const std::string& replace) {
		size_t pos = 0;
		while ((pos = subject.find(search, pos)) != std::string::npos) {
			subject.replace(pos, search.length(), replace);
			pos += replace.length();
		}
		return subject;
	};

	static bool initializedCommonShader = false;
	static std::string commonShader;
	static std::string shadowShader;
	if (!initializedCommonShader)
	{
		initializedCommonShader = true;

		jFile commonFile;
		commonFile.OpenFile("Shaders/common.glsl", FileType::TEXT, ReadWriteType::READ);
		commonFile.ReadFileToBuffer(false);
		commonShader = commonFile.GetBuffer();

		jFile shadowFile;
		shadowFile.OpenFile("Shaders/shadow.glsl", FileType::TEXT, ReadWriteType::READ);
		shadowFile.ReadFileToBuffer(false);
		shadowShader = shadowFile.GetBuffer();
	}

	uint32 program = 0;
	if (shaderInfo.cs.GetStringLength())
	{
		jFile csFile;
		csFile.OpenFile(shaderInfo.cs.ToStr(), FileType::TEXT, ReadWriteType::READ);
		csFile.ReadFileToBuffer(false);
		std::string csText(csFile.GetBuffer());

		auto compute_shader = glCreateShader(GL_COMPUTE_SHADER);
		const char* vsPtr = csText.c_str();
		glShaderSource(compute_shader, 1, &vsPtr, nullptr);
		glCompileShader(compute_shader);
		int isValid = 0;
		glGetShaderiv(compute_shader, GL_COMPILE_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetShaderiv(compute_shader, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::vector<char> errorLog(maxLength + 1, 0);
				glGetShaderInfoLog(compute_shader, maxLength, &maxLength, &errorLog[0]);
				JMESSAGE(&errorLog[0]);
				glDeleteShader(compute_shader);
			}
			return false;
		}

		program = glCreateProgram();
		glAttachShader(program, compute_shader);
		glLinkProgram(program);
		isValid = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::vector<char> errorLog(maxLength + 1, 0);
				glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
				JMESSAGE(&errorLog[0]);
				glDeleteShader(compute_shader);
				glDeleteProgram(program);
			}
			return false;
		}

		glDeleteShader(compute_shader);
	}
	else
	{
		jFile vsFile;
		vsFile.OpenFile(shaderInfo.vs.ToStr(), FileType::TEXT, ReadWriteType::READ);
		vsFile.ReadFileToBuffer(false);
		std::string vsText(vsFile.IsBufferEmpty() ? "" : vsFile.GetBuffer());
		vsText = ReplaceString(vsText, "#include \"shadow.glsl\"", shadowShader);
		vsText = ReplaceString(vsText, "#include \"common.glsl\"", commonShader);
		vsText = ReplaceString(vsText, "#preprocessor", shaderInfo.vsPreProcessor.ToStr());
		vsFile.CloseFile();

		jFile gsFile;
		gsFile.OpenFile(shaderInfo.gs.ToStr(), FileType::TEXT, ReadWriteType::READ);
		gsFile.ReadFileToBuffer(false);
		std::string gsText(gsFile.IsBufferEmpty() ? "" : gsFile.GetBuffer());
		gsText = ReplaceString(gsText, "#include \"shadow.glsl\"", shadowShader);
		gsText = ReplaceString(gsText, "#include \"common.glsl\"", commonShader);
		gsText = ReplaceString(gsText, "#preprocessor", shaderInfo.gsPreProcessor.ToStr());
		gsFile.CloseFile();

		jFile fsFile;
		fsFile.OpenFile(shaderInfo.fs.ToStr(), FileType::TEXT, ReadWriteType::READ);
		fsFile.ReadFileToBuffer(false);
		std::string fsText(fsFile.IsBufferEmpty() ? "" : fsFile.GetBuffer());
		fsText = ReplaceString(fsText, "#include \"shadow.glsl\"", shadowShader);
		fsText = ReplaceString(fsText, "#include \"common.glsl\"", commonShader);
		fsText = ReplaceString(fsText, "#preprocessor", shaderInfo.fsPreProcessor.ToStr());
		fsFile.CloseFile();

		auto vs = glCreateShader(GL_VERTEX_SHADER);
		const char* vsPtr = vsText.c_str();
		glShaderSource(vs, 1, &vsPtr, nullptr);
		glCompileShader(vs);

		auto checkShaderValid = [](uint32 shaderId, const std::string& filename)
		{
			int isValid = 0;
			glGetShaderiv(shaderId, GL_COMPILE_STATUS, &isValid);
			if (!isValid)
			{
				int maxLength = 0;
				glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &maxLength);

				if (maxLength > 0)
				{
					std::string errorLog(maxLength, 0);
					glGetShaderInfoLog(shaderId, maxLength, &maxLength, &errorLog[0]);
					if (maxLength > 0)
						errorLog.resize(maxLength - 1);	// remove null character to concatenate
					JMESSAGE(std::string(errorLog + "(" + filename + ")").c_str());
				}
				glDeleteShader(shaderId);
				return false;
			}
			return true;
		};
		if (!checkShaderValid(vs, shaderInfo.vs.ToStr()))
			return false;

		uint32 gs = 0;
		if (!gsText.empty())
		{
			gs = glCreateShader(GL_GEOMETRY_SHADER);
			const char* gsPtr = gsText.c_str();
			glShaderSource(gs, 1, &gsPtr, nullptr);
			glCompileShader(gs);

			if (!checkShaderValid(gs, shaderInfo.gs.ToStr()))
			{
				glDeleteShader(vs);
				return false;
			}
		}

		auto fs = glCreateShader(GL_FRAGMENT_SHADER);
		const char* fsPtr = fsText.c_str();
		int32 fragment_shader_string_length = static_cast<int32>(fsText.length());
		glShaderSource(fs, 1, &fsPtr, &fragment_shader_string_length);
		glCompileShader(fs);

		if (!checkShaderValid(fs, shaderInfo.fs.ToStr()))
		{
			glDeleteShader(vs);
			if (gs)
				glDeleteShader(gs);
			return false;
		}

		program = glCreateProgram();
		glAttachShader(program, vs);
		if (gs)
			glAttachShader(program, gs);
		glAttachShader(program, fs);
		glLinkProgram(program);

		int isValid = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::string errorLog(maxLength, 0);
				glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
				if (maxLength > 0)
					errorLog.resize(maxLength - 1);	// remove null character to concatenate
				JMESSAGE(std::string("Shader name : " + std::string(shaderInfo.name.ToStr()) + "\n-----\n" + errorLog).c_str());
			}
			glDeleteShader(vs);
			glDeleteShader(fs);
			if (gs)
				glDeleteShader(gs);
			glDeleteShader(program);
			return false;
		}

		glValidateProgram(program);
		isValid = 0;
		glGetProgramiv(program, GL_VALIDATE_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::string errorLog(maxLength, 0);
				glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
				if (maxLength > 0)
					errorLog.resize(maxLength - 1);	// remove null character to concatenate
				JMESSAGE(std::string("Shader name : " + std::string(shaderInfo.name.ToStr()) + "\n-----\n" + errorLog).c_str());
			}
			glDeleteShader(vs);
			glDeleteShader(fs);
			if (gs)
				glDeleteShader(gs);
			glDeleteShader(program);
			return false;
		}
		glDeleteShader(vs);
		if (gs)
			glDeleteShader(gs);
		glDeleteShader(fs);
	}

	auto shader_gl = static_cast<jShader_OpenGL*>(OutShader);
	if (shader_gl->program)
		glDeleteProgram(shader_gl->program);
	shader_gl->program = program;
	shader_gl->ShaderInfo = shaderInfo;
	shader_gl->ClearAllLocations();			// 기존 location 정보들을 초기화

	return true;
}

void jRHI_OpenGL::ReleaseShader(jShader* shader) const
{
	JASSERT(shader);
	auto shader_gl = static_cast<jShader_OpenGL*>(shader);
	if (shader_gl->program)
		glDeleteProgram(shader_gl->program);
	shader_gl->program = 0;
	delete shader_gl;
}

jTexture* jRHI_OpenGL::CreateNullTexture() const
{
	auto texture = new jTexture_OpenGL();
	texture->sRGB = false;
	texture->Format = ETextureFormat::RGB8;
	texture->Type = ETextureType::TEXTURE_CUBE;
	texture->Width = 2;
	texture->Height = 2;

	glGenTextures(1, &texture->TextureID);
	glBindTexture(GL_TEXTURE_2D, texture->TextureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return texture;
}

jTexture* jRHI_OpenGL::CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
	, ETextureFormat textureFormat, bool createMipmap) const
{
	const uint32 internalFormat = GetOpenGLTextureFormat(textureFormat);
	const uint32 simpleFormat = GetOpenGLTextureInternalFormat(textureFormat);
	const uint32 formatType = GetOpenGLPixelFormat(textureFormat);

	auto texture = new jTexture_OpenGL();
	texture->sRGB = sRGB;
	texture->Format = textureFormat;
	texture->Type = ETextureType::TEXTURE_2D;
	texture->Width = width;
	texture->Height = height;
	texture->MipLevel = createMipmap ? jTexture::GetMipLevels(width, height) : 1;
	glGenTextures(1, &texture->TextureID);
	glBindTexture(GL_TEXTURE_2D, texture->TextureID);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, simpleFormat, formatType, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	if (createMipmap)
		glGenerateMipmap(GL_TEXTURE_2D);
	return texture;
}

jTexture* jRHI_OpenGL::CreateCubeTextureFromData(std::vector<void*> faces, int32 width, int32 height, bool sRGB
	, ETextureFormat textureFormat, bool createMipmap) const
{
	const uint32 internalFormat = GetOpenGLTextureFormat(textureFormat);
	const uint32 simpleFormat = GetOpenGLTextureInternalFormat(textureFormat);
	const uint32 formatType = GetOpenGLPixelFormat(textureFormat);

	auto texture = new jTexture_OpenGL();
	texture->sRGB = sRGB;
	texture->Format = textureFormat;
	texture->Type = ETextureType::TEXTURE_CUBE;
	texture->Width = width;
	texture->Height = height;

	glGenTextures(1, &texture->TextureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, texture->TextureID);

	for (uint32 i = 0; i < faces.size(); i++)
	{
		unsigned char* data = (unsigned char* )faces[i];
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
			0, internalFormat, width, height, 0, simpleFormat, formatType, data);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	return texture;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Matrix& InData, const jShader* InShader) const
{
	JASSERT(name);

	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;

#if MATRIX_ROW_MAJOR_ORDER
	glUniformMatrix4fv(loc, 1, true, &InData.mm[0]);
#else
	glUniformMatrix4fv(loc, 1, false, &InData.mm[0]);
#endif
	
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const int InData, const jShader* InShader) const
{
	JASSERT(name);

	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform1i(loc, InData);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const uint32 InData, const jShader* InShader) const
{
	JASSERT(name);

	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform1ui(loc, InData);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const float InData, const jShader* InShader) const
{
	JASSERT(name);

	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform1f(loc, InData);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Vector2& InData, const jShader* InShader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform2fv(loc, 1, &InData.v[0]);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Vector& InData, const jShader* InShader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform3fv(loc, 1, &InData.v[0]);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Vector4& InData, const jShader* InShader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform4fv(loc, 1, &InData.v[0]);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Vector2i& InData, const jShader* InShader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform2iv(loc, 1, &InData.v[0]);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Vector3i& InData, const jShader* InShader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform3iv(loc, 1, &InData.v[0]);
	return true;
}

bool jRHI_OpenGL::SetUniformbuffer(const jName& name, const Vector4i& InData, const jShader* InShader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(InShader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glUniform4iv(loc, 1, &InData.v[0]);
	return true;
}

bool jRHI_OpenGL::GetUniformbuffer(Matrix& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformfv(shader_gl->program, loc, sizeof(Matrix), (float*)&outResult);

#if MATRIX_ROW_MAJOR_ORDER
	outResult.SetTranspose();
#endif

	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(int& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetUniformiv(shader_gl->program, loc, (int*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(uint32& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetUniformuiv(shader_gl->program, loc, (uint32*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(float& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetUniformfv(shader_gl->program, loc, (float*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(Vector2& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformfv(shader_gl->program, loc, sizeof(Vector2), (float*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(Vector& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformfv(shader_gl->program, loc, sizeof(Vector), (float*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(Vector4& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformfv(shader_gl->program, loc, sizeof(Vector4), (float*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(Vector2i& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformiv(shader_gl->program, loc, sizeof(Vector2i), (int*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(Vector3i& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformiv(shader_gl->program, loc, sizeof(Vector3i), (int*)&outResult);
	return true;
}
bool jRHI_OpenGL::GetUniformbuffer(Vector4i& outResult, const jName& name, const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	auto loc = shader_gl->TryGetUniformLocation(name);
	if (loc == -1)
		return false;
	glGetnUniformiv(shader_gl->program, loc, sizeof(Vector4i), (int*)&outResult);
	return true;
}

int32 jRHI_OpenGL::SetMatetrial(const jMaterialData* materialData, const jShader* shader, int32 baseBindingIndex /*= 0*/) const
{
	int index = baseBindingIndex;
	for (int32 i = 0; i < materialData->Params.size(); ++i)
	{
		const auto& matParam = materialData->Params[i];
		auto tex_gl = static_cast<const jTexture_OpenGL*>(matParam.Texture);
		if (!tex_gl)
			continue;

		if (!SetUniformbuffer(matParam.Name, index, shader))
			continue;
		SetTexture(index, matParam.Texture);

		if (matParam.SamplerState)
		{
			BindSamplerState(index, matParam.SamplerState);
		}
		else
		{
			BindSamplerState(index, nullptr);
			//SetTextureFilter(tex_gl->Type, ETextureFilterTarget::MAGNIFICATION, tex_gl->Magnification);
			//SetTextureFilter(tex_gl->Type, ETextureFilterTarget::MINIFICATION, tex_gl->Minification);
		}

		g_rhi->SetUniformbuffer(GetCommonTextureSRGBName(index), tex_gl->sRGB, shader);
		++index;
	}

	return index;
}

void jRHI_OpenGL::SetTexture(int32 index, const jTexture* texture) const
{
	glActiveTexture(GL_TEXTURE0 + index);
	
	auto texture_gl = static_cast<const jTexture_OpenGL*>(texture);
	auto textureType = GetOpenGLTextureType(texture_gl->Type, texture_gl->SampleCount);
	glBindTexture(textureType, texture_gl->TextureID);
}

void jRHI_OpenGL::SetTextureFilter(ETextureType type, int32 sampleCount, ETextureFilterTarget target, ETextureFilter filter) const
{
	const uint32 texFilter = GetOpenGLTextureFilterType(filter);
	const uint32 texTarget = GetOpenGLFilterTargetType(target);
	const uint32 textureType = GetOpenGLTextureType(type, sampleCount);

	glTexParameteri(textureType, texTarget, texFilter);
}

void jRHI_OpenGL::EnableCullFace(bool enable) const
{
	if (enable)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

void jRHI_OpenGL::SetFrontFace(EFrontFace frontFace) const
{
	glFrontFace(GetOpenGLFrontFaceType(frontFace));
}

void jRHI_OpenGL::EnableCullMode(ECullMode cullMode) const
{
	const uint32 cullmode_gl = GetOpenGLCullMode(cullMode);
	glCullFace(cullmode_gl);
}

jFrameBuffer* jRHI_OpenGL::CreateFrameBuffer(const jFrameBufferInfo& info) const
{
	const uint32 internalFormat = GetOpenGLTextureFormat(info.Format);
	const uint32 format = GetOpenGLTextureFormat(info.Format);
	const uint32 formatType = GetOpenGLPixelFormat(info.Format);

	const bool hasDepthAttachment = IsDepthFormat(info.Format);

	//const uint32 magnification = GetOpenGLTextureFilterType(info.Magnification);
	//const uint32 minification = GetOpenGLTextureFilterType(info.Minification);

	auto rt_gl = new jFrameBuffer_OpenGL();
	rt_gl->Info = info;
	
	const int32 MipLevels = jTexture::GetMipLevels(info.Width, info.Height);
	JASSERT(info.SampleCount >= 1);
	JASSERT(info.LayerCount > 0);
	if ((info.TextureType == ETextureType::TEXTURE_2D))
	{
		uint32 fbo = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		rt_gl->Textures.resize(info.LayerCount);
		for (int i = 0; i < info.LayerCount; ++i)
		{
			uint32 tbo = 0;
			glGenTextures(1, &tbo);
			if (info.SampleCount > 1)
			{
				glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tbo);
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, info.SampleCount, internalFormat, info.Width, info.Height, true);
				//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MIN_FILTER, minification);
				//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MAG_FILTER, magnification);
				//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D_MULTISAMPLE, tbo, 0);
				if (info.IsGenerateMipmap)
					glGenerateMipmap(GL_TEXTURE_2D_MULTISAMPLE);
			}
			else
			{
				glBindTexture(GL_TEXTURE_2D, tbo);
				glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, info.Width, info.Height, 0, format, formatType, nullptr);
				//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minification);
				//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magnification);
				//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				if (info.IsGenerateMipmap)
					glGenerateMipmap(GL_TEXTURE_2D);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tbo, 0);
			}
			rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);

			auto tex_gl = new jTexture_OpenGL();
			tex_gl->Type = info.TextureType;
			tex_gl->Format = info.Format;
			tex_gl->Width = info.Width;
			tex_gl->Height = info.Height;
			//tex_gl->Magnification = info.Magnification;
			//tex_gl->Minification = info.Minification;
			tex_gl->TextureID = tbo;
			tex_gl->MipLevel = info.IsGenerateMipmap ? jTexture::GetMipLevels(info.Width, info.Height) : 1;
			rt_gl->Textures[i] = std::shared_ptr<jTexture>(tex_gl);
		}

		if (hasDepthAttachment)
		{
			uint32 tbo = 0;
			glGenTextures(1, &tbo);
			if (info.SampleCount > 1)
			{
				glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tbo);
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, info.SampleCount, format, info.Width, info.Height, true);
				if (info.SampleCount > 1)
					glGenerateMipmap(GL_TEXTURE_2D_MULTISAMPLE);
				glFramebufferTexture2D(GL_FRAMEBUFFER, internalFormat, GL_TEXTURE_2D_MULTISAMPLE, tbo, 0);
			}
			else
			{
				glBindTexture(GL_TEXTURE_2D, tbo);
				glTexStorage2D(GL_TEXTURE_2D, MipLevels, format, info.Width, info.Height);
				if (info.SampleCount > 1)
					glGenerateMipmap(GL_TEXTURE_2D);
				glFramebufferTexture2D(GL_FRAMEBUFFER, internalFormat, GL_TEXTURE_2D, tbo, 0);
			}
			auto tex_gl = new jTexture_OpenGL();
			tex_gl->Type = info.TextureType;
			tex_gl->Format = info.Format;
			tex_gl->Width = info.Width;
			tex_gl->Height = info.Height;
			tex_gl->TextureID = tbo;
			tex_gl->MipLevel = info.IsGenerateMipmap ? jTexture::GetMipLevels(info.Width, info.Height) : 1;
			rt_gl->TextureDepth = std::shared_ptr<jTexture>(tex_gl);
		}

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			char szTemp[256] = { 0, };
			sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2D framebuffer which is not complete : %d", status_code);
			JMESSAGE(szTemp);
			return nullptr;
		}
	
		rt_gl->fbos.push_back(fbo);
	}
	else if (info.TextureType == ETextureType::TEXTURE_2D_ARRAY)
	{
		auto tex_gl = new jTexture_OpenGL();
		//tex_gl->Magnification = info.Magnification;
		//tex_gl->Minification = info.Minification;
		tex_gl->Format = info.Format;

		uint32 tbo = 0;
		glGenTextures(1, &tbo);
		if (info.SampleCount > 1)
		{
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, tbo);
			//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, GL_TEXTURE_MIN_FILTER, minification);
			//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, GL_TEXTURE_MAG_FILTER, magnification);
			glTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, internalFormat, info.Width, info.Height, info.LayerCount, true);
			if (info.IsGenerateMipmap)
				glGenerateMipmap(GL_TEXTURE_2D_MULTISAMPLE_ARRAY);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D_ARRAY, tbo);
			//glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, minification);
			//glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, magnification);
			glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, info.Width, info.Height, info.LayerCount, 0, format, formatType, nullptr);
			if (info.IsGenerateMipmap)
				glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
		}

		uint32 fbo = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		for (int32 i = 0; i < info.LayerCount; ++i)
		{
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, tbo, 0, i);
			rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
		}

		if (hasDepthAttachment)
		{
			uint32 tbo = 0;
			glGenTextures(1, &tbo);
			if (info.SampleCount > 1)
			{
				glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tbo);
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, info.SampleCount, format, info.Width, info.Height, true);
				if (info.SampleCount > 1)
					glGenerateMipmap(GL_TEXTURE_2D_MULTISAMPLE);
				glFramebufferTexture2D(GL_FRAMEBUFFER, internalFormat, GL_TEXTURE_2D_MULTISAMPLE, tbo, 0);
			}
			else
			{
				glBindTexture(GL_TEXTURE_2D, tbo);
				glTexStorage2D(GL_TEXTURE_2D, MipLevels, format, info.Width, info.Height);
				if (info.SampleCount > 1)
					glGenerateMipmap(GL_TEXTURE_2D);
				glFramebufferTexture2D(GL_FRAMEBUFFER, internalFormat, GL_TEXTURE_2D, tbo, 0);
			}
			auto tex_gl = new jTexture_OpenGL();
			tex_gl->Type = info.TextureType;
			tex_gl->Width = info.Width;
			tex_gl->Height = info.Height;
			//tex_gl->Magnification = info.Magnification;
			//tex_gl->Minification = info.Minification;
			tex_gl->TextureID = tbo;
			tex_gl->Format = info.Format;
			tex_gl->MipLevel = info.IsGenerateMipmap ? jTexture::GetMipLevels(info.Width, info.Height) : 1;
			rt_gl->TextureDepth = std::shared_ptr<jTexture>(tex_gl);
		}

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			char szTemp[256] = { 0, };
			sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2DArray framebuffer which is not complete : %d", status_code);
			JMESSAGE(szTemp);
			return nullptr;
		}

		tex_gl->Type = info.TextureType;
		tex_gl->Format = info.Format;
		tex_gl->Width = info.Width;
		tex_gl->Height = info.Height;
		tex_gl->TextureID = tbo;
		rt_gl->Textures.push_back(std::shared_ptr<jTexture>(tex_gl));

		rt_gl->fbos.push_back(fbo);
	}
	else if (info.TextureType == ETextureType::TEXTURE_CUBE)
	{
		JASSERT(info.SampleCount > 1);

		uint32 tbo = 0;
		glGenTextures(1, &tbo);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tbo);
		//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minification);
		//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, magnification);
		//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


		for (int i = 0; i < 6; ++i)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, info.Width, info.Height, 0, format, formatType, nullptr);

		if (info.IsGenerateMipmap)
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		auto tex_gl = new jTexture_OpenGL();
		tex_gl->Type = info.TextureType;
		tex_gl->Format = info.Format;
		tex_gl->Width = info.Width;
		tex_gl->Height = info.Height;
		tex_gl->TextureID = tbo;
		tex_gl->MipLevel = info.IsGenerateMipmap ? jTexture::GetMipLevels(info.Width, info.Height) : 1;
		//tex_gl->Magnification = info.Magnification;
		//tex_gl->Minification = info.Minification;
		rt_gl->Textures.push_back(std::shared_ptr<jTexture>(tex_gl));

		for (int i = 0; i < 6; ++i)
		{
			uint32 fbo = 0;
			glGenFramebuffers(1, &fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tbo, 0);

			if (hasDepthAttachment)
			{
				uint32 rbo = 0;
				glGenRenderbuffers(1, &rbo);
				glBindRenderbuffer(GL_RENDERBUFFER, rbo);
				glRenderbufferStorage(GL_RENDERBUFFER, format, info.Width, info.Height);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, internalFormat, GL_RENDERBUFFER, rbo);
				rt_gl->rbos.push_back(rbo);
			}

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
				char szTemp[256] = { 0, };
				sprintf_s(szTemp, sizeof(szTemp), "Failed to create TextureCube framebuffer %d which is not complete : %d", i, status_code);
				JMESSAGE(szTemp);
				return nullptr;
			}

			rt_gl->fbos.push_back(fbo);
		}

		rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0);
	}
	else
	{
		JMESSAGE("Unsupported type texture in FramebufferPool");
		return nullptr;
	}

	return rt_gl;
}

void jRHI_OpenGL::EnableDepthTest(bool enable) const
{
	if (enable)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
}

void jRHI_OpenGL::SetFrameBuffer(const jFrameBuffer* rt, int32 index /*= 0*/, bool mrt /*= false*/) const
{
	if (rt)
	{
		rt->FBOBegin(index, mrt);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
	}
}

void jRHI_OpenGL::SetDrawBuffers(const std::initializer_list<EDrawBufferType>& list) const
{
	std::vector<uint32> buffers;
	buffers.reserve(list.size());
	for(auto& iter : list)
		buffers.push_back(GetOpenGLDrawBufferType(iter));

	if (!buffers.empty())
		glDrawBuffers(static_cast<int32>(buffers.size()), &buffers[0]);
}

void jRHI_OpenGL::UpdateVertexBuffer(jVertexBuffer* vb, const std::shared_ptr<jVertexStreamData>& streamData) const
{
	auto vb_gl = static_cast<jVertexBuffer_OpenGL*>(vb);

	glBindVertexArray(vb_gl->VAO);
	for (auto& iter : vb_gl->Streams)
		glDeleteBuffers(1, &iter.BufferID);
	vb_gl->Streams.clear();

	vb_gl->VertexStreamData = streamData;

	for (const auto& iter : streamData->Params)
	{
		if (iter->Stride <= 0)
			continue;

		const uint32 bufferType = GetOpenGLBufferType(iter->BufferType);

		jVertexStream_OpenGL stream;
		glGenBuffers(1, &stream.BufferID);
		glBindBuffer(GL_ARRAY_BUFFER, stream.BufferID);
		glBufferData(GL_ARRAY_BUFFER, iter->GetBufferSize(), iter->GetBufferData(), bufferType);
		stream.Name = iter->Name;
		stream.Count = iter->Stride / iter->ElementTypeSize;
		stream.BufferType = iter->BufferType;
		stream.ElementType = iter->ElementType;
		stream.Stride = iter->Stride;
		stream.InstanceDivisor = iter->InstanceDivisor;
		stream.Offset = 0;

		vb_gl->Streams.emplace_back(stream);
	}
}

void jRHI_OpenGL::UpdateVertexBuffer(jVertexBuffer* vb, IStreamParam* streamParam, int32 streamParamIndex) const
{
	JASSERT(streamParam->Stride > 0);
	if (streamParam->Stride <= 0)
		return;

	auto vb_gl = static_cast<jVertexBuffer_OpenGL*>(vb);
	
	auto stream = vb_gl->Streams[streamParamIndex];
	const uint32 bufferType = GetOpenGLBufferType(streamParam->BufferType);

	glBindVertexArray(vb_gl->VAO);
	glBindBuffer(GL_ARRAY_BUFFER, stream.BufferID);
	glBufferData(GL_ARRAY_BUFFER, streamParam->GetBufferSize(), streamParam->GetBufferData(), bufferType);
	stream.Name = streamParam->Name;
	stream.Count = streamParam->Stride / streamParam->ElementTypeSize;
	stream.ElementType = streamParam->ElementType;
	stream.Stride = streamParam->Stride;
	stream.Offset = 0;
}

void jRHI_OpenGL::EnableBlend(bool enable) const
{
	if (enable)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
}

void jRHI_OpenGL::SetBlendFunc(EBlendFactor src, EBlendFactor dest) const
{
	const uint32 src_gl = GetOpenGLBlendSrc(src);
	const uint32 dest_gl = GetOpenGLBlendDest(dest);

	glBlendFunc(src_gl, dest_gl);
}

void jRHI_OpenGL::SetBlendFuncRT(EBlendFactor src, EBlendFactor dest, int32 rtIndex /*= 0*/) const
{
    const uint32 src_gl = GetOpenGLBlendSrc(src);
    const uint32 dest_gl = GetOpenGLBlendDest(dest);

	glBlendFunci(rtIndex, src_gl, dest_gl);
}

void jRHI_OpenGL::SetBlendEquation(EBlendOp func) const
{
	const uint32 equation = GetOpenGLBlendEquation(func);
	glBlendEquation(equation);
}

void jRHI_OpenGL::SetBlendEquation(EBlendOp func, int32 rtIndex) const
{
	const uint32 equation = GetOpenGLBlendEquation(func);
	glBlendEquationi(equation, rtIndex);
}

void jRHI_OpenGL::SetBlendColor(float r, float g, float b, float a) const
{
	glBlendColor(r, g, b, a);
}

void jRHI_OpenGL::EnableStencil(bool enable) const
{
	if (enable)
		glEnable(GL_STENCIL_TEST);
	else
		glDisable(GL_STENCIL_TEST);
}

void jRHI_OpenGL::SetStencilOpSeparate(EFace face, EStencilOp sFail, EStencilOp dpFail, EStencilOp dpPass) const
{
	const uint32 face_gl = GetOpenGLFaceType(face);
	glStencilOpSeparate(face_gl, GetStencilOp(sFail), GetStencilOp(dpFail), GetStencilOp(dpPass));
}

void jRHI_OpenGL::SetStencilFunc(ECompareOp func, int32 ref, uint32 mask) const
{
	uint32 func_gl = GetOpenGLComparisonFunc(func);
	glStencilFunc(func_gl, ref, mask);
}

void jRHI_OpenGL::SetDepthFunc(ECompareOp func) const
{
	uint32 func_gl = GetOpenGLComparisonFunc(func);
	glDepthFunc(func_gl);
}

void jRHI_OpenGL::SetDepthMask(bool enable) const
{
	glDepthMask(enable);
}

void jRHI_OpenGL::SetColorMask(bool r, bool g, bool b, bool a) const
{
	glColorMask(r, g, b, a);
}

IUniformBufferBlock* jRHI_OpenGL::CreateUniformBufferBlock(const char* blockname, size_t size /*= 0*/) const
{
	auto uniformBufferBlock = new jUniformBufferBlock_OpenGL(blockname);
	uniformBufferBlock->Init();
	return uniformBufferBlock;
}

//////////////////////////////////////////////////////////////////////////
void jVertexBuffer_OpenGL::Bind(const jShader* shader) const
{
	g_rhi->BindVertexBuffer(this, shader);
}

void jIndexBuffer_OpenGL::Bind(const jShader* shader) const
{
	g_rhi->BindIndexBuffer(this, shader);
}

bool jFrameBuffer_OpenGL::FBOBegin(int index, bool mrt) const
{
	if (mrt && mrt_fbo)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mrt_fbo);
		if (mrt_drawBuffers.empty())
			glDrawBuffer(GL_NONE);
		else
			glDrawBuffers(static_cast<int32>(mrt_drawBuffers.size()), &mrt_drawBuffers[0]);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
		if (drawBuffers.empty())
			glDrawBuffer(GL_NONE);
		else
			glDrawBuffers(static_cast<int32>(drawBuffers.size()), &drawBuffers[0]);
	}

	glViewport(0, 0, Info.Width, Info.Height);
	//glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	__super::FBOBegin(index, mrt);

	return true;
}

void jFrameBuffer_OpenGL::End() const
{
	__super::End();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
}

bool jFrameBuffer_OpenGL::SetDepthAttachment(const std::shared_ptr<jTexture>& InDepth)
{
	if (!__super::SetDepthAttachment(InDepth))
		return false;

	const jTexture_OpenGL* tex_gl = static_cast<jTexture_OpenGL*>(InDepth.get());

	// const uint32 format = GetOpenGLTextureFormat(tex_gl->Format);
	const uint32 internalFormat = GetOpenGLTextureFormat(tex_gl->Format);
	const bool hasDepthAttachment = IsDepthFormat(tex_gl->Format);
	JASSERT(hasDepthAttachment);
	if (!hasDepthAttachment)
		return false;

	JASSERT(fbos.size() == 1);
	glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
	glFramebufferTexture2D(GL_FRAMEBUFFER, internalFormat, GetOpenGLTextureType(tex_gl->Type, tex_gl->SampleCount), tex_gl->TextureID, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		char szTemp[256] = { 0, };
		sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2D framebuffer which is not complete : %d", status_code);
		JMESSAGE(szTemp);
		return false;
	}

	return true;
}

void jFrameBuffer_OpenGL::SetDepthMipLevel(int32 InLevel)
{
	const jTexture_OpenGL* tex_gl = static_cast<jTexture_OpenGL*>(TextureDepth.get());
	JASSERT(tex_gl);

	// const uint32 format = GetOpenGLTextureFormat(tex_gl->Format);
	const uint32 internalFormat = GetOpenGLTextureFormat(tex_gl->Format);
	const bool hasDepthAttachment = IsDepthFormat(tex_gl->Format);
	check(hasDepthAttachment);

	glFramebufferTexture2D(GL_FRAMEBUFFER, internalFormat, GetOpenGLTextureType(tex_gl->Type, tex_gl->SampleCount), tex_gl->TextureID, InLevel);
}

//////////////////////////////////////////////////////////////////////////
void jUniformBufferBlock_OpenGL::UpdateBufferData(const void* newData, size_t size)
{
	Size = size;
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	glBufferData(GL_UNIFORM_BUFFER, size, newData, GL_DYNAMIC_DRAW);
}

void jUniformBufferBlock_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	glClearBufferData(GL_UNIFORM_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jUniformBufferBlock_OpenGL::Init()
{
	BindingPoint = GetBindPoint();
	glGenBuffers(1, &UBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, BindingPoint, UBO);
}

void jUniformBufferBlock_OpenGL::Bind(const jShader* shader) const
{
	if (-1 == BindingPoint)
		return;

	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	uint32 index = glGetUniformBlockIndex(shader_gl->program, Name.c_str());
	if (-1 != index)
	{
		glUniformBlockBinding(shader_gl->program, index, BindingPoint);
		glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	}
}

void jShaderStorageBufferObject_OpenGL::Init()
{
	BindingPoint = GetBindPoint();
	glGenBuffers(1, &SSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BindingPoint, SSBO);
}

void jShaderStorageBufferObject_OpenGL::UpdateBufferData(void* newData, size_t size)
{
	Size = size;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, size, newData, GL_DYNAMIC_COPY);
}

void jShaderStorageBufferObject_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jShaderStorageBufferObject_OpenGL::GetBufferData(void* newData, size_t size)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
	void* p = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	if (p)
		memcpy(newData, p, size);
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
}

void jShaderStorageBufferObject_OpenGL::Bind(const jShader* shader) const
{
	if (-1 == BindingPoint)
		return;

	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	uint32 index = glGetProgramResourceIndex(shader_gl->program, GL_SHADER_STORAGE_BLOCK, Name.c_str());
	if (-1 != index)
		glShaderStorageBlockBinding(shader_gl->program, index, BindingPoint);
}

void jAtomicCounterBuffer_OpenGL::Init()
{
	glGenBuffers(1, &ACBO);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, BindingPoint, ACBO);
}

void jAtomicCounterBuffer_OpenGL::UpdateBufferData(void* newData, size_t size)
{
	Size = size;
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	glBufferData(GL_ATOMIC_COUNTER_BUFFER, size, newData, GL_DYNAMIC_COPY);
}

void jAtomicCounterBuffer_OpenGL::GetBufferData(void* newData, size_t size)
{
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	void* p = glMapBuffer(GL_ATOMIC_COUNTER_BUFFER, GL_READ_ONLY);
	if (p)
		memcpy(newData, p, size);
	glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
}

void jAtomicCounterBuffer_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	glClearBufferData(GL_ATOMIC_COUNTER_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jAtomicCounterBuffer_OpenGL::Bind(const jShader* shader) const
{
	if (-1 == BindingPoint)
		return;

	//glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	//void* p = glMapBuffer(GL_ATOMIC_COUNTER_BUFFER, GL_WRITE_ONLY);
	//if (Data)
	//	memcpy(p, Data, Size);
	//glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);

	//auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	//uint32 index = glGetUniformLocation(shader_gl->program, Name.c_str());
	//uint32 index = glGetProgramResourceIndex(shader_gl->program, GL_ATOMIC_COUNTER_BUFFER, Name.c_str());
	//if (-1 != index)
	//	glShaderStorageBlockBinding(shader_gl->program, index, BindingPoint);
}

void jTransformFeedbackBuffer_OpenGL::Init()
{
	glGenBuffers(1, &TFBO);
	glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, TFBO);
}

void jTransformFeedbackBuffer_OpenGL::Bind(const jShader* shader) const
{
	glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, TFBO);
}

void jTransformFeedbackBuffer_OpenGL::UpdateBufferData(void* newData, size_t size)
{
	Size = size;
	glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, TFBO);
	glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, size, newData, GL_DYNAMIC_COPY);
}

void jTransformFeedbackBuffer_OpenGL::GetBufferData(void* newData, size_t size)
{
	glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, size, newData);
}

void jTransformFeedbackBuffer_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, TFBO);
	glClearBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jTransformFeedbackBuffer_OpenGL::UpdateVaryingsToShader(const std::vector<std::string>& varyings, const jShader* shader)
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	check(shader_gl);

	Varyings = varyings;

	std::vector<const char*> tempVaryings;
	tempVaryings.reserve(Varyings.size());
	for (const auto& varying : Varyings)
		tempVaryings.push_back(varying.c_str());

	glTransformFeedbackVaryings(shader_gl->program, static_cast<int32>(tempVaryings.size()), tempVaryings.data(), GL_INTERLEAVED_ATTRIBS);
	LinkProgram(shader);
}

void jTransformFeedbackBuffer_OpenGL::LinkProgram(const jShader* shader) const
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	check(shader_gl);

	const auto program = shader_gl->program;

	glLinkProgram(program);
	int32 isValid = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &isValid);
	if (!isValid)
	{
		int maxLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

		if (maxLength > 0)
		{
			std::vector<char> errorLog(maxLength + 1, 0);
			glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
			JMESSAGE(&errorLog[0]);
		}
	}
}

void jTransformFeedbackBuffer_OpenGL::ClearVaryingsToShader(const jShader* shader)
{
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	check(shader_gl);

	glTransformFeedbackVaryings(shader_gl->program, 0, nullptr, GL_INTERLEAVED_ATTRIBS);
	LinkProgram(shader);
}

void jTransformFeedbackBuffer_OpenGL::Begin(EPrimitiveType type)
{
	++DepthCheck;
	check(DepthCheck == 1);

	glBeginTransformFeedback(GetPrimitiveType(type));
}

void jTransformFeedbackBuffer_OpenGL::End()
{
	--DepthCheck;
	check(DepthCheck == 0);

	glEndTransformFeedback();
}

void jTransformFeedbackBuffer_OpenGL::Pause()
{
	glPauseTransformFeedback();
}

bool jRenderPass_OpenGL::CreateRenderPass()
{
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	for (int32 i = 0; i < ColorAttachments.size(); ++i)
	{
		const jAttachment* attachmet = ColorAttachments[i];
		const std::shared_ptr<jRenderTarget>& RTPtr = attachmet->RenderTargetPtr;

		const uint32 textureType = GetOpenGLTextureType(RTPtr->Info.Type, RTPtr->Info.SampleCount);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, *(uint32*)RTPtr->GetViewHandle(), 0);
		drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
	}

	if (DepthAttachment)
	{
		const jAttachment* attachmet = DepthAttachment;
		const std::shared_ptr<jRenderTarget>& RTPtr = attachmet->RenderTargetPtr;
		const uint32 textureType = GetOpenGLTextureType(RTPtr->Info.Type, RTPtr->Info.SampleCount);
		const uint32 textureFormatInternal = GetOpenGLTextureFormat(RTPtr->Info.Format);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, textureType, *(uint32*)RTPtr->GetViewHandle(), 0);
	}

	if (ColorAttachmentResolve)
	{
		// OpenGL 은 Resolve texture를 지원하지 않음. 별도로 처리해야 함.
	}

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		char szTemp[256] = { 0, };
		sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2DArray framebuffer which is not complete : %d", status_code);
		JMESSAGE(szTemp);
		return false;
	}

	return true;
}

void jRenderPass_OpenGL::Release()
{

}


#endif // USE_OPENGL