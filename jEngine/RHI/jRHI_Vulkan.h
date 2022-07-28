#pragma once

#if USE_VULKAN

#include "jRHI.h"
#include "Shader/jShader.h"

#define VALIDATION_LAYER_VERBOSE 0
#define MULTIPLE_FRAME 1

#if MULTIPLE_FRAME
//static constexpr int32_t MAX_FRAMES_IN_FLIGHT = 2;
#endif // MULTIPLE_FRAME

FORCEINLINE VkPrimitiveTopology GetVulkanPrimitiveTopology(EPrimitiveType type);

template <typename T>
class TResourcePool
{
public:
	template <typename TInitializer>
	T* GetOrCreate(const TInitializer& initializer)
	{
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
};

struct jSimpleVec2
{
	float x;
	float y;

	bool operator == (const jSimpleVec2& other) const
	{
		return ((x == other.x) && (y == other.y));
	}
};

struct jSimpleVec3
{
	float x;
	float y;
	float z;

	bool operator == (const jSimpleVec3& other) const
	{
		return ((x == other.x) && (y == other.y) && (z == other.z));
	}
};


struct jVertex
{
	jSimpleVec3 pos;
	jSimpleVec3 color;
	jSimpleVec2 texCoord;		// UV 는 좌상단이 (0, 0), 우하단이 (1, 1) 좌표임.

	bool operator == (const jVertex& other) const
	{
		return ((pos == other.pos) && (color == other.color) && (texCoord == other.texCoord));
	}

	static VkVertexInputBindingDescription GetBindingDescription()
	{
		VkVertexInputBindingDescription bindingDescription = {};
		// 모든 데이터가 하나의 배열에 있어서 binding index는 0
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(jVertex);

		// VK_VERTEX_INPUT_RATE_VERTEX : 각각 버택스 마다 다음 데이터로 이동
		// VK_VERTEX_INPUT_RATE_INSTANCE : 각각의 인스턴스 마다 다음 데이터로 이동
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescription;
	};

	static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions()
	{
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;

		//float: VK_FORMAT_R32_SFLOAT
		//vec2 : VK_FORMAT_R32G32_SFLOAT
		//vec3 : VK_FORMAT_R32G32B32_SFLOAT
		//vec4 : VK_FORMAT_R32G32B32A32_SFLOAT
		//ivec2: VK_FORMAT_R32G32_SINT, a 2-component vector of 32-bit signed integers
		//uvec4: VK_FORMAT_R32G32B32A32_UINT, a 4-component vector of 32-bit unsigned integers
		//double: VK_FORMAT_R64_SFLOAT, a double-precision (64-bit) float
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(jVertex, pos);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(jVertex, color);

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(jVertex, texCoord);

		return attributeDescriptions;
	}
};

// Alignment
// 1. 스칼라 타입은 4 bytes align 필요.				Scalars have to be aligned by N(= 4 bytes given 32 bit floats).
// 2. vec2 타입은 8 bytes align 필요.				A vec2 must be aligned by 2N(= 8 bytes)
// 3. vec3 or vec4 타입은 16 bytes align 필요.		A vec3 or vec4 must be aligned by 4N(= 16 bytes)
// 4. 중첩 구조(stuct 같은 것이 멤버인 경우)의 경우 멤버의 기본정렬을 16 bytes 배수로 올림하여 align 되어야 함. 
//													A nested structure must be aligned by the base alignment of its members rounded up to a multiple of 16.
//		예로 C++ 와 Shader 쪽에서 아래와 같이 선언 되어있다고 하자.
//		C++ : struct Foo { Vector2 v; }; struct jUniformBufferObject { Foo f1; Foo f2; };
//		Shader : struct Foo { vec2 v; }; layout(binding = 0) uniform jUniformBufferObject { Foo f1; Foo f2; };
//		Foo는 8 bytes 이지만 C++ 에서 아래와 같이 align을 16으로 맞춰줘야 한다. 이유는 중첩구조(struct 형태의 멤버변수)이기 때문.
//			-> 이렇게 해줘야 함. struct jUniformBufferObject { Foo f1; alignas(16) Foo f2; };
// 5. mat4 타입은 vec4 처럼 16 bytes align 필요.		A mat4 matrix must have the same alignment as a vec4.
// 자세한 설명 : https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/chap14.html#interfaces-resources-layout
// 아래와 같은 경우 맨 처음 등장하는 Vector2 때문에 뒤에나오는 Matrix 가 16 Bytes align 되지 못함.
//struct jUniformBufferObject		|		layout(binding = 0) uniform UniformBufferObject
//{									|		{
//	Vector2 foo;					|			vec2 foo;
//	Matrix Model;					|			mat4 model;
//	Matrix View;					|			mat4 view;
//	Matrix Proj;					|			mat4 proj
//};								|		};
// 이 경우 아래와 같이 alignas(16) 을 써줘서 model 부터 16 bytes align 하면 정상작동 할 수 있음.
//struct jUniformBufferObject		|		layout(binding = 0) uniform UniformBufferObject
//{									|		{
//	Vector2 foo;					|			vec2 foo;
//	alignas(16)Matrix Model;		|			mat4 model;
//	Matrix View;					|			mat4 view;
//	Matrix Proj;					|			mat4 proj
//};								|		};
struct jUniformBufferObject
{
	Matrix Model;
	Matrix View;
	Matrix Proj;
};

class jRenderPass_Vulkan : public jRenderPass
{
public:
	using jRenderPass::jRenderPass;

    bool CreateRenderPass();
    void Release();

	virtual void* GetRenderPass() const override { return RenderPass; }
	FORCEINLINE const VkRenderPass& GetRenderPassRaw() const { return RenderPass; }
	virtual void* GetFrameBuffer() const override { return FrameBuffer; }

	virtual bool BeginRenderPass(const jCommandBuffer* commandBuffer) override
	{
		if (!ensure(commandBuffer))
			return false;

		CommandBuffer = commandBuffer;

		check(FrameBuffer);
		RenderPassInfo.framebuffer = FrameBuffer;
	
        // 커맨드를 기록하는 명령어는 prefix로 모두 vkCmd 가 붙으며, 리턴값은 void 로 에러 핸들링은 따로 안함.
        // VK_SUBPASS_CONTENTS_INLINE : 렌더 패스 명령이 Primary 커맨드 버퍼에 포함되며, Secondary 커맨드 버퍼는 실행되지 않는다.
        // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : 렌더 패스 명령이 Secondary 커맨드 버퍼에서 실행된다.
        vkCmdBeginRenderPass((VkCommandBuffer)commandBuffer->GetHandle(), &RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		return true;
    }

	virtual void EndRenderPass() override
	{
		ensure(CommandBuffer);

        // Finishing up
        vkCmdEndRenderPass((VkCommandBuffer)CommandBuffer->GetHandle());
		CommandBuffer = nullptr;
	}

	virtual size_t GetHash() const override;

private:
	const jCommandBuffer* CommandBuffer = nullptr;

	VkRenderPassBeginInfo RenderPassInfo;
	std::vector<VkClearValue> ClearValues;
	VkRenderPass RenderPass = nullptr;
	VkFramebuffer FrameBuffer = nullptr;
};

struct jShaderBindingInstance_Vulkan : public jShaderBindingInstance
{
	VkDescriptorSet DescriptorSet = nullptr;

	virtual void UpdateShaderBindings() override;
	virtual void Bind(void* pipelineLayout, int32 InSlot = 0) const override;
};

struct jShaderBindings_Vulkan : public jShaderBindings
{
    VkDescriptorSetLayout DescriptorSetLayout = nullptr;
	VkPipelineLayout PipelineLayout = nullptr;

	// Descriptor : 쉐이더가 버퍼나 이미지 같은 리소스에 자유롭게 접근하는 방법. 디스크립터의 사용방법은 아래 3가지로 구성됨.
	//	1. Pipeline 생성 도중 Descriptor Set Layout 명세
	//	2. Descriptor Pool로 Descriptor Set 생성
	//	3. Descriptor Set을 렌더링 하는 동안 묶어 주기.
	//
	// Descriptor set layout	: 파이프라인을 통해 접근할 리소스 타입을 명세함
	// Descriptor set			: Descriptor 에 묶일 실제 버퍼나 이미지 리소스를 명세함.
	VkDescriptorPool DescriptorPool = nullptr;

	mutable std::vector<jShaderBindingInstance_Vulkan*> CreatedBindingInstances;

    virtual bool CreateDescriptorSetLayout() override;
	virtual void CreatePool() override;

	virtual jShaderBindingInstance* CreateShaderBindingInstance() const override;
	virtual std::vector<jShaderBindingInstance*> CreateShaderBindingInstance(int32 count) const override;

	virtual size_t GetHash() const override;

	std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizeArray(uint32 maxAllocations) const
	{
		std::vector<VkDescriptorPoolSize> resultArray;

		if (UniformBuffers.size() > 0)
		{
			VkDescriptorPoolSize poolSize;
			poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSize.descriptorCount = static_cast<uint32>(UniformBuffers.size()) * maxAllocations;
			resultArray.push_back(poolSize);
		}

		if (Textures.size() > 0)
		{
			VkDescriptorPoolSize poolSize;
			poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = static_cast<uint32>(Textures.size()) * maxAllocations;
			resultArray.push_back(poolSize);
		}

		return std::move(resultArray);
	}
};

struct jTexture_Vulkan : public jTexture
{
	jTexture_Vulkan() = default;
	jTexture_Vulkan(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight, bool InSRGB, int32 InLayerCount, int32 InSampleCount
		, VkImage InImage, VkImageView InImageView, VkDeviceMemory InImageMemory = nullptr, VkSampler InSamplerState = nullptr)
		: jTexture(InType, InFormat, InWidth, InHeight, InLayerCount, InSampleCount, InSRGB), Image(InImage)
		, ImageView(InImageView), ImageMemory(InImageMemory), SamplerState(InSamplerState)
	{}
    VkImage Image = nullptr;
    VkImageView ImageView = nullptr;
    VkDeviceMemory ImageMemory = nullptr;
	VkSampler SamplerState = nullptr;

	virtual const void* GetHandle() const override { return ImageView; }
	virtual const void* GetSamplerStateHandle() const override { return SamplerState; }

	static VkSampler CreateDefaultSamplerState();
};

class jCommandBuffer_Vulkan : public jCommandBuffer
{
public:
	virtual void* GetHandle() const override { return CommandBuffer; }
	FORCEINLINE VkCommandBuffer& GetRef() { return CommandBuffer; }

	virtual bool Begin() const override
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		// VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 커맨드가 한번 실행된다음에 다시 기록됨
		// VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : Single Render Pass 범위에 있는 Secondary Command Buffer.
		// VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 실행 대기중인 동안에 다시 서밋 될 수 있음.
		beginInfo.flags = 0;					// Optional

		// 이 플래그는 Secondary command buffer를 위해서만 사용하며, Primary command buffer 로 부터 상속받을 상태를 명시함.
		beginInfo.pInheritanceInfo = nullptr;	// Optional

		if (!ensure(vkBeginCommandBuffer(CommandBuffer, &beginInfo) == VK_SUCCESS))
			return false;
		
		return true;
	}
	virtual bool End() const override
	{
		if (!ensure(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS))
			return false;

		return true;
	}

	virtual void Reset() const override
	{
		vkResetCommandBuffer(CommandBuffer, 0);
	}

	VkFence Fence = nullptr;

private:
	VkCommandBuffer CommandBuffer = nullptr;
};

class jCommandBufferManager_Vulkan // base 없음
{
public:
	bool CreatePool(uint32 QueueIndex);
	void Release()
	{
		//// Command buffer pool 을 다시 만들기 보다 있는 커맨드 버퍼 풀을 cleanup 하고 재사용 함.
		//vkFreeCommandBuffers(device, CommandBufferManager.GetPool(), static_cast<uint32>(commandBuffers.size()), commandBuffers.data());
	}

	FORCEINLINE const VkCommandPool& GetPool() const { return CommandPool; };

	jCommandBuffer_Vulkan GetOrCreateCommandBuffer();
	void ReturnCommandBuffer(jCommandBuffer_Vulkan commandBuffer);

private:
	VkCommandPool CommandPool;		// 커맨드 버퍼를 저장할 메모리 관리자로 커맨드 버퍼를 생성함.
	std::vector<jCommandBuffer_Vulkan> UsingCommandBuffers;
	std::vector<jCommandBuffer_Vulkan> PendingCommandBuffers;
};

class jShaderBindingsManager_Vulkan // base 없음
{
public:
	VkDescriptorPool CreatePool(const jShaderBindings_Vulkan& bindings, uint32 MaxAllocations = 32) const;
	void Release(VkDescriptorPool pool) const;
};

struct jVertexStream_Vulkan
{
	jName Name;
	uint32 Count;
	EBufferType BufferType = EBufferType::STATIC;
	EBufferElementType ElementType = EBufferElementType::BYTE;
	bool Normalized = false;
	int32 Stride = 0;
	size_t Offset = 0;
	int32 InstanceDivisor = 0;

	VkBuffer VertexBuffer = nullptr;
	VkDeviceMemory VertexBufferMemory = nullptr;
};

class jFenceManager_Vulkan	// base 없음
{
public:
	VkFence GetOrCreateFence();
	void ReturnFence(VkFence fence)
	{
		UsingFences.erase(fence);
		PendingFences.insert(fence);
	}

	std::unordered_set<VkFence> UsingFences;
	std::unordered_set<VkFence> PendingFences;
};

struct jVertexBuffer_Vulkan : public jVertexBuffer
{
	struct jBindInfo
	{
		void Reset() 
		{
			InputBindingDescriptions.clear();
			AttributeDescriptions.clear();
			Buffers.clear();
			Offsets.clear();
		}
		std::vector<VkVertexInputBindingDescription> InputBindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> AttributeDescriptions;
		std::vector<VkBuffer> Buffers;
		std::vector<VkDeviceSize> Offsets;

		VkPipelineVertexInputStateCreateInfo CreateVertexInputState() const
		{
			// Vertex Input
			// 1). Bindings : 데이터 사이의 간격과 버택스당 or 인스턴스당(인스턴싱 사용시) 데이터인지 여부
			// 2). Attribute descriptions : 버택스 쉐이더 전달되는 attributes 의 타입. 그것을 로드할 바인딩과 오프셋
			VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			vertexInputInfo.vertexBindingDescriptionCount = (uint32)InputBindingDescriptions.size();
			vertexInputInfo.pVertexBindingDescriptions = &InputBindingDescriptions[0];
			vertexInputInfo.vertexAttributeDescriptionCount = (uint32)AttributeDescriptions.size();;
			vertexInputInfo.pVertexAttributeDescriptions = &AttributeDescriptions[0];

			return vertexInputInfo;
		}

		FORCEINLINE size_t GetHash() const
		{
			size_t result = 0;
			result = CityHash64((const char*)InputBindingDescriptions.data(), sizeof(VkVertexInputBindingDescription) * InputBindingDescriptions.size());
			result ^= CityHash64((const char*)AttributeDescriptions.data(), sizeof(VkVertexInputAttributeDescription) * AttributeDescriptions.size());
			return result;
		}
	};

	virtual size_t GetHash() const override
	{
		if (Hash)
			return Hash;
		
		Hash = GetVertexInputStateHash() ^ GetInputAssemblyStateHash();
		return Hash;
	}

	FORCEINLINE size_t GetVertexInputStateHash() const
	{
		return BindInfos.GetHash();
	}

	FORCEINLINE size_t GetInputAssemblyStateHash() const
	{
		VkPipelineInputAssemblyStateCreateInfo state = CreateInputAssemblyState();
		return CityHash64((const char*)&state, sizeof(VkPipelineInputAssemblyStateCreateInfo));
	}

	FORCEINLINE VkPipelineVertexInputStateCreateInfo CreateVertexInputState() const
	{
		return BindInfos.CreateVertexInputState();
	}

	FORCEINLINE VkPipelineInputAssemblyStateCreateInfo CreateInputAssemblyState() const
	{
		check(VertexStreamData);

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = GetVulkanPrimitiveTopology(VertexStreamData->PrimitiveType);

		// primitiveRestartEnable 옵션이 VK_TRUE 이면, 인덱스버퍼의 특수한 index 0xFFFF or 0xFFFFFFFF 를 사용해서 line 과 triangle topology mode를 사용할 수 있다.
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		return inputAssembly;
	}

	jBindInfo BindInfos;
	std::vector<jVertexStream_Vulkan> Streams;
	mutable size_t Hash = 0;

	virtual void Bind(const jShader* shader) const override
	{
	}

	virtual void Bind() const override;

};

struct jIndexBuffer_Vulkan : public jIndexBuffer
{
	VkBuffer IndexBuffer = nullptr;
	VkDeviceMemory IndexBufferMemory = nullptr;
	
	virtual void Bind(const jShader* shader) const override {}
	virtual void Bind() const override;

	FORCEINLINE uint32 GetIndexCount() const
	{
		return IndexStreamData->ElementCount;
	}
};

struct jSamplerStateInfo_Vulkan : public jSamplerStateInfo
{
	jSamplerStateInfo_Vulkan() = default;
	jSamplerStateInfo_Vulkan(const jSamplerStateInfo& state) : jSamplerStateInfo(state) {}
	virtual ~jSamplerStateInfo_Vulkan() {}
	virtual void Initialize() override;

	virtual void* GetHandle() const { return SamplerState; }

	VkSamplerCreateInfo SamplerStateInfo = {};
	VkSampler SamplerState = nullptr;
};

struct jRasterizationStateInfo_Vulkan : public jRasterizationStateInfo
{
	jRasterizationStateInfo_Vulkan() = default;
	jRasterizationStateInfo_Vulkan(const jRasterizationStateInfo& state) : jRasterizationStateInfo(state) {}
	virtual ~jRasterizationStateInfo_Vulkan() {}
	virtual void Initialize() override;

	VkPipelineRasterizationStateCreateInfo RasterizationStateInfo = {};
};

struct jMultisampleStateInfo_Vulkan : public jMultisampleStateInfo
{
	jMultisampleStateInfo_Vulkan() = default;
	jMultisampleStateInfo_Vulkan(const jMultisampleStateInfo& state) : jMultisampleStateInfo(state) {}
	virtual ~jMultisampleStateInfo_Vulkan() {}
	virtual void Initialize() override;

	VkPipelineMultisampleStateCreateInfo MultisampleStateInfo = {};
};

struct jStencilOpStateInfo_Vulkan : public jStencilOpStateInfo
{
	jStencilOpStateInfo_Vulkan() = default;
	jStencilOpStateInfo_Vulkan(const jStencilOpStateInfo& state) : jStencilOpStateInfo(state) {}
	virtual ~jStencilOpStateInfo_Vulkan() {}
	virtual void Initialize() override;

	VkStencilOpState StencilOpStateInfo = {};
};

struct jDepthStencilStateInfo_Vulkan : public jDepthStencilStateInfo
{
	jDepthStencilStateInfo_Vulkan() = default;
	jDepthStencilStateInfo_Vulkan(const jDepthStencilStateInfo& state) : jDepthStencilStateInfo(state) {}
	virtual ~jDepthStencilStateInfo_Vulkan() {}
	virtual void Initialize() override;

	VkPipelineDepthStencilStateCreateInfo DepthStencilStateInfo = {};
};

struct jBlendingStateInfo_Vulakn : public jBlendingStateInfo
{
	jBlendingStateInfo_Vulakn() = default;
	jBlendingStateInfo_Vulakn(const jBlendingStateInfo& state) : jBlendingStateInfo(state) {}
	virtual ~jBlendingStateInfo_Vulakn() {}
	virtual void Initialize() override;

	VkPipelineColorBlendAttachmentState ColorBlendAttachmentInfo = {};
};

struct jPipelineStateFixedInfo
{
	jPipelineStateFixedInfo() = default;
	jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jMultisampleStateInfo* multisampleState, jDepthStencilStateInfo* depthStencilState
		, jBlendingStateInfo* blendingState, const std::vector<jViewport>& viewports, const std::vector<jScissor>& scissors)
		: RasterizationState(rasterizationState), MultisampleState(multisampleState), DepthStencilState(depthStencilState)
		, BlendingState(blendingState), Viewports(Viewports), Scissors(scissors)
	{}
	jPipelineStateFixedInfo(jRasterizationStateInfo* rasterizationState, jMultisampleStateInfo* multisampleState, jDepthStencilStateInfo* depthStencilState
		, jBlendingStateInfo* blendingState, const jViewport& viewport, const jScissor& scissor)
		: RasterizationState(rasterizationState), MultisampleState(multisampleState), DepthStencilState(depthStencilState)
		, BlendingState(blendingState), Viewports({ viewport }), Scissors({ scissor })
	{}

	size_t CreateHash() const
	{
		if (Hash)
			return Hash;

		Hash = 0;
		for (int32 i = 0; i < Viewports.size(); ++i)
			Hash ^= Viewports[i].GetHash();

		for (int32 i = 0; i < Scissors.size(); ++i)
			Hash ^= Scissors[i].GetHash();

		// 아래 내용들도 해시를 만들 수 있어야 함, todo
		Hash ^= RasterizationState->GetHash();
		Hash ^= MultisampleState->GetHash();
		Hash ^= DepthStencilState->GetHash();
		Hash ^= BlendingState->GetHash();

		return Hash;
	}

	std::vector<jViewport> Viewports;
	std::vector<jScissor> Scissors;

	jRasterizationStateInfo* RasterizationState = nullptr;
	jMultisampleStateInfo* MultisampleState = nullptr;
	jDepthStencilStateInfo* DepthStencilState = nullptr;
	jBlendingStateInfo* BlendingState = nullptr;

	mutable size_t Hash = 0;
};

struct jPipelineStateInfo
{
	jPipelineStateInfo() = default;
	jPipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader, const jVertexBuffer* vertexBuffer, const jRenderPass* renderPass, const std::vector<const jShaderBindings*> shaderBindings)
		: PipelineStateFixed(pipelineStateFixed), Shader(shader), VertexBuffer(vertexBuffer), RenderPass(renderPass), ShaderBindings(shaderBindings)
	{}

	FORCEINLINE size_t GetHash() const
	{
		if (Hash)
			return Hash;

		check(PipelineStateFixed);
		Hash = PipelineStateFixed->CreateHash();

		Hash ^= Shader->ShaderInfo.GetHash();
		Hash ^= VertexBuffer->GetHash();
		Hash ^= jShaderBindings::CreateShaderBindingsHash(ShaderBindings);
		Hash ^= RenderPass->GetHash();

		return Hash;
	}

	mutable size_t Hash = 0;

	const jShader* Shader = nullptr;
	const jVertexBuffer* VertexBuffer = nullptr;
	const jRenderPass* RenderPass = nullptr;
	std::vector<const jShaderBindings*> ShaderBindings;
	const jPipelineStateFixedInfo* PipelineStateFixed = nullptr;

	VkPipeline vkPipeline = nullptr;
	VkPipelineLayout vkPipelineLayout = nullptr;

	VkPipeline CreateGraphicsPipelineState();
	void Bind();

	static std::unordered_map<size_t, jPipelineStateInfo> PipelineStatePool;
};

struct jUniformBufferBlock_Vulkan : public IUniformBufferBlock
{
	using IUniformBufferBlock::IUniformBufferBlock;
	using IUniformBufferBlock::UpdateBufferData;
	virtual ~jUniformBufferBlock_Vulkan()
	{
		Destroy();
	}

	void Destroy();

	virtual void Init() override;
	virtual void UpdateBufferData(const void* InData, size_t InSize) override;

	virtual void ClearBuffer(int32 clearValue) override;

	virtual void* GetBuffer() const override { return Bufffer; }
	virtual void* GetBufferMemory() const override { return BufferMemory; }

	VkBuffer Bufffer = nullptr;
	VkDeviceMemory BufferMemory = nullptr;
};

struct jQueryPool_Vulkan : public jQueryPool
{
    virtual ~jQueryPool_Vulkan() {}

	virtual bool Create() override;
    virtual void ResetQueryPool(jCommandBuffer* pCommanBuffer = nullptr);

    VkQueryPool vkQueryPool = nullptr;
	int32 QueryIndex[jRHI::MaxWaitingQuerySet] = { 0, };
};

// todo
class jRHI_Vulkan : public jRHI
{
public:
	jRHI_Vulkan();
	virtual ~jRHI_Vulkan();

	GLFWwindow* window = nullptr;
	bool framebufferResized = false;

	VkInstance instance;		// Instance는 Application과 Vulkan Library를 연결시켜줌, 드라이버에 어플리케이션 정보를 전달하기도 한다.

	// What validation layers do?
	// 1. 명세와는 다른 값의 파라메터를 사용하는 것을 감지
	// 2. 오브젝트들의 생성과 소멸을 추적하여 리소스 Leak을 감지
	// 3. Calls을 호출한 스레드를 추적하여, 스레드 안정성을 체크
	// 4. 모든 calls를 로깅하고, 그의 파라메터를 standard output으로 보냄
	// 5. 프로파일링과 리플레잉을 위해서 Vulkan calls를 추적
	//
	// Validation layer for LunarG
	// 1. Instance specific : instance 같은 global vulkan object와 관련된 calls 를 체크
	// 2. Device specific(deprecated) : 특정 GPU Device와 관련된 calls 를 체크.
	VkDebugUtilsMessengerEXT debugMessenger;

	// Surface
	// VkInstance를 만들고 바로 Surface를 만들어야 한다. 물리 디바이스 선택에 영향을 미치기 때문
	// Window에 렌더링할 필요 없으면 그냥 만들지 않고 Offscreen rendering만 해도 됨. (OpenGL은 보이지 않는 창을 만들어야만 함)
	// 플랫폼 독립적인 구조이지만 Window의 Surface와 연결하려면 HWND or HMODULE 등을 사용해야 하므로 VK_KHR_win32_surface Extension을 사용해서 처리함.
	VkSurfaceKHR surface;

	// 물리 디바이스 - 물리 그래픽 카드를 선택
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	// Queue Families
	// 여러종류의 Queue type이 있을 수 있다. (ex. Compute or memory transfer related commands 만 만듬)
	// - 논리 디바이스(VkDevice) 생성시에 함께 생성시킴
	// - 논리 디바이스가 소멸될때 함께 알아서 소멸됨, 그래서 Cleanup 해줄필요가 없음.
	struct jQueue_Vulkan // base 없음
	{
		uint32 QueueIndex = 0;
		VkQueue Queue = nullptr;
	};
	jQueue_Vulkan GraphicsQueue;
	jQueue_Vulkan PresentQueue;

	// 논리 디바이스 생성
	VkDevice device;

	// Swapchain
	VkSwapchainKHR swapChain;
	std::vector<VkImage> swapChainImages;	// 스왑체인 이미지는 swapchain이 destroyed 될때 자동으로 cleanup 됨
	VkFormat swapChainImageFormat;
	VkExtent2D swapChainExtent;
	std::vector<VkImageView> swapChainImageViews;
	std::vector<VkFence> swapChainCommandBufferFences;

	// GraphicsPipieline
	//VkRenderPass renderPass;
	//VkDescriptorSetLayout descriptorSetLayout;
	//VkPipelineLayout pipelineLayout;
	std::vector<VkPipeline> graphicsPipelines;

	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
	VkImageView  colorImageView;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;

	// Framebuffers
	//std::vector<VkFramebuffer> swapChainFramebuffers;

	//////////////////////////////////////////////////////////////////////////
	// 현재 씬을 렌더링하기 위한 자료 구조들 모음, 일반화 되어야 함.
	// 그냥 일반적인 텍스쳐 로드 하는데 필요한 자료 구조임. 정리 예정
	//VkImage textureImage = nullptr;
	//VkImageView textureImageView = nullptr;
	//VkDeviceMemory textureImageMemory = nullptr;
	//VkSampler textureSampler = nullptr;

	// 그냥 일반적인 모델 로드에 필요한 자료 구조임. 정리 예정
	//std::vector<jVertex> vertices;
	//std::vector<uint32> indices;
	//VkBuffer vertexBuffer;
	//VkDeviceMemory vertexBufferMemory;
	//VkBuffer indexBuffer;
	//VkDeviceMemory indexBufferMemory;

	// 그냥 일반적인 
	//std::vector<VkBuffer> uniformBuffers;
	//std::vector<VkDeviceMemory> uniformBuffersMemory;
	//////////////////////////////////////////////////////////////////////////

	// Descriptor : 쉐이더가 버퍼나 이미지 같은 리소스에 자유롭게 접근하는 방법. 디스크립터의 사용방법은 아래 3가지로 구성됨.
	//	1. Pipeline 생성 도중 Descriptor Set Layout 명세
	//	2. Descriptor Pool로 Descriptor Set 생성
	//	3. Descriptor Set을 렌더링 하는 동안 묶어 주기.
	//
	// Descriptor set layout	: 파이프라인을 통해 접근할 리소스 타입을 명세함
	// Descriptor set			: Descriptor 에 묶일 실제 버퍼나 이미지 리소스를 명세함.
	//VkDescriptorPool descriptorPool;
	//std::vector<VkDescriptorSet> descriptorSets;		// DescriptorPool 이 소멸될때 자동으로 소멸되므로 따로 소멸시킬 필요없음.

	// Command buffers
	//VkCommandPool commandPool;		// 커맨드 버퍼를 저장할 메모리 관리자로 커맨드 버퍼를 생성함.
	// std::vector<jCommandBuffer_Vulkan> commandBuffers;
	jCommandBufferManager_Vulkan CommandBuffersManager;

	// Semaphores
#if MULTIPLE_FRAME
	// Semaphore 들은 GPU - GPU 간의 동기화를 맞춰줌. 여러개의 프레임이 동시에 만들어질 수 있게 함.
	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	// CPU - GPU 간 동기화를 위해서 Fence를 사용함. MAX_FRAMES_IN_FLIGHT 보다 더 많은 수의 frame을 만들려고 시도하지 않게 해줌
	std::vector<VkFence> inFlightFences;
	std::vector<VkFence> imagesInFlight;		// 스왑체인 개수보다 MAX_FRAMES_IN_FLIGHT 가 더 많은 경우를 위해서 추가한 펜스
	size_t currenFrame = 0;
#else
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;
#endif // MULTIPLE_FRAME
	//////////////////////////////////////////////////////////////////////////
	bool enableValidationLayers = true;

	const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation"
		//, "VK_LAYER_LUNARG_api_dump"		// display api call
	};

	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	std::vector<const char*> GetRequiredExtensions()
	{
		uint32 glfwExtensionCount = 0;
		const char** glfwExtensions = nullptr;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (enableValidationLayers)
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);	// VK_EXT_debug_utils 임

		return extensions;
	}

	bool CheckValidationLayerSupport()
	{
		uint32 layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : validationLayers)
		{
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers)
			{
				if (!strcmp(layerName, layerProperties.layerName))
				{
					layerFound = true;
					break;
				}
			}

			if (!layerFound)
				return false;
		}

		return true;
	}

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        // messageSeverity
        // 1. VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT : 진단 메시지
        // 2. VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT : 리소스 생성 정보
        // 3. VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT : 에러일 필요 없는 정보지만 버그를 낼수 있는것
        // 4. VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT : Invalid 되었거나 크래시 날수 있는 경우
        if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
        }

        // messageType
        // VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT : 사양이나 성능과 관련되지 않은 이벤트 발생
        // VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT : 사양 위반이나 발생 가능한 실수가 발생
        // VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT : 불칸을 잠재적으로 최적화 되게 사용하지 않은 경우

        // pCallbackData 의 중요 데이터 3종
        // pMessage : 디버그 메시지 문장열(null 로 끝남)
        // pObjects : 이 메시지와 연관된 불칸 오브젝트 핸들 배열
        // objectCount : 배열에 있는 오브젝트들의 개수

        // pUserData
        // callback 설정시에 전달한 포인터 데이터

        std::cerr << "validation layer : " << pCallbackData->pMessage << std::endl;

        // VK_TRUE 리턴시 VK_ERROR_VALIDATION_FAILED_EXT 와 validation layer message 가 중단된다.
        // 이것은 보통 validation layer 를 위해 사용하므로, 사용자는 항상 VK_FALSE 사용.
        return VK_FALSE;
    }


	void PopulateDebutMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
	{
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
#if VALIDATION_LAYER_VERBOSE
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
#endif
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback;
	}

	VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo
		, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
	{
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (func)
			return func(instance, pCreateInfo, pAllocator, pDebugMessenger);

		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	struct QueueFamilyIndices
	{
		std::optional<uint32> graphicsFamily;
		std::optional<uint32> presentFamily;

		bool IsComplete()
		{
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device)
	{
		QueueFamilyIndices indices;

		uint32 queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				VkBool32 presentSupport = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
				if (presentSupport)
				{
					indices.presentFamily = i;
					indices.graphicsFamily = i;
				}
				break;
			}
			++i;
		}

		return indices;
	}

	bool CheckDeviceExtensionSupport(VkPhysicalDevice device)
	{
		uint32 extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
		for (const auto& extension : availableExtensions)
			requiredExtensions.erase(extension.extensionName);

		return requiredExtensions.empty();
	}

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device)
	{
		SwapChainSupportDetails details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

		uint32 formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
		if (formatCount != 0)
		{
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
		}

		uint32 presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
		if (presentModeCount != 0)
		{
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}

	bool IsDeviceSuitable(VkPhysicalDevice device)
	{
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures = {};
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		// 현재는 Geometry Shader 지원 여부만 판단
		if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || !deviceFeatures.geometryShader)
			return false;

		QueueFamilyIndices indices = findQueueFamilies(device);
		bool extensionsSupported = CheckDeviceExtensionSupport(device);

		bool swapChainAdequate = false;
		if (extensionsSupported)
		{
			SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
			swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		return indices.IsComplete() && extensionsSupported && swapChainAdequate && deviceFeatures.samplerAnisotropy;
	}

	VkSampleCountFlagBits GetMaxUsableSampleCount()
	{
		VkPhysicalDeviceProperties physicalDeviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

		VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
		if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
		if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
		if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
		if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
		if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
		if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

		return VK_SAMPLE_COUNT_1_BIT;
	}
	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
	{
		check(availableFormats.size() > 0);
		for (const auto& availableFormat : availableFormats)
		{
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM
				&& availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return availableFormat;
			}
		}
		return availableFormats[0];
	}
	VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
	{
		// 1. VK_PRESENT_MODE_IMMEDIATE_KHR : 어플리케이션에 의해 제출된 이미지가 즉시 전송되며, 찢어짐이 있을 수도 있음.
		// 2. VK_PRESENT_MODE_FIFO_KHR : 디스플레이가 갱신될 때(Vertical Blank 라 부름), 디스플레이가 이미지를 스왑체인큐 앞쪽에서 가져간다.
		//								그리고 프로그램은 그려진 이미지를 큐의 뒤쪽에 채워넣는다.
		//								만약 큐가 가득차있다면 프로그램은 기다려야만 하며, 이것은 Vertical Sync와 유사하다.
		// 3. VK_PRESENT_MODE_FIFO_RELAXED_KHR : 이 것은 VK_PRESENT_MODE_FIFO_KHR와 한가지만 다른데 그것은 아래와 같음.
		//								만약 어플리케이션이 늦어서 마지막 Vertical Blank에 큐가 비어버렸다면, 다음 Vertical Blank를
		//								기다리지 않고 이미지가 도착했을때 즉시 전송한다. 이것 때문에 찢어짐이 나타날 수 있다.
		// 4. VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR의 또다른 변종인데, 큐가 가득차서 어플리케이션이 블록되야 하는경우
		//								블록하는 대신에 이미 큐에 들어가있는 이미지를 새로운 것으로 교체해버린다. 이것은 트리플버퍼링을
		//								구현하는데 사용될 수 있다. 트리플버퍼링은 더블 버퍼링에 Vertical Sync를 사용하는 경우 발생하는 
		//								대기시간(latency) 문제가 현저하게 줄일 수 있다.

		for (const auto& availablePresentMode : availablePresentModes)
		{
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
				return availablePresentMode;
		}

		return VK_PRESENT_MODE_FIFO_KHR;
	}
	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
	{
		// currentExtent == UINT32_MAX 면, 창의 너비를 minImageExtent와 maxImageExtent 사이에 적절한 사이즈를 선택할 수 있음.
		// currentExtent != UINT32_MAX 면, 윈도우 사이즈와 currentExtent 사이즈가 같음.
		if (capabilities.currentExtent.width != UINT32_MAX)
			return capabilities.currentExtent;

		//VkExtent2D actualExtent = { WIDTH, HEIGHT };

		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		VkExtent2D actualExtent = { static_cast<uint32>(width), static_cast<uint32>(height) };

		actualExtent.width = std::max<uint32>(capabilities.minImageExtent.width, std::min<uint32>(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max<uint32>(capabilities.minImageExtent.height, std::min<uint32>(capabilities.maxImageExtent.height, actualExtent.height));
		return actualExtent;
	}
	VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels) const
	{
		VkImageViewCreateInfo viewInfo = {};

		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;

		// SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
		viewInfo.subresourceRange.aspectMask = aspectMask;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		// RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
		// 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
		// 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		VkImageView imageView;
		ensure(vkCreateImageView(device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

		return imageView;
	}
	VkImageView CreateImage2DArrayView(VkImage image, uint32 layerCount, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels) const
	{
		VkImageViewCreateInfo viewInfo = {};

		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = format;

		// SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
		viewInfo.subresourceRange.aspectMask = aspectMask;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = layerCount;

		// RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
		// 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
		// 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		VkImageView imageView;
		ensure(vkCreateImageView(device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

		return imageView;
	}
	VkImageView CreateImageCubeView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, uint32 mipLevels) const
	{
		VkImageViewCreateInfo viewInfo = {};

		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
		viewInfo.format = format;

		// SubresourceRange에 이미지의 목적과 이미지의 어떤 파트에 접근할 것인지를 명세한다.
		viewInfo.subresourceRange.aspectMask = aspectMask;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;

		// RGBA 컴포넌트들에게 컬러 채널을 매핑할 수 있다.
		// 예를들어 VK_COMPONENT_SWIZZLE_R 을 모든 채널에 넣으면 R을 사용한 흑백 텍스쳐를 나타낼 수 있음.
		// 현재는 기본인 VK_COMPONENT_SWIZZLE_IDENTITY 를 설정해준다.
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		VkImageView imageView;
		ensure(vkCreateImageView(device, &viewInfo, nullptr, &imageView) == VK_SUCCESS);

		return imageView;
	}
	VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
		for (VkFormat format : candidates)
		{
			VkFormatProperties props;
			// props.linearTilingFeatures : Linear tiling 지원여부
			// props.optimalTilingFeatures : Optimal tiling 지원여부
			// props.bufferFeatures : 버퍼를 지원하는 경우
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
				return format;
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
				return format;
		}
		check(0);
		return VK_FORMAT_UNDEFINED;
	}
	VkFormat FindDepthFormat()
	{
		// VK_FORMAT_D32_SFLOAT : 32bit signed float for depth
		// VK_FORMAT_D32_SFLOAT_S8_UINT : 32bit signed float depth, 8bit stencil
		// VK_FORMAT_D24_UNORM_S8_UINT : 24bit float for depth, 8bit stencil

		return FindSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }
		, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}
	VkShaderModule CreateShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();

		// pCode 가 uint32* 형이라서 4 byte aligned 된 메모리를 넘겨줘야 함.
		// 다행히 std::vector의 default allocator가 가 메모리 할당시 4 byte aligned 을 이미 하고있어서 그대로 씀.
		createInfo.pCode = reinterpret_cast<const uint32*>(code.data());

		VkShaderModule shaderModule = {};
		ensure(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

		// compiling or linking 과정이 graphics pipeline 이 생성되기 전까지 처리되지 않는다.
		// 그래픽스 파이프라인이 생성된 후 VkShaderModule은 즉시 소멸 가능.
		return shaderModule;
	}
	uint32 FindMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) const
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (uint32 i = 0; i < memProperties.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
				return i;
		}

		check(0);	// failed to find sutable memory type!
		return 0;
	}
	FORCEINLINE bool CreateImage(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
		, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) const
	{
		return CreateImage2DArray(width, height, 1, mipLevels, numSamples, format, tiling, usage, properties, image, imageMemory);
	}
	FORCEINLINE bool CreateImage2DArray(uint32 width, uint32 height, uint32 arrayLayers, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
		, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) const
	{
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = arrayLayers;
		imageInfo.format = format;

		// VK_IMAGE_TILING_LINEAR : 텍셀이 Row-major 순서로 저장. pixels array 처럼.
		// VK_IMAGE_TILING_OPTIMAL : 텍셀이 최적의 접근을 위한 순서로 저장
		// image의 memory 안에 있는 texel에 직접 접근해야한다면, VK_IMAGE_TILING_LINEAR 를 써야함.
		// 그러나 지금 staging image가 아닌 staging buffer를 사용하기 때문에 VK_IMAGE_TILING_OPTIMAL 를 쓰면됨.
		imageInfo.tiling = tiling;

		// VK_IMAGE_LAYOUT_UNDEFINED : GPU에 의해 사용될 수 없으며, 첫번째 transition 에서 픽셀들을 버릴 것임.
		// VK_IMAGE_LAYOUT_PREINITIALIZED : GPU에 의해 사용될 수 없으며, 첫번째 transition 에서 픽셀들이 보존 될것임.
		// VK_IMAGE_LAYOUT_GENERAL : 성능은 좋지 않지만 image를 input / output 둘다 의 용도로 사용하는 경우 씀.
		// 첫번째 전환에서 텍셀이 보존되어야 하는 경우는 거의 없음.
		//	-> 이런 경우는 image를 staging image로 하고 VK_IMAGE_TILING_LINEAR를 쓴 경우이며, 이때는 Texel 데이터를
		//		image에 업로드하고, image를 transfer source로 transition 하는 경우가 됨.
		// 하지만 지금의 경우는 첫번째 transition에서 image는 transfer destination 이 된다. 그래서 기존 데이터를 유지
		// 할 필요가 없다 그래서 VK_IMAGE_LAYOUT_UNDEFINED 로 설정한다.
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;

		imageInfo.samples = numSamples;
		imageInfo.flags = 0;		// Optional
									// Sparse image 에 대한 정보를 설정가능
									// Sparse image는 특정 영역의 정보를 메모리에 담아두는 것임. 예를들어 3D image의 경우
									// 복셀영역의 air 부분의 표현을 위해 많은 메모리를 할당하는것을 피하게 해줌.

		if (!ensure(vkCreateImage(device, &imageInfo, nullptr, &image) == VK_SUCCESS))
			return false;

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);
		if (!ensure(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) == VK_SUCCESS))
			return false;

		vkBindImageMemory(device, image, imageMemory, 0);

		return true;
	}
	FORCEINLINE bool CreateImageCube(uint32 width, uint32 height, uint32 mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage
		, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) const
	{
		return CreateImage2DArray(width, height, 6, mipLevels, numSamples, format, tiling, usage, properties, image, imageMemory);

	}
	VkCommandBuffer BeginSingleTimeCommands() const
	{
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = CommandBufferManager.GetPool();
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);
		return commandBuffer;
	}

	void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
	{
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(GraphicsQueue.Queue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(GraphicsQueue.Queue);

		// 명령 완료를 기다리기 위해서 2가지 방법이 있는데, Fence를 사용하는 방법(vkWaitForFences)과 Queue가 Idle이 될때(vkQueueWaitIdle)를 기다리는 방법이 있음.
		// fence를 사용하는 방법이 여러개의 전송을 동시에 하고 마치는 것을 기다릴 수 있게 해주기 때문에 그것을 사용함.
		vkFreeCommandBuffers(device, CommandBufferManager.GetPool(), 1, &commandBuffer);
	}
	bool HasStencilComponent(VkFormat format) const
	{
		return ((format == VK_FORMAT_D32_SFLOAT_S8_UINT) || (format == VK_FORMAT_D24_UNORM_S8_UINT));
	}
	bool TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32 mipLevels) const
	{
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		// Layout Transition 에는 image memory barrier 사용
		// Pipeline barrier는 리소스들 간의 synchronize 를 맞추기 위해 사용 (버퍼를 읽기전에 쓰기가 완료되는 것을 보장받기 위해)
		// Pipeline barrier는 image layout 간의 전환과 VK_SHARING_MODE_EXCLUSIVE를 사용한 queue family ownership을 전달하는데에도 사용됨

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;		// 현재 image 내용이 존재하던말던 상관없다면 VK_IMAGE_LAYOUT_UNDEFINED 로 하면 됨
		barrier.newLayout = newLayout;

		// 아래 두필드는 Barrier를 사용해 Queue family ownership을 전달하는 경우에 사용됨.
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		barrier.image = image;

		// subresourcerange 는 image에서 영향을 받는 것과 부분을 명세함.
		// mimapping 이 없으므로 levelCount와 layercount 를 1로
		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (HasStencilComponent(format))
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		else
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;	// TODO
		barrier.dstAccessMask = 0;	// TODO

		// Barrier 는 동기화를 목적으로 사용하므로, 이 리소스와 연관되는 어떤 종류의 명령이 이전에 일어나야 하는지와
		// 어떤 종류의 명령이 Barrier를 기다려야 하는지를 명시해야만 한다. vkQueueWaitIdle 을 사용하지만 그래도 수동으로 해줘야 함.

		// Undefined -> transfer destination : 이 경우 기다릴 필요없이 바로 쓰면됨. Undefined 라 다른 곳에서 딱히 쓰거나 하는것이 없음.
		// Transfer destination -> frag shader reading : frag 쉐이더에서 읽기 전에 transfer destination 에서 쓰기가 완료 됨이 보장되어야 함. 그래서 shader 에서 읽기 가능.
		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;
		if ((oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) && (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			// 이전 작업을 기다릴 필요가 없어서 srcAccessMask에 0, sourceStage 에 가능한 pipeline stage의 가장 빠른 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;	// VK_ACCESS_TRANSFER_WRITE_BIT 는 Graphics 나 Compute stage에 실제 stage가 아닌 pseudo-stage 임.

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if ((oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) && (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

			// 둘중 가장 빠른 stage 인 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 를 선택
			// VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 에서 depth read 가 일어날 수 있음.
			// VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT 에서 depth write 가 일어날 수 있음.
			destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		else
		{
			check(!"Unsupported layout transition!");
			return false;
		}

		// 현재 싱글 커맨드버퍼 서브미션은 암시적으로 VK_ACCESS_HOST_WRITE_BIT 동기화를 함.

		// 모든 종류의 Pipeline barrier 가 같은 함수로 submit 함.
		vkCmdPipelineBarrier(commandBuffer
			, sourceStage		// 	- 이 barrier 를 기다릴 pipeline stage. 
								//		만약 barrier 이후 uniform 을 읽을 경우 VK_ACCESS_UNIFORM_READ_BIT 과 
								//		파이프라인 스테이지에서 유니폼 버퍼를 읽을 가장 빠른 쉐이더 지정
								//		(예를들면, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT - 이 barrier가 uniform을 수정했고, Fragment shader에서 uniform을 처음 읽는거라면)
			, destinationStage	// 	- 0 or VK_DEPENDENCY_BY_REGION_BIT(지금까지 쓰여진 리소스 부분을 읽기 시작할 수 있도록 함)
			, 0
			// 아래 3가지 부분은 이번에 사용할 memory, buffer, image  barrier 의 개수가 배열을 중 하나를 명시
			, 0, nullptr
			, 0, nullptr
			, 1, &barrier
		);

		EndSingleTimeCommands(commandBuffer);

		return true;
	}
	size_t CreateBuffer(VkBufferUsageFlags InUsage, VkMemoryPropertyFlags InProperties, VkDeviceSize InSize, VkBuffer& OutBuffer, VkDeviceMemory& OutBufferMemory) const
	{
		check(InSize);
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = InSize;

		// OR 비트연산자를 사용해 다양한 버퍼용도로 사용할 수 있음.
		bufferInfo.usage = InUsage;

		// swapchain과 마찬가지로 버퍼또한 특정 queue family가 소유하거나 혹은 여러 Queue에서 공유됨
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (!ensure(vkCreateBuffer(device, &bufferInfo, nullptr, &OutBuffer) == VK_SUCCESS))
			return 0;

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, OutBuffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits
			, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (!ensure(vkAllocateMemory(device, &allocInfo, nullptr, &OutBufferMemory) == VK_SUCCESS))
			return 0;

		// 마지막 파라메터 0은 메모리 영역의 offset 임.
		// 이 값이 0이 아니면 memRequirements.alignment 로 나눠야 함. (align 되어있다는 의미)
		vkBindBufferMemory(device, OutBuffer, OutBufferMemory, 0);

		return memRequirements.size;
	}
	void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height) const
	{
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		VkBufferImageCopy region = {};
		region.bufferOffset = 0;

		// 아래 2가지는 얼마나 많은 pixel이 들어있는지 설명, 둘다 0, 0이면 전체
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;

		// 아래 부분은 이미지의 어떤 부분의 픽셀을 복사할지 명세
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		vkCmdCopyBufferToImage(commandBuffer, buffer, image
			, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL		// image가 현재 어떤 레이아웃으로 사용되는지 명세
			, 1, &region);

		EndSingleTimeCommands(commandBuffer);
	}
	bool GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32 mipLevels) const
	{
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);
		if (!ensure(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
			return false;

		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		VkImageMemoryBarrier barrier = { };
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;
		for (uint32 i = 1; i < mipLevels; ++i)
		{
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0
				, 0, nullptr
				, 0, nullptr
				, 1, &barrier);

			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			// 멀티샘플된 source or dest image는 사용할 수 없으며, 그러려면 vkCmdResolveImage를 사용해야함.
			// 만약 VK_FILTER_LINEAR를 사용한다면, vkCmdBlitImage를 사용하기 위해서 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT 를 srcImage가 포함해야함.
			vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
				, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0
				, 0, nullptr
				, 0, nullptr
				, 1, &barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		// 마지막 mipmap 은 source 로 사용되지 않아서 아래와 같이 루프 바깥에서 Barrier 를 추가로 해줌.
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0
			, 0, nullptr
			, 0, nullptr
			, 1, &barrier);

		EndSingleTimeCommands(commandBuffer);

		return true;
	}
	bool CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
	{
		// 임시 커맨드 버퍼를 통해서 메모리를 전송함.
		VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

		// VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 커맨드버퍼를 1번만 쓰고, 복사가 다 될때까지 기다리기 위해서 사용

		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0;		// Optional
		copyRegion.dstOffset = 0;		// Optional
		copyRegion.size = size;			// 여기서는 VK_WHOLE_SIZE 사용 불가
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

		EndSingleTimeCommands(commandBuffer);

		return true;
	}
    static std::vector<char> ReadFile(const std::string& filename)
    {
        // 1. std::ios::ate : 파일의 끝에서 부터 읽기 시작한다. (파일의 끝에서 부터 읽어서 파일의 크기를 얻어 올수 있음)
        // 2. std::ios::binary : 바이너리 파일로서 파일을 읽음. (text transformations 을 피함)
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!ensure(file.is_open()))
            return std::vector<char>();

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);		// 파일의 맨 첨으로 이동
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }
	//////////////////////////////////////////////////////////////////////////

	virtual bool InitRHI() override;
	virtual void ReleaseRHI() override;
	bool CreateColorResources();
	bool CreateDepthResources();
	bool LoadModel();
	//bool RecordCommandBuffers();
	bool CreateSyncObjects();

	// 여기 있을 것은 아님
	void MainLoop();
	bool DrawFrame();
	void CleanupSwapChain();
	void RecreateSwapChain();
	void UpdateUniformBuffer(uint32 currentImage);

	virtual void* GetWindow() const override { return window; }
	FORCEINLINE const VkDevice& GetDevice() const { return device; }

	std::vector<jRenderPass_Vulkan*> RenderPasses;
	jShaderBindings_Vulkan ShaderBindings;
	std::vector<jShaderBindingInstance*> ShaderBindingInstances;
	std::vector<IUniformBufferBlock*> UniformBuffers;

	jCommandBufferManager_Vulkan CommandBufferManager;
	VkPipelineCache PipelineCache = nullptr;

	// jVertexBuffer_Vulkan VertexBuffer;
	jVertexBuffer* VertexBuffer = nullptr;
	jIndexBuffer* IndexBuffer = nullptr;

	jShaderBindingsManager_Vulkan ShaderBindingsManager;
	jFenceManager_Vulkan FenceManager;

	VkPhysicalDeviceProperties DeviceProperties;

	//////////////////////////////////////////////////////////////////////////
	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const override;
	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const override;
	virtual jTexture* CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
		, ETextureFormat textureFormat = ETextureFormat::RGBA8, bool createMipmap = false) const override;
	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) const override;
	virtual bool CreateShader(jShader* OutShader, const jShaderInfo& shaderInfo) const override;
	virtual jFrameBuffer* CreateFrameBuffer(const jFrameBufferInfo& info) const override;
	virtual std::shared_ptr<jRenderTarget> CreateRenderTarget(const jRenderTargetInfo& info) const override;

	virtual jSamplerStateInfo* CreateSamplerState(const jSamplerStateInfo& initializer) const override;
	virtual jRasterizationStateInfo* CreateRasterizationState(const jRasterizationStateInfo& initializer) const override;
	virtual jMultisampleStateInfo* CreateMultisampleState(const jMultisampleStateInfo& initializer) const override;
	virtual jStencilOpStateInfo* CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const override;
	virtual jDepthStencilStateInfo* CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const override;
	virtual jBlendingStateInfo* CreateBlendingState(const jBlendingStateInfo& initializer) const override;

	virtual void DrawArrays(EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const override;
	virtual void DrawArraysInstanced(EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const override;
	virtual void DrawElements(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const override;
	virtual void DrawElementsInstanced(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const override;
	virtual void DrawElementsBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const override;
	virtual void DrawElementsInstancedBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const override;

	virtual jShaderBindings* CreateShaderBindings(const std::vector<jShaderBinding>& InUniformBindings, const std::vector<jShaderBinding>& InTextureBindings) const override;
	virtual jShaderBindingInstance* CreateShaderBindingInstance(const std::vector<TShaderBinding<IUniformBufferBlock*>>& InUniformBuffers, const std::vector<TShaderBinding<jTextureBindings>>& InTextures) const override;
	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindings*>& shaderBindings) const override;
	virtual void* CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances) const override;

	virtual IUniformBufferBlock* CreateUniformBufferBlock(const char* blockname, size_t size = 0) const override;

    virtual jQueryTime* CreateQueryTime() const override;
    virtual void ReleaseQueryTime(jQueryTime* queryTime) const override;
	virtual void QueryTimeStampStart(const jQueryTime* queryTimeStamp) const override;
	virtual void QueryTimeStampEnd(const jQueryTime* queryTimeStamp) const override;
    virtual bool IsQueryTimeStampResult(const jQueryTime* queryTimeStamp, bool isWaitUntilAvailable) const override;
    virtual void GetQueryTimeStampResult(jQueryTime* queryTimeStamp) const override;
	virtual bool CanWholeQueryTimeStampResult() const override { return true; }
	virtual std::vector<uint64> GetWholeQueryTimeStampResult(int32 InWatingResultIndex) const override;
	virtual void GetQueryTimeStampResultFromWholeStampArray(jQueryTime* queryTimeStamp, int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryTimeStampArray) const override;

	static std::unordered_map<size_t, VkPipelineLayout> PipelineLayoutPool;
	static std::unordered_map<size_t, jShaderBindings*> ShaderBindingPool;
	static TResourcePool<jSamplerStateInfo_Vulkan> SamplerStatePool;
	static TResourcePool<jRasterizationStateInfo_Vulkan> RasterizationStatePool;
	static TResourcePool<jMultisampleStateInfo_Vulkan> MultisampleStatePool;
	static TResourcePool<jStencilOpStateInfo_Vulkan> StencilOpStatePool;
	static TResourcePool<jDepthStencilStateInfo_Vulkan> DepthStencilStatePool;
	static TResourcePool<jBlendingStateInfo_Vulakn> BlendingStatePool;

	class jObject* TestCube = nullptr;
	class jCamera* MainCamera = nullptr;
	struct jShader* shader = nullptr;

	jCommandBuffer_Vulkan* CurrentCommandBuffer = nullptr;
	jPipelineStateFixedInfo CurrentPipelineStateFixed_Shadow;
	jPipelineStateFixedInfo CurrentPipelineStateFixed;

	jQueryPool_Vulkan QueryPool;
};

extern jRHI_Vulkan* g_rhi_vk;

struct jShader_Vulkan : public jShader
{
	virtual ~jShader_Vulkan()
	{
		for (auto& Stage : ShaderStages)
		{
			vkDestroyShaderModule(g_rhi_vk->device, Stage.module, nullptr);
		}
	}

	std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
};

struct jQueryTime_Vulkan : public jQueryTime
{
	virtual ~jQueryTime_Vulkan() {}
	virtual void Init() override;

    uint32 QueryId = 0;
};

#endif // USE_VULKAN