#include "pch.h"

//#if USE_VULKAN
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
#include "Vulkan/jQueryPoolOcclusion_Vulkan.h"
#include "jOptions.h"

jRHI_Vulkan* g_rhi_vk = nullptr;
robin_hood::unordered_map<size_t, VkPipelineLayout> jRHI_Vulkan::PipelineLayoutPool;
robin_hood::unordered_map<size_t, jShaderBindingsLayout*> jRHI_Vulkan::ShaderBindingPool;
TResourcePool<jSamplerStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::SamplerStatePool;
TResourcePool<jRasterizationStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::RasterizationStatePool;
//TResourcePool<jMultisampleStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::MultisampleStatePool;
TResourcePool<jStencilOpStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::StencilOpStatePool;
TResourcePool<jDepthStencilStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::DepthStencilStatePool;
TResourcePool<jBlendingStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::BlendingStatePool;
TResourcePool<jPipelineStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::PipelineStatePool;
TResourcePool<jRenderPass_Vulkan, jMutexRWLock> jRHI_Vulkan::RenderPassPool;

// jGPUDebugEvent
jGPUDebugEvent::jGPUDebugEvent(const char* InName, jCommandBuffer* InCommandBuffer)
{
	if (g_rhi_vk->vkCmdDebugMarkerBegin)
	{
		CommandBuffer = InCommandBuffer;

		check(CommandBuffer);
		VkDebugMarkerMarkerInfoEXT markerInfo = {};
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;

		const float DefaultColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
		memcpy(markerInfo.color, &DefaultColor[0], sizeof(float) * 4);

		markerInfo.pMarkerName = InName;
		g_rhi_vk->vkCmdDebugMarkerBegin((VkCommandBuffer)CommandBuffer->GetHandle(), &markerInfo);
	}
}

jGPUDebugEvent::jGPUDebugEvent(const char* InName, jCommandBuffer* InCommandBuffer, const Vector4& InColor)
{
    if (g_rhi_vk->vkCmdDebugMarkerBegin)
    {
        CommandBuffer = InCommandBuffer;

        check(CommandBuffer);
        VkDebugMarkerMarkerInfoEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;

		static_assert(sizeof(markerInfo.color) == sizeof(Vector4));
        memcpy(markerInfo.color, &InColor, sizeof(markerInfo.color));

		markerInfo.pMarkerName = InName;
        g_rhi_vk->vkCmdDebugMarkerBegin((VkCommandBuffer)CommandBuffer->GetHandle(), &markerInfo);
    }
}

jGPUDebugEvent::~jGPUDebugEvent()
{
	if (g_rhi_vk->vkCmdDebugMarkerEnd)
	{
		g_rhi_vk->vkCmdDebugMarkerEnd((VkCommandBuffer)CommandBuffer->GetHandle());
	}
}

// jFrameBuffer_Vulkan
struct jFrameBuffer_Vulkan : public jFrameBuffer
{
	bool IsInitialized = false;
	std::vector<std::shared_ptr<jTexture> > AllTextures;

	virtual jTexture* GetTexture(int32 index = 0) const { return AllTextures[index].get(); }
};

// jRHI_Vulkan
jRHI_Vulkan::jRHI_Vulkan()
{
	g_rhi_vk = this;
}


jRHI_Vulkan::~jRHI_Vulkan()
{
}

bool jRHI_Vulkan::InitRHI()
{
    g_rhi = this;

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    Window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(Window, this);

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	// Must
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// add extension
	auto extensions = jVulkanDeviceUtil::GetRequiredInstanceExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

#if ENABLE_VALIDATION_LAYER
	// check validation layer
	if (!ensure(jVulkanDeviceUtil::CheckValidationLayerSupport()))
		return false;
#endif

	// add validation layer
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
#if ENABLE_VALIDATION_LAYER
	createInfo.enabledLayerCount = static_cast<uint32>(jVulkanDeviceUtil::validationLayers.size());
	createInfo.ppEnabledLayerNames = jVulkanDeviceUtil::validationLayers.data();

	jVulkanDeviceUtil::PopulateDebutMessengerCreateInfo(debugCreateInfo);
	createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
#else
	createInfo.enabledLayerCount = 0;
	createInfo.pNext = nullptr;
#endif // ENABLE_VALIDATION_LAYER

	VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);

	if (!ensure(result == VK_SUCCESS))
		return false;

	// SetupDebugMessenger
	{
#if ENABLE_VALIDATION_LAYER
		VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
		jVulkanDeviceUtil::PopulateDebutMessengerCreateInfo(createInfo);
		createInfo.pUserData = nullptr;	// optional

		VkResult result = jVulkanDeviceUtil::CreateDebugUtilsMessengerEXT(Instance, &createInfo, nullptr, &DebugMessenger);
		check(result == VK_SUCCESS);
#endif // ENABLE_VALIDATION_LAYER
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

	// Get All device extension properties
    uint32 extensionCount;
    vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &extensionCount, availableExtensions.data());

	bool IsSupportDebugMarker = false;

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

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = static_cast<uint32>(queueCreateInfos.size());

		VkPhysicalDeviceFeatures deviceFeatures = {};
		deviceFeatures.samplerAnisotropy = true;		// VkSampler 가 Anisotropy 를 사용할 수 있도록 하기 위해 true로 설정
		deviceFeatures.sampleRateShading = true;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
		deviceFeatures.multiDrawIndirect = true;
		deviceFeatures.geometryShader = true;
		deviceFeatures.depthClamp = true;

        VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
        physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        physicalDeviceFeatures2.features = deviceFeatures;		// VkSampler 가 Anisotropy 를 사용할 수 있도록 하기 위해 true로 설정

		// Added VariableShadingRate features
#if USE_VARIABLE_SHADING_RATE_TIER2
		VkPhysicalDeviceShadingRateImageFeaturesNV enabledPhysicalDeviceShadingRateImageFeaturesNV{};
        enabledPhysicalDeviceShadingRateImageFeaturesNV.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV;
        enabledPhysicalDeviceShadingRateImageFeaturesNV.shadingRateImage = true;
		physicalDeviceFeatures2.pNext = &enabledPhysicalDeviceShadingRateImageFeaturesNV;
        createInfo.pNext = &physicalDeviceFeatures2;
#else
		createInfo.pNext = &physicalDeviceFeatures2;		// disable VRS
#endif

		// Added CustomBorderColor features
		VkPhysicalDeviceCustomBorderColorFeaturesEXT enabledCustomBorderColorFeaturesEXT{};
		enabledCustomBorderColorFeaturesEXT.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
		enabledCustomBorderColorFeaturesEXT.customBorderColors = true;
		enabledCustomBorderColorFeaturesEXT.customBorderColorWithoutFormat = true;
		enabledCustomBorderColorFeaturesEXT.pNext = (void*)createInfo.pNext;
        createInfo.pNext = &enabledCustomBorderColorFeaturesEXT;

		// createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.pEnabledFeatures = nullptr;

		// extension
		// Prepare extensions
		std::vector<const char*> DeviceExtensions(jVulkanDeviceUtil::DeviceExtensions);

		// Add debug marker extension when it can be enabled
		for (auto& extension : availableExtensions)
		{
			if (!strcmp(extension.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
			{
				DeviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
				IsSupportDebugMarker = true;
				break;
			}
		}

		createInfo.enabledExtensionCount = static_cast<uint32>(DeviceExtensions.size());
		createInfo.ppEnabledExtensionNames = DeviceExtensions.data();

#if ENABLE_VALIDATION_LAYER
		// 최신 버젼에서는 validation layer는 무시되지만, 오래된 버젼을 호환을 위해 vkInstance와 맞춰줌
		createInfo.enabledLayerCount = static_cast<uint32>(jVulkanDeviceUtil::validationLayers.size());
		createInfo.ppEnabledLayerNames = jVulkanDeviceUtil::validationLayers.data();
#else
		createInfo.enabledLayerCount = 0;
#endif // ENABLE_VALIDATION_LAYER

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

	MemoryPool = new jMemoryPool_Vulkan();

	// Get vkCmdBindShadingRateImageNV function pointer for VRS
	vkCmdBindShadingRateImageNV = reinterpret_cast<PFN_vkCmdBindShadingRateImageNV>(vkGetDeviceProcAddr(Device, "vkCmdBindShadingRateImageNV"));

	// Get debug marker function pointer
	if (IsSupportDebugMarker)
	{
        vkDebugMarkerSetObjectTag = (PFN_vkDebugMarkerSetObjectTagEXT)vkGetDeviceProcAddr(Device, "vkDebugMarkerSetObjectTagEXT");
        vkDebugMarkerSetObjectName = (PFN_vkDebugMarkerSetObjectNameEXT)vkGetDeviceProcAddr(Device, "vkDebugMarkerSetObjectNameEXT");
        vkCmdDebugMarkerBegin = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(Device, "vkCmdDebugMarkerBeginEXT");
        vkCmdDebugMarkerEnd = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(Device, "vkCmdDebugMarkerEndEXT");
        vkCmdDebugMarkerInsert = (PFN_vkCmdDebugMarkerInsertEXT)vkGetDeviceProcAddr(Device, "vkCmdDebugMarkerInsertEXT");
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

	QueryPoolTime = new jQueryPoolTime_Vulkan();
	QueryPoolTime->Create();

	QueryPoolOcclusion = new jQueryPoolOcclusion_Vulkan();
	QueryPoolOcclusion->Create();

	OneFrameUniformRingBuffers.resize(Swapchain->GetNumOfSwapchain());
	for (auto& iter : OneFrameUniformRingBuffers)
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
		iter->Create(10000);
	}

	DescriptorPools2 = new jDescriptorPool_Vulkan();
	DescriptorPools2->Create(10000);

    jImGUI_Vulkan::Get().Initialize((float)SCR_WIDTH, (float)SCR_HEIGHT);

	CreateSampleVRSTexture();

	// Create InstanceVertexBuffer for cube map six face
	// todo : It will be removed, when I have a system that can generate per instance data for visible faces
	{
        struct jFaceInstanceData
        {
            uint16 LayerIndex = 0;
        };
        jFaceInstanceData instanceData[6];
        for (int16 i = 0; i < _countof(instanceData); ++i)
        {
            instanceData[i].LayerIndex = i;
        }

        auto streamParam = std::make_shared<jStreamParam<jFaceInstanceData>>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT16, sizeof(uint16)));
        streamParam->Stride = sizeof(jFaceInstanceData);
        streamParam->Name = jName("InstanceData");
        streamParam->Data.resize(6);
        memcpy(&streamParam->Data[0], instanceData, sizeof(instanceData));

        auto VertexStream_InstanceData = std::make_shared<jVertexStreamData>();
        VertexStream_InstanceData->ElementCount = _countof(instanceData);
        VertexStream_InstanceData->StartLocation = 1;
        VertexStream_InstanceData->BindingIndex = 1;
        VertexStream_InstanceData->VertexInputRate = EVertexInputRate::INSTANCE;
        VertexStream_InstanceData->Params.push_back(streamParam);
		CubeMapInstanceDataForSixFace = g_rhi->CreateVertexBuffer(VertexStream_InstanceData);
	}

	return true;
}

void jRHI_Vulkan::ReleaseRHI()
{
	Flush();

    delete SampleVRSTexture;
	SampleVRSTexture = nullptr;

	jImageFileLoader::ReleaseInstance();
    jImGUI_Vulkan::ReleaseInstance();

    RenderPassPool.Release();
    SamplerStatePool.Release();
    RasterizationStatePool.Release();
    StencilOpStatePool.Release();
    DepthStencilStatePool.Release();
    BlendingStatePool.Release();
    PipelineStatePool.Release();

    jTexture_Vulkan::DestroyDefaultSamplerState();
    jFrameBufferPool::Release();
    jRenderTargetPool::Release();

	delete Swapchain;
	Swapchain = nullptr;

	delete CommandBufferManager;
	CommandBufferManager = nullptr;

	{
		jScopeWriteLock s(&PipelineLayoutPoolLock);
		for (auto& iter : PipelineLayoutPool)
			vkDestroyPipelineLayout(Device, iter.second, nullptr);
		PipelineLayoutPool.clear();
	}

	{
		jScopeWriteLock s(&ShaderBindingPoolLock);
		for (auto& iter : ShaderBindingPool)
			delete iter.second;
		ShaderBindingPool.clear();
	}

	delete QueryPoolTime;
	QueryPoolTime = nullptr;

	delete QueryPoolOcclusion;
	QueryPoolOcclusion = nullptr;

	for (auto& iter : OneFrameUniformRingBuffers)
		delete iter;
	OneFrameUniformRingBuffers.clear();

    for (auto& iter : SSBORingBuffers)
        delete iter;
	SSBORingBuffers.clear();

	for (auto& iter : DescriptorPools)
		delete iter;
	DescriptorPools.clear();

	vkDestroyPipelineCache(Device, PipelineCache, nullptr);
	FenceManager.Release();
	SemaphoreManager.Release();

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

	if (CubeMapInstanceDataForSixFace)
	{
		delete CubeMapInstanceDataForSixFace;
		CubeMapInstanceDataForSixFace = nullptr;
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
	indexBuffer->Initialize(streamData);
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
	Flush();

	int32 width = 0, height = 0;
    glfwGetFramebufferSize(Window, &width, &height);
    while (width == 0 || height == 0)
    {
    	glfwGetFramebufferSize(Window, &width, &height);
    	glfwWaitEvents();
    }

	SCR_WIDTH = width;
	SCR_HEIGHT = height;

    delete CommandBufferManager;
	CommandBufferManager = new jCommandBufferManager_Vulkan();
	verify(CommandBufferManager->CreatePool(GraphicsQueue.QueueIndex));

    jFrameBufferPool::Release();
    jRenderTargetPool::ReleaseForRecreateSwapchain();
	PipelineStatePool.Release();
    RenderPassPool.Release();

    delete Swapchain;
    Swapchain = new jSwapchain_Vulkan();
    verify(Swapchain->Create());

    jImGUI_Vulkan::Get().Release();
    jImGUI_Vulkan::Get().Initialize((float)SCR_WIDTH, (float)SCR_HEIGHT);

    CurrentFrameIndex = 0;

	delete SampleVRSTexture;
	SampleVRSTexture = nullptr;
	CreateSampleVRSTexture();

	Flush();
}

uint32 jRHI_Vulkan::GetMaxSwapchainCount() const
{
	check(Swapchain);
	return (uint32)Swapchain->Images.size();
}

jVertexBuffer* jRHI_Vulkan::CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	jVertexBuffer_Vulkan* vertexBuffer = new jVertexBuffer_Vulkan();
	vertexBuffer->Initialize(streamData);
	return vertexBuffer;
}

jTexture* jRHI_Vulkan::CreateTextureFromData(void* data, int32 width, int32 height, bool sRGB
	, ETextureFormat textureFormat, bool createMipmap) const
{
    VkDeviceSize imageSize = width * height * GetVulkanTexturePixelSize(textureFormat);
    const uint32 MipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(width, height)))) + 1;

	jBuffer_Vulkan stagingBuffer;
	jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
        , imageSize, stagingBuffer);

	stagingBuffer.UpdateBuffer(data, imageSize);

	VkFormat vkTextureFormat = GetVulkanTextureFormat(textureFormat);

	VkImage TextureImage;
	VkDeviceMemory TextureImageMemory;
    if (!ensure(jVulkanBufferUtil::CreateImage((uint32)width, (uint32)height, MipLevels, VK_SAMPLE_COUNT_1_BIT, vkTextureFormat
        , VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT	// image를 shader 에서 접근가능하게 하고 싶은 경우
        , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, TextureImage, TextureImageMemory)))
    {
        return nullptr;
    }

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
	ensure(TransitionImageLayout(commandBuffer, TextureImage, vkTextureFormat, MipLevels, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

	jVulkanBufferUtil::CopyBufferToImage(commandBuffer, stagingBuffer.Buffer, stagingBuffer.Offset, TextureImage, (uint32)width, (uint32)height);

    // 밉맵을 만드는 동안 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 으로 전환됨.
    if (createMipmap)
    {
        jVulkanBufferUtil::GenerateMipmaps(commandBuffer, TextureImage, vkTextureFormat, width, height, MipLevels
			, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
	else
	{
        ensure(TransitionImageLayout(commandBuffer, TextureImage, vkTextureFormat, MipLevels, 1
			, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	}

	EndSingleTimeCommands(commandBuffer);

	stagingBuffer.Release();

    // Create Texture image view
    VkImageView textureImageView = jVulkanBufferUtil::CreateImageView(TextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, MipLevels);

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

    auto LoadShaderFromSRV = [](const char* fileName) -> VkShaderModule
    {
        std::ifstream is(fileName, std::ios::binary | std::ios::in | std::ios::ate);

        if (is.is_open())
        {
            size_t size = is.tellg();
            is.seekg(0, std::ios::beg);
            char* shaderCode = new char[size];
            is.read(shaderCode, size);
            is.close();

            assert(size > 0);

            VkShaderModule shaderModule = nullptr;
            VkShaderModuleCreateInfo moduleCreateInfo{};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = size;
            moduleCreateInfo.pCode = (uint32*)shaderCode;

            verify(VK_SUCCESS == vkCreateShaderModule(g_rhi_vk->Device, &moduleCreateInfo, NULL, &shaderModule));
            delete[] shaderCode;

            return shaderModule;
        }
        return nullptr;
    };

	jShader* shader_vk = OutShader;
	check(shader_vk->GetPermutationCount());
	{
		check(!shader_vk->CompiledShader);
		jCompiledShader_Vulkan* CurCompiledShader = new jCompiledShader_Vulkan();
		shader_vk->CompiledShader = CurCompiledShader;

		// PermutationId 를 설정하여 컴파일을 준비함
        shader_vk->SetPermutationId(shaderInfo.GetPermutationId());

		std::string PermutationDefines;
		shader_vk->GetPermutationDefines(PermutationDefines);

		VkShaderStageFlagBits ShaderFlagBits;
		ShaderConductor::ShaderStage ShaderConductorShaderStage;
		EShLanguage ShaderLangShaderStage;
		switch (shaderInfo.GetShaderType())
		{
		case EShaderAccessStageFlag::VERTEX:
			ShaderFlagBits = VK_SHADER_STAGE_VERTEX_BIT;
			ShaderConductorShaderStage = ShaderConductor::ShaderStage::VertexShader;
			ShaderLangShaderStage = EShLangVertex;
			break;
		case EShaderAccessStageFlag::GEOMETRY:
			ShaderFlagBits = VK_SHADER_STAGE_GEOMETRY_BIT;
			ShaderConductorShaderStage = ShaderConductor::ShaderStage::GeometryShader;
			ShaderLangShaderStage = EShLangGeometry;
			break;
		case EShaderAccessStageFlag::FRAGMENT:
			ShaderFlagBits = VK_SHADER_STAGE_FRAGMENT_BIT;
			ShaderConductorShaderStage = ShaderConductor::ShaderStage::PixelShader;
			ShaderLangShaderStage = EShLangFragment;
			break;
		case EShaderAccessStageFlag::COMPUTE:
			ShaderFlagBits = VK_SHADER_STAGE_COMPUTE_BIT;
			ShaderConductorShaderStage = ShaderConductor::ShaderStage::ComputeShader;
			ShaderLangShaderStage = EShLangCompute;
			break;
		default:
			check(0);
			break;
		}

		VkShaderModule shaderModule{};

		const bool isSpirv = !!strstr(shaderInfo.GetShaderFilepath().ToStr(), ".spv");
		if (isSpirv)
		{
			shaderModule = LoadShaderFromSRV(shaderInfo.GetShaderFilepath().ToStr());
		}
		else
		{
            const bool isHLSL = !!strstr(shaderInfo.GetShaderFilepath().ToStr(), ".hlsl");

            jFile ShaderFile;
            ShaderFile.OpenFile(shaderInfo.GetShaderFilepath().ToStr(), FileType::TEXT, ReadWriteType::READ);
            ShaderFile.ReadFileToBuffer(false);
            std::string ShaderText;

            if (shaderInfo.GetPreProcessors().GetStringLength() > 0)
            {
                ShaderText += shaderInfo.GetPreProcessors().ToStr();
                ShaderText += "\r\n";
            }
            ShaderText += PermutationDefines;
            ShaderText += "\r\n";

            ShaderText += ShaderFile.GetBuffer();

			// Find relative file path
			constexpr char includePrefixString[] = "#include \"";
			constexpr int32 includePrefixLength = sizeof(includePrefixString) - 1;

			const std::filesystem::path shaderFilePath(shaderInfo.GetShaderFilepath().ToStr());
			const std::string includeShaderPath = shaderFilePath.has_parent_path() ? (shaderFilePath.parent_path().string() + "/") : "";

			std::set<std::string> AlreadyIncludedSets;
			while (1)
			{
				size_t startOfInclude = ShaderText.find(includePrefixString);
				if (startOfInclude == std::string::npos)
					break;

				// Parse include file path
				startOfInclude += includePrefixLength;
				size_t endOfInclude = ShaderText.find("\"", startOfInclude);
				std::string includeFilepath = includeShaderPath + ShaderText.substr(startOfInclude, endOfInclude - startOfInclude);

                // Replace range '#include "filepath"' with shader file content
                const size_t ReplaceStartPos = startOfInclude - includePrefixLength;
                const size_t ReplaceCount = endOfInclude - ReplaceStartPos + 1;

				if (AlreadyIncludedSets.contains(includeFilepath))
				{
					ShaderText.replace(ReplaceStartPos, ReplaceCount, "");
					continue;
				}

				// If already included file, skip it.
				AlreadyIncludedSets.insert(includeFilepath);

				// Load include shader file
                jFile IncludeShaderFile;
                IncludeShaderFile.OpenFile(includeFilepath.c_str(), FileType::TEXT, ReadWriteType::READ);
                IncludeShaderFile.ReadFileToBuffer(false);
				ShaderText.replace(ReplaceStartPos, ReplaceCount, IncludeShaderFile.GetBuffer());
				IncludeShaderFile.CloseFile();
			}

            std::vector<uint32> SpirvCode;
			if (isHLSL)
				jSpirvHelper::HLSLtoSpirv(SpirvCode, ShaderConductorShaderStage, ShaderText.c_str());
			else
				jSpirvHelper::GLSLtoSpirv(SpirvCode, ShaderLangShaderStage, ShaderText.data());

			shaderModule = CreateShaderModule(SpirvCode);
		}

		CurCompiledShader->ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		CurCompiledShader->ShaderStage.stage = ShaderFlagBits;
		CurCompiledShader->ShaderStage.module = shaderModule;
		CurCompiledShader->ShaderStage.pName = "main";
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
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VkImageCreateFlagBits(0), image, imageMemory);
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
	const VkImageUsageFlags AllowingUsageFlag = info.IsMemoryless
		? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
		: VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;

	const VkImageUsageFlags ImageUsageFlag = AllowingUsageFlag & 
		((hasDepthAttachment ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT)
		| VK_IMAGE_USAGE_SAMPLED_BIT 
		| (info.IsUseAsSubpassInput ? VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT : 0)
		| (info.IsMemoryless ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0));
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
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VkImageCreateFlagBits(0), image, imageMemory);
		imageView = jVulkanBufferUtil::CreateImage2DArrayView(image, info.LayerCount, textureFormat, ImageAspectFlag, mipLevels);
		break;
	case ETextureType::TEXTURE_CUBE:
		check(info.LayerCount == 6);
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
    check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDraw((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), vertCount, 1, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDraw((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), vertCount, instanceCount, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), count, 1, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), count, instanceCount, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), count, 1, startIndex, baseVertexIndex, 0);
}

void jRHI_Vulkan::DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const
{
	check(InRenderFrameContext);
	check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), count, instanceCount, startIndex, baseVertexIndex, 0);
}

void jRHI_Vulkan::DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
    vkCmdDrawIndirect((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), (VkBuffer)buffer->GetHandle()
        , startIndex * sizeof(VkDrawIndirectCommand), drawCount, sizeof(VkDrawIndirectCommand));
}

void jRHI_Vulkan::DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
	, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
	vkCmdDrawIndexedIndirect((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), (VkBuffer)buffer->GetHandle()
		, startIndex * sizeof(VkDrawIndexedIndirectCommand), drawCount, sizeof(VkDrawIndexedIndirectCommand));
}

jShaderBindingsLayout* jRHI_Vulkan::CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const
{
	size_t hash = InShaderBindingArray.GetHash();

	{
		jScopeReadLock sr(&ShaderBindingPoolLock);

		auto it_find = ShaderBindingPool.find(hash);
		if (ShaderBindingPool.end() != it_find)
			return it_find->second;
	}

	{
		jScopeWriteLock sw(&ShaderBindingPoolLock);

		// Try again, to avoid entering creation section simultanteously.
        auto it_find = ShaderBindingPool.find(hash);
        if (ShaderBindingPool.end() != it_find)
            return it_find->second;

		auto NewShaderBinding = new jShaderBindingsLayout_Vulkan();
		NewShaderBinding->Initialize(InShaderBindingArray);
		NewShaderBinding->Hash = hash;
		ShaderBindingPool[hash] = NewShaderBinding;

		return NewShaderBinding;
	}
}

jShaderBindingInstance* jRHI_Vulkan::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
	auto shaderBindingsLayout = CreateShaderBindings(InShaderBindingArray);
	check(shaderBindingsLayout);
	return shaderBindingsLayout->CreateShaderBindingInstance(InShaderBindingArray, InType);
}

void* jRHI_Vulkan::CreatePipelineLayout(const jShaderBindingsLayoutArray& InShaderBindingLayoutArray, const jPushConstant* pushConstant) const
{
	if (InShaderBindingLayoutArray.NumOfData <= 0)
		return 0;

	VkPipelineLayout vkPipelineLayout = nullptr;
	size_t hash = InShaderBindingLayoutArray.GetHash();

	if (pushConstant)
		hash = CityHash64WithSeed(pushConstant->GetHash(), hash);
	check(hash);

	{
		jScopeReadLock sr(&PipelineLayoutPoolLock);
		auto it_find = PipelineLayoutPool.find(hash);
		if (PipelineLayoutPool.end() != it_find)
		{
			vkPipelineLayout = it_find->second;
			return vkPipelineLayout;
		}
	}

	{
		jScopeWriteLock sw(&PipelineLayoutPoolLock);

		// Try again, to avoid entering creation section simultanteously.
        auto it_find = PipelineLayoutPool.find(hash);
        if (PipelineLayoutPool.end() != it_find)
        {
            vkPipelineLayout = it_find->second;
            return vkPipelineLayout;
        }

		std::vector<VkDescriptorSetLayout> DescriptorSetLayouts;
		DescriptorSetLayouts.reserve(InShaderBindingLayoutArray.NumOfData);
		for (int32 i = 0; i < InShaderBindingLayoutArray.NumOfData; ++i)
		{
			const jShaderBindingsLayout_Vulkan* binding_vulkan = (const jShaderBindingsLayout_Vulkan*)InShaderBindingLayoutArray[i];
			DescriptorSetLayouts.push_back(binding_vulkan->DescriptorSetLayout);
		}

		std::vector<VkPushConstantRange> PushConstantRanges;
		if (pushConstant)
		{
			const jResourceContainer<jPushConstantRange>* pushConstantRanges = pushConstant->GetPushConstantRanges();
			check(pushConstantRanges);
			if (pushConstantRanges)
			{
				PushConstantRanges.reserve(pushConstantRanges->NumOfData);
				for (int32 i = 0; i < pushConstantRanges->NumOfData; ++i)
				{
					const jPushConstantRange& range = (*pushConstantRanges)[i];

					VkPushConstantRange pushConstantRange{};
					pushConstantRange.stageFlags = GetVulkanShaderAccessFlags(range.AccessStageFlag);
					pushConstantRange.offset = range.Offset;
					pushConstantRange.size = range.Size;
					PushConstantRanges.emplace_back(pushConstantRange);
				}
			}
		}

		// 쉐이더에 전달된 Uniform 들을 명세하기 위한 오브젝트
		// 이 오브젝트는 프로그램 실행동안 계속해서 참조되므로 cleanup 에서 제거해줌
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = (uint32)DescriptorSetLayouts.size();
		pipelineLayoutInfo.pSetLayouts = &DescriptorSetLayouts[0];
		if (PushConstantRanges.size() > 0)
		{
			pipelineLayoutInfo.pushConstantRangeCount = (int32)PushConstantRanges.size();
			pipelineLayoutInfo.pPushConstantRanges = PushConstantRanges.data();
		}
		if (!ensure(vkCreatePipelineLayout(Device, &pipelineLayoutInfo, nullptr, &vkPipelineLayout) == VK_SUCCESS))
			return nullptr;

		PipelineLayoutPool[hash] = vkPipelineLayout;
	}

	return vkPipelineLayout;
}

IUniformBufferBlock* jRHI_Vulkan::CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize /*= 0*/) const
{
	auto uniformBufferBlock = new jUniformBufferBlock_Vulkan(InName, InLifeTimeType);
	uniformBufferBlock->Init(InSize);
	return uniformBufferBlock;
}

jQuery* jRHI_Vulkan::CreateQueryTime() const
{
	auto queryTime = new jQueryTime_Vulkan();
	return queryTime;
}

void jRHI_Vulkan::ReleaseQueryTime(jQuery* queryTime) const
{
    auto queryTime_gl = static_cast<jQueryTime_Vulkan*>(queryTime);
    delete queryTime_gl;
}

std::shared_ptr<jRenderFrameContext> jRHI_Vulkan::BeginRenderFrame()
{
	SCOPE_CPU_PROFILE(BeginRenderFrame);

    // timeout 은 nanoseconds. UINT64_MAX 는 타임아웃 없음
    VkResult acquireNextImageResult = vkAcquireNextImageKHR(Device, (VkSwapchainKHR)Swapchain->GetHandle(), UINT64_MAX
		, (VkSemaphore)Swapchain->Images[CurrentFrameIndex]->Available->GetHandle(), VK_NULL_HANDLE, &CurrentFrameIndex);

    VkFence lastCommandBufferFence = Swapchain->Images[CurrentFrameIndex]->CommandBufferFence;

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

	GetOneFrameUniformRingBuffer()->Reset();
	GetDescriptorPoolForSingleFrame()->Reset();

    // 이 프레임에서 펜스를 사용한다고 마크 해둠
	Swapchain->Images[CurrentFrameIndex]->CommandBufferFence = (VkFence)commandBuffer->GetFenceHandle();

    auto renderFrameContextPtr = std::make_shared<jRenderFrameContext>(commandBuffer);
	renderFrameContextPtr->UseForwardRenderer = !gOptions.UseDeferredRenderer;
	renderFrameContextPtr->FrameIndex = CurrentFrameIndex;
	renderFrameContextPtr->SceneRenderTarget = new jSceneRenderTarget();
	renderFrameContextPtr->SceneRenderTarget->Create(Swapchain->GetSwapchainImage(CurrentFrameIndex));
	renderFrameContextPtr->CurrentWaitSemaphore = Swapchain->Images[CurrentFrameIndex]->Available;

	return renderFrameContextPtr;
}

void jRHI_Vulkan::EndRenderFrame(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr)
{
	SCOPE_CPU_PROFILE(EndRenderFrame);

	//check(renderFrameContextPtr->CommandBuffer);
	//VkCommandBuffer vkCommandBuffer = (VkCommandBuffer)renderFrameContextPtr->CommandBuffer->GetHandle();
	//VkFence vkFence = (VkFence)renderFrameContextPtr->CommandBuffer->GetFenceHandle();
	//const uint32 imageIndex = (uint32)CurrenFrameIndex;

 //   // Submitting the command buffer
 //   VkSubmitInfo submitInfo = {};
 //   submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

 //   VkSemaphore waitsemaphores[] = { Swapchain->Images[CurrenFrameIndex]->Available };
 //   VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
 //   submitInfo.waitSemaphoreCount = 1;
 //   submitInfo.pWaitSemaphores = waitsemaphores;
 //   submitInfo.pWaitDstStageMask = waitStages;
 //   submitInfo.commandBufferCount = 1;
 //   submitInfo.pCommandBuffers = &vkCommandBuffer;

 //   VkSemaphore signalSemaphores[] = { Swapchain->Images[CurrenFrameIndex]->RenderFinished };
 //   submitInfo.signalSemaphoreCount = 1;
 //   submitInfo.pSignalSemaphores = signalSemaphores;

 //   // SubmitInfo를 동시에 할수도 있음.
 //   vkResetFences(Device, 1, &vkFence);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

 //   // 마지막에 Fences 파라메터는 커맨드 버퍼가 모두 실행되고나면 Signaled 될 Fences.
	//auto queueSubmitResult = vkQueueSubmit(GraphicsQueue.Queue, 1, &submitInfo, vkFence);
 //   if ((queueSubmitResult == VK_ERROR_OUT_OF_DATE_KHR) || (queueSubmitResult == VK_SUBOPTIMAL_KHR))
 //   {
 //       RecreateSwapChain();
	//	return;
 //   }
 //   else if (queueSubmitResult != VK_SUCCESS)
 //   {
 //       check(0);
 //       return;
 //   }

	VkSemaphore signalSemaphore[] = { (VkSemaphore)Swapchain->Images[CurrentFrameIndex]->RenderFinished->GetHandle() };
	QueueSubmit(renderFrameContextPtr, Swapchain->Images[CurrentFrameIndex]->RenderFinished);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphore;

    VkSwapchainKHR swapChains[] = { Swapchain->Swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &CurrentFrameIndex;

    // 여러개의 스왑체인에 제출하는 경우만 모든 스왑체인으로 잘 제출 되었나 확인하는데 사용
    // 1개인 경우 그냥 vkQueuePresentKHR 의 리턴값을 확인하면 됨.
    presentInfo.pResults = nullptr;			// Optional
    VkResult queuePresentResult = vkQueuePresentKHR(PresentQueue.Queue, &presentInfo);

	// CPU 가 큐에 작업을 제출하는 속도가 GPU 에서 소모하는 속도보다 빠른 경우 큐에 작업이 계속 쌓이거나 
    // 여러 프레임에 걸쳐 동시에 imageAvailableSemaphore 와 renderFinishedSemaphore를 재사용하게 되는 문제가 있음.
    // 1). 한프레임을 마치고 큐가 빌때까지 기다리는 것으로 해결할 수 있음. 한번에 1개의 프레임만 완성 가능(최적의 해결방법은 아님)
    // 2). 여러개의 프레임을 동시에 처리 할수있도록 확장. 동시에 진행될 수 있는 최대 프레임수를 지정해줌.
    CurrentFrameIndex = (CurrentFrameIndex + 1) % Swapchain->Images.size();
    renderFrameContextPtr->Destroy();

    // 세마포어의 일관된 상태를 보장하기 위해서(세마포어 로직을 변경하지 않으려 노력한듯 함) vkQueuePresentKHR 이후에 framebufferResized 를 체크하는 것이 중요함.
    if ((queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR) || (queuePresentResult == VK_SUBOPTIMAL_KHR) || FramebufferResized)
    {
        RecreateSwapChain();
		FramebufferResized = false;
		return;
    }
    else if (queuePresentResult != VK_SUCCESS)
    {
        check(0);
        return;
    }
}

void jRHI_Vulkan::QueueSubmit(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr, jSemaphore* InSignalSemaphore)
{
    SCOPE_CPU_PROFILE(QueueSubmit);

    check(renderFrameContextPtr->GetActiveCommandBuffer());
    VkCommandBuffer vkCommandBuffer = (VkCommandBuffer)renderFrameContextPtr->GetActiveCommandBuffer()->GetHandle();
    VkFence vkFence = (VkFence)renderFrameContextPtr->GetActiveCommandBuffer()->GetFenceHandle();
	
    // Submitting the command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitsemaphores[] = { (VkSemaphore)renderFrameContextPtr->CurrentWaitSemaphore->GetHandle() };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitsemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkCommandBuffer;

    renderFrameContextPtr->CurrentWaitSemaphore = InSignalSemaphore;

    VkSemaphore signalSemaphores[] = { (VkSemaphore)InSignalSemaphore->GetHandle() };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // SubmitInfo를 동시에 할수도 있음.
    vkResetFences(Device, 1, &vkFence);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

    // 마지막에 Fences 파라메터는 커맨드 버퍼가 모두 실행되고나면 Signaled 될 Fences.
    auto queueSubmitResult = vkQueueSubmit(GraphicsQueue.Queue, 1, &submitInfo, vkFence);
    if ((queueSubmitResult == VK_ERROR_OUT_OF_DATE_KHR) || (queueSubmitResult == VK_SUBOPTIMAL_KHR))
    {
        RecreateSwapChain();
    }
    else if (queueSubmitResult != VK_SUCCESS)
    {
        check(0);
    }
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

bool jRHI_Vulkan::TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, uint32 mipLevels, uint32 layoutCount, VkImageLayout oldLayout, VkImageLayout newLayout) const
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
		if (IsDepthOnlyFormat(GetVulkanTextureFormat(format)))
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		else
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
	barrier.subresourceRange.layerCount = layoutCount;
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
		, texture_vk->MipLevel, texture_vk->LayerCount, GetVulkanImageLayout(texture_vk->Layout), GetVulkanImageLayout(newLayout)))
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
			, texture_vk->MipLevel, 1, GetVulkanImageLayout(texture_vk->Layout), GetVulkanImageLayout(newLayout));
		if (ret)
			((jTexture_Vulkan*)texture)->Layout = newLayout;

		EndSingleTimeCommands(commandBuffer);
		return ret;
	}

	return false;
}

jPipelineStateInfo* jRHI_Vulkan::CreatePipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader, const jVertexBufferArray& InVertexBufferArray
	, const jRenderPass* InRenderPass, const jShaderBindingsLayoutArray& InShaderBindingArray, const jPushConstant* InPushConstant, int32 InSubpassIndex) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(InPipelineStateFixed, InShader, InVertexBufferArray, InRenderPass, InShaderBindingArray, InPushConstant, InSubpassIndex)));
}

jPipelineStateInfo* jRHI_Vulkan::CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingsLayoutArray& InShaderBindingArray
	, const jPushConstant* pushConstant) const
{
    return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(shader, InShaderBindingArray, pushConstant)));
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

jRenderPass* jRHI_Vulkan::GetOrCreateRenderPass(const jRenderPassInfo& renderPassInfo, const Vector2i& offset, const Vector2i& extent) const
{
	return RenderPassPool.GetOrCreate(jRenderPass_Vulkan(renderPassInfo, offset, extent));
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

void jRHI_Vulkan::BindShadingRateImage(jCommandBuffer* commandBuffer, jTexture* vrstexture) const
{
	check(commandBuffer);
    g_rhi_vk->vkCmdBindShadingRateImageNV((VkCommandBuffer)commandBuffer->GetHandle()
        , (vrstexture ? (VkImageView)vrstexture->GetViewHandle() : nullptr), VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
}

void jRHI_Vulkan::NextSubpass(const jCommandBuffer* commandBuffer) const
{
	check(commandBuffer);
	vkCmdNextSubpass((VkCommandBuffer)commandBuffer->GetHandle(), VK_SUBPASS_CONTENTS_INLINE);
}

jTexture* jRHI_Vulkan::CreateSampleVRSTexture()
{
    if (ensure(!SampleVRSTexture))
    {
        VkPhysicalDeviceShadingRateImagePropertiesNV physicalDeviceShadingRateImagePropertiesNV{};
        physicalDeviceShadingRateImagePropertiesNV.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV;
        VkPhysicalDeviceProperties2 deviceProperties2{};
        deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties2.pNext = &physicalDeviceShadingRateImagePropertiesNV;
        vkGetPhysicalDeviceProperties2(g_rhi_vk->PhysicalDevice, &deviceProperties2);

        VkExtent3D imageExtent{};
        imageExtent.width = static_cast<uint32_t>(ceil(SCR_WIDTH / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.width));
        imageExtent.height = static_cast<uint32_t>(ceil(SCR_HEIGHT / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.height));
        imageExtent.depth = 1;

		auto NewVRSTexture = new jTexture_Vulkan();

        jVulkanBufferUtil::CreateImage(imageExtent.width, imageExtent.height, 1, (VkSampleCountFlagBits)1, GetVulkanTextureFormat(ETextureFormat::R8UI), VK_IMAGE_TILING_OPTIMAL
            , VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, *NewVRSTexture);

        VkDeviceSize imageSize = imageExtent.width * imageExtent.height * GetVulkanTexturePixelSize(ETextureFormat::R8UI);
        jBuffer_Vulkan stagingBuffer;
        jVulkanBufferUtil::AllocateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
            , imageSize, stagingBuffer);

		VkCommandBuffer commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
        ensure(g_rhi_vk->TransitionImageLayout(commandBuffer, (VkImage)NewVRSTexture->GetHandle(), GetVulkanTextureFormat(ETextureFormat::R8UI), 1, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

        jVulkanBufferUtil::CopyBufferToImage(commandBuffer, stagingBuffer.Buffer, stagingBuffer.Offset, (VkImage)NewVRSTexture->GetHandle()
            , static_cast<uint32>(imageExtent.width), static_cast<uint32>(imageExtent.height));

        // Create a circular pattern with decreasing sampling rates outwards (max. range, pattern)
        std::map<float, VkShadingRatePaletteEntryNV> patternLookup = {
            { 1.5f * 8.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV },
            { 1.5f * 12.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV },
            { 1.5f * 16.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV },
            { 1.5f * 18.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV },
            { 1.5f * 20.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV },
            { 1.5f * 24.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV }
        };

        VkDeviceSize bufferSize = imageExtent.width * imageExtent.height * sizeof(uint8);
        uint8_t val = VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV;
        uint8_t* shadingRatePatternData = new uint8_t[bufferSize];
        memset(shadingRatePatternData, val, bufferSize);
        uint8_t* ptrData = shadingRatePatternData;
        for (uint32_t y = 0; y < imageExtent.height; y++) {
            for (uint32_t x = 0; x < imageExtent.width; x++) {
                const float deltaX = (float)imageExtent.width / 2.0f - (float)x;
                const float deltaY = ((float)imageExtent.height / 2.0f - (float)y) * ((float)SCR_WIDTH / (float)SCR_HEIGHT);
                const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
                for (auto pattern : patternLookup) {
                    if (dist < pattern.first) {
                        *ptrData = pattern.second;
                        break;
                    }
                }
                ptrData++;
            }
        }

        check(imageSize == bufferSize);

        stagingBuffer.UpdateBuffer(shadingRatePatternData, bufferSize);

        g_rhi_vk->EndSingleTimeCommands(commandBuffer);

        stagingBuffer.Release();
        delete[]shadingRatePatternData;

		NewVRSTexture->View = jVulkanBufferUtil::CreateImageView((VkImage)NewVRSTexture->GetHandle(), GetVulkanTextureFormat(NewVRSTexture->Format)
            , VK_IMAGE_ASPECT_COLOR_BIT, 1);

		SampleVRSTexture = NewVRSTexture;
    }
	return SampleVRSTexture;
}

void jRHI_Vulkan::BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineStateLayout
	, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
	if (InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData)
	{
		check(InCommandBuffer);
		vkCmdBindDescriptorSets((VkCommandBuffer)InCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS
			, (VkPipelineLayout)InPiplineStateLayout->GetPipelineLayoutHandle(), InFirstSet
			, InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData, (const VkDescriptorSet*)&InShaderBindingInstanceCombiner.DescriptorSetHandles[0]
			, InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData, (InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData ? &InShaderBindingInstanceCombiner.DynamicOffsets[0] : nullptr));
	}	
}

void jRHI_Vulkan::BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineStateLayout
	, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
	if (InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData)
	{
		check(InCommandBuffer);
		vkCmdBindDescriptorSets((VkCommandBuffer)InCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE
			, (VkPipelineLayout)InPiplineStateLayout->GetPipelineLayoutHandle(), InFirstSet
			, InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData, (const VkDescriptorSet*)&InShaderBindingInstanceCombiner.DescriptorSetHandles[0]
			, InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData, (InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData ? &InShaderBindingInstanceCombiner.DynamicOffsets[0] : nullptr));
	}
}

//#endif // USE_VULKAN
