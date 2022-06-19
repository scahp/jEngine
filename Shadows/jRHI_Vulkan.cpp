#include "pch.h"

#if USE_VULKAN
#include "jRHI_Vulkan.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"	// 이거 안쓸예정
#include "jImageFileLoader.h"

jRHI_Vulkan* g_rhi_vk = nullptr;

VkSampler jTexture_Vulkan::CreateDefaultSamplerState()
{
	static VkSampler sampler = nullptr;
	if (sampler)
	{
		return sampler;
	}

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;

	// UV가 [0~1] 범위를 벗어는 경우 처리
	// VK_SAMPLER_ADDRESS_MODE_REPEAT : 반복해서 출력, UV % 1
	// VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : 반복하지만 거울에 비치듯 반대로 출력
	// VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : 범위 밖은 가장자리의 색으로 모두 출력
	// VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : 범위 밖은 반대편 가장자리의 색으로 모두 출력
	// VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : 단색으로 설정함. (samplerInfo.borderColor)
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;

	// 이게 true 이면 UV 좌표가 [0, texWidth], [0, texHeight] 가 됨. false 이면 [0, 1] 범위
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	// compareEnable이 ture 이면, 텍셀을 특정 값과 비교한 뒤 그 결과를 필터링 연산에 사용한다.
	// Percentage-closer filtering(PCF) 에 주로 사용됨.
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	uint32 textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(SCR_WIDTH, SCR_HEIGHT)))) + 1;		// 이것도 수정 필요. SamplerState 는 텍스쳐에 바인딩 해야 할듯 

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;	// Optional
	samplerInfo.minLod = 0.0f;		// Optional
	samplerInfo.maxLod = static_cast<float>(textureMipLevels);

	if (!ensure(vkCreateSampler(g_rhi_vk->device, &samplerInfo, nullptr, &sampler) == VK_SUCCESS))
		return nullptr;

	return sampler;
}


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

    window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);

	VkApplicationInfo appInfo;
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	// Must
	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// add extension
	auto extensions = GetRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	// check validation layer
	if (!ensure(!(enableValidationLayers && !CheckValidationLayerSupport())))
		return false;

	// add validation layer
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
	if (enableValidationLayers)
	{
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();

		PopulateDebutMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
	}
	else
	{
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);

	if (!ensure(result == VK_SUCCESS))
		return false;

	// SetupDebugMessenger
	{
		if (enableValidationLayers)
		{
			VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
			PopulateDebutMessengerCreateInfo(createInfo);
			createInfo.pUserData = nullptr;	// optional

			VkResult result = CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);
			check(result == VK_SUCCESS);
		}
	}

	{
		ensure(VK_SUCCESS == glfwCreateWindowSurface(instance, window, nullptr, &surface));
	}

	// Physical device
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		if (!ensure(deviceCount > 0))
			return false;

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		for (const auto& device : devices)
		{
			if (IsDeviceSuitable(device))
			{
				physicalDevice = device;
				msaaSamples = GetMaxUsableSampleCount();
				//msaaSamples = (VkSampleCountFlagBits)1;
				break;
			}
		}

		if (!ensure(physicalDevice != VK_NULL_HANDLE))
			return false;
	}

	// Logical device
	{
		QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

		float queuePriority = 1.0f;			// [0.0 ~ 1.0]
		for (uint32_t queueFamily : uniqueQueueFamilies)
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
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

		createInfo.pEnabledFeatures = &deviceFeatures;

		// extension
		createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		// 최신 버젼에서는 validation layer는 무시되지만, 오래된 버젼을 호환을 위해 vkInstance와 맞춰줌
		if (enableValidationLayers)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		if (!ensure(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) == VK_SUCCESS))
			return false;

		// 현재는 Queue가 1개 뿐이므로 QueueIndex를 0
		GraphicsQueue.QueueIndex = indices.graphicsFamily.value();
		PresentQueue.QueueIndex = indices.presentFamily.value();
		vkGetDeviceQueue(device, GraphicsQueue.QueueIndex, 0, &GraphicsQueue.Queue);
		vkGetDeviceQueue(device, PresentQueue.QueueIndex, 0, &PresentQueue.Queue);
	}

	// Swapchain
	{
		SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(physicalDevice);

		VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
		VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
		VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

		// SwapChain 개수 설정
		// 최소개수로 하게 되면, 우리가 렌더링할 새로운 이미지를 얻기 위해 드라이버가 내부 기능 수행을 기다려야 할 수 있으므로 min + 1로 설정.
		uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

		// maxImageCount가 0이면 최대 개수에 제한이 없음
		if ((swapChainSupport.capabilities.maxImageCount > 0) && (imageCount > swapChainSupport.capabilities.maxImageCount))
			imageCount = swapChainSupport.capabilities.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.minImageCount = imageCount;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;			// Stereoscopic 3D application(VR)이 아니면 항상 1
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;	// 즉시 스왑체인에 그리기 위해서 이걸로 설정
																		// 포스트 프로세스 같은 처리를 위해 별도의 이미지를 만드는 것이면
																		// VK_IMAGE_USAGE_TRANSFER_DST_BIT 으로 하면됨.

		QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
		uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

		// 그림은 Graphics Queue Family와 Present Queue Family가 다른경우 아래와 같이 동작한다.
		// - 이미지를 Graphics Queue에서 가져온 스왑체인에 그리고, Presentation Queue에 제출
		// 1. VK_SHARING_MODE_EXCLUSIVE : 이미지를 한번에 하나의 Queue Family 가 소유함. 소유권이 다른곳으로 전달될때 명시적으로 전달 해줘야함.
		//								이 옵션은 최고의 성능을 제공한다.
		// 2. VK_SHARING_MODE_CONCURRENT : 이미지가 여러개의 Queue Family 로 부터 명시적인 소유권 절달 없이 사용될 수 있다.
		if (indices.graphicsFamily != indices.presentFamily)
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0;		// Optional
			createInfo.pQueueFamilyIndices = nullptr;	// Optional
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;		// 스왑체인에 회전이나 flip 처리 할 수 있음.
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;					// 알파채널을 윈도우 시스템의 다른 윈도우와 블랜딩하는데 사용해야하는지 여부
																						// VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 는 알파채널 무시
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;			// 다른윈도우에 가려져서 보이지 않는 부분을 그리지 않을지에 대한 여부 VK_TRUE 면 안그림

		// 화면 크기 변경등으로 스왑체인이 다시 만들어져야 하는경우 여기에 oldSwapchain을 넘겨준다.
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		if (!ensure(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) == VK_SUCCESS))
			return false;

		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
		swapChainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;

		// ImageView 생성
		swapChainImageViews.resize(swapChainImages.size());
		for (size_t i = 0; i < swapChainImages.size(); ++i)
			swapChainImageViews[i] = CreateImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}

	CommandBufferManager.CratePool(GraphicsQueue.QueueIndex);

    ensure(CreateColorResources());
    ensure(CreateDepthResources());
    ensure(CreateSyncObjects());

	// 동적인 부분들 패스에 따라 달라짐
	{
		jAttachment color = { .Format = ETextureFormat::BGRA8, .SampleCount = (EMSAASamples)msaaSamples,
		.LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE, .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE, .ImageView = colorImageView };
		jAttachment depth = { .Format = ETextureFormat::D32, .SampleCount = (EMSAASamples)msaaSamples,
			.LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_DONTCARE, .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE, .ImageView = depthImageView };
		jAttachment resolve = { .Format = ETextureFormat::BGRA8, .SampleCount = (EMSAASamples)msaaSamples,
			.LoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_STORE, .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE, .ImageView = swapChainImageViews[0] };
		RenderPassTest.SetAttachemnt({ color }, depth, resolve);
		RenderPassTest.SetRenderArea({ 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
		RenderPassTest.CreateRenderPass();
	}

	FrameBufferTest.resize(swapChainImageViews.size());
	for (int32 i = 0; i < FrameBufferTest.size(); ++i)
	{
		FrameBufferTest[i].SetRenderPass(&RenderPassTest);
		ensure(FrameBufferTest[i].CreateFrameBuffer(i));
	}

	for (int32 i = 0; i < swapChainImageViews.size(); ++i)
	{
		IEmptyUniform* UniformBuffer = new EmptyUniform<jUniformBufferObject>();
		UniformBuffer->Create();
		UniformBuffers.push_back(UniformBuffer);
	}
	std::weak_ptr<jTexture> Texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName("chalet.jpg"), true, true);

	ShaderBindings.UniformBuffers.push_back(TBindings(0, EShaderAccessStageFlag::VERTEX));
	ShaderBindings.Textures.push_back(TBindings(1, EShaderAccessStageFlag::FRAGMENT));
	ShaderBindings.CreateDescriptorSetLayout();
	ShaderBindings.CreatePool();
	ShaderBindingInstances = ShaderBindings.CreateShaderBindingInstance((int32)swapChainImageViews.size());
	for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
	{
		ShaderBindingInstances[i].UniformBuffers.push_back(UniformBuffers[i]);
		ShaderBindingInstances[i].Textures.push_back(Texture.lock().get());
		ShaderBindingInstances[i].UpdateShaderBindings();
	}

	ensure(LoadModel());

	ensure(CreateGraphicsPipeline());
    ensure(RecordCommandBuffers());

	return true;
}

//std::vector<jAttachment> ColorAttachments;
//jAttachment DepthAttachment;
//jAttachment ColorAttachmentResolve;

//////////////////////////////////////////////////////////////////////////
// Auto generate type conversion code
template <typename T1, typename T2>
using ConversionTypePair = std::pair<T1, T2>;

template <typename T, typename... T1>
constexpr auto GenerateConversionTypeArray(T1... args)
{
    std::array<T, sizeof...(args)> newArray;
    auto AddElementFunc = [&newArray](const auto& arg)
    {
        newArray[(int32)arg.first] = arg.second;
    };

    int dummy[] = { 0, (AddElementFunc(args), 0)... };
    return newArray;
}

#define CONVERSION_TYPE_ELEMENT(x, y) ConversionTypePair<decltype(x), decltype(y)>(x, y)
#define GENERATE_STATIC_CONVERSION_ARRAY(...) {static auto _TypeArray_ = GenerateConversionTypeArray<T>(__VA_ARGS__); return (T)_TypeArray_[(int32)type];}

FORCEINLINE auto GetVulkanTextureInternalFormat(ETextureFormat type)
{
	using T = VkFormat;
    GENERATE_STATIC_CONVERSION_ARRAY(
        // TextureFormat + InternalFormat
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, VK_FORMAT_R32G32B32_SFLOAT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, VK_FORMAT_R16G16B16_SFLOAT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, VK_FORMAT_B10G11R11_UFLOAT_PACK32),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB, VK_FORMAT_R8G8B8_UNORM),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, VK_FORMAT_R16G16B16A16_SFLOAT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, VK_FORMAT_R32G32B32A32_SFLOAT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA, VK_FORMAT_R8G8B8A8_UNORM),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, VK_FORMAT_R32G32_SFLOAT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RG, VK_FORMAT_R8G8_UNORM),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::R, VK_FORMAT_R8_UNORM),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, VK_FORMAT_R32_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, VK_FORMAT_D24_UNORM_S8_UINT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, VK_FORMAT_D32_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, VK_FORMAT_D32_SFLOAT_S8_UINT),

        // below is Internal Format only
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA_INTEGER, VK_FORMAT_R8G8B8_SINT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::R_INTEGER, VK_FORMAT_R8_SINT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, VK_FORMAT_R8_UINT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, VK_FORMAT_R8G8B8A8_UNORM),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, VK_FORMAT_B8G8R8A8_UNORM),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, VK_FORMAT_R8G8B8A8_SINT),
        CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, VK_FORMAT_R8G8B8A8_UINT)
        // CONVERSION_TYPE_ELEMENT(ETextureFormat::DEPTH, GL_DEPTH_COMPONENT)
    );
}

FORCEINLINE void GetVulkanAttachmentLoadStoreOp(VkAttachmentLoadOp& OutLoadOp, VkAttachmentStoreOp& OutStoreOp, EAttachmentLoadStoreOp InType)
{
	switch(InType)
	{
	case EAttachmentLoadStoreOp::LOAD_STORE:
		OutLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		OutStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		break;
	case EAttachmentLoadStoreOp::LOAD_DONTCARE:
        OutLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        OutStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		break;
	case EAttachmentLoadStoreOp::CLEAR_STORE:
        OutLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        OutStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		break;
	case EAttachmentLoadStoreOp::CLEAR_DONTCARE:
        OutLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        OutStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		break;
	case EAttachmentLoadStoreOp::DONTCARE_STORE:
        OutLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        OutStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		break;
	case EAttachmentLoadStoreOp::DONTCARE_DONTCARE:
        OutLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        OutStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		break;
	case EAttachmentLoadStoreOp::MAX:
	default:
		check(0);
		break;
	}
}

FORCEINLINE auto GetVulkanShaderAccessFlags(EShaderAccessStageFlag type)
{
	switch(type)
	{
	case EShaderAccessStageFlag::VERTEX:
		return VK_SHADER_STAGE_VERTEX_BIT;
	case EShaderAccessStageFlag::TESSELLATION_CONTROL:
		return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	case EShaderAccessStageFlag::TESSELLATION_EVALUATION:
		return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	case EShaderAccessStageFlag::GEOMETRY:
		return VK_SHADER_STAGE_GEOMETRY_BIT;
	case EShaderAccessStageFlag::FRAGMENT:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case EShaderAccessStageFlag::COMPUTE:
		return VK_SHADER_STAGE_COMPUTE_BIT;
	case EShaderAccessStageFlag::ALL_GRAPHICS:
		return VK_SHADER_STAGE_ALL_GRAPHICS;
	case EShaderAccessStageFlag::ALL:
		return VK_SHADER_STAGE_ALL;
	default:
		check(0);
		break;
	}
	return VK_SHADER_STAGE_ALL;
}

FORCEINLINE auto GetVulkanTextureComponentCount(ETextureFormat type)
{
    using T = int8;
	GENERATE_STATIC_CONVERSION_ARRAY(
		// TextureFormat + InternalFormat
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, 2),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG, 2),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA_INTEGER, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R_INTEGER, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::DEPTH, 1)
	);
}

FORCEINLINE VkPrimitiveTopology GetVulkanPrimitiveTopology(EPrimitiveType type)
{
	using T = VkPrimitiveTopology;
	GENERATE_STATIC_CONVERSION_ARRAY(
		// TextureFormat + InternalFormat
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::POINTS, VK_PRIMITIVE_TOPOLOGY_POINT_LIST),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::LINES, VK_PRIMITIVE_TOPOLOGY_LINE_LIST),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::LINES_ADJACENCY, VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::LINE_STRIP_ADJACENCY, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLES, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLES_ADJACENCY, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLE_STRIP_ADJACENCY, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY),
		CONVERSION_TYPE_ELEMENT(EPrimitiveType::TRIANGLE_STRIP, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
	);
}

//bool jRHI_Vulkan::CreateDescriptorSetLayout()
//{
//	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
//	uboLayoutBinding.binding = 0;
//	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//	uboLayoutBinding.descriptorCount = 1;
//
//	// VkShaderStageFlagBits 에 값을 | 연산으로 조합가능
//	//  - VK_SHADER_STAGE_ALL_GRAPHICS 로 설정하면 모든곳에서 사용
//	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
//	uboLayoutBinding.pImmutableSamplers = nullptr;	// Optional (이미지 샘플링 시에서만 사용)
//
//	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
//	samplerLayoutBinding.binding = 1;
//	samplerLayoutBinding.descriptorCount = 1;
//	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//	samplerLayoutBinding.pImmutableSamplers = nullptr;
//	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;	// sampler 를 fragment shader에 붙임.
//
//	std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
//
//	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
//	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
//	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
//	layoutInfo.pBindings = bindings.data();
//
//	if (!ensure(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) == VK_SUCCESS))
//		return false;
//
//	return true;
//}

struct jRasterizationStateInfo
{
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

	// VkPipelineRasterizationStateCreateFlags flags;
};

template <EPolygonMode TPolygonMode = EPolygonMode::FILL, ECullMode TCullMode = ECullMode::BACK, EFrontFace TFrontFace = EFrontFace::CCW,
	bool TDepthBiasEnable = false, float TDepthBiasConstantFactor = 0.0f, float TDepthBiasClamp = 0.0f, float TDepthBiasSlopeFactor = 0.0f,
	float TLineWidth = 1.0f, bool TDepthClampEnable = false, bool TRasterizerDiscardEnable = false>
struct TRasterizationStateInfo
{
	FORCEINLINE static jRasterizationStateInfo Create()
	{
		jRasterizationStateInfo state;
		state.PolygonMode = TPolygonMode;
		state.CullMode = TCullMode;
		state.FrontFace = TFrontFace;
		state.DepthBiasEnable = TDepthBiasEnable;
		state.DepthBiasConstantFactor = TDepthBiasConstantFactor;
		state.DepthBiasClamp = TDepthBiasClamp;
		state.DepthBiasSlopeFactor = TDepthBiasSlopeFactor;
		state.LineWidth = TLineWidth;
		state.DepthClampEnable = TDepthClampEnable;
		state.RasterizerDiscardEnable = TRasterizerDiscardEnable;
		return state;
	}
};

class jMultisampleStateInfo
{
	EMSAASamples SampleCount = EMSAASamples::COUNT_1;
	bool SampleShadingEnable = true;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
	float MinSampleShading = 0.2f;
	bool AlphaToCoverageEnable = false;
	bool AlphaToOneEnable = false;

	// VkPipelineMultisampleStateCreateFlags flags;
	// const VkSampleMask* pSampleMask;
};

template <EMSAASamples TSampleCount = EMSAASamples::COUNT_1, bool TSampleShadingEnable = true, float TMinSampleShading = 0.2f,
		  bool TAlphaToCoverageEnable = false, bool TAlphaToOneEnable = false>
struct TMultisampleStateInfo
{
	FORCEINLINE static jMultisampleStateInfo Create()
	{
		jMultisampleStateInfo state;
		state.SampleCount = TSampleCount;
		state.SampleShadingEnable = TSampleShadingEnable;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
		state.MinSampleShading = TMinSampleShading;
		state.AlphaToCoverageEnable = TAlphaToCoverageEnable;
		state.AlphaToOneEnable = TAlphaToOneEnable;
		return state;
	}
};

struct jStencilOpStateInfo
{
	EStencilOp FailOp = EStencilOp::KEEP;
	EStencilOp PassOp = EStencilOp::KEEP;
	EStencilOp DepthFailOp = EStencilOp::KEEP;
	EComparisonOp CompareOp = EComparisonOp::NEVER;
	uint32 CompareMask = 0;
	uint32 WriteMask = 0;
	uint32 Reference = 0;
};

template <EStencilOp TFailOp = EStencilOp::KEEP, EStencilOp TPassOp = EStencilOp::KEEP, EStencilOp TDepthFailOp = EStencilOp::KEEP,
		  EComparisonOp TCompareOp = EComparisonOp::NEVER, uint32 TCompareMask = 0, uint32 TWriteMask = 0, uint32 TReference = 0>
struct TStencilOpStateInfo
{
	FORCEINLINE static jStencilOpStateInfo Create()
	{
		jStencilOpStateInfo state;
		state.FailOp = TFailOp;
		state.PassOp = TPassOp;
		state.DepthFailOp = TDepthFailOp;
		state.CompareOp = TCompareOp;
		state.CompareMask = TCompareMask;
		state.WriteMask = TWriteMask;
		state.Reference = TReference;
		return state;
	}
};

struct jDepthStencilStateInfo
{
	bool DepthTestEnable = false;
	bool DepthWriteEnable = false;
	EDepthComparionFunc DepthCompareOp = EDepthComparionFunc::LESS_EQUAL;
	bool DepthBoundsTestEnable = false;
	bool StencilTestEnable = false;
	jStencilOpStateInfo Front;
	jStencilOpStateInfo Back;
	float MinDepthBounds = 0.0f;
	float MaxDepthBounds = 1.0f;

	// VkPipelineDepthStencilStateCreateFlags    flags;
};

template <bool TDepthTestEnable = false, bool TDepthWriteEnable = false, EDepthComparionFunc TDepthCompareOp = EDepthComparionFunc::LESS_EQUAL,
		  bool TDepthBoundsTestEnable = false, bool TStencilTestEnable = false, 
		  jStencilOpStateInfo TFront = jStencilOpStateInfo(), jStencilOpStateInfo TBack = jStencilOpStateInfo(),
		  float TMinDepthBounds = 0.0f, float TMaxDepthBounds = 1.0f>
struct TDepthStencilStateInfo
{
	FORCEINLINE static jDepthStencilStateInfo Create()
	{
		jDepthStencilStateInfo state;
		state.DepthTestEnable = TDepthTestEnable;
		state.DepthWriteEnable = TDepthWriteEnable;
		state.DepthCompareOp = TDepthCompareOp;
		state.DepthBoundsTestEnable = TDepthBoundsTestEnable;
		state.StencilTestEnable = TStencilTestEnable;
		state.Front = TFront;
		state.Back = TBack;
		state.MinDepthBounds = TMinDepthBounds;
		state.MaxDepthBounds = TMaxDepthBounds;
		return state;
	}
};

struct jBlendingStateInfo
{
	bool BlendEnable = false;
	EBlendSrc Src = EBlendSrc::SRC_COLOR;
	EBlendDest Dest = EBlendDest::ONE_MINUS_SRC_ALPHA;
	EBlendOp BlendOp = EBlendOp::ADD;
	EBlendSrc SrcAlpha = EBlendSrc::SRC_ALPHA;
	EBlendDest DestAlpha = EBlendDest::ONE_MINUS_SRC_ALPHA;
	EBlendOp AlphaBlendOp = EBlendOp::ADD;
	EColorMask ColorWriteMask = EColorMask::NONE;

	//VkPipelineColorBlendStateCreateFlags          flags;
	//VkBool32                                      logicOpEnable;
	//VkLogicOp                                     logicOp;
	//uint32_t                                      attachmentCount;
	//const VkPipelineColorBlendAttachmentState* pAttachments;
	//float                                         blendConstants[4];
};

template <bool TBlendEnable = false, EBlendSrc TSrc = EBlendSrc::SRC_COLOR, EBlendDest TDest = EBlendDest::ONE_MINUS_SRC_ALPHA, EBlendOp TBlendOp = EBlendOp::ADD,
	EBlendSrc TSrcAlpha = EBlendSrc::SRC_ALPHA, EBlendDest TDestAlpha = EBlendDest::ONE_MINUS_SRC_ALPHA, EBlendOp TAlphaBlendOp = EBlendOp::ADD,
	EColorMask TColorWriteMask = EColorMask::NONE>
struct TBlendingStateInfo
{
	FORCEINLINE static jBlendingStateInfo Create()
	{
		jBlendingStateInfo state;
		state.BlendEnable = TBlendEnable;
		state.Src = TSrc;
		state.Dest = TDest;
		state.BlendOp = TBlendOp;
		state.SrcAlpha = TSrcAlpha;
		state.DestAlpha = TDestAlpha;
		state.AlphaBlendOp = TAlphaBlendOp;
		state.ColorWriteMask = TColorWriteMask;
		return state;
	}
};

bool jRHI_Vulkan::CreateGraphicsPipeline()
{
	struct jPipelineStateInfo
	{
		jShader* VertexShader = nullptr;
		jShader* GeometryShader = nullptr;
		jShader* PixelShader = nullptr;
		jShader* ComputeShader = nullptr;

		jVertexBuffer* VertexBuffer = nullptr;
		std::vector<jViewport> Viewports;
		std::vector<jScissor> Scissors;

		jFrameBuffer* FrameBuffer = nullptr;
		jShaderBindings* ShaderBindings = nullptr;

		jRasterizationStateInfo RasterizationState;
		jMultisampleStateInfo MultisampleSate;
		jStencilOpStateInfo StencilOpState;
		jDepthStencilStateInfo DepthStencilState;
		jBlendingStateInfo BlendingState;

		VkPipeline CreateGraphicsPipelineState()
		{
			VkPipeline t;
			return t;

			//// 1. Create Shader
			//auto vertShaderCode = ReadFile("Shaders/vert.spv");
			//auto fragShaderCode = ReadFile("Shaders/frag.spv");

			//VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
			//VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

			//VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
			//vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			//vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			//vertShaderStageInfo.module = vertShaderModule;
			//vertShaderStageInfo.pName = "main";

			//// pSpecializationInfo 을 통해 쉐이더에서 사용하는 상수값을 설정해줄 수 있음. 이 상수 값에 따라 if 분기문에 없어지거나 하는 최적화가 일어날 수 있음.
			////vertShaderStageInfo.pSpecializationInfo

			//VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
			//fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			//fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			//fragShaderStageInfo.module = fragShaderModule;
			//fragShaderStageInfo.pName = "main";

			//VkPipelineShaderStageCreateInfo shaderStage[] = { vertShaderStageInfo, fragShaderStageInfo };
			//// VkShaderModule 은 이 함수의 끝에서 Destroy 시킴

			////////////////////////////////////////////////////////////////////////////
			//// 2. Vertex Input
			//// 1). Bindings : 데이터 사이의 간격과 버택스당 or 인스턴스당(인스턴싱 사용시) 데이터인지 여부
			//// 2). Attribute descriptions : 버택스 쉐이더 전달되는 attributes 의 타입. 그것을 로드할 바인딩과 오프셋
			//VkPipelineVertexInputStateCreateInfo vertexInputInfo = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateVertexInputState();

			//// 3. Input Assembly
			//VkPipelineInputAssemblyStateCreateInfo inputAssembly = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateInputAssemblyState();

			//// 4. Viewports and scissors
			//// SwapChain의 이미지 사이즈가 이 클래스에 정의된 상수 WIDTH, HEIGHT와 다를 수 있다는 것을 기억 해야함.
			//// 그리고 Viewports 사이즈는 SwapChain 크기 이하로 마추면 됨.
			//// [minDepth ~ maxDepth] 는 [0.0 ~ 1.0] 이며 특별한 경우가 아니면 이 범위로 사용하면 된다.
			//// Scissor Rect 영역을 설정해주면 영역내에 있는 Pixel만 레스터라이저를 통과할 수 있으며 나머지는 버려(Discard)진다.
			//std::vector<VkViewport> viewports;
			//viewports.resize(Viewports.size());
			//for (int32 i = 0; i < Viewports.size(); ++i)
			//{
			//	viewports[i].x = Viewports[i].X;
			//	viewports[i].y = Viewports[i].Y;
			//	viewports[i].width = Viewports[i].Width;
			//	viewports[i].height = Viewports[i].Height;
			//	viewports[i].minDepth = Viewports[i].MinDepth;
			//	viewports[i].maxDepth = Viewports[i].MaxDepth;
			//}

			//std::vector<VkRect2D> scissors;
			//scissors.resize(Scissors.size());
			//for (int32 i = 0; i < Scissors.size(); ++i)
			//{
			//	scissors[i].offset = { .x = Scissors[i].Offset.x, .y = Scissors[i].Offset.y };
			//	scissors[i].extent = { .width = (uint32)Scissors[i].Extent.x, .height = (uint32)Scissors[i].Extent.y };
			//}

			//// Viewport와 Scissor 를 여러개 설정할 수 있는 멀티 뷰포트를 사용할 수 있기 때문임
			//VkPipelineViewportStateCreateInfo viewportState = {};
			//viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			//viewportState.viewportCount = viewports.size();
			//viewportState.pViewports = &viewports[0];
			//viewportState.scissorCount = scissors.size();
			//viewportState.pScissors = &scissors[0];

			//// 5. Rasterizer
			//VkPipelineRasterizationStateCreateInfo rasterizer = {};
			//rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			//rasterizer.depthClampEnable = RasterizationState.DepthBiasClamp;		// 이 값이 VK_TRUE 면 Near나 Far을 벗어나는 영역을 [0.0 ~ 1.0]으로 Clamp 시켜줌.(쉐도우맵에서 유용)
			//rasterizer.rasterizerDiscardEnable = RasterizationState.RasterizerDiscardEnable;	// 이 값이 VK_TRUE 면, 레스터라이저 스테이지를 통과할 수 없음. 즉 Framebuffer 로 결과가 넘어가지 않음.
			//rasterizer.polygonMode = RasterizationState.PolygonMode VK_POLYGON_MODE_FILL;	// FILL, LINE, POINT 세가지가 있음
			//rasterizer.lineWidth = RasterizationState.LineWidth;
			//rasterizer.cullMode = RasterizationState.CullMode VK_CULL_MODE_BACK_BIT;
			//rasterizer.frontFace = RasterizationState.FrontFace VK_FRONT_FACE_COUNTER_CLOCKWISE;
			//rasterizer.depthBiasEnable = RasterizationState.DepthBiasEnable;			// 쉐도우맵 용
			//rasterizer.depthBiasConstantFactor = RasterizationState.DepthBiasConstantFactor;		// Optional
			//rasterizer.depthBiasClamp = RasterizationState.DepthBiasClamp;				// Optional
			//rasterizer.depthBiasSlopeFactor = RasterizationState.DepthBiasSlopeFactor;			// Optional

			//// 6. Multisampling
			//// 현재는 사용하지 않음
			//VkPipelineMultisampleStateCreateInfo multisampling = {};
			//multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			//multisampling.rasterizationSamples = (VkSampleCountFlagBits)MultisampleSate.SampleCount;
			//multisampling.sampleShadingEnable = MultisampleSate.SampleShadingEnable;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
			//multisampling.minSampleShading = MultisampleSate.MinSampleShading;
			//multisampling.alphaToCoverageEnable = MultisampleSate.AlphaToCoverageEnable;		// Optional
			//multisampling.alphaToOneEnable = MultisampleSate.AlphaToOneEnable;			// Optional

			//// 7. Depth and stencil testing
			//VkPipelineDepthStencilStateCreateInfo depthStencil = {};
			//depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			//depthStencil.depthTestEnable = DepthStencilState.DepthTestEnable;
			//depthStencil.depthWriteEnable = DepthStencilState.DepthWriteEnable;
			//depthStencil.depthCompareOp = DepthStencilState.DepthCompareOp VK_COMPARE_OP_LESS;
			//depthStencil.depthBoundsTestEnable = DepthStencilState.DepthBoundsTestEnable;
			//depthStencil.minDepthBounds = DepthStencilState.MinDepthBounds;		// Optional
			//depthStencil.maxDepthBounds = DepthStencilState.MaxDepthBounds;		// Optional
			//depthStencil.stencilTestEnable = DepthStencilState.StencilTestEnable;
			//depthStencil.front = DepthStencilState.Front;
			//depthStencil.back = DepthStencilState.Back;

			//// 8. Color blending
			//// 2가지 방식의 blending 이 있음
			//// 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
			//// 2). 기존과 새로운 값을 비트 연산으로 결합한다.
			///*
			//	// Blend 가 켜져있으면 대략 이런식으로 동작함
			//	if (blendEnable)
			//	{
			//		finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
			//		finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
			//	} else
			//	{
			//		finalColor = newColor;
			//	}

			//	finalColor = finalColor & colorWriteMask;
			//*/			

			//// 아래 내용들은 Attachment 정의할 때 같이 해주는게 나을까?

			//VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
			//colorBlendAttachment.colorWriteMask = BlendingState.ColorWriteMask;
			//colorBlendAttachment.blendEnable = BlendingState.BlendEnable;						// 현재 VK_FALSE라 새로운 값이 그대로 사용됨
			//colorBlendAttachment.srcColorBlendFactor = BlendingState.Src;		// Optional
			//colorBlendAttachment.dstColorBlendFactor = BlendingState.Dest;	// Optional
			//colorBlendAttachment.colorBlendOp = BlendingState.BlendOp;				// Optional
			//colorBlendAttachment.srcAlphaBlendFactor = BlendingState.SrcAlpha;		// Optional
			//colorBlendAttachment.dstAlphaBlendFactor = BlendingState.DestAlpha;	// Optional
			//colorBlendAttachment.alphaBlendOp = BlendingState.AlphaBlendOp;				// Optional

			//// 일반적인 경우 블랜드는 아래와 같은 식을 사용한다.
			///*
			//	finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
			//	finalColor.a = newAlpha.a;
			//*/
			//// 이렇게 하려면 아래와 같이 설정해야 함.
			////colorBlendAttachment.blendEnable = VK_TRUE;
			////colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			////colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			////colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			////colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			////colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			////colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			//// 2가지 blending 방식을 모두 끌 수도있는데 그렇게 되면, 변경되지 않은 fragment color 값이 그대로 framebuffer에 쓰여짐.
			//VkPipelineColorBlendStateCreateInfo colorBlending = {};
			//colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			//// 2). 기존과 새로운 값을 비트 연산으로 결합한다.
			//colorBlending.logicOpEnable = VK_FALSE;			// 모든 framebuffer에 사용하는 blendEnable을 VK_FALSE로 했다면 자동으로 logicOpEnable은 꺼진다.
			//colorBlending.logicOp = VK_LOGIC_OP_COPY;		// Optional

			//// 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
			//colorBlending.attachmentCount = 1;						// framebuffer 개수에 맞게 설정해준다.
			//colorBlending.pAttachments = &colorBlendAttachment;

			//colorBlending.blendConstants[0] = 0.0f;		// Optional
			//colorBlending.blendConstants[1] = 0.0f;		// Optional
			//colorBlending.blendConstants[2] = 0.0f;		// Optional
			//colorBlending.blendConstants[3] = 0.0f;		// Optional

			//// 9. Dynamic state
			//// 이전에 정의한 state에서 제한된 범위 내에서 새로운 pipeline을 만들지 않고 state를 변경할 수 있음. (viewport size, line width, blend constants)
			//// 이것을 하고싶으면 Dynamic state를 만들어야 함. 이경우 Pipeline에 설정된 값은 무시되고, 매 렌더링시에 새로 설정해줘야 함.
			//VkDynamicState dynamicStates[] = {
			//	VK_DYNAMIC_STATE_VIEWPORT,
			//	VK_DYNAMIC_STATE_LINE_WIDTH
			//};
			//// 현재는 사용하지 않음.
			////VkPipelineDynamicStateCreateInfo dynamicState = {};
			////dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			////dynamicState.dynamicStateCount = 2;
			////dynamicState.pDynamicStates = dynamicStates;

			//// 10. Pipeline layout
			//VkPipelineLayout pipelineLayout = ShaderBindings.CreatePipelineLayout();
			//if (!ensure(pipelineLayout))
			//{
			//	vkDestroyShaderModule(device, fragShaderModule, nullptr);
			//	vkDestroyShaderModule(device, vertShaderModule, nullptr);
			//	return false;
			//}

			//VkGraphicsPipelineCreateInfo pipelineInfo = {};
			//pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

			//// Shader stage
			//pipelineInfo.stageCount = 2;
			//pipelineInfo.pStages = shaderStage;

			//// Fixed-function stage
			//pipelineInfo.pVertexInputState = &vertexInputInfo;
			//pipelineInfo.pInputAssemblyState = &inputAssembly;
			//pipelineInfo.pViewportState = &viewportState;
			//pipelineInfo.pRasterizationState = &rasterizer;
			//pipelineInfo.pMultisampleState = &multisampling;
			//pipelineInfo.pDepthStencilState = &depthStencil;
			//pipelineInfo.pColorBlendState = &colorBlending;
			//pipelineInfo.pDynamicState = nullptr;			// Optional
			//pipelineInfo.layout = pipelineLayout;
			//pipelineInfo.renderPass = (VkRenderPass)RenderPassTest.GetRenderPass();
			//pipelineInfo.subpass = 0;		// index of subpass

			//// Pipeline을 상속받을 수 있는데, 아래와 같은 장점이 있다.
			//// 1). 공통된 내용이 많으면 파이프라인 설정이 저렴하다.
			//// 2). 공통된 부모를 가진 파이프라인 들끼리의 전환이 더 빠를 수 있다.
			//// BasePipelineHandle 이나 BasePipelineIndex 가 로 Pipeline 내용을 상속받을 수 있다.
			//// 이 기능을 사용하려면 VkGraphicsPipelineCreateInfo의 flags 에 VK_PIPELINE_CREATE_DERIVATIVE_BIT  가 설정되어있어야 함
			//pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;		// Optional
			//pipelineInfo.basePipelineIndex = -1;					// Optional

			//// 여기서 두번째 파라메터 VkPipelineCache에 VK_NULL_HANDLE을 넘겼는데, VkPipelineCache는 VkPipeline을 저장하고 생성하는데 재사용할 수 있음.
			//// 또한 파일로드 저장할 수 있어서 다른 프로그램에서 다시 사용할 수도있다. VkPipelineCache를 사용하면 VkPipeline을 생성하는 시간을 
			//// 굉장히 빠르게 할수있다. (듣기로는 대략 1/10 의 속도로 생성해낼 수 있다고 함)
			//if (!ensure(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) == VK_SUCCESS))
			//	return false;

			//return true;
		}
	};

	// 1. Create Shader
	auto vertShaderCode = ReadFile("Shaders/vert.spv");
	auto fragShaderCode = ReadFile("Shaders/frag.spv");

	VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	// pSpecializationInfo 을 통해 쉐이더에서 사용하는 상수값을 설정해줄 수 있음. 이 상수 값에 따라 if 분기문에 없어지거나 하는 최적화가 일어날 수 있음.
	//vertShaderStageInfo.pSpecializationInfo

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStage[] = { vertShaderStageInfo, fragShaderStageInfo };
	// VkShaderModule 은 이 함수의 끝에서 Destroy 시킴

	// 2. Vertex Input
	// 1). Bindings : 데이터 사이의 간격과 버택스당 or 인스턴스당(인스턴싱 사용시) 데이터인지 여부
	// 2). Attribute descriptions : 버택스 쉐이더 전달되는 attributes 의 타입. 그것을 로드할 바인딩과 오프셋
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateVertexInputState();

	// 3. Input Assembly
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateInputAssemblyState();

	// 4. Viewports and scissors
	// SwapChain의 이미지 사이즈가 이 클래스에 정의된 상수 WIDTH, HEIGHT와 다를 수 있다는 것을 기억 해야함.
	// 그리고 Viewports 사이즈는 SwapChain 크기 이하로 마추면 됨.
	// [minDepth ~ maxDepth] 는 [0.0 ~ 1.0] 이며 특별한 경우가 아니면 이 범위로 사용하면 된다.
	// Scissor Rect 영역을 설정해주면 영역내에 있는 Pixel만 레스터라이저를 통과할 수 있으며 나머지는 버려(Discard)진다.
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(swapChainExtent.width);
	viewport.height = static_cast<float>(swapChainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = swapChainExtent;

	// Viewport와 Scissor 를 여러개 설정할 수 있는 멀티 뷰포트를 사용할 수 있기 때문임
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	// 5. Rasterizer
	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;		// 이 값이 VK_TRUE 면 Near나 Far을 벗어나는 영역을 [0.0 ~ 1.0]으로 Clamp 시켜줌.(쉐도우맵에서 유용)
	rasterizer.rasterizerDiscardEnable = VK_FALSE;	// 이 값이 VK_TRUE 면, 레스터라이저 스테이지를 통과할 수 없음. 즉 Framebuffer 로 결과가 넘어가지 않음.
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;	// FILL, LINE, POINT 세가지가 있음
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	//rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;			// 쉐도우맵 용
	rasterizer.depthBiasConstantFactor = 0.0f;		// Optional
	rasterizer.depthBiasClamp = 0.0f;				// Optional
	rasterizer.depthBiasSlopeFactor = 0.0f;			// Optional

	// 6. Multisampling
	// 현재는 사용하지 않음
	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = msaaSamples;
	multisampling.sampleShadingEnable = VK_TRUE;		// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
	multisampling.minSampleShading = 0.2f;
	multisampling.pSampleMask = nullptr;				// Optional
	multisampling.alphaToCoverageEnable = VK_FALSE;		// Optional
	multisampling.alphaToOneEnable = VK_FALSE;			// Optional

	// 7. Depth and stencil testing
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f;		// Optional
	depthStencil.maxDepthBounds = 1.0f;		// Optional

	// 8. Color blending
	// 2가지 방식의 blending 이 있음
	// 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
	// 2). 기존과 새로운 값을 비트 연산으로 결합한다.
	/*
		// Blend 가 켜져있으면 대략 이런식으로 동작함
		if (blendEnable)
		{
			finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
			finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
		} else
		{
			finalColor = newColor;
		}

		finalColor = finalColor & colorWriteMask;
	*/
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;						// 현재 VK_FALSE라 새로운 값이 그대로 사용됨
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;		// Optional
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;	// Optional
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;				// Optional
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;		// Optional
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;	// Optional
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;				// Optional

	// 일반적인 경우 블랜드는 아래와 같은 식을 사용한다.
	/*
		finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
		finalColor.a = newAlpha.a;
	*/
	// 이렇게 하려면 아래와 같이 설정해야 함.
	//colorBlendAttachment.blendEnable = VK_TRUE;
	//colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	//colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	//colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	//colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	//colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	//colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	// 2가지 blending 방식을 모두 끌 수도있는데 그렇게 되면, 변경되지 않은 fragment color 값이 그대로 framebuffer에 쓰여짐.
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	// 2). 기존과 새로운 값을 비트 연산으로 결합한다.
	colorBlending.logicOpEnable = VK_FALSE;			// 모든 framebuffer에 사용하는 blendEnable을 VK_FALSE로 했다면 자동으로 logicOpEnable은 꺼진다.
	colorBlending.logicOp = VK_LOGIC_OP_COPY;		// Optional

	// 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
	colorBlending.attachmentCount = 1;						// framebuffer 개수에 맞게 설정해준다.
	colorBlending.pAttachments = &colorBlendAttachment;

	colorBlending.blendConstants[0] = 0.0f;		// Optional
	colorBlending.blendConstants[1] = 0.0f;		// Optional
	colorBlending.blendConstants[2] = 0.0f;		// Optional
	colorBlending.blendConstants[3] = 0.0f;		// Optional

	// 9. Dynamic state
	// 이전에 정의한 state에서 제한된 범위 내에서 새로운 pipeline을 만들지 않고 state를 변경할 수 있음. (viewport size, line width, blend constants)
	// 이것을 하고싶으면 Dynamic state를 만들어야 함. 이경우 Pipeline에 설정된 값은 무시되고, 매 렌더링시에 새로 설정해줘야 함.
	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_LINE_WIDTH
	};
	// 현재는 사용하지 않음.
	//VkPipelineDynamicStateCreateInfo dynamicState = {};
	//dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	//dynamicState.dynamicStateCount = 2;
	//dynamicState.pDynamicStates = dynamicStates;

	// 10. Pipeline layout
	VkPipelineLayout pipelineLayout = ShaderBindings.CreatePipelineLayout();
	if (!ensure(pipelineLayout))
	{
		vkDestroyShaderModule(device, fragShaderModule, nullptr);
		vkDestroyShaderModule(device, vertShaderModule, nullptr);
		return false;
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

	// Shader stage
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStage;

	// Fixed-function stage
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = nullptr;			// Optional
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = (VkRenderPass)RenderPassTest.GetRenderPass();
	pipelineInfo.subpass = 0;		// index of subpass

	// Pipeline을 상속받을 수 있는데, 아래와 같은 장점이 있다.
	// 1). 공통된 내용이 많으면 파이프라인 설정이 저렴하다.
	// 2). 공통된 부모를 가진 파이프라인 들끼리의 전환이 더 빠를 수 있다.
	// BasePipelineHandle 이나 BasePipelineIndex 가 로 Pipeline 내용을 상속받을 수 있다.
	// 이 기능을 사용하려면 VkGraphicsPipelineCreateInfo의 flags 에 VK_PIPELINE_CREATE_DERIVATIVE_BIT  가 설정되어있어야 함
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;		// Optional
	pipelineInfo.basePipelineIndex = -1;					// Optional

	// 여기서 두번째 파라메터 VkPipelineCache에 VK_NULL_HANDLE을 넘겼는데, VkPipelineCache는 VkPipeline을 저장하고 생성하는데 재사용할 수 있음.
	// 또한 파일로드 저장할 수 있어서 다른 프로그램에서 다시 사용할 수도있다. VkPipelineCache를 사용하면 VkPipeline을 생성하는 시간을 
	// 굉장히 빠르게 할수있다. (듣기로는 대략 1/10 의 속도로 생성해낼 수 있다고 함)
	if (!ensure(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) == VK_SUCCESS))
	{
		vkDestroyShaderModule(device, fragShaderModule, nullptr);
		vkDestroyShaderModule(device, vertShaderModule, nullptr);
		return false;
	}

	vkDestroyShaderModule(device, fragShaderModule, nullptr);
	vkDestroyShaderModule(device, vertShaderModule, nullptr);

	return true;
}

//bool jRHI_Vulkan::CreateCommandPool()
//{
//	QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
//
//	VkCommandPoolCreateInfo poolInfo = {};
//	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
//	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
//
//	// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 커맨드 버퍼가 새로운 커맨드를 자주 다시 기록한다고 힌트를 줌.
//	//											(메모리 할당 동작을 변경할 것임)
//	// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 커맨드 버퍼들이 개별적으로 다시 기록될 수 있다.
//	//													이 플래그가 없으면 모든 커맨드 버퍼들이 동시에 리셋되야 함.
//	// 우리는 프로그램 시작시에 커맨드버퍼를 한번 녹화하고 계속해서 반복사용 할 것이므로 flags를 설정하지 않음.
//	poolInfo.flags = 0;		// Optional
//
//	if (!ensure(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS))
//		return false;
//
//	return true;
//}

bool jRHI_Vulkan::CreateColorResources()
{
	VkFormat colorFormat = swapChainImageFormat;

	if (!ensure(CreateImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL
		, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory)))
	{
		return false;
	}
	colorImageView = CreateImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	return true;
}

bool jRHI_Vulkan::CreateDepthResources()
{
	VkFormat depthFormat = FindDepthFormat();

	if (!ensure(CreateImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
		, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory)))
	{
		return false;
	}
	depthImageView = CreateImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

	TransitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);

	return true;
}

//bool jRHI_Vulkan::CreateFrameBuffers()
//{
//	swapChainFramebuffers.resize(swapChainImageViews.size());
//	for (size_t i = 0; i < swapChainImageViews.size(); ++i)
//	{
//		// DepthBuffer는 같은 것을 씀. 세마포어 때문에 한번에 1개의 subpass 만 실행되기 때문.
//		std::vector<VkImageView> attachments;
//		if (msaaSamples > 1)
//			attachments = { colorImageView, depthImageView, swapChainImageViews[i] };
//		else
//			attachments = { swapChainImageViews[i], depthImageView };
//
//		VkFramebufferCreateInfo framebufferInfo = {};
//		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
//		framebufferInfo.renderPass = RenderPassTest.GetRenderPass();
//
//		// RenderPass와 같은 수와 같은 타입의 attachment 를 사용
//		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
//		framebufferInfo.pAttachments = attachments.data();
//
//		framebufferInfo.width = swapChainExtent.width;
//		framebufferInfo.height = swapChainExtent.height;
//		framebufferInfo.layers = 1;			// 이미지 배열의 레이어수(3D framebuffer에 사용할 듯)
//
//		if (!ensure(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) == VK_SUCCESS))
//			return false;
//	}
//
//	return true;
//}

#include "stb_image.h"
const std::string MODEL_PATH = "chalet.obj";
const std::string TEXTURE_PATH = "chalet.jpg";
uint32_t textureMipLevels;
//bool jRHI_Vulkan::CreateTextureImage()
//{
//	int texWidth, texHeight, texChannels;
//	stbi_uc* pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
//
//	if (!ensure(pixels))
//		return false;
//
//	VkDeviceSize imageSize = texWidth * texHeight * 4;
//	textureMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max<int>(texWidth, texHeight)))) + 1;
//
//	VkBuffer stagingBuffer;
//	VkDeviceMemory stagingBufferMemory;
//
//	CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
//		, stagingBuffer, stagingBufferMemory);
//
//	void* data;
//	vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
//	memcpy(data, pixels, static_cast<size_t>(imageSize));
//	vkUnmapMemory(device, stagingBufferMemory);
//
//	stbi_image_free(pixels);
//
//	if (!ensure(CreateImage(static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), textureMipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM
//		, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
//		| VK_IMAGE_USAGE_SAMPLED_BIT	// image를 shader 에서 접근가능하게 하고 싶은 경우
//		, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory)))
//	{
//		return false;
//	}
//
//	if (!TransitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, textureMipLevels))
//		return false;
//	CopyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
//
//	// 밉맵을 만드는 동안 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 으로 전환됨.
//	//// 이제 쉐이더에 읽기가 가능하게 하기위해서 아래와 같이 적용.
//	//if (TransitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, textureMipLevels))
//	//	return false;
//
//	vkDestroyBuffer(device, stagingBuffer, nullptr);
//	vkFreeMemory(device, stagingBufferMemory, nullptr);
//
//	if (!ensure(GenerateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, textureMipLevels)))
//		return false;
//
//	// Create Texture image view
//	textureImageView = CreateImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, textureMipLevels);
//
//	return true;
//}

//bool jRHI_Vulkan::CreateTextureSampler()
//{
//	VkSamplerCreateInfo samplerInfo = {};
//	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
//	samplerInfo.magFilter = VK_FILTER_LINEAR;
//	samplerInfo.minFilter = VK_FILTER_LINEAR;
//
//	// UV가 [0~1] 범위를 벗어는 경우 처리
//	// VK_SAMPLER_ADDRESS_MODE_REPEAT : 반복해서 출력, UV % 1
//	// VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : 반복하지만 거울에 비치듯 반대로 출력
//	// VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : 범위 밖은 가장자리의 색으로 모두 출력
//	// VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : 범위 밖은 반대편 가장자리의 색으로 모두 출력
//	// VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : 단색으로 설정함. (samplerInfo.borderColor)
//	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
//	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
//	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
//	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
//
//	samplerInfo.anisotropyEnable = VK_TRUE;
//	samplerInfo.maxAnisotropy = 16;
//
//	// 이게 true 이면 UV 좌표가 [0, texWidth], [0, texHeight] 가 됨. false 이면 [0, 1] 범위
//	samplerInfo.unnormalizedCoordinates = VK_FALSE;
//
//	// compareEnable이 ture 이면, 텍셀을 특정 값과 비교한 뒤 그 결과를 필터링 연산에 사용한다.
//	// Percentage-closer filtering(PCF) 에 주로 사용됨.
//	samplerInfo.compareEnable = VK_FALSE;
//	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
//
//	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
//	samplerInfo.mipLodBias = 0.0f;	// Optional
//	samplerInfo.minLod = 0.0f;		// Optional
//	samplerInfo.maxLod = static_cast<float>(textureMipLevels);
//
//	if (!ensure(vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) == VK_SUCCESS))
//		return false;
//
//	return true;
//}

bool jRHI_Vulkan::LoadModel()
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!ensure(tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())))
		return false;

	struct jVertexHashFunc
	{
		std::size_t operator()(const Vector& pos, const Vector2& texCoord, const Vector& color) const
		{
			size_t result = 0;
			result = std::hash<float>{}(pos.x);
			result ^= std::hash<float>{}(pos.y);
			result ^= std::hash<float>{}(pos.z);
			result ^= std::hash<float>{}(color.x);
			result ^= std::hash<float>{}(color.y);
			result ^= std::hash<float>{}(color.z);
			result ^= std::hash<float>{}(texCoord.x);
			result ^= std::hash<float>{}(texCoord.y);
			return result;
		}
	};

	std::unordered_map<size_t, uint32> uniqueVertices = {};

	std::vector<float> vertices_temp;
	std::vector<float> texCoords_temp;
	std::vector<float> colors_temp;
	std::vector<uint32> indices;

	for (const auto& shape : shapes)
	{
		for (const auto& index : shape.mesh.indices)
		{
			Vector pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};

			Vector2 texCoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
			};

			Vector color = { 1.0f, 1.0f, 1.0f };

			const size_t hash = jVertexHashFunc()(pos, texCoord, color);
			if (uniqueVertices.count(hash) == 0)
			{
				uniqueVertices[hash] = static_cast<uint32>(vertices_temp.size() / 3);

				vertices_temp.push_back(pos.x);
				vertices_temp.push_back(pos.y);
				vertices_temp.push_back(pos.z);

				texCoords_temp.push_back(texCoord.x);
				texCoords_temp.push_back(texCoord.y);

				colors_temp.push_back(color.x);
				colors_temp.push_back(color.y);
				colors_temp.push_back(color.z);
			}

			indices.push_back(uniqueVertices[hash]);
		}
	}

	auto vertexStreamData = std::shared_ptr<jVertexStreamData>(new jVertexStreamData());
	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = (uint32)(vertices_temp.size() / 3);

	{
		auto streamParam = new jStreamParam<float>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->ElementTypeSize = sizeof(float);
		streamParam->ElementType = EBufferElementType::FLOAT;
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("Pos");
		streamParam->Data = std::move(vertices_temp);
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = new jStreamParam<float>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->ElementType = EBufferElementType::FLOAT;
		streamParam->ElementTypeSize = sizeof(float);
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("Color");
		streamParam->Data = std::move(colors_temp);
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = new jStreamParam<float>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->ElementType = EBufferElementType::FLOAT;
		streamParam->ElementTypeSize = sizeof(float);
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TexCoord");
		streamParam->Data = std::move(texCoords_temp);
		vertexStreamData->Params.push_back(streamParam);
	}
	VertexBuffer = g_rhi_vk->CreateVertexBuffer(vertexStreamData);

	auto indexStreamData = std::shared_ptr<jIndexStreamData>(new jIndexStreamData());
	indexStreamData->ElementCount = static_cast<int32>(indices.size());
	{
		auto streamParam = new jStreamParam<uint32>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->ElementType = EBufferElementType::UNSIGNED_INT;
		streamParam->ElementTypeSize = sizeof(uint32);
		streamParam->Stride = sizeof(uint32) * 3;
		streamParam->Name = jName("Index");
		streamParam->Data = std::move(indices);
		indexStreamData->Param = streamParam;
	}
	IndexBuffer = g_rhi_vk->CreateIndexBuffer(indexStreamData);

	return true;
}

//struct jVertexStreamData : public std::enable_shared_from_this<jVertexStreamData>
//{
//	~jVertexStreamData()
//	{
//		for (auto param : Params)
//			delete param;
//		Params.clear();
//	}
//
//	std::vector<IStreamParam*> Params;
//	EPrimitiveType PrimitiveType;
//	int ElementCount = 0;
//};
//
//struct jVertexBuffer : public IBuffer
//{
//	std::weak_ptr<jVertexStreamData> VertexStreamData;
//
//	virtual void Bind(const jShader* shader) const {}
//};

//struct jVertexBuffer_OpenGL : public jVertexBuffer
//{
//	uint32 VAO = 0;
//	std::vector<jVertexStream_OpenGL> Streams;
//
//	virtual void Bind(const jShader* shader) const override;
//};


//bool jRHI_Vulkan::CreateVertexBuffer()
//{
//	VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
//	VkBuffer stagingBuffer;
//	VkDeviceMemory stagingBufferMemory;
//
//	// VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 이 버퍼가 메모리 전송 연산의 소스가 될 수 있음.
//	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT
//		, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
//
//	//// 마지막 파라메터 0은 메모리 영역의 offset 임.
//	//// 이 값이 0이 아니면 memRequirements.alignment 로 나눠야 함. (align 되어있다는 의미)
//	//vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);
//
//	void* data;
//	// size 항목에 VK_WHOLE_SIZE  를 넣어서 모든 메모리를 잡을 수도 있음.
//	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
//	memcpy(data, vertices.data(), (size_t)bufferSize);
//	vkUnmapMemory(device, stagingBufferMemory);
//
//	// Map -> Unmap 했다가 메모리에 데이터가 즉시 반영되는게 아님
//	// 바로 사용하려면 아래 2가지 방법이 있음.
//	// 1. VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 사용 (항상 반영, 약간 느릴 수도)
//	// 2. 쓰기 이후 vkFlushMappedMemoryRanges 호출, 읽기 이후 vkInvalidateMappedMemoryRanges 호출
//	// 위의 2가지 방법을 사용해도 이 데이터가 GPU에 바로 보인다고 보장할 수는 없지만 다음 vkQueueSubmit 호출 전에는 완료될 것을 보장함.
//
//	// VK_BUFFER_USAGE_TRANSFER_DST_BIT : 이 버퍼가 메모리 전송 연산의 목적지가 될 수 있음.
//	// DEVICE LOCAL 메모리에 VertexBuffer를 만들었으므로 이제 vkMapMemory 같은 것은 할 수 없음.
//	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
//		, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
//
//	CopyBuffer(stagingBuffer, vertexBuffer, bufferSize);
//
//	vkDestroyBuffer(device, stagingBuffer, nullptr);
//	vkFreeMemory(device, stagingBufferMemory, nullptr);
//
//	return true;
//}

//bool jRHI_Vulkan::CreateIndexBuffer()
//{
//	VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
//
//	VkBuffer stagingBuffer;
//	VkDeviceMemory stagingBufferMemory;
//	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
//
//	void* data;
//	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
//	memcpy(data, indices.data(), (size_t)bufferSize);
//	vkUnmapMemory(device, stagingBufferMemory);
//
//	CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);
//
//	CopyBuffer(stagingBuffer, indexBuffer, bufferSize);
//
//	vkDestroyBuffer(device, stagingBuffer, nullptr);
//	vkFreeMemory(device, stagingBufferMemory, nullptr);
//
//	return true;
//}

jIndexBuffer* jRHI_Vulkan::CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) const
{
	check(streamData);
	check(streamData->Param);
	jIndexBuffer_Vulkan* indexBuffer = new jIndexBuffer_Vulkan();
	indexBuffer->IndexStreamData = streamData;

	VkDeviceSize bufferSize = streamData->Param->GetBufferSize();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	g_rhi_vk->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data = nullptr;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, streamData->Param->GetBufferData(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	g_rhi_vk->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer->IndexBuffer, indexBuffer->IndexBufferMemory);
	g_rhi_vk->CopyBuffer(stagingBuffer, indexBuffer->IndexBuffer, bufferSize);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
	return indexBuffer;
}

//bool jRHI_Vulkan::CreateUniformBuffers()
//{
//	VkDeviceSize bufferSize = sizeof(jUniformBufferObject);
//
//	uniformBuffers.resize(swapChainImages.size());
//	uniformBuffersMemory.resize(swapChainImages.size());
//
//	for (size_t i = 0; i < swapChainImages.size(); ++i)
//	{
//		CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i], uniformBuffersMemory[i]);
//	}
//	return true;
//}

//bool jRHI_Vulkan::CreateDescriptorPool()
//{
//	std::array<VkDescriptorPoolSize, 2> poolSizes = {};
//	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//	poolSizes[0].descriptorCount = static_cast<uint32_t>(swapChainImages.size());
//	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//	poolSizes[1].descriptorCount = static_cast<uint32_t>(swapChainImages.size());
//
//	VkDescriptorPoolCreateInfo poolInfo = {};
//	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
//	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
//	poolInfo.pPoolSizes = poolSizes.data();
//	poolInfo.maxSets = static_cast<uint32_t>(swapChainImages.size());
//	poolInfo.flags = 0;		// Descriptor Set을 만들고나서 더 이상 손대지 않을거라 그냥 기본값 0으로 설정
//
//	if (!ensure(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) == VK_SUCCESS))
//		return false;
//
//	return true;
//}

//bool jRHI_Vulkan::CreateDescriptorSets()
//{
//	std::vector<VkDescriptorSetLayout> layouts(swapChainImages.size(), ShaderBindings.DescriptorSetLayout);
//	VkDescriptorSetAllocateInfo allocInfo = {};
//	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
//	allocInfo.descriptorPool = descriptorPool;
//	allocInfo.descriptorSetCount = static_cast<uint32_t>(swapChainImages.size());
//	allocInfo.pSetLayouts = layouts.data();
//
//	descriptorSets.resize(swapChainImages.size());
//	if (!ensure(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) == VK_SUCCESS))
//		return false;
//
//	for (size_t i = 0; i < swapChainImages.size(); ++i)
//	{
//		VkDescriptorBufferInfo bufferInfo = {};
//		bufferInfo.buffer = uniformBuffers[i];
//		bufferInfo.offset = 0;
//		bufferInfo.range = sizeof(jUniformBufferObject);		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능
//
//		VkDescriptorImageInfo imageInfo = {};
//		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//		imageInfo.imageView = ((jTexture_Vulkan*)ShaderBindings.Textures[0].Data)->ImageView;
//		imageInfo.sampler = textureSampler;
//
//		std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
//		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//		descriptorWrites[0].dstSet = ShaderBindingInstances[i].DescriptorSet;
//		descriptorWrites[0].dstBinding = 0;
//		descriptorWrites[0].dstArrayElement = 0;
//		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//		descriptorWrites[0].descriptorCount = 1;
//		descriptorWrites[0].pBufferInfo = &bufferInfo;		// 현재는 Buffer 기반 Desriptor 이므로 이것을 사용
//		descriptorWrites[0].pImageInfo = nullptr;			// Optional	(Image Data 기반에 사용)
//		descriptorWrites[0].pTexelBufferView = nullptr;		// Optional (Buffer View 기반에 사용)
//
//		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//		descriptorWrites[1].dstSet = ShaderBindingInstances[i].DescriptorSet;
//		descriptorWrites[1].dstBinding = 1;
//		descriptorWrites[1].dstArrayElement = 0;
//		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//		descriptorWrites[1].descriptorCount = 1;
//		descriptorWrites[1].pImageInfo = &imageInfo;
//
//		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size())
//			, descriptorWrites.data(), 0, nullptr);
//	}
//
//	return true;
//}

bool jRHI_Vulkan::RecordCommandBuffers()
{
	// Begin command buffers
	for (int32 i = 0; i < swapChainImageViews.size(); ++i)
	{
		commandBuffers.push_back(CommandBufferManager.GetOrCreateCommandBuffer());

		if (!ensure(commandBuffers[i].Begin()))
			return false;

		RenderPassTest.BeginRenderPass(commandBuffers[i].Get(), (VkFramebuffer)FrameBufferTest[i].GetFrameBuffer());

		// Basic drawing commands
		vkCmdBindPipeline(commandBuffers[i].Get(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

		ShaderBindingInstances[i].Bind(commandBuffers[i]);

		((jVertexBuffer_Vulkan*)VertexBuffer)->Bind(commandBuffers[i]);
		((jIndexBuffer_Vulkan*)IndexBuffer)->Bind(commandBuffers[i]);

		//vkCmdDraw(commandBuffers[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0);			// VertexBuffer 만 있는 경우 호출
		vkCmdDrawIndexed(commandBuffers[i].Get(), ((jIndexBuffer_Vulkan*)IndexBuffer)->GetIndexCount(), 1, 0, 0, 0);

		// Finishing up
		RenderPassTest.EndRenderPass();

		if (!ensure(commandBuffers[i].End()))
			return false;
	}

	return true;
}

bool jRHI_Vulkan::CreateSyncObjects()
{
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

#if MULTIPLE_FRAME
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
	imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (!ensure(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) == VK_SUCCESS)
			|| !ensure(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) == VK_SUCCESS)
			|| !ensure(vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) == VK_SUCCESS))
		{
			return false;
		}
	}
#else
	if (!ensure(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) == VK_SUCCESS)
		|| !ensure(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) == VK_SUCCESS))
	{
		return false;
	}
#endif // MULTIPLE_FRAME
	return true;
}

void jRHI_Vulkan::MainLoop()
{
	//while (!glfwWindowShouldClose(window))
	//{
		glfwPollEvents();
		DrawFrame();
	//}

	// Logical device 가 작업을 모두 마칠때까지 기다렸다가 Destory를 진행할 수 있게 기다림.
	vkDeviceWaitIdle(device);
}

bool jRHI_Vulkan::DrawFrame()
{
	// 이 함수는 아래 3가지 기능을 수행함
// 1. 스왑체인에서 이미지를 얻음
// 2. Framebuffer 에서 Attachment로써 얻어온 이미지를 가지고 커맨드 버퍼를 실행
// 3. 스왑체인 제출을 위해 이미지를 반환

// 스왑체인을 동기화는 2가지 방법
// 1. Fences		: vkWaitForFences 를 사용하여 프로그램에서 fences의 상태에 접근 할 수 있음.
//						Fences는 어플리케이션과 렌더링 명령의 동기화를 위해 설계됨
// 2. Semaphores	: 세마포어는 안됨.
//						Semaphores 는 커맨드 Queue 내부 혹은 Queue 들 사이에 명령어 동기화를 위해서 설계됨

// 현재는 Draw 와 presentation 커맨드를 동기화 하는 곳에 사용할 것이므로 세마포어가 적합.
// 2개의 세마포어를 만들 예정
// 1. 이미지를 획득해서 렌더링 준비가 완료된 경우 Signal(Lock 이 풀리는) 되는 것 (imageAvailableSemaphore)
// 2. 렌더링을 마쳐서 Presentation 가능한 상태에서 Signal 되는 것 (renderFinishedSemaphore)

#if MULTIPLE_FRAME
	vkWaitForFences(device, 1, &inFlightFences[currenFrame], VK_TRUE, UINT64_MAX);
#endif // MULTIPLE_FRAME

	uint32_t imageIndex;
	// timeout 은 nanoseconds. UINT64_MAX 는 타임아웃 없음
#if MULTIPLE_FRAME
	VkResult acquireNextImageResult = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currenFrame], VK_NULL_HANDLE, &imageIndex);

	// 이전 프레임에서 현재 사용하려는 이미지를 사용중에 있나? (그렇다면 펜스를 기다려라)
	if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
		vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);

	// 이 프레임에서 펜스를 사용한다고 마크 해둠
	imagesInFlight[imageIndex] = inFlightFences[currenFrame];
#else
	VkResult acquireNextImageResult = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
#endif // MULTIPLE_FRAME

	// 여기서는 VK_SUCCESS or VK_SUBOPTIMAL_KHR 은 성공했다고 보고 계속함.
	// VK_ERROR_OUT_OF_DATE_KHR : 스왑체인이 더이상 서피스와 렌더링하는데 호환되지 않는 경우. (보통 윈도우 리사이즈 이후)
	// VK_SUBOPTIMAL_KHR : 스왑체인이 여전히 서피스에 제출 가능하지만, 서피스의 속성이 더이상 정확하게 맞지 않음.
	if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapChain();		// 이 경우 렌더링이 더이상 불가능하므로 즉시 스왑체인을 새로 만듬.
		return false;
	}
	else if (acquireNextImageResult != VK_SUCCESS && acquireNextImageResult != VK_SUBOPTIMAL_KHR)
	{
		check(0);
		return false;
	}

	UpdateUniformBuffer(imageIndex);

	// Submitting the command buffer
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

#if MULTIPLE_FRAME
	VkSemaphore waitsemaphores[] = { imageAvailableSemaphores[currenFrame] };
#else
	VkSemaphore waitsemaphores[] = { imageAvailableSemaphore };
#endif // MULTIPLE_FRAME
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitsemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[imageIndex].GetRef();

#if MULTIPLE_FRAME
	VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currenFrame] };
#else
	VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
#endif // MULTIPLE_FRAME
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	// SubmitInfo를 동시에 할수도 있음.
#if MULTIPLE_FRAME
	vkResetFences(device, 1, &inFlightFences[currenFrame]);		// 세마포어와는 다르게 수동으로 펜스를 unsignaled 상태로 재설정 해줘야 함

	// 마지막에 Fences 파라메터는 커맨드 버퍼가 모두 실행되고나면 Signaled 될 Fences.
	if (!ensure(vkQueueSubmit(GraphicsQueue.Queue, 1, &submitInfo, inFlightFences[currenFrame]) == VK_SUCCESS))
		return false;
#else
	if (!ensure(vkQueueSubmit(GraphicsQueue.Queue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS))
		return false;
#endif // MULTIPLE_FRAME

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = { swapChain };
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
		return false;
	}


	// CPU 가 큐에 작업을 제출하는 속도가 GPU 에서 소모하는 속도보다 빠른 경우 큐에 작업이 계속 쌓이거나 
	// 여러 프레임에 걸쳐 동시에 imageAvailableSemaphore 와 renderFinishedSemaphore를 재사용하게 되는 문제가 있음.
	// 1). 한프레임을 마치고 큐가 빌때까지 기다리는 것으로 해결할 수 있음. 한번에 1개의 프레임만 완성 가능(최적의 해결방법은 아님)
	// 2). 여러개의 프레임을 동시에 처리 할수있도록 확장. 동시에 진행될 수 있는 최대 프레임수를 지정해줌.
#if MULTIPLE_FRAME
	currenFrame = (currenFrame + 1) % MAX_FRAMES_IN_FLIGHT;
#else
	vkQueueWaitIdle(PresentQueue);
#endif // MULTIPLE_FRAME

	return true;
}

void jRHI_Vulkan::CleanupSwapChain()
{
	vkDestroyImageView(device, colorImageView, nullptr);
	vkDestroyImage(device, colorImage, nullptr);
	vkFreeMemory(device, colorImageMemory, nullptr);

	vkDestroyImageView(device, depthImageView, nullptr);
	vkDestroyImage(device, depthImage, nullptr);
	vkFreeMemory(device, depthImageMemory, nullptr);

	// ImageViews and RenderPass 가 소멸되기전에 호출되어야 함
	//for (auto framebuffer : swapChainFramebuffers)
	//	vkDestroyFramebuffer(device, framebuffer, nullptr);
	RenderPassTest.Release();

	for (auto imageView : swapChainImageViews)
		vkDestroyImageView(device, imageView, nullptr);

	vkDestroySwapchainKHR(device, swapChain, nullptr);

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

#include "jCamera.h"
void jRHI_Vulkan::UpdateUniformBuffer(uint32_t currentImage)
{
	static auto startTime = std::chrono::high_resolution_clock::now();

	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	jUniformBufferObject ubo = {};
	ubo.Model.SetIdentity();
	ubo.View.SetIdentity();
	ubo.Proj.SetIdentity();
	//ubo.Model = Matrix::MakeRotate(Vector(0.0f, 0.0f, 1.0f), time * DegreeToRadian(90.0f)).GetTranspose();
	ubo.Model = Matrix::MakeRotate(Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(245.0f)).GetTranspose();
	ubo.View = jCameraUtil::CreateViewMatrix(Vector(2.0f, 2.0f, 2.0f), Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, 1.0f)).GetTranspose();
	ubo.Proj = jCameraUtil::CreatePerspectiveMatrix(static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height)
		, DegreeToRadian(45.0f), 10.0f, 0.1f).GetTranspose();
	ubo.Proj.m[1][1] *= -1;

	//ubo.Model.SetTranslate({ 0.2f, 0.2f,0.2f });
	//ubo.Model = ubo.Model.MakeRotateZ(time * DegreeToRadian(90.0f));

	void* data = nullptr;
	vkMapMemory(device, (VkDeviceMemory)UniformBuffers[currentImage]->GetBufferMemory(), 0, sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(device, (VkDeviceMemory)UniformBuffers[currentImage]->GetBufferMemory());
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
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingBufferMemory;

			// VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 이 버퍼가 메모리 전송 연산의 소스가 될 수 있음.
			g_rhi_vk->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

			//// 마지막 파라메터 0은 메모리 영역의 offset 임.
			//// 이 값이 0이 아니면 memRequirements.alignment 로 나눠야 함. (align 되어있다는 의미)
			//vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

			void* data = nullptr;
			// size 항목에 VK_WHOLE_SIZE  를 넣어서 모든 메모리를 잡을 수도 있음.
			vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
			memcpy(data, iter->GetBufferData(), (size_t)bufferSize);
			vkUnmapMemory(device, stagingBufferMemory);

			// Map -> Unmap 했다가 메모리에 데이터가 즉시 반영되는게 아님
			// 바로 사용하려면 아래 2가지 방법이 있음.
			// 1. VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 사용 (항상 반영, 약간 느릴 수도)
			// 2. 쓰기 이후 vkFlushMappedMemoryRanges 호출, 읽기 이후 vkInvalidateMappedMemoryRanges 호출
			// 위의 2가지 방법을 사용해도 이 데이터가 GPU에 바로 보인다고 보장할 수는 없지만 다음 vkQueueSubmit 호출 전에는 완료될 것을 보장함.

			// VK_BUFFER_USAGE_TRANSFER_DST_BIT : 이 버퍼가 메모리 전송 연산의 목적지가 될 수 있음.
			// DEVICE LOCAL 메모리에 VertexBuffer를 만들었으므로 이제 vkMapMemory 같은 것은 할 수 없음.
			g_rhi_vk->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
				, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, stream.VertexBuffer, stream.VertexBufferMemory);

			g_rhi_vk->CopyBuffer(stagingBuffer, stream.VertexBuffer, bufferSize);

			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingBufferMemory, nullptr);
		}
		vertexBuffer->BindInfos.Buffers.push_back(stream.VertexBuffer);
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
		case EBufferElementType::UNSIGNED_INT:
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
	, EFormatType dataType, ETextureFormat textureFormat, bool createMipmap) const
{
    VkDeviceSize imageSize = width * height * GetVulkanTextureComponentCount(textureFormat);
    textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(width, height)))) + 1;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        , stagingBuffer, stagingBufferMemory);

    void* dset = nullptr;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &dset);
    memcpy(dset, data, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

	VkFormat vkTextureFormat = GetVulkanTextureInternalFormat(textureFormat);

	VkImage TextureImage;
	VkDeviceMemory TextureImageMemory;
    if (!ensure(CreateImage(static_cast<uint32>(width), static_cast<uint32>(height), textureMipLevels, VK_SAMPLE_COUNT_1_BIT, vkTextureFormat
        , VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT	// image를 shader 에서 접근가능하게 하고 싶은 경우
        , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, TextureImage, TextureImageMemory)))
    {
        return nullptr;
    }

    if (!TransitionImageLayout(TextureImage, vkTextureFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, textureMipLevels))
        return nullptr;
    CopyBufferToImage(stagingBuffer, TextureImage, static_cast<uint32>(width), static_cast<uint32>(height));

    // 밉맵을 만드는 동안 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 으로 전환됨.
    //// 이제 쉐이더에 읽기가 가능하게 하기위해서 아래와 같이 적용.
    //if (TransitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, textureMipLevels))
    //	return false;

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

	if (createMipmap)
	{
		if (!ensure(GenerateMipmaps(TextureImage, vkTextureFormat, width, height, textureMipLevels)))
			return nullptr;
	}

    // Create Texture image view
    VkImageView textureImageView = CreateImageView(TextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, textureMipLevels);

    auto texture = new jTexture_Vulkan();
    texture->sRGB = sRGB;
    texture->ColorBufferType = textureFormat;
    texture->ColorPixelType = dataType;
    texture->Type = ETextureType::TEXTURE_2D;
    texture->Width = width;
    texture->Height = height;
    
	texture->Image = TextureImage;
	texture->ImageView = textureImageView;
	texture->ImageMemory = TextureImageMemory;

	return texture;
}

void jRenderPass_Vulkan::Release()
{
    vkDestroyRenderPass(g_rhi_vk->device, RenderPass, nullptr);
}

bool jRenderPass_Vulkan::CreateRenderPass()
{
    int32 AttachmentIndex = 0;
    std::vector<VkAttachmentDescription> AttachmentDescs;
	AttachmentDescs.reserve(ColorAttachments.size() + 2);		// Colors, Depth, ColorResolve
    AttachmentDescs.resize(ColorAttachments.size() + 1);

	bool IsUseMSAA = false;

    std::vector<VkAttachmentReference> colorAttachmentRefs;
    colorAttachmentRefs.resize(ColorAttachments.size());
    for (int32 i = 0; i < ColorAttachments.size(); ++i)
    {
        VkAttachmentDescription& colorDesc = AttachmentDescs[AttachmentIndex];
        colorDesc.format = GetVulkanTextureInternalFormat(ColorAttachments[i].Format);
        colorDesc.samples = (VkSampleCountFlagBits)ColorAttachments[i].SampleCount;
        GetVulkanAttachmentLoadStoreOp(colorDesc.loadOp, colorDesc.storeOp, ColorAttachments[i].LoadStoreOp);
        GetVulkanAttachmentLoadStoreOp(colorDesc.stencilLoadOp, colorDesc.stencilStoreOp, ColorAttachments[i].StencilLoadStoreOp);

		if ((int32)ColorAttachments[i].SampleCount > 1)
			IsUseMSAA = true;

        // Texture나 Framebuffer의 경우 VkImage 객체로 특정 픽셀 형식을 표현함.
        // 그러나 메모리의 픽셀 레이아웃은 이미지로 수행하려는 작업에 따라서 변경될 수 있음.
        // 그래서 여기서는 수행할 작업에 적절한 레이아웃으로 image를 전환시켜주는 작업을 함.
        // 주로 사용하는 layout의 종류
        // 1). VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : Color attachment의 이미지
        // 2). VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : Swapchain으로 제출된 이미지
        // 3). VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : Memory copy 연산의 Destination으로 사용된 이미지
        colorDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			// RenderPass가 시작되기 전에 어떤 Image 레이아웃을 가지고 있을지 여부를 지정
                                                                            // VK_IMAGE_LAYOUT_UNDEFINED 은 이전 이미지 레이아웃을 무시한다는 의미.
                                                                            // 주의할 점은 이미지의 내용이 보존되지 않습니다. 그러나 현재는 이미지를 Clear할 것이므로 보존할 필요가 없음.

        //colorDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;	// RenderPass가 끝날때 자동으로 전환될 Image 레이아웃을 정의함
        //																	// 우리는 렌더링 결과를 스왑체인에 제출할 것이기 때문에 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 사용

        // MSAA 를 사용하게 되면, Swapchain에 제출전에 resolve 를 해야하므로, 아래와 같이 final layout 을 변경해줌.
        // 그리고 reoslve를 위한 VkAttachmentDescription 을 추가함. depth buffer의 경우는 Swapchain에 제출하지 않기 때문에 이 과정이 필요없음.
		if (IsUseMSAA)
			colorDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		else
			colorDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;			// MSAA 안하면 바로 Present 할 것이므로

        //////////////////////////////////////////////////////////////////////////

        // Subpasses
        // 하나의 렌더링패스에는 여러개의 서브렌더패스가 존재할 수 있다. 예를 들어서 포스트프로세스를 처리할때 여러 포스트프로세스를 
        // 서브패스로 전달하여 하나의 렌더패스로 만들 수 있다. 이렇게 하면 불칸이 Operation과 메모리 대역폭을 아껴쓸 수도 있으므로 성능이 더 나아진다.
        // 포스프 프로세스 처리들 사이에 (GPU <-> Main memory에 계속해서 Framebuffer 내용을 올렸다 내렸다 하지 않고 계속 GPU에 올려두고 처리할 수 있음)
        // 현재는 1개의 서브패스만 사용함.
        VkAttachmentReference& colorAttachmentRef = colorAttachmentRefs[i];
        colorAttachmentRef.attachment = AttachmentIndex++;							// VkAttachmentDescription 배열의 인덱스
                                                                                    // fragment shader의 layout(location = 0) out vec4 outColor; 에 location=? 에 인덱스가 매칭됨.
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;		// 서브패스에서 어떤 이미지 레이아웃을 사용할것인지를 명세함.
                                                                                    // Vulkan에서 이 서브패스가 되면 자동으로 Image 레이아웃을 이것으로 변경함.
                                                                                    // 우리는 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 으로 설정하므로써 color attachment로 사용
    }

    VkAttachmentDescription& depthAttachment = AttachmentDescs[AttachmentIndex];
    depthAttachment.format = GetVulkanTextureInternalFormat(DepthAttachment.Format);
    depthAttachment.samples = (VkSampleCountFlagBits)DepthAttachment.SampleCount;
    GetVulkanAttachmentLoadStoreOp(depthAttachment.loadOp, depthAttachment.storeOp, DepthAttachment.LoadStoreOp);
    GetVulkanAttachmentLoadStoreOp(depthAttachment.stencilLoadOp, depthAttachment.stencilStoreOp, DepthAttachment.StencilLoadStoreOp);
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = AttachmentIndex++;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentResolveRef = {};
	if (IsUseMSAA)
	{
		VkAttachmentDescription colorAttachmentResolve = {};
		colorAttachmentResolve.format = GetVulkanTextureInternalFormat(ColorAttachmentResolve.Format);
		colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
		GetVulkanAttachmentLoadStoreOp(colorAttachmentResolve.loadOp, colorAttachmentResolve.storeOp, ColorAttachmentResolve.LoadStoreOp);
		GetVulkanAttachmentLoadStoreOp(colorAttachmentResolve.stencilLoadOp, colorAttachmentResolve.stencilStoreOp, ColorAttachmentResolve.StencilLoadStoreOp);
		colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		AttachmentDescs.emplace_back(colorAttachmentResolve);

		colorAttachmentResolveRef.attachment = AttachmentIndex++;		// color attachments + depth attachment
		colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32)colorAttachmentRefs.size();
    subpass.pColorAttachments = &colorAttachmentRefs[0];
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
	if (IsUseMSAA)
		subpass.pResolveAttachments = &colorAttachmentResolveRef;
	else
		subpass.pResolveAttachments = nullptr;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(AttachmentDescs.size());
    renderPassInfo.pAttachments = AttachmentDescs.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    // 1. Subpasses 들의 이미지 레이아웃 전환은 자동적으로 일어나며, 이런 전환은 subpass dependencies 로 관리 됨.
    // 현재 서브패스를 1개 쓰고 있지만 서브패스 앞, 뒤에 암묵적인 서브페이스가 있음.
    // 2. 2개의 내장된 dependencies 가 렌더패스 시작과 끝에 있음.
    //		- 렌더패스 시작에 있는 경우 정확한 타이밍에 발생하지 않음. 파이프라인 시작에서 전환이 발생한다고 가정되지만 우리가 아직 이미지를 얻지 못한 경우가 있을 수 있음.
    //		 이 문제를 해결하기 위해서 2가지 방법이 있음.
    //			1). imageAvailableSemaphore 의 waitStages를 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 로 변경하여, 이미지가 사용가능하기 전에 렌더패스가 시작되지 못하도록 함.
    //			2). 렌더패스가 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 스테이지를 기다리게 함. (여기선 이거 씀)
    VkSubpassDependency dependency = {};
    // VK_SUBPASS_EXTERNAL : implicit subpass 인 before or after render pass 에서 발생하는 subpass
    // 여기서 dstSubpass 는 0으로 해주는데 현재 1개의 subpass 만 만들어서 0번 index로 설정함.
    // 디펜던시 그래프에서 사이클 발생을 예방하기 위해서 dstSubpass는 항상 srcSubpass 더 높아야한다. (VK_SUBPASS_EXTERNAL 은 예외)
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    // 수행되길 기다려야하는 작업과 이런 작업이 수행되야할 스테이지를 명세하는 부분.
    // 우리가 이미지에 접근하기 전에 스왑체인에서 읽기가 완료될때 까지 기다려야하는데, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 에서 가능.
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;

    // 기다려야하는 작업들은 Color Attachment 스테이지에 있고 Color Attachment를 읽고 쓰는 것과 관련되어 있음.
    // 이 설정으로 실제 이미지가 필요할때 혹은 허용될때까지 전환이 발생하는것을 방지함.
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (!ensure(vkCreateRenderPass(g_rhi_vk->GetDevice(), &renderPassInfo, nullptr, &RenderPass) == VK_SUCCESS))
        return false;

	// Create RenderPassInfo
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassInfo.renderPass = RenderPass;

    // 렌더될 영역이며, 최상의 성능을 위해 attachment의 크기와 동일해야함.
	RenderPassInfo.renderArea.offset = { RenderOffset.x, RenderOffset.y };
	RenderPassInfo.renderArea.extent = { (uint32)RenderExtent.x, (uint32)RenderExtent.y };

	ClearValues.resize(ColorAttachments.size() + 1);
	for (int32 i = 0; i < ColorAttachments.size(); ++i)
	{
		const auto& color = ColorAttachments[i].ClearColor;
		ClearValues[i].color = { color.x, color.y, color.z, color.w };
	}
	ClearValues[ColorAttachments.size()].depthStencil = { DepthAttachment.ClearDepth.x, (uint32)DepthAttachment.ClearDepth.y };
	RenderPassInfo.clearValueCount = static_cast<uint32_t>(ClearValues.size());
	RenderPassInfo.pClearValues = ClearValues.data();

    return true;
}

bool jFrameBuffer_Vulkan::CreateFrameBuffer(int32 index)
{
	check(RenderPass);
	check(index >= 0 && index < g_rhi_vk->swapChainImageViews.size());
	
	Index = index;

	VkImageView FinalImageView = g_rhi_vk->swapChainImageViews[Index];

    // DepthBuffer는 같은 것을 씀. 세마포어 때문에 한번에 1개의 subpass 만 실행되기 때문.
    std::vector<VkImageView> attachments;
	attachments.reserve(3);
	if (g_rhi_vk->msaaSamples > 1)
	{
		for(int32 k=0;k<RenderPass->ColorAttachments.size();++k)
		{
			attachments.push_back((VkImageView)RenderPass->ColorAttachments[k].ImageView);
		}
		attachments.push_back((VkImageView)RenderPass->DepthAttachment.ImageView);
		attachments.push_back(FinalImageView);
	}
	else
	{
        attachments.push_back(FinalImageView);
        attachments.push_back((VkImageView)RenderPass->DepthAttachment.ImageView);
	}

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = (VkRenderPass)RenderPass->GetRenderPass();

    // RenderPass와 같은 수와 같은 타입의 attachment 를 사용
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();

    framebufferInfo.width = RenderPass->RenderExtent.x;
    framebufferInfo.height = RenderPass->RenderExtent.y;
    framebufferInfo.layers = 1;			// 이미지 배열의 레이어수(3D framebuffer에 사용할 듯)

    if (!ensure(vkCreateFramebuffer(g_rhi_vk->device, &framebufferInfo, nullptr, &FrameBuffer) == VK_SUCCESS))
        return false;

    return true;
}

void jFrameBuffer_Vulkan::Release()
{
    // ImageViews and RenderPass 가 소멸되기전에 호출되어야 함
    vkDestroyFramebuffer(g_rhi_vk->device, FrameBuffer, nullptr);
}

bool jShaderBindings::CreateDescriptorSetLayout()
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    for (int32 i = 0; i < UniformBuffers.size(); ++i)
    {
        auto& UBBindings = UniformBuffers[i];

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = UBBindings.BindingPoint;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = GetVulkanShaderAccessFlags(UBBindings.AccessStageFlags);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    for (int32 i = 0; i < Textures.size(); ++i)
    {
        auto& TexBindings = Textures[i];

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = TexBindings.BindingPoint;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = GetVulkanShaderAccessFlags(TexBindings.AccessStageFlags);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (!ensure(vkCreateDescriptorSetLayout(g_rhi_vk->device, &layoutInfo, nullptr, &DescriptorSetLayout) == VK_SUCCESS))
        return false;

    return true;
}

jShadingBindingInstance jShaderBindings::CreateShaderBindingInstance() const
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &DescriptorSetLayout;

	jShadingBindingInstance Instance = {};
    if (!ensure(vkAllocateDescriptorSets(g_rhi_vk->device, &allocInfo, &Instance.DescriptorSet) == VK_SUCCESS))
        return Instance;

	return Instance;
}

std::vector<jShadingBindingInstance> jShaderBindings::CreateShaderBindingInstance(int32 count) const
{
    std::vector<VkDescriptorSetLayout> descSetLayout(count, DescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.descriptorSetCount = (uint32)descSetLayout.size();
    allocInfo.pSetLayouts = descSetLayout.data();

    std::vector<jShadingBindingInstance> Instances;

	std::vector<VkDescriptorSet> DescriptorSets;
	DescriptorSets.resize(descSetLayout.size());
    if (!ensure(vkAllocateDescriptorSets(g_rhi_vk->device, &allocInfo, DescriptorSets.data()) == VK_SUCCESS))
        return Instances;

    Instances.resize(DescriptorSets.size());
	for (int32 i = 0; i < DescriptorSets.size(); ++i)		// todo opt
	{
		Instances[i].DescriptorSet = DescriptorSets[i];
		Instances[i].ShaderBindings = this;
	}

    return Instances;
}

void jShaderBindings::CreatePool()
{
	DescriptorPool = g_rhi_vk->ShaderBindingsManager.CreatePool(*this);
}

VkPipelineLayout jShaderBindings::CreatePipelineLayout()
{
	check(DescriptorSetLayout);

	// 쉐이더에 전달된 Uniform 들을 명세하기 위한 오브젝트
	// 이 오브젝트는 프로그램 실행동안 계속해서 참조되므로 cleanup 에서 제거해줌
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &DescriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 0;		// Optional		// 쉐이더에 값을 constant 값을 전달 할 수 있음. 이후에 배움
	pipelineLayoutInfo.pPushConstantRanges = nullptr;	// Optional
	if (!ensure(vkCreatePipelineLayout(g_rhi_vk->device, &pipelineLayoutInfo, nullptr, &PipelineLayout) == VK_SUCCESS))
		return nullptr;

	return PipelineLayout;
}

void jShadingBindingInstance::UpdateShaderBindings()
{
	check(ShaderBindings->UniformBuffers.size() == UniformBuffers.size());
	check(ShaderBindings->Textures.size() == Textures.size());

	std::vector<VkDescriptorBufferInfo> descriptorBuffers;
	descriptorBuffers.resize(ShaderBindings->UniformBuffers.size());
	int32 bufferOffset = 0;
	for (int32 i = 0; i < ShaderBindings->UniformBuffers.size(); ++i)
	{
		descriptorBuffers[i].buffer = (VkBuffer)UniformBuffers[i]->GetBuffer();
		descriptorBuffers[i].offset = bufferOffset;
		descriptorBuffers[i].range = UniformBuffers[i]->GetBufferSize();		// 전체 사이즈라면 VK_WHOLE_SIZE 이거 가능

		bufferOffset += UniformBuffers[i]->GetBufferSize();
	}

	std::vector<VkDescriptorImageInfo> descriptorImages;
	descriptorImages.resize(ShaderBindings->Textures.size());
	for (int32 i = 0; i < ShaderBindings->UniformBuffers.size(); ++i)
	{
		descriptorImages[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		descriptorImages[i].imageView = ((jTexture_Vulkan*)Textures[i])->ImageView;
		descriptorImages[i].sampler = jTexture_Vulkan::CreateDefaultSamplerState();		// todo 수정 필요, 텍스쳐를 어떻게 바인드 해야할지 고민 필요
	}

	std::vector<VkWriteDescriptorSet> descriptorWrites;
	descriptorWrites.resize(descriptorBuffers.size() + descriptorImages.size());

	int32 writeDescIndex = 0;
	if (descriptorBuffers.size() > 0)
	{
		descriptorWrites[writeDescIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[writeDescIndex].dstSet = DescriptorSet;
		descriptorWrites[writeDescIndex].dstBinding = writeDescIndex;
		descriptorWrites[writeDescIndex].dstArrayElement = 0;
		descriptorWrites[writeDescIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[writeDescIndex].descriptorCount = (uint32)descriptorBuffers.size();
		descriptorWrites[writeDescIndex].pBufferInfo = &descriptorBuffers[0];		// 현재는 Buffer 기반 Desriptor 이므로 이것을 사용
		descriptorWrites[writeDescIndex].pImageInfo = nullptr;						// Optional	(Image Data 기반에 사용)
		descriptorWrites[writeDescIndex].pTexelBufferView = nullptr;				// Optional (Buffer View 기반에 사용)
		++writeDescIndex;
	}
	if (descriptorImages.size() > 0)
	{
		descriptorWrites[writeDescIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[writeDescIndex].dstSet = DescriptorSet;
		descriptorWrites[writeDescIndex].dstBinding = writeDescIndex;
		descriptorWrites[writeDescIndex].dstArrayElement = 0;
		descriptorWrites[writeDescIndex].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[writeDescIndex].descriptorCount = (uint32)descriptorImages.size();
		descriptorWrites[writeDescIndex].pImageInfo = &descriptorImages[0];
		++writeDescIndex;
	}

	vkUpdateDescriptorSets(g_rhi_vk->device, static_cast<uint32>(descriptorWrites.size())
		, descriptorWrites.data(), 0, nullptr);
}

void jShadingBindingInstance::Bind(const jCommandBuffer_Vulkan& commandBuffer) const
{
	check(ShaderBindings);
	check(ShaderBindings->PipelineLayout);
	vkCmdBindDescriptorSets(commandBuffer.Get(), VK_PIPELINE_BIND_POINT_GRAPHICS, ShaderBindings->PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
}

bool jCommandBufferManager_Vulkan::CratePool(uint32 QueueIndex)
{
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = QueueIndex;

	// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 커맨드 버퍼가 새로운 커맨드를 자주 다시 기록한다고 힌트를 줌.
	//											(메모리 할당 동작을 변경할 것임)
	// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 커맨드 버퍼들이 개별적으로 다시 기록될 수 있다.
	//													이 플래그가 없으면 모든 커맨드 버퍼들이 동시에 리셋되야 함.
	// 우리는 프로그램 시작시에 커맨드버퍼를 한번 녹화하고 계속해서 반복사용 할 것이므로 flags를 설정하지 않음.
	poolInfo.flags = 0;		// Optional

	if (!ensure(vkCreateCommandPool(g_rhi_vk->device, &poolInfo, nullptr, &CommandPool) == VK_SUCCESS))
		return false;

	return true;
}

jCommandBuffer_Vulkan jCommandBufferManager_Vulkan::GetOrCreateCommandBuffer()
{
	if (PendingCommandBuffers.size() > 0)
	{
		auto iter_find = PendingCommandBuffers.begin();
		jCommandBuffer_Vulkan commandBuffer = *iter_find;
		PendingCommandBuffers.erase(iter_find);

		UsingCommandBuffers.push_back(commandBuffer);
		return commandBuffer;
	}

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = GetPool();

	// VK_COMMAND_BUFFER_LEVEL_PRIMARY : 실행을 위해 Queue를 제출할 수 있으면 다른 커맨드버퍼로 부터 호출될 수 없다.
	// VK_COMMAND_BUFFER_LEVEL_SECONDARY : 직접 제출할 수 없으며, Primary command buffer 로 부터 호출될 수 있다.
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	jCommandBuffer_Vulkan commandBuffer;
	if (!ensure(vkAllocateCommandBuffers(g_rhi_vk->device, &allocInfo, &commandBuffer.GetRef()) == VK_SUCCESS))
		return commandBuffer;

	UsingCommandBuffers.push_back(commandBuffer);

	return commandBuffer;
}

void jCommandBufferManager_Vulkan::ReturnCommandBuffer(jCommandBuffer_Vulkan commandBuffer)
{
	for (int32 i = 0; i < UsingCommandBuffers.size(); ++i)
	{
		if (UsingCommandBuffers[i].Get() == commandBuffer.Get())
		{
			UsingCommandBuffers.erase(UsingCommandBuffers.begin() + i);
			break;
		}
	}
	PendingCommandBuffers.push_back(commandBuffer);
}

VkDescriptorPool jShaderBindingsManager_Vulkan::CreatePool(const jShaderBindings& bindings, uint32 maxAllocations) const
{
	auto DescriptorPoolSizes = bindings.GetDescriptorPoolSizeArray(maxAllocations);

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32>(DescriptorPoolSizes.size());
	poolInfo.pPoolSizes = DescriptorPoolSizes.data();
	poolInfo.maxSets = maxAllocations;
	poolInfo.flags = 0;		// Descriptor Set을 만들고나서 더 이상 손대지 않을거라 그냥 기본값 0으로 설정

	VkDescriptorPool pool = nullptr;
	if (!ensure(vkCreateDescriptorPool(g_rhi_vk->device, &poolInfo, nullptr, &pool) == VK_SUCCESS))
		return nullptr;

	return pool;
}

void jShaderBindingsManager_Vulkan::Release(VkDescriptorPool pool) const
{
	if (pool)
		vkDestroyDescriptorPool(g_rhi_vk->device, pool, nullptr);
}

void jVertexBuffer_Vulkan::Bind(const jCommandBuffer_Vulkan& commandBuffer) const
{
	vkCmdBindVertexBuffers(commandBuffer.Get(), 0, (uint32)BindInfos.Buffers.size(), &BindInfos.Buffers[0], &BindInfos.Offsets[0]);
}

#endif // USE_VULKAN

