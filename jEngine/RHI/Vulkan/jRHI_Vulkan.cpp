#include "pch.h"

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
#include "jVertexBuffer_Vulkan.h"
#include "jIndexBuffer_Vulkan.h"
#include "jRHIType_Vulkan.h"
#include "jTexture_Vulkan.h"
#include "jUniformBufferBlock_Vulkan.h"
#include "jVulkanDeviceUtil.h"
#include "jBufferUtil_Vulkan.h"
#include "jShader_Vulkan.h"
#include "jBuffer_Vulkan.h"
#include "jCommandBufferManager_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "jRingBuffer_Vulkan.h"
#include "jDescriptorPool_Vulkan.h"
#include "jQueryPoolOcclusion_Vulkan.h"
#include "jOptions.h"
#include "jRenderFrameContext_Vulkan.h"
#include "jImGui_Vulkan.h"
#include "Scene/Light/jLight.h"
#include "../DX12/jShaderCompiler_DX12.h"
#include "jRaytracingScene_Vulkan.h"
#include "jResourceBarrierBatcher_Vulkan.h"
#include "../jResourceBarrierBatcher.h"

jRHI_Vulkan* g_rhi_vk = nullptr;
robin_hood::unordered_map<size_t, jShaderBindingLayout*> jRHI_Vulkan::ShaderBindingPool;
TResourcePool<jSamplerStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::SamplerStatePool;
TResourcePool<jRasterizationStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::RasterizationStatePool;
TResourcePool<jStencilOpStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::StencilOpStatePool;
TResourcePool<jDepthStencilStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::DepthStencilStatePool;
TResourcePool<jBlendingStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::BlendingStatePool;
TResourcePool<jPipelineStateInfo_Vulkan, jMutexRWLock> jRHI_Vulkan::PipelineStatePool;
TResourcePool<jRenderPass_Vulkan, jMutexRWLock> jRHI_Vulkan::RenderPassPool;

void jStandaloneResourceVulkan::ReleaseBufferResource(VkBuffer InBuffer, VkDeviceMemory InMemory, const VkAllocationCallbacks* InAllocatorCallback)
{
	check(g_rhi_vk);

	auto StandaloneResourcePtr = std::make_shared<jStandaloneResourceVulkan>();
	StandaloneResourcePtr->Buffer = InBuffer;
	StandaloneResourcePtr->Memory = InMemory;
	StandaloneResourcePtr->AllocatorCallback = InAllocatorCallback;
	g_rhi_vk->DeallocatorMultiFrameStandaloneResource.Free(StandaloneResourcePtr);
}

void jStandaloneResourceVulkan::ReleaseImageResource(VkImage InImage, VkDeviceMemory InMemory, const std::vector<VkImageView>& InViews, const VkAllocationCallbacks* InAllocatorCallback)
{
	check(g_rhi_vk);

	auto StandaloneResourcePtr = std::make_shared<jStandaloneResourceVulkan>();
	StandaloneResourcePtr->Image = InImage;
	StandaloneResourcePtr->Memory = InMemory;
	StandaloneResourcePtr->Views = InViews;
	StandaloneResourcePtr->AllocatorCallback = InAllocatorCallback;
	g_rhi_vk->DeallocatorMultiFrameStandaloneResource.Free(StandaloneResourcePtr);
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

    // Initialize COM library for using DirectTex library
    HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
    if (FAILED(hr))
    {
		OutputDebugStringA("Failed to initialize COM library, this need to be preinitialized before load image by using DirectTex");
		check(0);
    }

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, TRUE);

    Window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(Window, this);

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

#if ENABLE_VALIDATION_LAYER
	// check validation layer
	GIsValidationLayerSupport = jVulkanDeviceUtil::CheckValidationLayerSupport();
#endif

	// Must
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// add extension
	auto extensions = jVulkanDeviceUtil::GetRequiredInstanceExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	// add validation layer
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
#if ENABLE_VALIDATION_LAYER
	if (GIsValidationLayerSupport && gOptions.EnableDebuggerLayer)
	{
		createInfo.enabledLayerCount = static_cast<uint32>(jVulkanDeviceUtil::validationLayers.size());
		createInfo.ppEnabledLayerNames = jVulkanDeviceUtil::validationLayers.data();

		jVulkanDeviceUtil::PopulateDebutMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
	}
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
		if (GIsValidationLayerSupport && gOptions.EnableDebuggerLayer)
		{
			VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
			jVulkanDeviceUtil::PopulateDebutMessengerCreateInfo(createInfo);
			createInfo.pUserData = nullptr;	// optional

			VkResult result = jVulkanDeviceUtil::CreateDebugUtilsMessengerEXT(Instance, &createInfo, nullptr, &DebugMessenger);
			check(result == VK_SUCCESS);
		}
#endif // ENABLE_VALIDATION_LAYER
	}

	{
		ensure(VK_SUCCESS == glfwCreateWindowSurface(Instance, Window, nullptr, &Surface));
	}

	// AvailableExtension from PhysicalDevice
	std::vector<VkExtensionProperties> availableExtensions;

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

		if (!ensure(PhysicalDevice))
			return false;

        // Get All device extension properties
        uint32 extensionCount;
        vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &extensionCount, nullptr);
		
		availableExtensions.resize(extensionCount);
        vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &extensionCount, availableExtensions.data());

#if USE_RAYTRACING
		GSupportRaytracing = jVulkanDeviceUtil::IsSupportRaytracing(availableExtensions);
#else // USE_RAYTRACING
		GSupportRaytracing = false;
#endif // USE_RAYTRACING

		// Check if is enabled that the both 'Non-uniform indexing' and 'Update after bind' flags for resources.
		VkPhysicalDeviceDescriptorIndexingFeatures indexing_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr };

		DeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		DeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		if (GSupportRaytracing)
		{
			RayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
			RayTracingPipelineProperties.pNext = (void*)DeviceProperties.pNext;
			DeviceProperties.pNext = &RayTracingPipelineProperties;
			vkGetPhysicalDeviceProperties2(PhysicalDevice, &DeviceProperties);

			AccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			AccelerationStructureFeatures.pNext = (void*)DeviceProperties.pNext;
			DeviceFeatures2.pNext = &AccelerationStructureFeatures;

			indexing_features.pNext = (void*)DeviceProperties.pNext;
			DeviceFeatures2.pNext = &indexing_features;
			vkGetPhysicalDeviceFeatures2(PhysicalDevice, &DeviceFeatures2);
		}
		else
		{
			vkGetPhysicalDeviceProperties2(PhysicalDevice, &DeviceProperties);

			DeviceFeatures2.pNext = &indexing_features;
			vkGetPhysicalDeviceFeatures2(PhysicalDevice, &DeviceFeatures2);
		}
		bool bindless_supported = indexing_features.descriptorBindingPartiallyBound && indexing_features.runtimeDescriptorArray;
		check(bindless_supported);
	}

	jSpirvHelper::Init(DeviceProperties);

	bool IsSupportDebugMarker = false;

	// Logical device
	{
		jVulkanDeviceUtil::QueueFamilyIndices indices = jVulkanDeviceUtil::FindQueueFamilies(PhysicalDevice, Surface);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<uint32> uniqueQueueFamilies = { indices.GraphicsFamily.value(), indices.ComputeFamily.value(), indices.CopyFamily.value(), indices.PresentFamily.value() };

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
		deviceFeatures.shaderInt64 = true;				// Buffer device address requires the 64-bit integer feature to be enabled

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

		VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
        physicalDeviceDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
        physicalDeviceDescriptorIndexingFeatures.shaderInputAttachmentArrayDynamicIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderUniformTexelBufferArrayDynamicIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderStorageTexelBufferArrayDynamicIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderUniformBufferArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderStorageImageArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderInputAttachmentArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderUniformTexelBufferArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.shaderStorageTexelBufferArrayNonUniformIndexing = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingUniformTexelBufferUpdateAfterBind = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingStorageTexelBufferUpdateAfterBind = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingPartiallyBound = true;
        physicalDeviceDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = true;
        physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = true;
		physicalDeviceDescriptorIndexingFeatures.pNext = (void*)createInfo.pNext;
		createInfo.pNext = &physicalDeviceDescriptorIndexingFeatures;

        // Enable features required for ray tracing using feature chaining via pNext		
        VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{};
        enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        enabledBufferDeviceAddresFeatures.bufferDeviceAddress = true;
		enabledBufferDeviceAddresFeatures.pNext = (void*)createInfo.pNext;
		createInfo.pNext = &enabledBufferDeviceAddresFeatures;

		VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};
		VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQueryFeatures{};
		if (GSupportRaytracing)
		{
			enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			enabledRayTracingPipelineFeatures.rayTracingPipeline = true;
			enabledRayTracingPipelineFeatures.pNext = (void*)createInfo.pNext;
			createInfo.pNext = &enabledRayTracingPipelineFeatures;

			enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			enabledAccelerationStructureFeatures.accelerationStructure = true;
			enabledAccelerationStructureFeatures.pNext = (void*)createInfo.pNext;
			createInfo.pNext = &enabledAccelerationStructureFeatures;

			enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
			enabledRayQueryFeatures.rayQuery = true;
			enabledRayQueryFeatures.pNext = (void*)createInfo.pNext;
			createInfo.pNext = &enabledRayQueryFeatures;
		}

		// Added CustomBorderColor features
		VkPhysicalDeviceCustomBorderColorFeaturesEXT enabledCustomBorderColorFeaturesEXT{};
		enabledCustomBorderColorFeaturesEXT.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
		enabledCustomBorderColorFeaturesEXT.customBorderColors = true;
		enabledCustomBorderColorFeaturesEXT.customBorderColorWithoutFormat = true;
		enabledCustomBorderColorFeaturesEXT.pNext = (void*)createInfo.pNext;
        createInfo.pNext = &enabledCustomBorderColorFeaturesEXT;

		// Added Timeline Semaphore features
        VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures{};
        timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        timelineSemaphoreFeatures.timelineSemaphore = true;
		timelineSemaphoreFeatures.pNext = (void*)createInfo.pNext;
		createInfo.pNext = &timelineSemaphoreFeatures;

		// createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.pEnabledFeatures = nullptr;

		// extension
		// Prepare extensions
		std::vector<const char*> DeviceExtensions(jVulkanDeviceUtil::DeviceExtensions);

		if (GSupportRaytracing)
		{
			DeviceExtensions.insert(DeviceExtensions.end(), jVulkanDeviceUtil::RaytracingDeviceExtensions.begin(), jVulkanDeviceUtil::RaytracingDeviceExtensions.end());
		}

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
		if (GIsValidationLayerSupport && gOptions.EnableDebuggerLayer)
		{
			// 최신 버젼에서는 validation layer는 무시되지만, 오래된 버젼을 호환을 위해 vkInstance와 맞춰줌
			createInfo.enabledLayerCount = static_cast<uint32>(jVulkanDeviceUtil::validationLayers.size());
			createInfo.ppEnabledLayerNames = jVulkanDeviceUtil::validationLayers.data();
		}
#else
		createInfo.enabledLayerCount = 0;
#endif // ENABLE_VALIDATION_LAYER

		if (!ensure(vkCreateDevice(PhysicalDevice, &createInfo, nullptr, &Device) == VK_SUCCESS))
			return false;

		// 현재는 Queue가 1개 뿐이므로 QueueIndex를 0
		GraphicsQueue.QueueIndex = indices.GraphicsFamily.value();
		ComputeQueue.QueueIndex = indices.ComputeFamily.value();
		CopyQueue.QueueIndex = indices.CopyFamily.value();
		PresentQueue.QueueIndex = indices.PresentFamily.value();
		vkGetDeviceQueue(Device, GraphicsQueue.QueueIndex, 0, &GraphicsQueue.Queue);
		vkGetDeviceQueue(Device, ComputeQueue.QueueIndex, 0, &ComputeQueue.Queue);
		vkGetDeviceQueue(Device, CopyQueue.QueueIndex, 0, &CopyQueue.Queue);
		vkGetDeviceQueue(Device, PresentQueue.QueueIndex, 0, &PresentQueue.Queue);
	}

	// Check if the Vsync support
	{
		check(PhysicalDevice);
		check(Surface);
		jVulkanDeviceUtil::SwapChainSupportDetails swapChainSupport = jVulkanDeviceUtil::QuerySwapChainSupport(PhysicalDevice, Surface);

		for (auto it : swapChainSupport.PresentModes)
		{
			if (it == VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				GRHISupportVsync = true;
				break;
			}
		}
	}

	MemoryPool = new jMemoryPool_Vulkan();

	BarrierBatcher = new jResourceBarrierBatcher_Vulkan();

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

	if (GSupportRaytracing)
	{
		vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(Device, "vkGetBufferDeviceAddressKHR");
		vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(Device, "vkCmdBuildAccelerationStructuresKHR");
		vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(Device, "vkBuildAccelerationStructuresKHR");
		vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(Device, "vkCreateAccelerationStructureKHR");
		vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(Device, "vkDestroyAccelerationStructureKHR");
		vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(Device, "vkGetAccelerationStructureBuildSizesKHR");
		vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(Device, "vkGetAccelerationStructureDeviceAddressKHR");
		vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(Device, "vkCmdTraceRaysKHR");
		vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(Device, "vkGetRayTracingShaderGroupHandlesKHR");
		vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(Device, "vkCreateRayTracingPipelinesKHR");
	}

	// Swapchain
    Swapchain = new jSwapchain_Vulkan();
	verify(Swapchain->Create());

	DeallocatorMultiFrameStandaloneResource.FreeDelegate = [](const std::shared_ptr<jStandaloneResourceVulkan>& InStandaloneResourcePtr)
	{
		check(g_rhi_vk);
		check(g_rhi_vk->Device);

        if (InStandaloneResourcePtr->Memory)
            vkFreeMemory(g_rhi_vk->Device, InStandaloneResourcePtr->Memory, nullptr);

		std::set<VkImageView> t;
        for (VkImageView View : InStandaloneResourcePtr->Views)
        {
            check(View);
			check(t.count(View) == 0);
			t.insert(View);
            //vkDestroyImageView(g_rhi_vk->Device, View, nullptr);
        }

		if (InStandaloneResourcePtr->Buffer)
		{
			// Either Image or Buffer resource at one.
			check(!InStandaloneResourcePtr->Image);
			check(InStandaloneResourcePtr->Views.size() == 0);

			vkDestroyBuffer(g_rhi_vk->Device, InStandaloneResourcePtr->Buffer, nullptr);
		}
		else if (InStandaloneResourcePtr->Image)
		{
			vkDestroyImage(g_rhi_vk->Device, InStandaloneResourcePtr->Image, nullptr);
		}
	};

	DeallocatorMultiFrameRenderPass.FreeDelegate = [](jRenderPass_Vulkan* InRenderPass)
	{
		check(InRenderPass);
		delete InRenderPass;
	};

	GraphicsCommandBufferManager = new jCommandBufferManager_Vulkan(ECommandBufferType::GRAPHICS);
	GraphicsCommandBufferManager->CreatePool(GraphicsQueue.QueueIndex);
	ComputeCommandBufferManager = new jCommandBufferManager_Vulkan(ECommandBufferType::COMPUTE);
	ComputeCommandBufferManager->CreatePool(ComputeQueue.QueueIndex);
	CopyCommandBufferManager = new jCommandBufferManager_Vulkan(ECommandBufferType::COPY);
	CopyCommandBufferManager->CreatePool(CopyQueue.QueueIndex);

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	verify(VK_SUCCESS == vkCreatePipelineCache(Device, &pipelineCacheCreateInfo, nullptr, &PipelineCache));

    for (int32 i = 0; i < _countof(QueryPoolTime); ++i)
    {
        QueryPoolTime[i] = new jQueryPoolTime_Vulkan((ECommandBufferType)i);
        QueryPoolTime[i]->Create();
    }

	QueryPoolOcclusion = new jQueryPoolOcclusion_Vulkan();
	QueryPoolOcclusion->Create();

	OneFrameUniformRingBuffers.resize(Swapchain->GetNumOfSwapchain());
	for (auto& iter : OneFrameUniformRingBuffers)
	{
        iter = new jRingBuffer_Vulkan();
		iter->Create(EVulkanBufferBits::UNIFORM_BUFFER, 16 * 1024 * 1024, (uint32)GetDevicePropertyLimits().minUniformBufferOffsetAlignment);
	}

    SSBORingBuffers.resize(Swapchain->GetNumOfSwapchain());
    for (auto& iter : SSBORingBuffers)
    {
        iter = new jRingBuffer_Vulkan();
        iter->Create(EVulkanBufferBits::STORAGE_BUFFER, 16 * 1024 * 1024, (uint32)GetDevicePropertyLimits().minStorageBufferOffsetAlignment);
    }

	DescriptorPools.resize(Swapchain->GetNumOfSwapchain());
	for (auto& iter : DescriptorPools)
	{
		iter = new jDescriptorPool_Vulkan();
		iter->Create(10000);
	}

	DescriptorPools2 = new jDescriptorPool_Vulkan();
	DescriptorPools2->Create(10000);

	g_ImGUI = new jImGUI_Vulkan();
	g_ImGUI->Initialize((float)SCR_WIDTH, (float)SCR_HEIGHT);

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

	if (GSupportRaytracing)
		RaytracingScene = CreateRaytracingScene();

	return true;
}

void jRHI_Vulkan::ReleaseRHI()
{
	Flush();

	jRHI::ReleaseRHI();

	jImageFileLoader::ReleaseInstance();
	
	delete g_ImGUI;
	g_ImGUI = nullptr;

    RenderPassPool.ReleaseAll();
    SamplerStatePool.ReleaseAll();
    RasterizationStatePool.ReleaseAll();
    StencilOpStatePool.ReleaseAll();
    DepthStencilStatePool.ReleaseAll();
    BlendingStatePool.ReleaseAll();
    PipelineStatePool.ReleaseAll();

    jTexture_Vulkan::DestroyDefaultSamplerState();
    jFrameBufferPool::Release();
    jRenderTargetPool::Release();

    delete Swapchain;
    Swapchain = nullptr;

    delete GraphicsCommandBufferManager;
    GraphicsCommandBufferManager = nullptr;

    delete ComputeCommandBufferManager;
	ComputeCommandBufferManager = nullptr;

    delete CopyCommandBufferManager;
	CopyCommandBufferManager = nullptr;

	jShaderBindingLayout_Vulkan::ClearPipelineLayout();

	{
		jScopeWriteLock s(&ShaderBindingPoolLock);
		for (auto& iter : ShaderBindingPool)
			delete iter.second;
		ShaderBindingPool.clear();
	}

    for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
    {
        delete QueryPoolTime[i];
        QueryPoolTime[i] = nullptr;
    }

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

	DeallocatorMultiFrameStandaloneResource.Release();
	DeallocatorMultiFrameRenderPass.Release();

	vkDestroyPipelineCache(Device, PipelineCache, nullptr);
	FenceManager.Release();
	SemaphoreManager.Release();

	delete BarrierBatcher;

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

std::shared_ptr<jIndexBuffer> jRHI_Vulkan::CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	check(streamData);
	check(streamData->Param);
	auto indexBufferPtr = std::make_shared<jIndexBuffer_Vulkan>();
	indexBufferPtr->Initialize(streamData);
	return indexBufferPtr;
}

void jRHI_Vulkan::CleanupSwapChain()
{
	delete Swapchain;
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

    delete GraphicsCommandBufferManager;
	GraphicsCommandBufferManager = new jCommandBufferManager_Vulkan(ECommandBufferType::GRAPHICS);
	verify(GraphicsCommandBufferManager->CreatePool(GraphicsQueue.QueueIndex));

    delete ComputeCommandBufferManager;
	ComputeCommandBufferManager = new jCommandBufferManager_Vulkan(ECommandBufferType::COMPUTE);
    verify(ComputeCommandBufferManager->CreatePool(ComputeQueue.QueueIndex));

    delete CopyCommandBufferManager;
	CopyCommandBufferManager = new jCommandBufferManager_Vulkan(ECommandBufferType::COPY);
    verify(CopyCommandBufferManager->CreatePool(CopyQueue.QueueIndex));

    jFrameBufferPool::Release();
    jRenderTargetPool::ReleaseForRecreateSwapchain();
	PipelineStatePool.ReleaseAll();
    RenderPassPool.ReleaseAll();

    delete Swapchain;
    Swapchain = new jSwapchain_Vulkan();
    verify(Swapchain->Create());

    g_ImGUI->Release();
    g_ImGUI->Initialize((float)SCR_WIDTH, (float)SCR_HEIGHT);

    CurrentFrameIndex = 0;

	CreateSampleVRSTexture();

	Flush();
}

uint32 jRHI_Vulkan::GetMaxSwapchainCount() const
{
	check(Swapchain);
	return (uint32)Swapchain->Images.size();
}

std::shared_ptr<jVertexBuffer> jRHI_Vulkan::CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) const
{
	if (!streamData)
		return nullptr;

	auto vertexBufferPtr = std::make_shared<jVertexBuffer_Vulkan>();
	vertexBufferPtr->Initialize(streamData);
	return vertexBufferPtr;
}

std::shared_ptr<jTexture> jRHI_Vulkan::CreateTextureFromData(const jImageData* InImageData) const
{
	check(InImageData);

    int32 MipLevel = InImageData->MipLevel;
    if ((MipLevel == 1) && (InImageData->CreateMipmapIfPossible))
    {
        MipLevel = jTexture::GetMipLevels(InImageData->Width, InImageData->Height);
    }

	auto stagingBufferPtr = jBufferUtil_Vulkan::CreateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
        , InImageData->ImageBulkData.ImageData.size(), EResourceLayout::TRANSFER_SRC);

	stagingBufferPtr->UpdateBuffer(&InImageData->ImageBulkData.ImageData[0], InImageData->ImageBulkData.ImageData.size());

	std::shared_ptr<jTexture_Vulkan> TexturePtr;
	if (InImageData->TextureType == ETextureType::TEXTURE_CUBE)
	{
		TexturePtr = g_rhi->CreateCubeTexture<jTexture_Vulkan>((uint32)InImageData->Width, (uint32)InImageData->Height, MipLevel, InImageData->Format
			, ETextureCreateFlag::TransferSrc | ETextureCreateFlag::TransferDst | ETextureCreateFlag::UAV, EResourceLayout::SHADER_READ_ONLY, InImageData->ImageBulkData);
        if (!ensure(TexturePtr))
        {
            return nullptr;
        }
	}
	else
	{
		TexturePtr = g_rhi->Create2DTexture<jTexture_Vulkan>((uint32)InImageData->Width, (uint32)InImageData->Height, (uint32)InImageData->LayerCount, MipLevel, InImageData->Format
			, ETextureCreateFlag::TransferSrc | ETextureCreateFlag::TransferDst | ETextureCreateFlag::UAV, EResourceLayout::SHADER_READ_ONLY, InImageData->ImageBulkData);
		if (!ensure(TexturePtr))
		{
			return nullptr;
		}
	}

	return TexturePtr;
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

	std::vector<jName> IncludeFilePaths;
	jShader* shader_vk = OutShader;
	check(shader_vk->GetPermutationCount());
	{
		check(!shader_vk->CompiledShader);
		jCompiledShader_Vulkan* CurCompiledShader = new jCompiledShader_Vulkan();
		shader_vk->CompiledShader = CurCompiledShader;

		// PermutationId 를 설정하여 컴파일을 준비함
        shader_vk->SetPermutationId(shaderInfo.GetPermutationId());

		std::string Defines;
		shaderInfo.GetShaderTypeDefines(Defines, shaderInfo.GetShaderType());
		shader_vk->GetPermutationDefines(Defines);

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
			if (!ShaderFile.OpenFile(shaderInfo.GetShaderFilepath().ToStr(), FileType::TEXT, ReadWriteType::READ))
				return false;
            ShaderFile.ReadFileToBuffer(false);
            std::string ShaderText;

            if (shaderInfo.GetPreProcessors().GetStringLength() > 0)
            {
                ShaderText += shaderInfo.GetPreProcessors().ToStr();
                ShaderText += "\r\n";
            }
            ShaderText += Defines;
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
				IncludeFilePaths.push_back(jName(includeFilepath.c_str()));

				// Load include shader file
                jFile IncludeShaderFile;
				if (!IncludeShaderFile.OpenFile(includeFilepath.c_str(), FileType::TEXT, ReadWriteType::READ))
					return false;
                IncludeShaderFile.ReadFileToBuffer(false);
				ShaderText.replace(ReplaceStartPos, ReplaceCount, IncludeShaderFile.GetBuffer());
				IncludeShaderFile.CloseFile();
			}

            const wchar_t* ShadingModel = nullptr;
            switch (shaderInfo.GetShaderType())
            {
            case EShaderAccessStageFlag::VERTEX:
                ShadingModel = TEXT("vs_6_6");
                break;
            case EShaderAccessStageFlag::GEOMETRY:
                ShadingModel = TEXT("gs_6_6");
                break;
            case EShaderAccessStageFlag::FRAGMENT:
                ShadingModel = TEXT("ps_6_6");
                break;
            case EShaderAccessStageFlag::COMPUTE:
                ShadingModel = TEXT("cs_6_6");
                break;
            case EShaderAccessStageFlag::RAYTRACING:
			case EShaderAccessStageFlag::RAYTRACING_RAYGEN:
			case EShaderAccessStageFlag::RAYTRACING_MISS:
			case EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT:
			case EShaderAccessStageFlag::RAYTRACING_ANYHIT:
                ShadingModel = TEXT("lib_6_6");
                break;
            default:
                check(0);
                break;
            }

			const std::wstring EntryPoint = ConvertToWchar(shaderInfo.GetEntryPoint());
			auto ShaderBlob = jShaderCompiler_DX12::Get().Compile(ShaderText.c_str(), (uint32)ShaderText.length(), ShadingModel, EntryPoint.c_str(), false
				, {TEXT("-spirv"), TEXT("-fspv-target-env=vulkan1.1spirv1.4"), TEXT("-fvk-use-scalar-layout")
				, TEXT("-fspv-extension=SPV_EXT_descriptor_indexing"), TEXT("-fspv-extension=SPV_KHR_ray_tracing")
				, TEXT("-fspv-extension=SPV_KHR_ray_query"), TEXT("-fspv-extension=SPV_EXT_shader_viewport_index_layer") });

			std::vector<uint8> SpirvCode;
			if (ShaderBlob->GetBufferSize() > 0)
			{
				SpirvCode.resize(ShaderBlob->GetBufferSize());
				memcpy(SpirvCode.data(), ShaderBlob->GetBufferPointer(), ShaderBlob->GetBufferSize());
			}

			{
                VkShaderModuleCreateInfo createInfo = {};
                createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                createInfo.codeSize = SpirvCode.size();

                // pCode 가 uint32* 형이라서 4 byte aligned 된 메모리를 넘겨줘야 함.
                // 다행히 std::vector의 default allocator가 가 메모리 할당시 4 byte aligned 을 이미 하고있어서 그대로 씀.
                createInfo.pCode = reinterpret_cast<const uint32*>(SpirvCode.data());

                ensure(vkCreateShaderModule(Device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

                // compiling or linking 과정이 graphics pipeline 이 생성되기 전까지 처리되지 않는다.
                // 그래픽스 파이프라인이 생성된 후 VkShaderModule은 즉시 소멸 가능.
                //return shaderModule;
			}
		}

		if (!shaderModule)
			return false;

        VkShaderStageFlagBits ShaderFlagBits;
        switch (shaderInfo.GetShaderType())
        {
        case EShaderAccessStageFlag::VERTEX:
            ShaderFlagBits = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case EShaderAccessStageFlag::GEOMETRY:
            ShaderFlagBits = VK_SHADER_STAGE_GEOMETRY_BIT;
            break;
        case EShaderAccessStageFlag::FRAGMENT:
            ShaderFlagBits = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case EShaderAccessStageFlag::COMPUTE:
            ShaderFlagBits = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
		case EShaderAccessStageFlag::RAYTRACING:
			ShaderFlagBits = VK_SHADER_STAGE_ALL;
			break;
        case EShaderAccessStageFlag::RAYTRACING_RAYGEN:
			ShaderFlagBits = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
			break;
        case EShaderAccessStageFlag::RAYTRACING_MISS:
			ShaderFlagBits = VK_SHADER_STAGE_MISS_BIT_KHR;
			break;
        case EShaderAccessStageFlag::RAYTRACING_CLOSESTHIT:
			ShaderFlagBits = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
			break;
        case EShaderAccessStageFlag::RAYTRACING_ANYHIT:
            ShaderFlagBits = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            break;
        default:
            check(0);
            break;
        }

		CurCompiledShader->ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		CurCompiledShader->ShaderStage.stage = ShaderFlagBits;
		CurCompiledShader->ShaderStage.module = shaderModule;
		CurCompiledShader->ShaderStage.pName = shaderInfo.GetEntryPoint().ToStr();
	}
	shader_vk->ShaderInfo = shaderInfo;
	shader_vk->ShaderInfo.SetIncludeShaderFilePaths(IncludeFilePaths);

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

	std::shared_ptr<jTexture_Vulkan> TexturePtr;
	switch (info.TextureType)
	{
	case ETextureType::TEXTURE_2D:
		TexturePtr = jBufferUtil_Vulkan::Create2DTexture(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkImageCreateFlagBits(), VK_IMAGE_LAYOUT_UNDEFINED);
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		TexturePtr = jBufferUtil_Vulkan::CreateTexture2DArray(info.Width, info.Height, info.LayerCount, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkImageCreateFlagBits(), VK_IMAGE_LAYOUT_UNDEFINED);
		break;
	case ETextureType::TEXTURE_CUBE:
		TexturePtr = jBufferUtil_Vulkan::CreateCubeTexture(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkImageCreateFlagBits(), VK_IMAGE_LAYOUT_UNDEFINED);
		break;
	default:
		JMESSAGE("Unsupported type texture in FramebufferPool");
		return nullptr;
	}

	auto fb_vk = new jFrameBuffer_Vulkan();
	fb_vk->Info = info;
	fb_vk->Textures.push_back(TexturePtr);
	fb_vk->AllTextures.push_back(TexturePtr);

	return fb_vk;
}

std::shared_ptr<jRenderTarget> jRHI_Vulkan::CreateRenderTarget(const jRenderTargetInfo& info) const
{
	const VkFormat textureFormat = GetVulkanTextureFormat(info.Format);
	const bool hasDepthAttachment = IsDepthFormat(info.Format);
	const int32 mipLevels = (info.SampleCount > EMSAASamples::COUNT_1 || !info.IsGenerateMipmap) ? 1 : jTexture::GetMipLevels(info.Width, info.Height);		// MipLevel 은 SampleCount 1인 경우만 가능
	JASSERT((int32)info.SampleCount >= 1);

	VkImageView imageViewUAV = nullptr;
	std::map<int32, VkImageView> imageViewForMipMap;
	std::map<int32, VkImageView> imageViewForMipMapUAV;
	std::shared_ptr<jTexture_Vulkan> TexturePtr;

	ETextureCreateFlag TextureCreateFlag{};
	if (hasDepthAttachment)
		TextureCreateFlag |= ETextureCreateFlag::DSV;
	else
		TextureCreateFlag |= ETextureCreateFlag::RTV | ETextureCreateFlag::UAV;
	
	if (info.IsUseAsSubpassInput)
		TextureCreateFlag |= ETextureCreateFlag::SubpassInput;

	if (info.IsMemoryless)
		TextureCreateFlag |= ETextureCreateFlag::Memoryless;

	const VkImageAspectFlags ImageAspectFlag = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	switch (info.Type)
	{
	case ETextureType::TEXTURE_2D:
		TexturePtr = g_rhi->Create2DTexture<jTexture_Vulkan>(info.Width, info.Height, 1, mipLevels, info.Format, TextureCreateFlag, EResourceLayout::GENERAL);
		imageViewForMipMap[0] = TexturePtr->View;
		
		check(mipLevels > 0);
		for(int32 i=1;i<mipLevels;++i)
		{
			imageViewForMipMap[i] = jBufferUtil_Vulkan::CreateTextureViewForSpecificMipMap(TexturePtr->Image, textureFormat, ImageAspectFlag, i);
		}
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
        TexturePtr = g_rhi->Create2DTexture<jTexture_Vulkan>(info.Width, info.Height, info.LayerCount, mipLevels, info.Format, TextureCreateFlag, EResourceLayout::GENERAL);
        imageViewForMipMap[0] = TexturePtr->View;

        check(mipLevels > 0);
        for (int32 i = 1; i < mipLevels; ++i)
        {
            imageViewForMipMap[i] = jBufferUtil_Vulkan::CreateTexture2DArrayViewForSpecificMipMap(TexturePtr->Image, info.LayerCount, textureFormat, ImageAspectFlag, i);
        }
		break;
	case ETextureType::TEXTURE_CUBE:
		check(info.LayerCount == 6);
		TexturePtr = g_rhi->CreateCubeTexture<jTexture_Vulkan>(info.Width, info.Height, mipLevels, info.Format, TextureCreateFlag, EResourceLayout::GENERAL);

		// Create for Shader Resource (TextureCube)
		{
			imageViewForMipMap[0] = TexturePtr->View;

			check(mipLevels > 0);
			for (int32 i = 1; i < mipLevels; ++i)
			{
				imageViewForMipMap[i] = jBufferUtil_Vulkan::CreateTextureCubeViewForSpecificMipMap(TexturePtr->Image, textureFormat, ImageAspectFlag, i);
			}
		}

		// Create for UAV (writing compute shader resource) (Texture2DArray)
		{
			imageViewUAV = TexturePtr->View;
            imageViewForMipMapUAV[0] = TexturePtr->View;

            check(mipLevels > 0);
            for (int32 i = 1; i < mipLevels; ++i)
            {
                imageViewForMipMapUAV[i] = jBufferUtil_Vulkan::CreateTexture2DArrayViewForSpecificMipMap(TexturePtr->Image, info.LayerCount, textureFormat, ImageAspectFlag, i);
            }
		}
		break;
	default:
		JMESSAGE("Unsupported type texture in FramebufferPool");
		return nullptr;
	}

	TexturePtr->ViewForMipMap = imageViewForMipMap;
	TexturePtr->ViewUAV = imageViewUAV;
	TexturePtr->ViewUAVForMipMap = imageViewForMipMapUAV;

	auto RenderTargetPtr = std::make_shared<jRenderTarget>();
	RenderTargetPtr->Info = info;
	RenderTargetPtr->TexturePtr = TexturePtr;

	return RenderTargetPtr;
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

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
	InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDraw((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), vertCount, 1, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawArraysInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const
{
	check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDraw((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), vertCount, instanceCount, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawElements(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount) const
{
	check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), indexCount, 1, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsInstanced(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 instanceCount) const
{
	check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), indexCount, instanceCount, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex) const
{
	check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), indexCount, 1, startIndex, baseVertexIndex, 0);
}

void jRHI_Vulkan::DrawElementsInstancedBaseVertex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, int32 elementSize, int32 startIndex, int32 indexCount, int32 baseVertexIndex, int32 instanceCount) const
{
	check(InRenderFrameContext);
	check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDrawIndexed((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), indexCount, instanceCount, startIndex, baseVertexIndex, 0);
}

void jRHI_Vulkan::DrawIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const
{
	check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER
	
	vkCmdDrawIndirect((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), (VkBuffer)buffer->GetHandle()
        , startIndex * sizeof(VkDrawIndirectCommand), drawCount, sizeof(VkDrawIndirectCommand));
}

void jRHI_Vulkan::DrawElementsIndirect(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
	, EPrimitiveType type, jBuffer* buffer, int32 startIndex, int32 drawCount) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDrawIndexedIndirect((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), (VkBuffer)buffer->GetHandle()
		, startIndex * sizeof(VkDrawIndexedIndirectCommand), drawCount, sizeof(VkDrawIndexedIndirectCommand));
}

void jRHI_Vulkan::DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	vkCmdDispatch((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), numGroupsX, numGroupsY, numGroupsZ);
}

void jRHI_Vulkan::DispatchRay(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jRaytracingDispatchData& InDispatchData) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());

#if USE_RESOURCE_BARRIER_BATCHER
    BarrierBatcher->Flush(InRenderFrameContext->GetActiveCommandBuffer());
    InRenderFrameContext->GetActiveCommandBuffer()->FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

	auto PipelineState_Vulkan = (jPipelineStateInfo_Vulkan*)InDispatchData.PipelineState;
	check(PipelineState_Vulkan);

    VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
    g_rhi_vk->vkCmdTraceRaysKHR(
		(VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(),
        &PipelineState_Vulkan->RaygenStridedDeviceAddressRegion,
        &PipelineState_Vulkan->MissStridedDeviceAddressRegion,
        &PipelineState_Vulkan->HitStridedDeviceAddressRegion,
        &emptySbtEntry,
		InDispatchData.Width,
		InDispatchData.Height,
		InDispatchData.Depth);
}

jShaderBindingLayout* jRHI_Vulkan::CreateShaderBindings(const jShaderBindingArray& InShaderBindingArray) const
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

		// Try again, to avoid entering creation section simultaneously.
        auto it_find = ShaderBindingPool.find(hash);
        if (ShaderBindingPool.end() != it_find)
            return it_find->second;

		auto NewShaderBinding = new jShaderBindingLayout_Vulkan();
		NewShaderBinding->Initialize(InShaderBindingArray);
		NewShaderBinding->Hash = hash;
		ShaderBindingPool[hash] = NewShaderBinding;

		return NewShaderBinding;
	}
}

std::shared_ptr<jShaderBindingInstance> jRHI_Vulkan::CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const
{
	auto shaderBindingsLayout = CreateShaderBindings(InShaderBindingArray);
	check(shaderBindingsLayout);
	return shaderBindingsLayout->CreateShaderBindingInstance(InShaderBindingArray, InType);
}

std::shared_ptr<IUniformBufferBlock> jRHI_Vulkan::CreateUniformBufferBlock(jName InName, jLifeTimeType InLifeTimeType, size_t InSize /*= 0*/) const
{
	auto uniformBufferBlockPtr = std::make_shared<jUniformBufferBlock_Vulkan>(InName, InLifeTimeType);
	uniformBufferBlockPtr->Init(InSize);
	return uniformBufferBlockPtr;
}

VkImageUsageFlags jRHI_Vulkan::GetImageUsageFlags(ETextureCreateFlag InTextureCreateFlag) const
{
    VkImageUsageFlags UsageFlag = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!!(InTextureCreateFlag & ETextureCreateFlag::RTV))
        UsageFlag |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::DSV))
        UsageFlag |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::UAV))
        UsageFlag |= VK_IMAGE_USAGE_STORAGE_BIT;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::TransferSrc))
        UsageFlag |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::TransferDst))
        UsageFlag |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::ShadingRate))
        UsageFlag |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::SubpassInput))
        UsageFlag |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    // This should be placed last, because Memoryless has only Color, Depth, Input attachment usages.
    if (!!(InTextureCreateFlag & ETextureCreateFlag::Memoryless))
        UsageFlag &= ~(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

	return UsageFlag;
}

VkMemoryPropertyFlagBits jRHI_Vulkan::GetMemoryPropertyFlagBits(ETextureCreateFlag InTextureCreateFlag) const
{
    VkMemoryPropertyFlagBits PropertyFlagBits{};
    if (!!(InTextureCreateFlag & ETextureCreateFlag::CPUAccess))
    {
        PropertyFlagBits = (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
	return PropertyFlagBits;
}

std::shared_ptr<jTexture> jRHI_Vulkan::Create2DTexture(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
    , EResourceLayout InImageLayout, const jImageBulkData& InImageBulkData, const jRTClearValue& InClearValue, const wchar_t* InResourceName) const
{
	VkImageCreateFlagBits ImageCreateFlags{};
	const VkMemoryPropertyFlagBits PropertyFlagBits = GetMemoryPropertyFlagBits(InTextureCreateFlag);
	const VkImageUsageFlags UsageFlag = GetImageUsageFlags(InTextureCreateFlag);
	const VkImageLayout InitialImageLayout = (InImageBulkData.ImageData.size() > 0) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PREINITIALIZED;
	check(!IsDepthFormat(InFormat) || (IsDepthFormat(InFormat) && (UsageFlag & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)));


	// If there is Image or Depth bit, it should be initialized VK_IMAGE_LAYOUT_PREINITIALIZED, if not, VK_IMAGE_LAYOUT_UNDEFINED
	auto TexturePtr = jBufferUtil_Vulkan::Create2DTexture(InWidth, InHeight, InMipLevels, VK_SAMPLE_COUNT_1_BIT, GetVulkanTextureFormat(InFormat), VK_IMAGE_TILING_OPTIMAL
		, UsageFlag, PropertyFlagBits, ImageCreateFlags, InitialImageLayout);

	if (InImageBulkData.ImageData.size() > 0)
	{
		// todo : recycle temp buffer
        auto stagingBufferPtr = jBufferUtil_Vulkan::CreateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
            , InImageBulkData.ImageData.size(), EResourceLayout::TRANSFER_SRC);

        stagingBufferPtr->UpdateBuffer(&InImageBulkData.ImageData[0], InImageBulkData.ImageData.size());

        auto commandBuffer = BeginSingleTimeCommands();
        TransitionLayout(commandBuffer, TexturePtr.get(), EResourceLayout::TRANSFER_DST);

        if (InImageBulkData.SubresourceFootprints.size() > 0)
        {
            for (int32 i = 0; i < (int32)InImageBulkData.SubresourceFootprints.size(); ++i)
            {
                const jImageSubResourceData SubResourceData = InImageBulkData.SubresourceFootprints[i];

                jBufferUtil_Vulkan::CopyBufferToTexture(commandBuffer, stagingBufferPtr->Buffer, stagingBufferPtr->Offset + SubResourceData.Offset, TexturePtr->Image
                    , SubResourceData.Width, SubResourceData.Height, SubResourceData.MipLevel, SubResourceData.Depth);
            }
        }
        else
        {
            jBufferUtil_Vulkan::CopyBufferToTexture(commandBuffer, stagingBufferPtr->Buffer, stagingBufferPtr->Offset, TexturePtr->Image, InWidth, InHeight);
        }

        TransitionLayout(commandBuffer, TexturePtr.get(), InImageLayout);
        EndSingleTimeCommands(commandBuffer);
	}

	return TexturePtr;
}

std::shared_ptr<jTexture> jRHI_Vulkan::CreateCubeTexture(uint32 InWidth, uint32 InHeight, uint32 InMipLevels, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag
    , EResourceLayout InImageLayout, const jImageBulkData& InImageBulkData, const jRTClearValue& InClearValue, const wchar_t* InResourceName) const
{
	VkImageCreateFlagBits ImageCreateFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    const VkMemoryPropertyFlagBits PropertyFlagBits = GetMemoryPropertyFlagBits(InTextureCreateFlag);
    const VkImageUsageFlags UsageFlag = GetImageUsageFlags(InTextureCreateFlag);
	const VkImageLayout InitialImageLayout = (InImageBulkData.ImageData.size() > 0) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PREINITIALIZED;
    check(!IsDepthFormat(InFormat) || (IsDepthFormat(InFormat) && (UsageFlag & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)));

	// If there is Image or Depth bit, it should be initialized VK_IMAGE_LAYOUT_PREINITIALIZED, if not, VK_IMAGE_LAYOUT_UNDEFINED
	auto TexturePtr = jBufferUtil_Vulkan::CreateCubeTexture(InWidth, InHeight, InMipLevels, VK_SAMPLE_COUNT_1_BIT, GetVulkanTextureFormat(InFormat), VK_IMAGE_TILING_OPTIMAL
		, UsageFlag, PropertyFlagBits, ImageCreateFlags, InitialImageLayout);

    if (InImageBulkData.ImageData.size() > 0)
    {
        // todo : recycle temp buffer
        auto stagingBufferPtr = jBufferUtil_Vulkan::CreateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT
            , InImageBulkData.ImageData.size(), EResourceLayout::TRANSFER_SRC);

        stagingBufferPtr->UpdateBuffer(&InImageBulkData.ImageData[0], InImageBulkData.ImageData.size());

        auto commandBuffer = BeginSingleTimeCommands();
        TransitionLayout(commandBuffer, TexturePtr.get(), EResourceLayout::TRANSFER_DST);

        if (InImageBulkData.SubresourceFootprints.size() > 0)
        {
            for (int32 i = 0; i < (int32)InImageBulkData.SubresourceFootprints.size(); ++i)
            {
                const jImageSubResourceData SubResourceData = InImageBulkData.SubresourceFootprints[i];

                jBufferUtil_Vulkan::CopyBufferToTexture(commandBuffer, stagingBufferPtr->Buffer, stagingBufferPtr->Offset + SubResourceData.Offset, TexturePtr->Image
                    , SubResourceData.Width, SubResourceData.Height, SubResourceData.MipLevel, SubResourceData.Depth);
            }
        }
        else
        {
            jBufferUtil_Vulkan::CopyBufferToTexture(commandBuffer, stagingBufferPtr->Buffer, stagingBufferPtr->Offset, TexturePtr->Image, InWidth, InHeight);
        }

        TransitionLayout(commandBuffer, TexturePtr.get(), InImageLayout);
        EndSingleTimeCommands(commandBuffer);
    }

	return TexturePtr;
}

bool jRHI_Vulkan::IsSupportVSync() const
{
	return GRHISupportVsync && GUseVsync;
}

void jRHI_Vulkan::RemoveRenderPassByHash(const std::vector<uint64>& InRelatedRenderPassHashes) const
{
	for (auto Hash : InRelatedRenderPassHashes)
	{
		jRenderPass_Vulkan* ToReleaseRP = g_rhi_vk->RenderPassPool.Release(Hash);
		if (ToReleaseRP)
			g_rhi_vk->DeallocatorMultiFrameRenderPass.Free(ToReleaseRP);
	}
}

float jRHI_Vulkan::GetCurrentMonitorDPIScale() const
{
	if (g_ImGUI)
		return g_ImGUI->GetCurrentMonitorDPIScale();

	return 1.0f;
}

jQuery* jRHI_Vulkan::CreateQueryTime(ECommandBufferType InCmdBufferType) const
{
	auto queryTime = new jQueryTime_Vulkan(InCmdBufferType);
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
		, (VkSemaphore)Swapchain->Images[CurrentFrameIndex]->Available->GetHandle(), nullptr, &CurrentFrameIndex);

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

	jCommandBuffer_Vulkan* commandBuffer = (jCommandBuffer_Vulkan*)g_rhi_vk->GraphicsCommandBufferManager->GetOrCreateCommandBuffer();

    // 이전 프레임에서 현재 사용하려는 이미지를 사용중에 있나? (그렇다면 펜스를 기다려라)
    if (lastCommandBufferFence != VK_NULL_HANDLE)
        vkWaitForFences(Device, 1, &lastCommandBufferFence, VK_TRUE, UINT64_MAX);

	GetOneFrameUniformRingBuffer()->Reset();
	GetDescriptorPoolForSingleFrame()->Reset();

    // 이 프레임에서 펜스를 사용한다고 마크 해둠
	Swapchain->Images[CurrentFrameIndex]->CommandBufferFence = (VkFence)commandBuffer->GetFenceHandle();

    auto renderFrameContextPtr = std::make_shared<jRenderFrameContext_Vulkan>(commandBuffer);
	renderFrameContextPtr->RaytracingScene = g_rhi->RaytracingScene;
	renderFrameContextPtr->UseForwardRenderer = !gOptions.UseDeferredRenderer;
	renderFrameContextPtr->FrameIndex = CurrentFrameIndex;
	renderFrameContextPtr->SceneRenderTargetPtr = std::make_shared<jSceneRenderTarget>();
	renderFrameContextPtr->SceneRenderTargetPtr->Create(Swapchain->GetSwapchainImage(CurrentFrameIndex), &jLight::GetLights());
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

std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> jRHI_Vulkan::QueueSubmit(const std::shared_ptr<jRenderFrameContext>& renderFrameContextPtr, jSemaphore* InSignalSemaphore)
{
    SCOPE_CPU_PROFILE(QueueSubmit);

	auto renderFrameContext = (jRenderFrameContext_Vulkan*)renderFrameContextPtr.get();
	check(renderFrameContext);

	auto CurrentWaitSemaphore = renderFrameContext->CurrentWaitSemaphore;

	check(renderFrameContext->GetActiveCommandBuffer());
	auto CommandBufferManager = (jCommandBufferManager_Vulkan*)g_rhi_vk->GetCommandBufferManager(renderFrameContext->GetActiveCommandBuffer()->Type);
	
	std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> Sync_Vulkan 
		= CommandBufferManager->QueueSubmit((jCommandBuffer_Vulkan*)renderFrameContext->GetActiveCommandBuffer(), renderFrameContext->CurrentWaitSemaphore, InSignalSemaphore);

	renderFrameContext->CurrentWaitSemaphore = InSignalSemaphore;		// Update Next Wait Semaphore

	return Sync_Vulkan;
}

jCommandBuffer_Vulkan* jRHI_Vulkan::BeginSingleTimeCommands() const
{
	return (jCommandBuffer_Vulkan*)GraphicsCommandBufferManager->GetOrCreateCommandBuffer();
}

void jRHI_Vulkan::EndSingleTimeCommands(jCommandBuffer* commandBuffer) const
{
	auto CommandBuffer_Vulkan = (jCommandBuffer_Vulkan*)commandBuffer;

	CommandBuffer_Vulkan->End();

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &CommandBuffer_Vulkan->GetRef();

    VkFence vkFence = (VkFence)CommandBuffer_Vulkan->GetFenceHandle();
	vkResetFences(Device, 1, &vkFence);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

	const jQueue_Vulkan& CurrentQueue = GetQueue(CommandBuffer_Vulkan->Type);

	auto Result = vkQueueSubmit(CurrentQueue.Queue, 1, &submitInfo, vkFence);
	ensure(VK_SUCCESS == Result);

	Result = vkQueueWaitIdle(CurrentQueue.Queue);
	ensure(VK_SUCCESS == Result);

    GraphicsCommandBufferManager->ReturnCommandBuffer(CommandBuffer_Vulkan);
}

void jRHI_Vulkan::TransitionLayout(jCommandBuffer* commandBuffer, jTexture* texture, EResourceLayout newLayout) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddTransition(texture, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::TransitionLayout(jTexture* texture, EResourceLayout newLayout) const
{
    check(BarrierBatcher);
	BarrierBatcher->AddTransition(texture, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::UAVBarrier(jCommandBuffer* commandBuffer, jTexture* texture) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddUAV(texture);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::UAVBarrier(jCommandBuffer* commandBuffer, jBuffer* buffer) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
	commandBuffer->GetBarrierBatcher()->AddUAV(buffer);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::UAVBarrier(jTexture* texture) const
{
    check(BarrierBatcher);
    BarrierBatcher->AddUAV(texture);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::UAVBarrier(jBuffer* buffer) const
{
    check(BarrierBatcher);
    BarrierBatcher->AddUAV(buffer);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::TransitionLayout(jCommandBuffer* commandBuffer, jBuffer* buffer, EResourceLayout newLayout) const
{
    check(commandBuffer);
    check(commandBuffer->GetBarrierBatcher());
    commandBuffer->GetBarrierBatcher()->AddTransition(buffer, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
		commandBuffer->FlushBarrierBatch();
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

void jRHI_Vulkan::TransitionLayout(jBuffer* buffer, EResourceLayout newLayout) const
{
    check(BarrierBatcher);
	BarrierBatcher->AddTransition(buffer, newLayout);

#if !USE_RESOURCE_BARRIER_BATCHER
    {
        auto CommandBuffer = BeginSingleTimeCommands();
        BarrierBatcher->Flush(CommandBuffer);
        EndSingleTimeCommands(CommandBuffer);
    }
#endif // USE_RESOURCE_BARRIER_BATCHER
}

jPipelineStateInfo* jRHI_Vulkan::CreatePipelineStateInfo(const jPipelineStateFixedInfo* InPipelineStateFixed, const jGraphicsPipelineShader InShader, const jVertexBufferArray& InVertexBufferArray
	, const jRenderPass* InRenderPass, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* InPushConstant, int32 InSubpassIndex) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(InPipelineStateFixed, InShader, InVertexBufferArray, InRenderPass, InShaderBindingArray, InPushConstant, InSubpassIndex)));
}

jPipelineStateInfo* jRHI_Vulkan::CreateComputePipelineStateInfo(const jShader* shader, const jShaderBindingLayoutArray& InShaderBindingArray
	, const jPushConstant* pushConstant) const
{
    return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(shader, InShaderBindingArray, pushConstant)));
}

jPipelineStateInfo* jRHI_Vulkan::CreateRaytracingPipelineStateInfo(const std::vector<jRaytracingPipelineShader>& InShaders
	, const jRaytracingPipelineData& InRaytracingData, const jShaderBindingLayoutArray& InShaderBindingArray, const jPushConstant* pushConstant) const
{
	return PipelineStatePool.GetOrCreateMove(std::move(jPipelineStateInfo(InShaders, InRaytracingData, InShaderBindingArray, pushConstant)));
}

void jRHI_Vulkan::RemovePipelineStateInfo(size_t InHash)
{
    PipelineStatePool.Release(InHash);
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
	vkQueueWaitIdle(CopyQueue.Queue);
	vkQueueWaitIdle(PresentQueue.Queue);
}

void jRHI_Vulkan::Finish() const
{
    vkQueueWaitIdle(GraphicsQueue.Queue);
    vkQueueWaitIdle(ComputeQueue.Queue);
	vkQueueWaitIdle(CopyQueue.Queue);
    vkQueueWaitIdle(PresentQueue.Queue);
}

void jRHI_Vulkan::BindShadingRateImage(jCommandBuffer* commandBuffer, jTexture* vrstexture) const
{
	check(commandBuffer);
    g_rhi_vk->vkCmdBindShadingRateImageNV((VkCommandBuffer)commandBuffer->GetHandle()
        , (vrstexture ? ((jTexture_Vulkan*)vrstexture)->View : nullptr), VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
}

void jRHI_Vulkan::NextSubpass(const jCommandBuffer* commandBuffer) const
{
	check(commandBuffer);
	vkCmdNextSubpass((VkCommandBuffer)commandBuffer->GetHandle(), VK_SUBPASS_CONTENTS_INLINE);
}

jTexture* jRHI_Vulkan::CreateSampleVRSTexture()
{
    if (ensure(!SampleVRSTexturePtr))
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

		SampleVRSTexturePtr = jBufferUtil_Vulkan::Create2DTexture(imageExtent.width, imageExtent.height, 1, (VkSampleCountFlagBits)1, GetVulkanTextureFormat(ETextureFormat::R8UI), VK_IMAGE_TILING_OPTIMAL
            , VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkImageCreateFlagBits(), VK_IMAGE_LAYOUT_UNDEFINED);

        VkDeviceSize imageSize = imageExtent.width * imageExtent.height * GetVulkanTexturePixelSize(ETextureFormat::R8UI);
		auto stagingBufferPtr = jBufferUtil_Vulkan::CreateBuffer(EVulkanBufferBits::TRANSFER_SRC, EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT, imageSize, EResourceLayout::TRANSFER_SRC);

		auto commandBuffer = g_rhi_vk->BeginSingleTimeCommands();
		g_rhi->TransitionLayout(commandBuffer, SampleVRSTexturePtr.get(), EResourceLayout::TRANSFER_DST);

        jBufferUtil_Vulkan::CopyBufferToTexture(commandBuffer, stagingBufferPtr->Buffer, stagingBufferPtr->Offset, (VkImage)SampleVRSTexturePtr->GetHandle()
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

        stagingBufferPtr->UpdateBuffer(shadingRatePatternData, bufferSize);

        g_rhi_vk->EndSingleTimeCommands(commandBuffer);

        delete[]shadingRatePatternData;
    }
	return SampleVRSTexturePtr.get();
}

void jRHI_Vulkan::BindGraphicsShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
	, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
	if (InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData)
	{
		check(InCommandBuffer);
		vkCmdBindDescriptorSets((VkCommandBuffer)InCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS
			, (VkPipelineLayout)InPiplineState->GetPipelineLayoutHandle(), InFirstSet
			, InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData, (const VkDescriptorSet*)&InShaderBindingInstanceCombiner.DescriptorSetHandles[0]
			, InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData, (InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData ? &InShaderBindingInstanceCombiner.DynamicOffsets[0] : nullptr));
	}	
}

void jRHI_Vulkan::BindComputeShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
	, const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
	if (InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData)
	{
		check(InCommandBuffer);
		vkCmdBindDescriptorSets((VkCommandBuffer)InCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE
			, (VkPipelineLayout)InPiplineState->GetPipelineLayoutHandle(), InFirstSet
			, InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData, (const VkDescriptorSet*)&InShaderBindingInstanceCombiner.DescriptorSetHandles[0]
			, InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData, (InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData ? &InShaderBindingInstanceCombiner.DynamicOffsets[0] : nullptr));
	}
}

void jRHI_Vulkan::BindRaytracingShaderBindingInstances(const jCommandBuffer* InCommandBuffer, const jPipelineStateInfo* InPiplineState
    , const jShaderBindingInstanceCombiner& InShaderBindingInstanceCombiner, uint32 InFirstSet) const
{
	if (InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData)
	{
		check(InCommandBuffer);
		vkCmdBindDescriptorSets((VkCommandBuffer)InCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
			, (VkPipelineLayout)InPiplineState->GetPipelineLayoutHandle(), InFirstSet
			, InShaderBindingInstanceCombiner.DescriptorSetHandles.NumOfData, (const VkDescriptorSet*)&InShaderBindingInstanceCombiner.DescriptorSetHandles[0]
			, InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData, (InShaderBindingInstanceCombiner.DynamicOffsets.NumOfData ? &InShaderBindingInstanceCombiner.DynamicOffsets[0] : nullptr));
	}
}

void jRHI_Vulkan::BeginDebugEvent(jCommandBuffer* InCommandBuffer, const char* InName, const Vector4& InColor) const
{
    if (g_rhi_vk->vkCmdDebugMarkerBegin)
    {
        check(InCommandBuffer);
        VkDebugMarkerMarkerInfoEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;

        const float DefaultColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
        memcpy(markerInfo.color, &InColor, sizeof(InColor));

        markerInfo.pMarkerName = InName;
        g_rhi_vk->vkCmdDebugMarkerBegin((VkCommandBuffer)InCommandBuffer->GetHandle(), &markerInfo);
    }
}

void jRHI_Vulkan::EndDebugEvent(jCommandBuffer* InCommandBuffer) const
{
    if (g_rhi_vk->vkCmdDebugMarkerEnd)
    {
        g_rhi_vk->vkCmdDebugMarkerEnd((VkCommandBuffer)InCommandBuffer->GetHandle());
    }
}

bool jRHI_Vulkan::OnHandleResized(uint32 InWidth, uint32 InHeight, bool InIsMinimized)
{
    JASSERT(InWidth > 0);
    JASSERT(InHeight > 0);

    {
        char szTemp[126];
        sprintf_s(szTemp, sizeof(szTemp), "Called OnHandleResized %d %d\n", InWidth, InHeight);
        OutputDebugStringA(szTemp);
    }

	Finish();

	SCR_WIDTH = InWidth;
	SCR_HEIGHT = InHeight;

    // Swapchain
	check(Swapchain);
    verify(Swapchain->CreateInternal(Swapchain->Swapchain));

    return true;
}

jRaytracingScene* jRHI_Vulkan::CreateRaytracingScene() const
{
	return new jRaytracingScene_Vulkan();
}

std::shared_ptr<jBuffer> jRHI_Vulkan::CreateBufferInternal(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag, EResourceLayout InInitialState
	, const void* InData, uint64 InDataSize, const wchar_t* InResourceName) const
{
	if (InAlignment > 0)
		InSize = Align(InSize, InAlignment);

    EVulkanBufferBits BufferBits = EVulkanBufferBits::SHADER_DEVICE_ADDRESS | EVulkanBufferBits::TRANSFER_DST | EVulkanBufferBits::TRANSFER_SRC;
    if (!!(EBufferCreateFlag::UAV & InBufferCreateFlag))
    {
        BufferBits = BufferBits | EVulkanBufferBits::STORAGE_BUFFER;
    }

    if (!!(EBufferCreateFlag::AccelerationStructureBuildInput & InBufferCreateFlag))
    {
        BufferBits = BufferBits | EVulkanBufferBits::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY;
    }

    if (!!(EBufferCreateFlag::AccelerationStructure& InBufferCreateFlag))
    {
        BufferBits = BufferBits | EVulkanBufferBits::ACCELERATION_STRUCTURE_STORAGE;
    }

    if (!!(EBufferCreateFlag::VertexBuffer & InBufferCreateFlag))
    {
        BufferBits = BufferBits | EVulkanBufferBits::VERTEX_BUFFER;
    }
    
	if (!!(EBufferCreateFlag::IndexBuffer & InBufferCreateFlag))
    {
        BufferBits = BufferBits | EVulkanBufferBits::INDEX_BUFFER;
    }

	if (!!(EBufferCreateFlag::IndirectCommand & InBufferCreateFlag))
	{
		BufferBits = BufferBits | EVulkanBufferBits::INDIRECT_BUFFER;
	}

	if (!!(EBufferCreateFlag::ShaderBindingTable & InBufferCreateFlag))
	{
		BufferBits = BufferBits | EVulkanBufferBits::SHADER_BINDING_TABLE;
	}

    EVulkanMemoryBits MemoryBits = EVulkanMemoryBits::DEVICE_LOCAL;
    if (!!(EBufferCreateFlag::CPUAccess & InBufferCreateFlag))
    {
		// EBufferCreateFlag::CPUAccess 사용시 고민해볼 점
        // Map -> Unmap 했다가 메모리에 데이터가 즉시 반영되는게 아님, 바로 사용하려면 아래 2가지 방법이 있음.
		// 1. VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 사용 (항상 반영, 약간 느릴 수도)
		// 2. 쓰기 이후 vkFlushMappedMemoryRanges 호출, 읽기 이후 vkInvalidateMappedMemoryRanges 호출
		// 위의 2가지 방법을 사용해도 이 데이터가 GPU에 바로 보인다고 보장할 수는 없지만 다음 vkQueueSubmit 호출 전에는 완료될 것을 보장함.
		
		// 현재 코드는 모든 CPUAccess 는 HOST_COHERENT 로 사용.
        MemoryBits |= EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT;
    }

	auto Buffer_Vulkan = jBufferUtil_Vulkan::CreateBuffer(BufferBits, MemoryBits, InSize, InInitialState);
    if (InData && InDataSize > 0)
    {
		if (!!(EBufferCreateFlag::CPUAccess & InBufferCreateFlag))
		{
			Buffer_Vulkan->UpdateBuffer(InData, InDataSize);
		}
		else
		{
			auto stagingBufferPtr = g_rhi->CreateRawBuffer<jBuffer_Vulkan>(InDataSize, InAlignment, EBufferCreateFlag::CPUAccess, EResourceLayout::TRANSFER_SRC, InData, InDataSize, TEXT("StagingBuffer"));
			jBufferUtil_Vulkan::CopyBuffer(*stagingBufferPtr, *Buffer_Vulkan, InDataSize);
		}
    }
	return std::shared_ptr<jBuffer>(Buffer_Vulkan);
}

std::shared_ptr<jBuffer> jRHI_Vulkan::CreateStructuredBuffer(uint64 InSize, uint64 InAlignment, uint64 InStride
	, EBufferCreateFlag InBufferCreateFlag, EResourceLayout InInitialState, const void* InData, uint64 InDataSize , const wchar_t* InResourceName) const
{
	return CreateBufferInternal(InSize, InAlignment, InBufferCreateFlag, InInitialState, InData, InDataSize, InResourceName);
}

std::shared_ptr<jBuffer> jRHI_Vulkan::CreateRawBuffer(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
	, EResourceLayout InInitialState, const void* InData, uint64 InDataSize , const wchar_t* InResourceName) const
{
	return CreateBufferInternal(InSize, InAlignment, InBufferCreateFlag, InInitialState, InData, InDataSize, InResourceName);
}

std::shared_ptr<jBuffer> jRHI_Vulkan::CreateFormattedBuffer(uint64 InSize, uint64 InAlignment, ETextureFormat InFormat
	, EBufferCreateFlag InBufferCreateFlag, EResourceLayout InInitialState, const void* InData, uint64 InDataSize , const wchar_t* InResourceName) const
{
	return CreateBufferInternal(InSize, InAlignment, InBufferCreateFlag, InInitialState, InData, InDataSize, InResourceName);
}
