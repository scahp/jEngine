#pragma once
#include "jRHIType.h"
#include "Math\Matrix.h"
#include "Core\jName.h"
#include "Math\Vector.h"

extern class jRHI* g_rhi;

struct jShader;
struct jShaderInfo;
struct jSamplerStateInfo;

struct IBuffer
{
	virtual ~IBuffer() {}

	virtual jName GetName() const { return jName::Invalid; }
	virtual void Bind(const jShader* shader) const = 0;
};

class jCommandBuffer;

struct jVertexBuffer : public IBuffer
{
	std::shared_ptr<jVertexStreamData> VertexStreamData;

	virtual size_t GetHash() const { return 0; }
	virtual void Bind(const jShader* shader) const {}
	virtual void Bind() const {}
};

struct jIndexBuffer : public IBuffer
{
	std::shared_ptr<jIndexStreamData> IndexStreamData;

	virtual void Bind(const jShader* shader) const {}
	virtual void Bind() const {}
};

struct jTexture
{
	constexpr jTexture() = default;
	constexpr jTexture(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight
		, int32 InLayerCount = 1, int32 InSampleCount = 1, bool InSRGB = false)
		: Type(InType), Format(InFormat), Width(InWidth), Height(InHeight), LayerCount(InLayerCount)
		, SampleCount(InSampleCount), sRGB(InSRGB)
	{}
	virtual ~jTexture() {}
	static int32 GetMipLevels(int32 InWidth, int32 InHeight)
	{
		return 1 + (int32)floorf(log2f(fmaxf((float)InWidth, (float)InHeight)));
	}

	virtual const void* GetHandle() const { return nullptr; }
	virtual const void* GetSamplerStateHandle() const { return nullptr; }

	ETextureType Type = ETextureType::MAX;
	ETextureFormat Format = ETextureFormat::RGB8;
	int32 LayerCount = 1;
	int32 SampleCount = 1;

	int32 Width = 0;
	int32 Height = 0;

	bool sRGB = false;
};

struct jViewport
{
	jViewport() = default;
	jViewport(int32 x, int32 y, int32 width, int32 height, float minDepth = 0.0f, float maxDepth = 1.0f)
		: X(static_cast<float>(x)), Y(static_cast<float>(y))
		, Width(static_cast<float>(width)), Height(static_cast<float>(height))
		, MinDepth(minDepth), MaxDepth(maxDepth)
	{}
	jViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f) : X(x), Y(y), Width(width), Height(height), MinDepth(minDepth), MaxDepth(maxDepth) {}

	float X = 0.0f;
	float Y = 0.0f;
	float Width = 0.0f;
	float Height = 0.0f;
	float MinDepth = 0.0f;
	float MaxDepth = 1.0f;

	size_t CreateHash() const
	{
		size_t result = 0;
		result = std::hash<float>{}(X);
		result ^= std::hash<float>{}(Y);
		result ^= std::hash<float>{}(Width);
		result ^= std::hash<float>{}(Height);
		result ^= std::hash<float>{}(MinDepth);
		result ^= std::hash<float>{}(MaxDepth);
		return result;
	}
};

struct jScissor
{
	jScissor() = default;
	jScissor(int32 x, int32 y, int32 width, int32 height)
		: Offset(x, y), Extent(width, height)
	{}
	Vector2i Offset;
	Vector2i Extent;

	size_t CreateHash() const
	{
		size_t result = 0;
		result = std::hash<int32>{}(Offset.x);
		result ^= std::hash<int32>{}(Offset.y);
		result ^= std::hash<int32>{}(Extent.x);
		result ^= std::hash<int32>{}(Extent.y);
		return result;
	}
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

struct IUniformBuffer : public IBuffer
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
	virtual void Bind(const jShader* shader) const override;
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

struct IUniformBufferBlock : public IBuffer
{
	IUniformBufferBlock() = default;
	IUniformBufferBlock(const std::string& name, size_t size = 0)
		: Name(name), Size(size)
	{}
	virtual ~IUniformBufferBlock() {}

	std::string Name;
	size_t Size = 0;

	FORCEINLINE size_t GetBufferSize() const { return Size; }

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const override { }
	virtual void UpdateBufferData(const void* newData, size_t size) = 0;
	virtual void ClearBuffer(int32 clearValue = 0) = 0;
	
	virtual void* GetBuffer() const { return nullptr; }
	virtual void* GetBufferMemory() const { return nullptr; }
};

struct IShaderStorageBufferObject : public IBuffer
{
	IShaderStorageBufferObject() = default;
	IShaderStorageBufferObject(const std::string& name)
		: Name(name)
	{}
	virtual ~IShaderStorageBufferObject() {}

	std::string Name;
	size_t Size = 0;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const override {}
	virtual void UpdateBufferData(void* newData, size_t size) = 0;
	virtual void GetBufferData(void* newData, size_t size) = 0;
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
	size_t Size = 0;
	uint32 BindingPoint = -1;

	virtual void Init() = 0;
	virtual void Bind(const jShader* shader) const override {}
	virtual void UpdateBufferData(void* newData, size_t size) = 0;
	virtual void GetBufferData(void* newData, size_t size) = 0;
	virtual void ClearBuffer(int32 clearValue) = 0;

	template <typename T>
	void GetBufferData(typename std::remove_reference<T>::type& out)
	{
		GetBufferData(&out, sizeof(T));
	}
};

struct ITransformFeedbackBuffer : public IBuffer
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
	virtual void Bind(const jShader* shader) const override {}
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

//////////////////////////////////////////////////////////////////////////
struct jFrameBufferInfo
{
	jFrameBufferInfo() = default;
	jFrameBufferInfo(ETextureType textureType, ETextureFormat format, int32 width, int32 height, int32 layerCount = 1
		, bool isGenerateMipmap = false, int32 sampleCount = 1)
		: TextureType(textureType), Format(format), Width(width), Height(height), LayerCount(layerCount)
		, IsGenerateMipmap(isGenerateMipmap), SampleCount(sampleCount)
	{}

	size_t GetHash() const
	{
		size_t result = 0;
		hash_combine(result, TextureType);
		hash_combine(result, Format);
		hash_combine(result, Width);
		hash_combine(result, Height);
		hash_combine(result, LayerCount);
		hash_combine(result, IsGenerateMipmap);
		hash_combine(result, SampleCount);
		return result;
	}

	ETextureType TextureType = ETextureType::TEXTURE_2D;
	ETextureFormat Format = ETextureFormat::RGB8;
	int32 Width = 0;
	int32 Height = 0;
	int32 LayerCount = 1;
	bool IsGenerateMipmap = false;
	int32 SampleCount = 1;
};

struct jFrameBuffer : public std::enable_shared_from_this<jFrameBuffer>
{
	virtual ~jFrameBuffer() {}

	virtual jTexture* GetTexture(int32 index = 0) const { return Textures[index].get(); }
	virtual jTexture* GetTextureDepth(int32 index = 0) const { return TextureDepth.get(); }
	virtual ETextureType GetTextureType() const { return Info.TextureType; }
	virtual bool SetDepthAttachment(const std::shared_ptr<jTexture>& InDepth) { TextureDepth = InDepth; return true; }
	virtual void SetDepthMipLevel(int32 InLevel) {}

	virtual bool FBOBegin(int index = 0, bool mrt = false) const { return true; };
	virtual void End() const {}

	jFrameBufferInfo Info;
	std::vector<std::shared_ptr<jTexture> > Textures;
	std::shared_ptr<jTexture> TextureDepth;
};

class jCommandBuffer
{
public:
	virtual ~jCommandBuffer() {}

	virtual void* GetHandle() const { return nullptr; }
	virtual bool Begin() const { return false; }
	virtual bool End() const { return false; }
};

struct jRenderTargetInfo
{
	constexpr jRenderTargetInfo() = default;
	constexpr jRenderTargetInfo(ETextureType textureType, ETextureFormat format, int32 width, int32 height, int32 layerCount = 1
		, bool isGenerateMipmap = false, int32 sampleCount = 1)
		: Type(textureType), Format(format), Width(width), Height(height), LayerCount(layerCount)
		, IsGenerateMipmap(isGenerateMipmap), SampleCount(sampleCount)
	{}

	size_t GetHash() const
	{
		size_t result = 0;
		hash_combine(result, Type);
		hash_combine(result, Format);
		hash_combine(result, Width);
		hash_combine(result, Height);
		hash_combine(result, LayerCount);
		hash_combine(result, IsGenerateMipmap);
		hash_combine(result, SampleCount);
		return result;
	}

	ETextureType Type = ETextureType::TEXTURE_2D;
	ETextureFormat Format = ETextureFormat::RGB8;
	int32 Width = 0;
	int32 Height = 0;
	int32 LayerCount = 1;
	bool IsGenerateMipmap = false;
	int32 SampleCount = 1;
};

struct jRenderTarget final : public std::enable_shared_from_this<jRenderTarget>
{
	// Create render target from texture, It is useful to create render target from swapchain texture
	template <typename T1, class... T2>
	static std::shared_ptr<jRenderTarget> CreateFromTexture(const T2&... args)
	{
		const auto& T1Ptr = std::shared_ptr<T1>(new T1(args...));
		return std::shared_ptr<jRenderTarget>(new jRenderTarget(T1Ptr));
	}

	constexpr jRenderTarget() = default;
	jRenderTarget(const std::shared_ptr<jTexture>& InTexturePtr)
		: TexturePtr(InTexturePtr)
	{
		jTexture* texture = TexturePtr.get();
		if (texture)
		{
			Info.Type = texture->Type;
			Info.Format = texture->Format;
			Info.Width = texture->Width;
			Info.Height = texture->Height;
			Info.LayerCount = texture->LayerCount;
			Info.SampleCount = texture->SampleCount;
		}
	}

	jTexture* GetTexture() const { return TexturePtr.get(); }
	const void* GetTexureHandle() const { return TexturePtr.get() ? TexturePtr->GetHandle() : nullptr; }

	jRenderTargetInfo Info;
	std::shared_ptr<jTexture> TexturePtr;
};

template <typename T1>
struct TRenderTarget
{
};

struct jQueryTime
{
	virtual ~jQueryTime() {}

	uint64 TimeStamp = 0;
};

struct jQueryPrimitiveGenerated
{
	virtual ~jQueryPrimitiveGenerated() {}

	uint64 NumOfGeneratedPrimitives = 0;

	void Begin() const;
	void End() const;
	uint64 GetResult();
};

struct jRasterizationStateInfo
{
	virtual ~jRasterizationStateInfo() {}
	virtual void Initialize() {}
	virtual size_t GetHash() const
	{
		size_t result = CityHash64((const char*)&PolygonMode, sizeof(PolygonMode));
		result ^= CityHash64((const char*)&CullMode, sizeof(CullMode));
		result ^= CityHash64((const char*)&FrontFace, sizeof(FrontFace));
		result ^= CityHash64((const char*)&DepthBiasEnable, sizeof(DepthBiasEnable));
		result ^= CityHash64((const char*)&DepthBiasConstantFactor, sizeof(DepthBiasConstantFactor));
		result ^= CityHash64((const char*)&DepthBiasClamp, sizeof(DepthBiasClamp));
		result ^= CityHash64((const char*)&DepthBiasSlopeFactor, sizeof(DepthBiasSlopeFactor));
		result ^= CityHash64((const char*)&LineWidth, sizeof(LineWidth));
		result ^= CityHash64((const char*)&DepthClampEnable, sizeof(DepthClampEnable));
		result ^= CityHash64((const char*)&RasterizerDiscardEnable, sizeof(RasterizerDiscardEnable));
		return result;
	}

	EPolygonMode PolygonMode = EPolygonMode::FILL;
	ECullMode CullMode = ECullMode::BACK;
	EFrontFace FrontFace = EFrontFace::CCW;
	bool DepthBiasEnable = false;
	float DepthBiasConstantFactor = 0.0f;
	float DepthBiasClamp = 0.0f;
	float DepthBiasSlopeFactor = 0.0f;
	float LineWidth = 1.0f;
	bool DepthClampEnable = false;
	bool RasterizerDiscardEnable = false;
};

struct jMultisampleStateInfo
{
	virtual ~jMultisampleStateInfo() {}
	virtual void Initialize() {}
	virtual size_t GetHash() const
	{
		size_t result = CityHash64((const char*)&SampleCount, sizeof(SampleCount));
		result ^= CityHash64((const char*)&SampleShadingEnable, sizeof(SampleShadingEnable));
		result ^= CityHash64((const char*)&MinSampleShading, sizeof(MinSampleShading));
		result ^= CityHash64((const char*)&AlphaToCoverageEnable, sizeof(AlphaToCoverageEnable));
		result ^= CityHash64((const char*)&AlphaToOneEnable, sizeof(AlphaToOneEnable));
		return result;
	}

	EMSAASamples SampleCount = EMSAASamples::COUNT_1;
	bool SampleShadingEnable = true;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
	float MinSampleShading = 0.2f;
	bool AlphaToCoverageEnable = false;
	bool AlphaToOneEnable = false;
};

struct jStencilOpStateInfo
{
	virtual ~jStencilOpStateInfo() {}
	virtual void Initialize() {}
	virtual size_t GetHash() const
	{
		size_t result = CityHash64((const char*)&FailOp, sizeof(FailOp));
		result ^= CityHash64((const char*)&PassOp, sizeof(PassOp));
		result ^= CityHash64((const char*)&DepthFailOp, sizeof(DepthFailOp));
		result ^= CityHash64((const char*)&CompareOp, sizeof(CompareOp));
		result ^= CityHash64((const char*)&CompareMask, sizeof(CompareMask));
		result ^= CityHash64((const char*)&WriteMask, sizeof(WriteMask));
		result ^= CityHash64((const char*)&Reference, sizeof(Reference));
		return result;
	}

	EStencilOp FailOp = EStencilOp::KEEP;
	EStencilOp PassOp = EStencilOp::KEEP;
	EStencilOp DepthFailOp = EStencilOp::KEEP;
	ECompareOp CompareOp = ECompareOp::NEVER;
	uint32 CompareMask = 0;
	uint32 WriteMask = 0;
	uint32 Reference = 0;
};

struct jDepthStencilStateInfo
{
	virtual ~jDepthStencilStateInfo() {}
	virtual void Initialize() {}
	virtual size_t GetHash() const
	{
		size_t result = CityHash64((const char*)&DepthTestEnable, sizeof(DepthTestEnable));
		result ^= CityHash64((const char*)&DepthWriteEnable, sizeof(DepthWriteEnable));
		result ^= CityHash64((const char*)&DepthCompareOp, sizeof(DepthCompareOp));
		result ^= CityHash64((const char*)&DepthBoundsTestEnable, sizeof(DepthBoundsTestEnable));
		result ^= CityHash64((const char*)&StencilTestEnable, sizeof(StencilTestEnable));
		if (Front)
			result ^= Front->GetHash();
		if (Back)
			result ^= Back->GetHash();
		result ^= CityHash64((const char*)&MinDepthBounds, sizeof(MinDepthBounds));
		result ^= CityHash64((const char*)&MaxDepthBounds, sizeof(MaxDepthBounds));
		return result;
	}

	bool DepthTestEnable = false;
	bool DepthWriteEnable = false;
	ECompareOp DepthCompareOp = ECompareOp::LEQUAL;
	bool DepthBoundsTestEnable = false;
	bool StencilTestEnable = false;
	jStencilOpStateInfo* Front = nullptr;
	jStencilOpStateInfo* Back = nullptr;
	float MinDepthBounds = 0.0f;
	float MaxDepthBounds = 1.0f;

	// VkPipelineDepthStencilStateCreateFlags    flags;
};

struct jBlendingStateInfo
{
	virtual ~jBlendingStateInfo() {}
	virtual void Initialize() {}
	virtual size_t GetHash() const
	{
		size_t result = CityHash64((const char*)&BlendEnable, sizeof(BlendEnable));
		result ^= CityHash64((const char*)&Src, sizeof(Src));
		result ^= CityHash64((const char*)&Dest, sizeof(Dest));
		result ^= CityHash64((const char*)&BlendOp, sizeof(BlendOp));
		result ^= CityHash64((const char*)&SrcAlpha, sizeof(SrcAlpha));
		result ^= CityHash64((const char*)&DestAlpha, sizeof(DestAlpha));
		result ^= CityHash64((const char*)&AlphaBlendOp, sizeof(AlphaBlendOp));
		result ^= CityHash64((const char*)&ColorWriteMask, sizeof(ColorWriteMask));
		return result;
	}

	bool BlendEnable = false;
	EBlendFactor Src = EBlendFactor::SRC_COLOR;
	EBlendFactor Dest = EBlendFactor::ONE_MINUS_SRC_ALPHA;
	EBlendOp BlendOp = EBlendOp::ADD;
	EBlendFactor SrcAlpha = EBlendFactor::SRC_ALPHA;
	EBlendFactor DestAlpha = EBlendFactor::ONE_MINUS_SRC_ALPHA;
	EBlendOp AlphaBlendOp = EBlendOp::ADD;
	EColorMask ColorWriteMask = EColorMask::NONE;

	//VkPipelineColorBlendStateCreateFlags          flags;
	//VkBool32                                      logicOpEnable;
	//VkLogicOp                                     logicOp;
	//uint32_t                                      attachmentCount;
	//const VkPipelineColorBlendAttachmentState* pAttachments;
	//float                                         blendConstants[4];
};

struct jAttachment
{
	jAttachment() = default;
	jAttachment(const std::shared_ptr<jRenderTarget>& InRTPtr
		, EAttachmentLoadStoreOp InLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
		, EAttachmentLoadStoreOp InStencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE
		, Vector4 InClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f)
		, Vector2 InClearDepth = Vector2(1.0f, 0.0f))
		: RenderTargetPtr(InRTPtr), LoadStoreOp(InLoadStoreOp), StencilLoadStoreOp(InStencilLoadStoreOp), ClearColor(InClearColor), ClearDepth(InClearDepth)
	{}

	std::shared_ptr<jRenderTarget> RenderTargetPtr;

	// 아래 2가지 옵션은 렌더링 전, 후에 attachment에 있는 데이터에 무엇을 할지 결정하는 부분.
	// 1). loadOp
	//		- VK_ATTACHMENT_LOAD_OP_LOAD : attachment에 있는 내용을 그대로 유지
	//		- VK_ATTACHMENT_LOAD_OP_CLEAR : attachment에 있는 내용을 constant 모두 값으로 설정함.
	//		- VK_ATTACHMENT_LOAD_OP_DONT_CARE : attachment에 있는 내용에 대해 어떠한 것도 하지 않음. 정의되지 않은 상태.
	// 2). storeOp
	//		- VK_ATTACHMENT_STORE_OP_STORE : 그려진 내용이 메모리에 저장되고 추후에 읽어질 수 있음.
	//		- VK_ATTACHMENT_STORE_OP_DONT_CARE : 렌더링을 수행한 후에 framebuffer의 내용이 어떻게 될지 모름(정의되지 않음).
	EAttachmentLoadStoreOp LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE;
	EAttachmentLoadStoreOp StencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE;

	Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	Vector2 ClearDepth = Vector2(1.0f, 0.0f);

	size_t GetHash() const
	{
		size_t result = 0;
		result ^= CityHash64((const char*)&LoadStoreOp, sizeof(LoadStoreOp));
		result ^= CityHash64((const char*)&StencilLoadStoreOp, sizeof(StencilLoadStoreOp));
		result ^= CityHash64((const char*)&ClearColor, sizeof(ClearColor));
		result ^= CityHash64((const char*)&ClearDepth, sizeof(ClearDepth));
		return result;
	}
};

class jRenderPass
{
public:
	virtual ~jRenderPass() {}

	jRenderPass() = default;
	jRenderPass(const jAttachment* colorAttachment, const jAttachment* depthAttachment, const jAttachment* colorResolveAttachment, const Vector2i& offset, const Vector2i& extent)
	{
		SetAttachemnt(colorAttachment, depthAttachment, colorResolveAttachment);
		SetRenderArea(offset, extent);
	}

	void SetAttachemnt(const jAttachment* colorAttachment, const jAttachment* depthAttachment, const jAttachment* colorResolveAttachment)
	{
		ColorAttachments.push_back(colorAttachment);
		DepthAttachment = depthAttachment;
		ColorAttachmentResolve = colorResolveAttachment;
	}

	void SetRenderArea(const Vector2i& offset, const Vector2i& extent)
	{
		RenderOffset = offset;
		RenderExtent = extent;
	}

	virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) { return false; }
	virtual void EndRenderPass() {}

	virtual size_t GetHash() const;

	virtual void* GetRenderPass() const { return nullptr; }

	std::vector<const jAttachment*> ColorAttachments;
	const jAttachment* DepthAttachment = nullptr;
	const jAttachment* ColorAttachmentResolve = nullptr;
	Vector2i RenderOffset;
	Vector2i RenderExtent;
};


struct TBindings
{
	TBindings() = default;
	TBindings(int32 bindingPoint, EShaderAccessStageFlag accessStageFlags)
		: BindingPoint(bindingPoint), AccessStageFlags(accessStageFlags)
	{ }

	int32 BindingPoint = 0;
	EShaderAccessStageFlag AccessStageFlags = EShaderAccessStageFlag::ALL_GRAPHICS;
};

struct jTextureBindings
{
	std::shared_ptr<jTexture> texturePtr;
	std::shared_ptr<jSamplerStateInfo> samplerStatePtr;
};

struct jShaderBindingInstance
{
	virtual ~jShaderBindingInstance() {}

	const struct jShaderBindings* ShaderBindings = nullptr;
	std::vector<IUniformBufferBlock*> UniformBuffers;
	std::vector<jTextureBindings> Textures;

	virtual void UpdateShaderBindings() {}
	virtual void Bind(void* pipelineLayout, int32 InSlot = 0) const {}
};

struct jShaderBindings
{
	virtual ~jShaderBindings() {}

	virtual bool CreateDescriptorSetLayout() { return false; }
	virtual void CreatePool() {}

	std::vector<TBindings> UniformBuffers;
	std::vector<TBindings> Textures;

	virtual jShaderBindingInstance* CreateShaderBindingInstance() const { return nullptr; }
	virtual std::vector<jShaderBindingInstance*> CreateShaderBindingInstance(int32 count) const { return {}; }

	virtual size_t GetHash() const;

	FORCEINLINE static size_t CreateShaderBindingsHash(const std::vector<const jShaderBindings*>& shaderBindings)
	{
		size_t result = 0;
		for (int32 i = 0; i < shaderBindings.size(); ++i)
		{
			result ^= shaderBindings[i]->GetHash();
		}
		return result;
	}
};

class jRHI
{
public:
	jRHI();
	virtual ~jRHI() {}

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
	virtual void MapBufferdata(IBuffer* buffer) const;
	virtual void SetTextureFilter(ETextureType type, int32 sampleCount, ETextureFilterTarget target, ETextureFilter filter) const {}
	virtual void SetTextureWrap(int flag) const {}
	virtual void SetTexture(int32 index, const jTexture* texture) const {}
	virtual void DrawArrays(EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const {}
	virtual void DrawArraysInstanced(EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const {}
	virtual void DrawElements(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const {}
	virtual void DrawElementsInstanced(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const {}
	virtual void DrawElementsBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const {}
	virtual void DrawElementsInstancedBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const {}
	virtual void DispatchCompute(uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const {}
	virtual void EnableDepthBias(bool enable, EPolygonMode polygonMode = EPolygonMode::FILL) const {}
	virtual void SetDepthBias(float constant, float slope) const {}
	virtual void SetShader(const jShader* shader) const {}
	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) const { return nullptr; }
	virtual bool CreateShader(jShader* OutShader, const jShaderInfo& shaderInfo) const { return false;  }
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
	virtual void QueryTimeStamp(const jQueryTime* queryTimeStamp) const {}
	virtual bool IsQueryTimeStampResult(const jQueryTime* queryTimeStamp, bool isWaitUntilAvailable) const { return false; }
	virtual void GetQueryTimeStampResult(jQueryTime* queryTimeStamp) const {}
	virtual void BeginQueryTimeElapsed(const jQueryTime* queryTimeElpased) const {}
	virtual void EndQueryTimeElapsed(const jQueryTime* queryTimeElpased) const {}
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

	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const { return nullptr; }
	virtual jMultisampleStateInfo* CreateMultisampleState(const jMultisampleStateInfo& initializer) const { return nullptr; }
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const { return nullptr; }
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const { return nullptr; }
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const { return nullptr; }

	virtual jShaderBindings* CreateShaderBindings() const { check(0); return nullptr; }

	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindings*>& shaderBindings) const { return nullptr; }
	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances) const { return nullptr; }

	template <typename T>
	FORCEINLINE T* CreatePipelineLayout(const std::vector<const jShaderBindings*>& shaderBindings)
	{
		return (T*)CreatePipelineLayout(shaderBindings);
	}

	template <typename T>
	FORCEINLINE T* CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances)
	{
		return (T*)CreatePipelineLayout(shaderBindingInstances);
	}
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
struct jSamplerStateInfo
{
	virtual ~jSamplerStateInfo() {}
	virtual void Initialize() {}
	virtual void* GetHandle() const { return nullptr; }

	virtual size_t GetHash() const
	{
		size_t result = 0;
		hash_combine(result, Minification);
		hash_combine(result, Magnification);
		hash_combine(result, AddressU);
		hash_combine(result, AddressV);
		hash_combine(result, AddressW);
		hash_combine(result, MipLODBias);
		hash_combine(result, MaxAnisotropy);
		hash_combine(result, BorderColor.x);
		hash_combine(result, BorderColor.y);
		hash_combine(result, BorderColor.z);
		hash_combine(result, BorderColor.w);
		hash_combine(result, MinLOD);
		hash_combine(result, MaxLOD);
		return result;
	}

	ETextureFilter Minification = ETextureFilter::NEAREST;
	ETextureFilter Magnification = ETextureFilter::NEAREST;
	ETextureAddressMode AddressU = ETextureAddressMode::CLAMP_TO_EDGE;
	ETextureAddressMode AddressV = ETextureAddressMode::CLAMP_TO_EDGE;
	ETextureAddressMode AddressW = ETextureAddressMode::CLAMP_TO_EDGE;
	float MipLODBias = 0.0f;
	float MaxAnisotropy = 1.0f;			// if you anisotropy filtering tuned on, set this variable greater than 1.
	ETextureComparisonMode TextureComparisonMode = ETextureComparisonMode::NONE;
	ECompareOp ComparisonFunc = ECompareOp::LESS;
	Vector4 BorderColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	float MinLOD = -FLT_MAX;
	float MaxLOD = FLT_MAX;
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

template <EMSAASamples TSampleCount = EMSAASamples::COUNT_1, bool TSampleShadingEnable = true, float TMinSampleShading = 0.2f,
	bool TAlphaToCoverageEnable = false, bool TAlphaToOneEnable = false>
struct TMultisampleStateInfo
{
	FORCEINLINE static jMultisampleStateInfo* Create()
	{
		jMultisampleStateInfo initializer;
		initializer.SampleCount = TSampleCount;
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
	jStencilOpStateInfo* TFront = nullptr, jStencilOpStateInfo* TBack = nullptr,
	float TMinDepthBounds = 0.0f, float TMaxDepthBounds = 1.0f>
struct TDepthStencilStateInfo
{
	FORCEINLINE static jDepthStencilStateInfo* Create()
	{
		jDepthStencilStateInfo initializer;
		initializer.DepthTestEnable = TDepthTestEnable;
		initializer.DepthWriteEnable = TDepthWriteEnable;
		initializer.DepthCompareOp = TDepthCompareOp;
		initializer.DepthBoundsTestEnable = TDepthBoundsTestEnable;
		initializer.StencilTestEnable = TStencilTestEnable;
		initializer.Front = TFront;
		initializer.Back = TBack;
		initializer.MinDepthBounds = TMinDepthBounds;
		initializer.MaxDepthBounds = TMaxDepthBounds;
		return g_rhi->CreateDepthStencilState(initializer);
	}
};

template <bool TBlendEnable = false, EBlendFactor TSrc = EBlendFactor::SRC_COLOR, EBlendFactor TDest = EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp TBlendOp = EBlendOp::ADD,
	EBlendFactor TSrcAlpha = EBlendFactor::SRC_ALPHA, EBlendFactor TDestAlpha = EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp TAlphaBlendOp = EBlendOp::ADD,
	EColorMask TColorWriteMask = EColorMask::NONE>
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

