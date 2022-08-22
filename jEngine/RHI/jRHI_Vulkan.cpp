#include "pch.h"

#if USE_VULKAN
#include "jRHI_Vulkan.h"

#include "FileLoader/jImageFileLoader.h"
#include "FileLoader/jFile.h"
#include "RHI/jFrameBufferPool.h"
#include "RHI/jRenderTargetPool.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"

#include "stb_image.h"
#include "jPrimitiveUtil.h"
#include "Profiler/jPerformanceProfile.h"
#include "Vulkan/jVertexBuffer_Vulkan.h"
#include "Vulkan/jIndexBuffer_Vulkan.h"
#include "Vulkan/jRHIType_Vulkan.h"
#include "Vulkan/jTexture_Vulkan.h"
#include "Vulkan/jUniformBufferBlock_Vulkan.h"
#include "Vulkan/jVulkanDeviceUtil.h"
#include "Vulkan/jVulkanBufferUtil.h"
#include "Vulkan/jShader_Vulkan.h"
#include "Vulkan/jBuffer_Vulkan.h"
#include "Vulkan/jCommandBufferManager_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "Vulkan/jRingBuffer_Vulkan.h"
#include "Vulkan/jDescriptorPool_Vulkan.h"

jRHI_Vulkan* g_rhi_vk = nullptr;
std::unordered_map<size_t, VkPipelineLayout> jRHI_Vulkan::PipelineLayoutPool;
std::unordered_map<size_t, jShaderBindingsLayout*> jRHI_Vulkan::ShaderBindingPool;
TResourcePool<jSamplerStateInfo_Vulkan> jRHI_Vulkan::SamplerStatePool;
TResourcePool<jRasterizationStateInfo_Vulkan> jRHI_Vulkan::RasterizationStatePool;
TResourcePool<jMultisampleStateInfo_Vulkan> jRHI_Vulkan::MultisampleStatePool;
TResourcePool<jStencilOpStateInfo_Vulkan> jRHI_Vulkan::StencilOpStatePool;
TResourcePool<jDepthStencilStateInfo_Vulkan> jRHI_Vulkan::DepthStencilStatePool;
TResourcePool<jBlendingStateInfo_Vulakn> jRHI_Vulkan::BlendingStatePool;
TResourcePool<jPipelineStateInfo_Vulkan> jRHI_Vulkan::PipelineStatePool;
TResourcePool<jRenderPass_Vulkan> jRHI_Vulkan::RenderPassPool;
TResourcePool<jShader_Vulkan> jRHI_Vulkan::ShaderPool;

struct jFrameBuffer_Vulkan : public jFrameBuffer
{
	bool IsInitialized = false;
	std::vector<std::shared_ptr<jTexture> > AllTextures;

	virtual jTexture* GetTexture(int32 index = 0) const { return AllTextures[index].get(); }
};

jRHI_Vulkan::jRHI_Vulkan()
{
	g_rhi_vk = this;
}


jRHI_Vulkan::~jRHI_Vulkan()
{
}

bool jRHI_Vulkan::InitRHI()
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    Window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(Window, this);

	VkApplicationInfo appInfo;
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	// Must
	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// add extension
	auto extensions = jVulkanDeviceUtil::GetRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	// check validation layer
	if (!ensure(!(jVulkanDeviceUtil::EnableValidationLayers && !jVulkanDeviceUtil::CheckValidationLayerSupport())))
		return false;

	// add validation layer
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
	if (jVulkanDeviceUtil::EnableValidationLayers)
	{
		createInfo.enabledLayerCount = static_cast<uint32>(jVulkanDeviceUtil::validationLayers.size());
		createInfo.ppEnabledLayerNames = jVulkanDeviceUtil::validationLayers.data();

		jVulkanDeviceUtil::PopulateDebutMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
	}
	else
	{
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);

	if (!ensure(result == VK_SUCCESS))
		return false;

	// SetupDebugMessenger
	{
		if (jVulkanDeviceUtil::EnableValidationLayers)
		{
			VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
			jVulkanDeviceUtil::PopulateDebutMessengerCreateInfo(createInfo);
			createInfo.pUserData = nullptr;	// optional

			VkResult result = jVulkanDeviceUtil::CreateDebugUtilsMessengerEXT(Instance, &createInfo, nullptr, &DebugMessenger);
			check(result == VK_SUCCESS);
		}
	}

	{
		ensure(VK_SUCCESS == glfwCreateWindowSurface(Instance, Window, nullptr, &Surface));
	}

	// Physical device
	{
		uint32 deviceCount = 0;
		vkEnumeratePhysicalDevices(Instance, &deviceCount, nullptr);
		if (!ensure(deviceCount > 0))
			return false;

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(Instance, &deviceCount, devices.data());

		for (const auto& device : devices)
		{
			if (jVulkanDeviceUtil::IsDeviceSuitable(device, Surface))
			{
				PhysicalDevice = device;
				// msaaSamples = GetMaxUsableSampleCount();
				SelectedMSAASamples = (EMSAASamples)1;
				break;
			}
		}

		if (!ensure(PhysicalDevice != VK_NULL_HANDLE))
			return false;

		vkGetPhysicalDeviceProperties(PhysicalDevice, &DeviceProperties);
	}

	jSpirvHelper::Init(DeviceProperties);

	// Logical device
	{
		jVulkanDeviceUtil::QueueFamilyIndices indices = jVulkanDeviceUtil::FindQueueFamilies(PhysicalDevice, Surface);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<uint32> uniqueQueueFamilies = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

		float queuePriority = 1.0f;			// [0.0 ~ 1.0]
		for (uint32 queueFamily : uniqueQueueFamilies)
		{
			VkDeviceQueueCreateInfo queueCreateInfo = {};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;		// 현재는 여러 스레드에서 커맨드 버퍼를 각각 만들어서 
												// 메인스레드에서 모두 한번에 제출하기 때문에 큐가 한개 이상일 필요가 없다.
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures = {};
		deviceFeatures.samplerAnisotropy = VK_TRUE;		// VkSampler 가 Anisotropy 를 사용할 수 있도록 하기 위해 true로 설정
		deviceFeatures.sampleRateShading = VK_TRUE;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());

		createInfo.pEnabledFeatures = &deviceFeatures;

		// extension
		createInfo.enabledExtensionCount = static_cast<uint32>(jVulkanDeviceUtil::DeviceExtensions.size());
		createInfo.ppEnabledExtensionNames = jVulkanDeviceUtil::DeviceExtensions.data();

		// 최신 버젼에서는 validation layer는 무시되지만, 오래된 버젼을 호환을 위해 vkInstance와 맞춰줌
		if (jVulkanDeviceUtil::EnableValidationLayers)
		{
			createInfo.enabledLayerCount = static_cast<uint32>(jVulkanDeviceUtil::validationLayers.size());
			createInfo.ppEnabledLayerNames = jVulkanDeviceUtil::validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		if (!ensure(vkCreateDevice(PhysicalDevice, &createInfo, nullptr, &Device) == VK_SUCCESS))
			return false;

		// 현재는 Queue가 1개 뿐이므로 QueueIndex를 0
		GraphicsQueue.QueueIndex = indices.GraphicsFamily.value();
		ComputeQueue.QueueIndex = indices.ComputeFamily.value();
		PresentQueue.QueueIndex = indices.PresentFamily.value();
		vkGetDeviceQueue(Device, GraphicsQueue.QueueIndex, 0, &GraphicsQueue.Queue);
		vkGetDeviceQueue(Device, ComputeQueue.QueueIndex, 0, &ComputeQueue.Queue);
		vkGetDeviceQueue(Device, PresentQueue.QueueIndex, 0, &PresentQueue.Queue);
	}

	// Swapchain
    Swapchain = new jSwapchain_Vulkan();
	verify(Swapchain->Create());

	CommandBufferManager = new jCommandBufferManager_Vulkan();
	CommandBufferManager->CreatePool(GraphicsQueue.QueueIndex);

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	verify(VK_SUCCESS == vkCreatePipelineCache(Device, &pipelineCacheCreateInfo, nullptr, &PipelineCache));

	QueryPool = new jQueryPool_Vulkan();
	QueryPool->Create();

	UniformRingBuffers.resize(Swapchain->GetNumOfSwapchain());
	for (auto& iter : UniformRingBuffers)
	{
        iter = new jRingBuffer_Vulkan();
		iter->Create(EVulkanBufferBits::UNIFORM_BUFFER, 16 * 1024 * 1024, (uint32)DeviceProperties.limits.minUniformBufferOffsetAlignment);
	}

    SSBORingBuffers.resize(Swapchain->GetNumOfSwapchain());
    for (auto& iter : SSBORingBuffers)
    {
        iter = new jRingBuffer_Vulkan();
        iter->Create(EVulkanBufferBits::STORAGE_BUFFER, 16 * 1024 * 1024, (uint32)DeviceProperties.limits.minStorageBufferOffsetAlignment);
    }

	DescriptorPools.resize(Swapchain->GetNumOfSwapchain());
	for (auto& iter : DescriptorPools)
	{
		iter = new jDescriptorPool_Vulkan();
		iter->Create();
	}

    jImGUI_Vulkan::Get().Initialize((float)SCR_WIDTH, (float)SCR_HEIGHT);

	return true;
}

void jRHI_Vulkan::ReleaseRHI()
{
	Flush();

	jImageFileLoader::ReleaseInstance();
    jImGUI_Vulkan::ReleaseInstance();

    RenderPassPool.Release();
    SamplerStatePool.Release();
    RasterizationStatePool.Release();
    MultisampleStatePool.Release();
    StencilOpStatePool.Release();
    DepthStencilStatePool.Release();
    BlendingStatePool.Release();
    PipelineStatePool.Release();
    ShaderPool.Release();

    jTexture_Vulkan::DestroyDefaultSamplerState();
    jFrameBufferPool::Release();
    jRenderTargetPool::Release();

	delete Swapchain;
	Swapchain = nullptr;

	delete CommandBufferManager;
	CommandBufferManager = nullptr;

	for (auto& iter : PipelineLayoutPool)
		vkDestroyPipelineLayout(Device, iter.second, nullptr);
	PipelineLayoutPool.clear();

	for (auto& iter : ShaderBindingPool)
		delete iter.second;
	ShaderBindingPool.clear();

	delete QueryPool;
	QueryPool = nullptr;

	for (auto& iter : UniformRingBuffers)
		delete iter;
	UniformRingBuffers.clear();

    for (auto& iter : SSBORingBuffers)
        delete iter;
	SSBORingBuffers.clear();

	for (auto& iter : DescriptorPools)
		delete iter;
	DescriptorPools.clear();

	vkDestroyPipelineCache(Device, PipelineCache, nullptr);
	FenceManager.Release();

	if (Device)
	{
		vkDestroyDevice(Device, nullptr);
		Device = nullptr;
	}

	if (Surface)
	{
		vkDestroySurfaceKHR(Instance, Surface, nullptr);
		Surface = nullptr;
	}

	if (DebugMessenger)
	{
		jVulkanDeviceUtil::DestroyDebugUtilsMessengerEXT(Instance, DebugMessenger, nullptr);
		DebugMessenger = nullptr;
	}

	if (Instance)
	{
		vkDestroyInstance(Instance, nullptr);
		Instance = nullptr;
	}

    jSpirvHelper::Finalize();
}

jIndexBuffer* jRHI_Vulkan::CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	check(streamData);
	check(streamData->Param);
	jIndexBuffer_Vulkan* indexBuffer = new jIndexBuffer_Vulkan();
	indexBuffer->IndexStreamData = streamData;

	VkDeviceSize bufferSize = streamData->Param->GetBufferSize();

	jBuffer_Vulkan stagingBuffer;
	jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, stagingBuffer);

	stagingBuffer.UpdateBuffer(streamData->Param->GetBufferData(), bufferSize);

	indexBuffer->BufferPtr = std::make_shared<jBuffer_Vulkan>();
    jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, *indexBuffer->BufferPtr.get());
    jVulkanBufferUtil::CopyBuffer(stagingBuffer.Buffer, indexBuffer->BufferPtr->Buffer, bufferSize);

	stagingBuffer.Release();
	return indexBuffer;
}

void jRHI_Vulkan::CleanupSwapChain()
{
	// ImageViews and RenderPass 가 소멸되기전에 호출되어야 함
	//for (auto framebuffer : swapChainFramebuffers)
	//	vkDestroyFramebuffer(device, framebuffer, nullptr);
	//for (int32 i = 0; i < RenderPasses.size(); ++i)
	//{
	//	RenderPasses[i]->Release();
	//	delete RenderPasses[i];
	//}
	//RenderPasses.clear();

	delete Swapchain;

	//for (size_t i = 0; i < swapChainImages.size(); ++i)
	//{
	//	vkDestroyBuffer(device, uniformBuffers[i], nullptr);
	//	vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
	//}

	//vkDestroyDescriptorPool(device, descriptorPool, nullptr);
}

void jRHI_Vulkan::RecreateSwapChain()
{
	//// 윈도우 최소화 된경우 창 사이즈가 0이 되는데 이경우는 Pause 상태로 뒀다가 윈도우 사이즈가 0이 아닐때 계속 진행하도록 함.
	//int width = 0, height = 0;
	//glfwGetFramebufferSize(window, &width, &height);
	//while (width == 0 || height == 0)
	//{
	//	glfwGetFramebufferSize(window, &width, &height);
	//	glfwWaitEvents();
	//}

	//// 사용중인 리소스에 손을 댈수 없기 때문에 모두 사용될때까지 기다림
	//vkDeviceWaitIdle(device);

	//CleanupSwapChain();

	//CreateSwapChain();
	//CreateImageViews();			// Swapchain images 과 연관 있어서 다시 만듬
	//CreateRenderPass();			// ImageView 와 연관 있어서 다시 만듬
	//CreateGraphicsPipeline();	// 가끔 image format 이 다르기도 함.
	//							// Viewport나 Scissor Rectangle size 가 Graphics Pipeline 에 있으므로 재생성.
	//							// (DynamicState로 Viewport 와 Scissor 사용하고 변경점이 이것 뿐이면 재생성 피할수 있음)
	//CreateColorResources();
	//CreateDepthResources();
	//CreateFrameBuffers();		// Swapchain images 과 연관 있어서 다시 만듬
	//CreateUniformBuffers();
	//CreateDescriptorPool();
	//CreateDescriptorSets();
	//CreateCommandBuffers();		// Swapchain images 과 연관 있어서 다시 만듬
}

jVertexBuffer* jRHI_Vulkan::CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	jVertexBuffer_Vulkan* vertexBuffer = new jVertexBuffer_Vulkan();
	vertexBuffer->VertexStreamData = streamData;
	vertexBuffer->BindInfos.Reset();

	std::list<uint32> buffers;
	int32 index = 0;
	for (const auto& iter : streamData->Params)
	{
		if (iter->Stride <= 0)
			continue;

		jVertexStream_Vulkan stream;
		stream.Name = iter->Name;
		stream.Count = iter->Stride / iter->ElementTypeSize;
		stream.BufferType = EBufferType::STATIC;
		stream.ElementType = iter->ElementType;
		stream.Stride = iter->Stride;
		stream.Offset = 0;
		stream.InstanceDivisor = iter->InstanceDivisor;

		{
			VkDeviceSize bufferSize = iter->GetBufferSize();
			jBuffer_Vulkan stagingBuffer;

			// VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 이 버퍼가 메모리 전송 연산의 소스가 될 수 있음.
			jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, stagingBuffer);

			//// 마지막 파라메터 0은 메모리 영역의 offset 임.
			//// 이 값이 0이 아니면 memRequirements.alignment 로 나눠야 함. (align 되어있다는 의미)
			//vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

			stagingBuffer.UpdateBuffer(iter->GetBufferData(), bufferSize);

			// Map -> Unmap 했다가 메모리에 데이터가 즉시 반영되는게 아님
			// 바로 사용하려면 아래 2가지 방법이 있음.
			// 1. VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 사용 (항상 반영, 약간 느릴 수도)
			// 2. 쓰기 이후 vkFlushMappedMemoryRanges 호출, 읽기 이후 vkInvalidateMappedMemoryRanges 호출
			// 위의 2가지 방법을 사용해도 이 데이터가 GPU에 바로 보인다고 보장할 수는 없지만 다음 vkQueueSubmit 호출 전에는 완료될 것을 보장함.

			// VK_BUFFER_USAGE_TRANSFER_DST_BIT : 이 버퍼가 메모리 전송 연산의 목적지가 될 수 있음.
			// DEVICE LOCAL 메모리에 VertexBuffer를 만들었으므로 이제 vkMapMemory 같은 것은 할 수 없음.
			stream.BufferPtr = std::make_shared<jBuffer_Vulkan>();
			jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
				, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, *stream.BufferPtr.get());

			jVulkanBufferUtil::CopyBuffer(stagingBuffer.Buffer, stream.BufferPtr->Buffer, bufferSize);

			stagingBuffer.Release();
		}
		vertexBuffer->BindInfos.Buffers.push_back(stream.BufferPtr->Buffer);
		vertexBuffer->BindInfos.Offsets.push_back(stream.Offset);

		/////////////////////////////////////////////////////////////
		VkVertexInputBindingDescription bindingDescription = {};
		// 모든 데이터가 하나의 배열에 있어서 binding index는 0
		bindingDescription.binding = index;
		bindingDescription.stride = iter->Stride;

		// VK_VERTEX_INPUT_RATE_VERTEX : 각각 버택스 마다 다음 데이터로 이동
		// VK_VERTEX_INPUT_RATE_INSTANCE : 각각의 인스턴스 마다 다음 데이터로 이동
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		vertexBuffer->BindInfos.InputBindingDescriptions.push_back(bindingDescription);
		/////////////////////////////////////////////////////////////
		VkVertexInputAttributeDescription attributeDescription = {};
		attributeDescription.binding = index;
		attributeDescription.location = index;

		VkFormat AttrFormat = VK_FORMAT_UNDEFINED;
		switch (iter->ElementType)
		{
		case EBufferElementType::BYTE:
		{
			const int32 elementCount = iter->Stride / sizeof(char);
			switch (elementCount)
			{
			case 1:
				AttrFormat = VK_FORMAT_R8_SINT;
				break;
			case 2:
				AttrFormat = VK_FORMAT_R8G8_SINT;
				break;
			case 3:
				AttrFormat = VK_FORMAT_R8G8B8_SINT;
				break;
			case 4:
				AttrFormat = VK_FORMAT_R8G8B8A8_SINT;
				break;
			default:
				check(0);
				break;
			}
			break;
		}
		case EBufferElementType::UINT32:
		{
			const int32 elementCount = iter->Stride / sizeof(uint32);
			switch (elementCount)
			{
			case 1:
				AttrFormat = VK_FORMAT_R32_SINT;
				break;
			case 2:
				AttrFormat = VK_FORMAT_R32G32_SINT;
				break;
			case 3:
				AttrFormat = VK_FORMAT_R32G32B32_SINT;
				break;
			case 4:
				AttrFormat = VK_FORMAT_R32G32B32A32_SINT;
				break;
			default:
				check(0);
				break;
			}
			break;
		}
		case EBufferElementType::FLOAT:
		{
			const int32 elementCount = iter->Stride / sizeof(float);
			switch(elementCount)
			{
			case 1:
				AttrFormat = VK_FORMAT_R32_SFLOAT;
				break;
			case 2:
				AttrFormat = VK_FORMAT_R32G32_SFLOAT;
				break;
			case 3:
				AttrFormat = VK_FORMAT_R32G32B32_SFLOAT;
				break;
			case 4:
				AttrFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
				break;
			default:
				check(0);
				break;
			}
			break;
		}
		default:
			check(0);
			break;
		}
		check(AttrFormat != VK_FORMAT_UNDEFINED);
		//float: VK_FORMAT_R32_SFLOAT
		//vec2 : VK_FORMAT_R32G32_SFLOAT
		//vec3 : VK_FORMAT_R32G32B32_SFLOAT
		//vec4 : VK_FORMAT_R32G32B32A32_SFLOAT
		//ivec2: VK_FORMAT_R32G32_SINT, a 2-component vector of 32-bit signed integers
		//uvec4: VK_FORMAT_R32G32B32A32_UINT, a 4-component vector of 32-bit unsigned integers
		//double: VK_FORMAT_R64_SFLOAT, a double-precision (64-bit) float
		attributeDescription.format = AttrFormat;
		attributeDescription.offset = 0;
		vertexBuffer->BindInfos.AttributeDescriptions.push_back(attributeDescription);
		/////////////////////////////////////////////////////////////
		vertexBuffer->Streams.emplace_back(stream);

		++index;
	}

	return vertexBuffer;
}

jTexture* jRHI_Vulkan::CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
	, ETextureFormat textureFormat, bool createMipmap) const
{
    VkDeviceSize imageSize = width * height * GetVulkanTextureComponentCount(textureFormat);
    uint32 textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(width, height)))) + 1;

	jBuffer_Vulkan stagingBuffer;

	jVulkanBufferUtil::CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        , imageSize, stagingBuffer);

	stagingBuffer.UpdateBuffer(data, imageSize);

	VkFormat vkTextureFormat = GetVulkanTextureFormat(textureFormat);

	VkImage TextureImage;
	VkDeviceMemory TextureImageMemory;
    if (!ensure(jVulkanBufferUtil::CreateImage(static_cast<uint32>(width), static_cast<uint32>(height), textureMipLevels, VK_SAMPLE_COUNT_1_BIT, vkTextureFormat
        , VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT	// image를 shader 에서 접근가능하게 하고 싶은 경우
        , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, TextureImage, TextureImageMemory)))
    {
        return nullptr;
    }

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
	ensure(TransitionImageLayout(commandBuffer, TextureImage, vkTextureFormat, textureMipLevels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

	jVulkanBufferUtil::CopyBufferToImage(commandBuffer, stagingBuffer.Buffer, TextureImage, static_cast<uint32>(width), static_cast<uint32>(height));

    // 밉맵을 만드는 동안 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 으로 전환됨.
    if (createMipmap)
    {
        jVulkanBufferUtil::GenerateMipmaps(commandBuffer, TextureImage, vkTextureFormat, width, height, textureMipLevels
			, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
	else
	{
        ensure(TransitionImageLayout(commandBuffer, TextureImage, vkTextureFormat, textureMipLevels
			, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	}

	EndSingleTimeCommands(commandBuffer);

	stagingBuffer.Release();

    // Create Texture image view
    VkImageView textureImageView = jVulkanBufferUtil::CreateImageView(TextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, textureMipLevels);

    auto texture = new jTexture_Vulkan();
    texture->sRGB = sRGB;
    texture->Format = textureFormat;
    texture->Type = ETextureType::TEXTURE_2D;
    texture->Width = width;
    texture->Height = height;
	texture->MipLevel = createMipmap ? jTexture::GetMipLevels(width, height) : 1;
	texture->Image = TextureImage;
	texture->View = textureImageView;
	texture->Memory = TextureImageMemory;
	texture->Layout = EImageLayout::SHADER_READ_ONLY;

	return texture;
}

jShader* jRHI_Vulkan::CreateShader(const jShaderInfo& shaderInfo) const
{
	return ShaderPool.GetOrCreate(shaderInfo);
}

bool jRHI_Vulkan::CreateShaderInternal(jShader* OutShader, const jShaderInfo& shaderInfo) const
{
	auto CreateShaderModule = [Device = this->Device](const std::vector<uint32>& code) -> VkShaderModule
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size() * sizeof(uint32);

		// pCode 가 uint32* 형이라서 4 byte aligned 된 메모리를 넘겨줘야 함.
		// 다행히 std::vector의 default allocator가 가 메모리 할당시 4 byte aligned 을 이미 하고있어서 그대로 씀.
		createInfo.pCode = reinterpret_cast<const uint32*>(code.data());

		VkShaderModule shaderModule = {};
		ensure(vkCreateShaderModule(Device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

		// compiling or linking 과정이 graphics pipeline 이 생성되기 전까지 처리되지 않는다.
		// 그래픽스 파이프라인이 생성된 후 VkShaderModule은 즉시 소멸 가능.
		return shaderModule;
	};

	jShader_Vulkan* shader_vk = (jShader_Vulkan*)OutShader;

	if (shaderInfo.cs.GetStringLength() > 0)
	{
		jFile csFile;
		csFile.OpenFile(shaderInfo.cs.ToStr(), FileType::TEXT, ReadWriteType::READ);
		csFile.ReadFileToBuffer(false);
		std::string csText(csFile.GetBuffer());

		std::vector<uint32> SpirvCode;
        const bool isHLSL = !!strstr(shaderInfo.cs.ToStr(), ".hlsl");
		if (isHLSL)
			jSpirvHelper::HLSLtoSpirv(SpirvCode, ShaderConductor::ShaderStage::ComputeShader, csText.c_str());
		else
            jSpirvHelper::GLSLtoSpirv(SpirvCode, EShLanguage::EShLangCompute, csText.data());
		VkShaderModule computeShaderModule = CreateShaderModule(SpirvCode);

		VkPipelineShaderStageCreateInfo computeShaderStageInfo = {};
		computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		computeShaderStageInfo.module = computeShaderModule;
		computeShaderStageInfo.pName = "main";

		shader_vk->ShaderStages.push_back(computeShaderStageInfo);
	}
	else
	{
		if (shaderInfo.vs.GetStringLength() > 0)
		{
			jFile vsFile;
			vsFile.OpenFile(shaderInfo.vs.ToStr(), FileType::TEXT, ReadWriteType::READ);
			vsFile.ReadFileToBuffer(false);
			std::string vsText(vsFile.GetBuffer());

			std::vector<uint32> SpirvCode;
            const bool isHLSL = !!strstr(shaderInfo.vs.ToStr(), ".hlsl");
            if (isHLSL)
                jSpirvHelper::HLSLtoSpirv(SpirvCode, ShaderConductor::ShaderStage::VertexShader, vsText.c_str());
			else
				jSpirvHelper::GLSLtoSpirv(SpirvCode, EShLanguage::EShLangVertex, vsText.data());
			VkShaderModule vertShaderModule = CreateShaderModule(SpirvCode);
			VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";
			// pSpecializationInfo 을 통해 쉐이더에서 사용하는 상수값을 설정해줄 수 있음. 이 상수 값에 따라 if 분기문에 없어지거나 하는 최적화가 일어날 수 있음.
			//vertShaderStageInfo.pSpecializationInfo

			shader_vk->ShaderStages.push_back(vertShaderStageInfo);
		}

		if (shaderInfo.gs.GetStringLength() > 0)
		{
			jFile gsFile;
			gsFile.OpenFile(shaderInfo.gs.ToStr(), FileType::TEXT, ReadWriteType::READ);
			gsFile.ReadFileToBuffer(false);
			std::string gsText(gsFile.GetBuffer());

			std::vector<uint32> SpirvCode;
            const bool isHLSL = !!strstr(shaderInfo.gs.ToStr(), ".hlsl");
            if (isHLSL)
                jSpirvHelper::HLSLtoSpirv(SpirvCode, ShaderConductor::ShaderStage::GeometryShader, gsText.c_str());
			else
				jSpirvHelper::GLSLtoSpirv(SpirvCode, EShLanguage::EShLangGeometry, gsText.data());
			VkShaderModule geoShaderModule = CreateShaderModule(SpirvCode);
			VkPipelineShaderStageCreateInfo geoShaderStageInfo = {};
			geoShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			geoShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
			geoShaderStageInfo.module = geoShaderModule;
			geoShaderStageInfo.pName = "main";

			shader_vk->ShaderStages.push_back(geoShaderStageInfo);
		}

		if (shaderInfo.fs.GetStringLength() > 0)
		{
			jFile fsFile;
			fsFile.OpenFile(shaderInfo.fs.ToStr(), FileType::TEXT, ReadWriteType::READ);
			fsFile.ReadFileToBuffer(false);
			std::string fsText(fsFile.GetBuffer());

			std::vector<uint32> SpirvCode;
            const bool isHLSL = !!strstr(shaderInfo.fs.ToStr(), ".hlsl");
            if (isHLSL)
                jSpirvHelper::HLSLtoSpirv(SpirvCode, ShaderConductor::ShaderStage::PixelShader, fsText.c_str());
			else
				jSpirvHelper::GLSLtoSpirv(SpirvCode, EShLanguage::EShLangFragment, fsText.data());
			VkShaderModule fragShaderModule = CreateShaderModule(SpirvCode);
			VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			shader_vk->ShaderStages.push_back(fragShaderStageInfo);
		}
	}

	shader_vk->ShaderInfo = shaderInfo;

	return true;
}

jFrameBuffer* jRHI_Vulkan::CreateFrameBuffer(const jFrameBufferInfo& info) const
{
	const VkFormat textureFormat =  GetVulkanTextureFormat(info.Format);
	const bool hasDepthAttachment = IsDepthFormat(info.Format);

	const VkImageUsageFlagBits ImageUsageFlagBit = hasDepthAttachment ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	const VkImageAspectFlagBits ImageAspectFlagBit = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	// VK_IMAGE_TILING_LINEAR 설정시 크래시 나서 VK_IMAGE_TILING_OPTIMAL 로 함.
	//const VkImageTiling TilingMode = IsMobile ? VkImageTiling::VK_IMAGE_TILING_OPTIMAL : VkImageTiling::VK_IMAGE_TILING_LINEAR;
	const VkImageTiling TilingMode = VkImageTiling::VK_IMAGE_TILING_OPTIMAL;

	const int32 mipLevels = info.IsGenerateMipmap ? jTexture::GetMipLevels(info.Width, info.Height) : 1;

	JASSERT(info.SampleCount >= 1);

	VkImage image = nullptr;
	VkImageView imageView = nullptr;
	VkDeviceMemory imageMemory = nullptr;

	switch (info.TextureType)
	{
	case ETextureType::TEXTURE_2D:
		jVulkanBufferUtil::CreateImage(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImageView(image, textureFormat, ImageAspectFlagBit, mipLevels);
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		jVulkanBufferUtil::CreateImage2DArray(info.Width, info.Height, info.LayerCount, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImage2DArrayView(image, info.LayerCount, textureFormat, ImageAspectFlagBit, mipLevels);
		break;
	case ETextureType::TEXTURE_CUBE:
		jVulkanBufferUtil::CreateImageCube(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImageCubeView(image, textureFormat, ImageAspectFlagBit, mipLevels);
		break;
	default:
		JMESSAGE("Unsupported type texture in FramebufferPool");
		return nullptr;
	}

	auto tex_vk = new jTexture_Vulkan();
	tex_vk->Type = info.TextureType;
	tex_vk->Format = info.Format;
	tex_vk->Width = info.Width;
	tex_vk->Height = info.Height;
	tex_vk->Image = image;
	tex_vk->View = imageView;
	tex_vk->Memory = imageMemory;
	tex_vk->MipLevel = mipLevels;
	auto texturePtr = std::shared_ptr<jTexture>(tex_vk);

	auto fb_vk = new jFrameBuffer_Vulkan();
	fb_vk->Info = info;
	fb_vk->Textures.push_back(texturePtr);
	fb_vk->AllTextures.push_back(texturePtr);

	return fb_vk;
}

std::shared_ptr<jRenderTarget> jRHI_Vulkan::CreateRenderTarget(const jRenderTargetInfo& info) const
{
	const VkFormat textureFormat = GetVulkanTextureFormat(info.Format);
	const bool hasDepthAttachment = IsDepthFormat(info.Format);

	const VkImageUsageFlags ImageUsageFlag = (hasDepthAttachment ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT)
		| VK_IMAGE_USAGE_SAMPLED_BIT;
	const VkImageAspectFlags ImageAspectFlag = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	// VK_IMAGE_TILING_LINEAR 설정시 크래시 나서 VK_IMAGE_TILING_OPTIMAL 로 함.
	//const VkImageTiling TilingMode = IsMobile ? VkImageTiling::VK_IMAGE_TILING_OPTIMAL : VkImageTiling::VK_IMAGE_TILING_LINEAR;
	const VkImageTiling TilingMode = VkImageTiling::VK_IMAGE_TILING_OPTIMAL;

	const int32 mipLevels = (info.SampleCount > EMSAASamples::COUNT_1 || !info.IsGenerateMipmap) ? 1 : jTexture::GetMipLevels(info.Width, info.Height);		// MipLevel 은 SampleCount 1인 경우만 가능
	JASSERT((int32)info.SampleCount >= 1);

	VkImage image = nullptr;
	VkImageView imageView = nullptr;
	VkDeviceMemory imageMemory = nullptr;

	switch (info.Type)
	{
	case ETextureType::TEXTURE_2D:
		jVulkanBufferUtil::CreateImage(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImageView(image, textureFormat, ImageAspectFlag, mipLevels);
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		jVulkanBufferUtil::CreateImage2DArray(info.Width, info.Height, info.LayerCount, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImage2DArrayView(image, info.LayerCount, textureFormat, ImageAspectFlag, mipLevels);
		break;
	case ETextureType::TEXTURE_CUBE:
		jVulkanBufferUtil::CreateImageCube(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImageCubeView(image, textureFormat, ImageAspectFlag, mipLevels);
		break;
	default:
		JMESSAGE("Unsupported type texture in FramebufferPool");
		return nullptr;
	}

	auto tex_vk = new jTexture_Vulkan();
	tex_vk->Type = info.Type;
	tex_vk->Format = info.Format;
	tex_vk->Width = info.Width;
	tex_vk->Height = info.Height;
	tex_vk->SampleCount = info.SampleCount;
	tex_vk->LayerCount = info.LayerCount;
	tex_vk->Image = image;
	tex_vk->View = imageView;
	tex_vk->Memory = imageMemory;
	tex_vk->Layout = EImageLayout::UNDEFINED;
	tex_vk->MipLevel = mipLevels;

	auto rt_vk = new jRenderTarget();
	rt_vk->Info = info;
	rt_vk->TexturePtr = std::shared_ptr<jTexture>(tex_vk);

	return std::shared_ptr<jRenderTarget>(rt_vk);
}

jSamplerStateInfo* jRHI_Vulkan::CreateSamplerState(const jSamplerStateInfo& initializer) const
{
	return SamplerStatePool.GetOrCreate(initializer);
}

jRasterizationStateInfo* jRHI_Vulkan::CreateRasterizationState(const jRasterizationStateInfo& initializer) const
{
	return RasterizationStatePool.GetOrCreate(initializer);
}

jMultisampleStateInfo* jRHI_Vulkan::CreateMultisampleState(const jMultisampleStateInfo& initializer) const
{
	return MultisampleStatePool.GetOrCreate(initializer);
}

jStencilOpStateInfo* jRHI_Vulkan::CreateStencilOpStateInfo(const jStencilOpStateInfo& initializer) const
{
	return StencilOpStatePool.GetOrCreate(initializer);
}

jDepthStencilStateInfo* jRHI_Vulkan::CreateDepthStencilState(const jDepthStencilStateInfo& initializer) const
{
	return DepthStencilStatePool.GetOrCreate(initializer);
}

jBlendingStateInfo* jRHI_Vulkan::CreateBlendingState(const jBlendingStateInfo& initializer) const
{
	return BlendingStatePool.GetOrCreate(initializer);
}

void jRHI_Vulkan::DrawArrays(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdDraw((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), vertCount, 1, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdDraw((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), vertCount, instanceCount, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), count, 1, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), count, instanceCount, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), count, 1, startIndex, baseVertexIndex, 0);
}

void jRHI_Vulkan::DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const
{
	check(InRenderFrameContext);
	check(InRenderFrameContext->CommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), count, instanceCount, startIndex, baseVertexIndex, 0);
}

jShaderBindingsLayout* jRHI_Vulkan::CreateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings) const
{
	const auto hash = jShaderBindingsLayout::GenerateHash(InShaderBindings);

	auto it_find = ShaderBindingPool.find(hash);
	if (ShaderBindingPool.end() != it_find)
		return it_find->second;

	auto NewShaderBinding = new jShaderBindingLayout_Vulkan();
	NewShaderBinding->Initialize(InShaderBindings);
	ShaderBindingPool.insert(std::make_pair(hash, NewShaderBinding));

	return NewShaderBinding;
}

jShaderBindingInstance* jRHI_Vulkan::CreateShaderBindingInstance(const std::vector<jShaderBinding>& InShaderBindings) const
{
	auto shaderBindings = CreateShaderBindings(InShaderBindings);
	check(shaderBindings);
	return shaderBindings->CreateShaderBindingInstance(InShaderBindings);
}

void* jRHI_Vulkan::CreatePipelineLayout(const std::vector<const jShaderBindingsLayout*>& shaderBindings) const
{
	if (shaderBindings.size() <= 0)
		return 0;

	VkPipelineLayout vkPipelineLayout = nullptr;

	const size_t hash = jShaderBindingsLayout::CreateShaderBindingLayoutHash(shaderBindings);

	auto it_find = PipelineLayoutPool.find(hash);
    if (PipelineLayoutPool.end() != it_find)
    {
        vkPipelineLayout = it_find->second;
    }
    else
	{
		std::vector<VkDescriptorSetLayout> DescriptorSetLayouts;
		DescriptorSetLayouts.reserve(shaderBindings.size());
		for (const jShaderBindingsLayout* binding : shaderBindings)
		{
			const jShaderBindingLayout_Vulkan* binding_vulkan = (const jShaderBindingLayout_Vulkan*)binding;
			DescriptorSetLayouts.push_back(binding_vulkan->DescriptorSetLayout);
		}

		// 쉐이더에 전달된 Uniform 들을 명세하기 위한 오브젝트
		// 이 오브젝트는 프로그램 실행동안 계속해서 참조되므로 cleanup 에서 제거해줌
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = (uint32)DescriptorSetLayouts.size();
		pipelineLayoutInfo.pSetLayouts = &DescriptorSetLayouts[0];
		pipelineLayoutInfo.pushConstantRangeCount = 0;		// Optional		// 쉐이더에 값을 constant 값을 전달 할 수 있음. 이후에 배움
		pipelineLayoutInfo.pPushConstantRanges = nullptr;	// Optional
		if (!ensure(vkCreatePipelineLayout(Device, &pipelineLayoutInfo, nullptr, &vkPipelineLayout) == VK_SUCCESS))
			return nullptr;

		PipelineLayoutPool.insert(std::make_pair(hash, vkPipelineLayout));
	}

	return vkPipelineLayout;
}

void* jRHI_Vulkan::CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances) const
{
	if (shaderBindingInstances.size() <= 0)
		return 0;

	std::vector<const jShaderBindingsLayout*> bindings;
	bindings.resize(shaderBindingInstances.size());
	for (int32 i = 0; i < shaderBindingInstances.size(); ++i)
	{
		bindings[i] = shaderBindingInstances[i]->ShaderBindingsLayouts;
	}
	return CreatePipelineLayout(bindings);
}

IUniformBufferBlock* jRHI_Vulkan::CreateUniformBufferBlock(const char* blockname, size_t size /*= 0*/) const
{
	auto uniformBufferBlock = new jUniformBufferBlock_Vulkan(jName(blockname));
	uniformBufferBlock->Init(size);
	return uniformBufferBlock;
}

jQueryTime* jRHI_Vulkan::CreateQueryTime() const
{
	auto queryTime = new jQueryTime_Vulkan();
	return queryTime;
}

void jRHI_Vulkan::ReleaseQueryTime(jQueryTime* queryTime) const
{
    auto queryTime_gl = static_cast<jQueryTime_Vulkan*>(queryTime);
    delete queryTime_gl;
}

void jRHI_Vulkan::QueryTimeStampStart(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jQueryTime* queryTimeStamp) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdWriteTimestamp((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, QueryPool->vkQueryPool, ((jQueryTime_Vulkan*)queryTimeStamp)->QueryId);
}

void jRHI_Vulkan::QueryTimeStampEnd(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jQueryTime* queryTimeStamp) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
	vkCmdWriteTimestamp((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, QueryPool->vkQueryPool, ((jQueryTime_Vulkan*)queryTimeStamp)->QueryId + 1);
}

bool jRHI_Vulkan::IsQueryTimeStampResult(const jQueryTime* queryTimeStamp, bool isWaitUntilAvailable) const
{
	auto queryTimeStamp_vk = static_cast<const jQueryTime_Vulkan*>(queryTimeStamp);

	uint64 time[2] = { 0, 0 };
    VkResult result = VK_RESULT_MAX_ENUM;
	if (isWaitUntilAvailable)
    {
		while (result == VK_SUCCESS)
		{
			result = (vkGetQueryPoolResults(Device, QueryPool->vkQueryPool, queryTimeStamp_vk->QueryId, 2, sizeof(uint64) * 2, time, sizeof(uint64), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
		}
	}

    result = (vkGetQueryPoolResults(Device, QueryPool->vkQueryPool, queryTimeStamp_vk->QueryId, 2, sizeof(uint64) * 2, time, sizeof(uint64), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
    return (result == VK_SUCCESS);
}

void jRHI_Vulkan::GetQueryTimeStampResult(jQueryTime* queryTimeStamp) const
{
	auto queryTimeStamp_vk = static_cast<jQueryTime_Vulkan*>(queryTimeStamp);
	vkGetQueryPoolResults(Device, QueryPool->vkQueryPool, queryTimeStamp_vk->QueryId, 2, sizeof(uint64) * 2, queryTimeStamp_vk->TimeStampStartEnd, sizeof(uint64), VK_QUERY_RESULT_64_BIT);
}

std::vector<uint64> jRHI_Vulkan::GetWholeQueryTimeStampResult(int32 InWatingResultIndex) const
{
	std::vector<uint64> result;
	result.resize(MaxQueryTimeCount);
    vkGetQueryPoolResults(Device, QueryPool->vkQueryPool, InWatingResultIndex * MaxQueryTimeCount, MaxQueryTimeCount, sizeof(uint64) * MaxQueryTimeCount, result.data(), sizeof(uint64), VK_QUERY_RESULT_64_BIT);
	return result;
}

void jRHI_Vulkan::GetQueryTimeStampResultFromWholeStampArray(jQueryTime* queryTimeStamp, int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryTimeStampArray) const
{
	auto queryTimeStamp_vk = static_cast<jQueryTime_Vulkan*>(queryTimeStamp);
	const uint32 queryStart = (queryTimeStamp_vk->QueryId) - InWatingResultIndex * MaxQueryTimeCount;
	const uint32 queryEnd = (queryTimeStamp_vk->QueryId + 1) - InWatingResultIndex * MaxQueryTimeCount;
	check(queryStart >= 0 && queryStart < MaxQueryTimeCount);
	check(queryEnd >= 0 && queryEnd < MaxQueryTimeCount);
	
	queryTimeStamp_vk->TimeStampStartEnd[0] = wholeQueryTimeStampArray[queryStart];
	queryTimeStamp_vk->TimeStampStartEnd[1] = wholeQueryTimeStampArray[queryEnd];
}

std::shared_ptr<jRenderFrameContext> jRHI_Vulkan::BeginRenderFrame()
{
	uint32 imageIndex = -1;

    VkFence lastCommandBufferFence = Swapchain->Images[CurrenFrameIndex]->CommandBufferFence;

    // timeout 은 nanoseconds. UINT64_MAX 는 타임아웃 없음
    VkResult acquireNextImageResult = vkAcquireNextImageKHR(Device, (VkSwapchainKHR)Swapchain->GetHandle(), UINT64_MAX
		, Swapchain->Images[CurrenFrameIndex]->Available, VK_NULL_HANDLE, &imageIndex);
    if (acquireNextImageResult != VK_SUCCESS)
        return nullptr;

	// 여기서는 VK_SUCCESS or VK_SUBOPTIMAL_KHR 은 성공했다고 보고 계속함.
	// VK_ERROR_OUT_OF_DATE_KHR : 스왑체인이 더이상 서피스와 렌더링하는데 호환되지 않는 경우. (보통 윈도우 리사이즈 이후)
	// VK_SUBOPTIMAL_KHR : 스왑체인이 여전히 서피스에 제출 가능하지만, 서피스의 속성이 더이상 정확하게 맞지 않음.
	if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapChain();		// 이 경우 렌더링이 더이상 불가능하므로 즉시 스왑체인을 새로 만듬.
		return nullptr;
	}
	else if (acquireNextImageResult != VK_SUCCESS && acquireNextImageResult != VK_SUBOPTIMAL_KHR)
	{
		check(0);
		return nullptr;
	}

	jCommandBuffer_Vulkan* commandBuffer = (jCommandBuffer_Vulkan*)g_rhi_vk->CommandBufferManager->GetOrCreateCommandBuffer();

    // 이전 프레임에서 현재 사용하려는 이미지를 사용중에 있나? (그렇다면 펜스를 기다려라)
    if (lastCommandBufferFence != VK_NULL_HANDLE)
        vkWaitForFences(Device, 1, &lastCommandBufferFence, VK_TRUE, UINT64_MAX);

	GetUniformRingBuffer()->Reset();
	GetDescriptorPools()->Reset();

    // 이 프레임에서 펜스를 사용한다고 마크 해둠
	Swapchain->Images[CurrenFrameIndex]->CommandBufferFence = (VkFence)commandBuffer->GetFenceHandle();

    auto renderFrameContextPtr = std::make_shared<jRenderFrameContext>();
	renderFrameContextPtr->FrameIndex = CurrenFrameIndex;
	renderFrameContextPtr->CommandBuffer = commandBuffer;
	renderFrameContextPtr->SceneRenderTarget = new jSceneRenderTarget();
	renderFrameContextPtr->SceneRenderTarget->Create(Swapchain->GetSwapchainImage(CurrenFrameIndex));

	return renderFrameContextPtr;
}

void jRHI_Vulkan::EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr)
{
	check(renderFrameContextPtr->CommandBuffer);
	VkCommandBuffer vkCommandBuffer = (VkCommandBuffer)renderFrameContextPtr->CommandBuffer->GetHandle();
	VkFence vkFence = (VkFence)renderFrameContextPtr->CommandBuffer->GetFenceHandle();
	const uint32 imageIndex = (uint32)CurrenFrameIndex;

    // Submitting the command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitsemaphores[] = { Swapchain->Images[CurrenFrameIndex]->Available };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitsemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkCommandBuffer;

    VkSemaphore signalSemaphores[] = { Swapchain->Images[CurrenFrameIndex]->RenderFinished };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // SubmitInfo를 동시에 할수도 있음.
    vkResetFences(Device, 1, &vkFence);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

    // 마지막에 Fences 파라메터는 커맨드 버퍼가 모두 실행되고나면 Signaled 될 Fences.
    if (!ensure(vkQueueSubmit(GraphicsQueue.Queue, 1, &submitInfo, vkFence) == VK_SUCCESS))
        return;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { Swapchain->Swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    // 여러개의 스왑체인에 제출하는 경우만 모든 스왑체인으로 잘 제출 되었나 확인하는데 사용
    // 1개인 경우 그냥 vkQueuePresentKHR 의 리턴값을 확인하면 됨.
    presentInfo.pResults = nullptr;			// Optional
    VkResult queuePresentResult = vkQueuePresentKHR(PresentQueue.Queue, &presentInfo);

    // 세마포어의 일관된 상태를 보장하기 위해서(세마포어 로직을 변경하지 않으려 노력한듯 함) vkQueuePresentKHR 이후에 framebufferResized 를 체크하는 것이 중요함.
    if ((queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR) || (queuePresentResult == VK_SUBOPTIMAL_KHR) || framebufferResized)
    {
        RecreateSwapChain();
        framebufferResized = false;
    }
    else if (queuePresentResult != VK_SUCCESS)
    {
        check(0);
        return;
    }

    // CPU 가 큐에 작업을 제출하는 속도가 GPU 에서 소모하는 속도보다 빠른 경우 큐에 작업이 계속 쌓이거나 
    // 여러 프레임에 걸쳐 동시에 imageAvailableSemaphore 와 renderFinishedSemaphore를 재사용하게 되는 문제가 있음.
    // 1). 한프레임을 마치고 큐가 빌때까지 기다리는 것으로 해결할 수 있음. 한번에 1개의 프레임만 완성 가능(최적의 해결방법은 아님)
    // 2). 여러개의 프레임을 동시에 처리 할수있도록 확장. 동시에 진행될 수 있는 최대 프레임수를 지정해줌.
    CurrenFrameIndex = (CurrenFrameIndex + 1) % Swapchain->Images.size();
	renderFrameContextPtr->Destroy();
}

VkCommandBuffer jRHI_Vulkan::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = CommandBufferManager->GetPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void jRHI_Vulkan::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const
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
    vkFreeCommandBuffers(Device, CommandBufferManager->GetPool(), 1, &commandBuffer);
}

bool jRHI_Vulkan::TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, uint32 mipLevels, VkImageLayout oldLayout, VkImageLayout newLayout) const
{
	if (oldLayout == newLayout)
		return true;

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
	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		break;
	default:
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		break;
	}
	//if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	//{
	//    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	//    if (jVulkanBufferUtil::HasStencilComponent(format))
	//        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	//}
	//else
	//{
	//    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//}
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;	// TODO
	barrier.dstAccessMask = 0;	// TODO

	VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;


	//  // Barrier 는 동기화를 목적으로 사용하므로, 이 리소스와 연관되는 어떤 종류의 명령이 이전에 일어나야 하는지와
	//  // 어떤 종류의 명령이 Barrier를 기다려야 하는지를 명시해야만 한다. vkQueueWaitIdle 을 사용하지만 그래도 수동으로 해줘야 함.

	//  // Undefined -> transfer destination : 이 경우 기다릴 필요없이 바로 쓰면됨. Undefined 라 다른 곳에서 딱히 쓰거나 하는것이 없음.
	//  // Transfer destination -> frag shader reading : frag 쉐이더에서 읽기 전에 transfer destination 에서 쓰기가 완료 됨이 보장되어야 함. 그래서 shader 에서 읽기 가능.
	//  VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	//  VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	//  if ((oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) && (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
	//  {
	//      // 이전 작업을 기다릴 필요가 없어서 srcAccessMask에 0, sourceStage 에 가능한 pipeline stage의 가장 빠른 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
	//      barrier.srcAccessMask = 0;
	//      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;	// VK_ACCESS_TRANSFER_WRITE_BIT 는 Graphics 나 Compute stage에 실제 stage가 아닌 pseudo-stage 임.

	//      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	//      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	//  }
	//  else if ((oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) && (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
	//  {
	//      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	//      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	//      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	//      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	//  }
	//  else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
	//  {
	//      barrier.srcAccessMask = 0;
	//      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	//      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	//      // 둘중 가장 빠른 stage 인 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 를 선택
	//      // VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 에서 depth read 가 일어날 수 있음.
	//      // VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT 에서 depth write 가 일어날 수 있음.
	//      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	//  }
	//  else
	//  {
		  //if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		  //{
		  //	barrier.srcAccessMask = 0;
		  //}
		  //else
		  //{
		  //	check(!"Unsupported layout transition!");
		  //	return false;
		  //}
	//  }

	  // Source layouts (old)
	  // Source access mask controls actions that have to be finished on the old layout
	  // before it will be transitioned to the new layout
	switch (oldLayout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
		// Image layout is undefined (or does not matter)
		// Only valid as initial layout
		// No flags required, listed only for completeness
		barrier.srcAccessMask = 0;
		break;

	case VK_IMAGE_LAYOUT_PREINITIALIZED:
		// Image is preinitialized
		// Only valid as initial layout for linear images, preserves memory contents
		// Make sure host writes have been finished
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Image is a color attachment
		// Make sure any writes to the color buffer have been finished
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Image is a depth/stencil attachment
		// Make sure any writes to the depth/stencil buffer have been finished
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Image is a transfer source
		// Make sure any reads from the image have been finished
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Image is a transfer destination
		// Make sure any writes to the image have been finished
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Image is read by a shader
		// Make sure any shader reads from the image have been finished
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		// Other source layouts aren't handled (yet)
		break;
	}

	// Target layouts (new)
	// Destination access mask controls the dependency for the new image layout
	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Image will be used as a transfer destination
		// Make sure any writes to the image have been finished
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Image will be used as a transfer source
		// Make sure any reads from the image have been finished
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Image will be used as a color attachment
		// Make sure any writes to the color buffer have been finished
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Image layout will be used as a depth/stencil attachment
		// Make sure any writes to depth/stencil buffer have been finished
		barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Image will be read in a shader (sampler, input attachment)
		// Make sure any writes to the image have been finished
		if (barrier.srcAccessMask == 0)
		{
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		// Other source layouts aren't handled (yet)
		break;
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

	return true;
}

bool jRHI_Vulkan::TransitionImageLayout(jCommandBuffer* commandBuffer, jTexture* texture, EImageLayout newLayout) const
{
	check(commandBuffer);
	check(texture);

	auto texture_vk = (jTexture_Vulkan*)texture;
	if (TransitionImageLayout((VkCommandBuffer)commandBuffer->GetHandle(), texture_vk->Image, GetVulkanTextureFormat(texture_vk->Format)
		, texture_vk->MipLevel, GetVulkanImageLayout(texture_vk->Layout), GetVulkanImageLayout(newLayout)))
	{
		((jTexture_Vulkan*)texture)->Layout = newLayout;
		return true;
	}
	return false;
}

bool jRHI_Vulkan::TransitionImageLayoutImmediate(jTexture* texture, EImageLayout newLayout) const
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    check(commandBuffer);
    
	if (commandBuffer)
	{
		check(texture);

		auto texture_vk = (jTexture_Vulkan*)texture;
		const bool ret = TransitionImageLayout(commandBuffer, texture_vk->Image, GetVulkanTextureFormat(texture_vk->Format)
			, texture_vk->MipLevel, GetVulkanImageLayout(texture_vk->Layout), GetVulkanImageLayout(newLayout));
		if (ret)
			((jTexture_Vulkan*)texture)->Layout = newLayout;

		EndSingleTimeCommands(commandBuffer);
		return ret;
	}

	return false;
}

jPipelineStateInfo* jRHI_Vulkan::CreatePipelineStateInfo(const jPipelineStateFixedInfo* pipelineStateFixed, const jShader* shader
    , const jVertexBuffer* vertexBuffer, const jRenderPass* renderPass, const std::vector<const jShaderBindingsLayout*> shaderBindings) const
{
	return PipelineStatePool.GetOrCreate(jPipelineStateInfo(pipelineStateFixed, shader, vertexBuffer, renderPass, shaderBindings));
}

jPipelineStateInfo* jRHI_Vulkan::CreateComputePipelineStateInfo(const jShader* shader, const std::vector<const jShaderBindingsLayout*> shaderBindings) const
{
    return PipelineStatePool.GetOrCreate(jPipelineStateInfo(shader, shaderBindings));
}

jRenderPass* jRHI_Vulkan::GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_Vulkan(colorAttachments, offset, extent));
}

jRenderPass* jRHI_Vulkan::GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_Vulkan(colorAttachments, depthAttachment, offset, extent));
}

jRenderPass* jRHI_Vulkan::GetOrCreateRenderPass(const std::vector<jAttachment>& colorAttachments, const jAttachment& depthAttachment, const jAttachment& colorResolveAttachment
	, const Vector2i& offset, const Vector2i& extent) const
{
    return RenderPassPool.GetOrCreate(jRenderPass_Vulkan(colorAttachments, depthAttachment, colorResolveAttachment, offset, extent));
}

void jRHI_Vulkan::Flush() const
{
	vkQueueWaitIdle(GraphicsQueue.Queue);
	vkQueueWaitIdle(ComputeQueue.Queue);
	vkQueueWaitIdle(PresentQueue.Queue);
}

void jRHI_Vulkan::Finish() const
{
    vkQueueWaitIdle(GraphicsQueue.Queue);
    vkQueueWaitIdle(ComputeQueue.Queue);
    vkQueueWaitIdle(PresentQueue.Queue);
}

#endif // USE_VULKAN
