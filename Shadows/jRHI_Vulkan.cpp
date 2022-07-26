#include "pch.h"

#if USE_VULKAN
#include "jRHI_Vulkan.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"	// 이거 안쓸예정
#include "jImageFileLoader.h"
#include "jFile.h"
#include "jFrameBufferPool.h"
#include "jRenderTargetPool.h"

#include "stb_image.h"
#include "jPrimitiveUtil.h"
#include "jRenderObject.h"
#include "jCamera.h"

const std::string MODEL_PATH = "chalet.obj";
const std::string TEXTURE_PATH = "chalet.jpg";
uint32_t textureMipLevels;

jRHI_Vulkan* g_rhi_vk = nullptr;
std::unordered_map<size_t, jPipelineStateInfo> jPipelineStateInfo::PipelineStatePool;
std::unordered_map<size_t, VkPipelineLayout> jRHI_Vulkan::PipelineLayoutPool;
std::unordered_map<size_t, jShaderBindings*> jRHI_Vulkan::ShaderBindingPool;
TResourcePool<jSamplerStateInfo_Vulkan> jRHI_Vulkan::SamplerStatePool;
TResourcePool<jRasterizationStateInfo_Vulkan> jRHI_Vulkan::RasterizationStatePool;
TResourcePool<jMultisampleStateInfo_Vulkan> jRHI_Vulkan::MultisampleStatePool;
TResourcePool<jStencilOpStateInfo_Vulkan> jRHI_Vulkan::StencilOpStatePool;
TResourcePool<jDepthStencilStateInfo_Vulkan> jRHI_Vulkan::DepthStencilStatePool;
TResourcePool<jBlendingStateInfo_Vulakn> jRHI_Vulkan::BlendingStatePool;

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


FORCEINLINE VkFilter GetVulkanTextureFilterType(ETextureFilter type)
{
	using T = VkFilter;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST, VK_FILTER_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR, VK_FILTER_LINEAR),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST_MIPMAP_NEAREST, VK_FILTER_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR_MIPMAP_NEAREST, VK_FILTER_LINEAR),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST_MIPMAP_LINEAR, VK_FILTER_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR_MIPMAP_LINEAR, VK_FILTER_LINEAR)
	);
}

FORCEINLINE VkSamplerAddressMode GetVulkanTextureAddressMode(ETextureAddressMode type)
{
	using T = VkSamplerAddressMode;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER),
		CONVERSION_TYPE_ELEMENT(ETextureAddressMode::MIRROR_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE)
	);
}

FORCEINLINE VkSamplerMipmapMode GetVulkanTextureMipmapMode(ETextureFilter type)
{
	using T = VkSamplerMipmapMode;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST_MIPMAP_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR_MIPMAP_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::NEAREST_MIPMAP_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR),
		CONVERSION_TYPE_ELEMENT(ETextureFilter::LINEAR_MIPMAP_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR)
	);
}

FORCEINLINE auto GetVulkanTextureFormat(ETextureFormat type)
{
	using T = VkFormat;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB8, VK_FORMAT_R8G8B8_UNORM),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, VK_FORMAT_R32G32B32_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, VK_FORMAT_R16G16B16_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, VK_FORMAT_B10G11R11_UFLOAT_PACK32),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, VK_FORMAT_R8G8B8A8_UNORM),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, VK_FORMAT_R16G16B16A16_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, VK_FORMAT_R32G32B32A32_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, VK_FORMAT_R8G8B8A8_SINT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, VK_FORMAT_R8G8B8A8_UINT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, VK_FORMAT_B8G8R8A8_UNORM),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::R8, VK_FORMAT_R8_UNORM),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R16F, VK_FORMAT_R16_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, VK_FORMAT_R32_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, VK_FORMAT_R32_UINT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG8, VK_FORMAT_R8G8_UNORM),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG16F, VK_FORMAT_R16G16_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, VK_FORMAT_R32G32_SFLOAT),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16, VK_FORMAT_D16_UNORM),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16_S8, VK_FORMAT_D16_UNORM_S8_UINT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24, VK_FORMAT_X8_D24_UNORM_PACK32),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, VK_FORMAT_D24_UNORM_S8_UINT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, VK_FORMAT_D32_SFLOAT),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, VK_FORMAT_D32_SFLOAT_S8_UINT)
	);
}

FORCEINLINE auto GetVulkanTextureComponentCount(ETextureFormat type)
{
	using T = int8;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB8, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB32F, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGB16F, 3),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R11G11B10F, 3),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA16F, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA32F, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8I, 4),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RGBA8UI, 4),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::BGRA8, 4),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::R8, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R16F, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32F, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::R32UI, 1),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG8, 2),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG16F, 2),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::RG32F, 2),

		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D16_S8, 2),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D24_S8, 2),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32, 1),
		CONVERSION_TYPE_ELEMENT(ETextureFormat::D32_S8, 2)
	);
}

FORCEINLINE void GetVulkanAttachmentLoadStoreOp(VkAttachmentLoadOp& OutLoadOp, VkAttachmentStoreOp& OutStoreOp, EAttachmentLoadStoreOp InType)
{
	switch (InType)
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
	switch (type)
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

FORCEINLINE VkPrimitiveTopology GetVulkanPrimitiveTopology(EPrimitiveType type)
{
	using T = VkPrimitiveTopology;
	GENERATE_STATIC_CONVERSION_ARRAY(
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

FORCEINLINE VkPolygonMode GetVulkanPolygonMode(EPolygonMode type)
{
	using T = VkPolygonMode;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EPolygonMode::POINT, VK_POLYGON_MODE_POINT),
		CONVERSION_TYPE_ELEMENT(EPolygonMode::LINE, VK_POLYGON_MODE_LINE),
		CONVERSION_TYPE_ELEMENT(EPolygonMode::FILL, VK_POLYGON_MODE_FILL)
	);
}

FORCEINLINE VkCullModeFlagBits GetVulkanCullMode(ECullMode type)
{
	using T = VkCullModeFlagBits;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ECullMode::NONE, VK_CULL_MODE_NONE),
		CONVERSION_TYPE_ELEMENT(ECullMode::BACK, VK_CULL_MODE_BACK_BIT),
		CONVERSION_TYPE_ELEMENT(ECullMode::FRONT, VK_CULL_MODE_FRONT_BIT),
		CONVERSION_TYPE_ELEMENT(ECullMode::FRONT_AND_BACK, VK_CULL_MODE_FRONT_AND_BACK)
	);
}

FORCEINLINE VkFrontFace GetVulkanFrontFace(EFrontFace type)
{
	using T = VkFrontFace;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EFrontFace::CW, VK_FRONT_FACE_CLOCKWISE),
		CONVERSION_TYPE_ELEMENT(EFrontFace::CCW, VK_FRONT_FACE_COUNTER_CLOCKWISE)
	);
}

struct jFrameBuffer_Vulkan : public jFrameBuffer
{
	bool IsInitialized = false;
	std::vector<std::shared_ptr<jTexture> > AllTextures;

	virtual jTexture* GetTexture(int32 index = 0) const { return AllTextures[index].get(); }
};

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
				// msaaSamples = GetMaxUsableSampleCount();
				msaaSamples = (VkSampleCountFlagBits)1;
				break;
			}
		}

		if (!ensure(physicalDevice != VK_NULL_HANDLE))
			return false;

		vkGetPhysicalDeviceProperties(physicalDevice, &DeviceProperties);
	}

	jSpirvHelper::Init(DeviceProperties);

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
	swapChainCommandBufferFences.resize(swapChainImageViews.size(), 0);

	CommandBufferManager.CreatePool(GraphicsQueue.QueueIndex);

    ensure(CreateColorResources());
    ensure(CreateDepthResources());
    ensure(CreateSyncObjects());

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    check(VK_SUCCESS == vkCreatePipelineCache(g_rhi_vk->device, &pipelineCacheCreateInfo, nullptr, &PipelineCache));

	// 동적인 부분들 패스에 따라 달라짐
	static bool s_Initializes = false;
	if (!s_Initializes)
	{
		s_Initializes = true;
		//jRenderTargetInfo(ETextureType textureType, ETextureFormat internalFormat, ETextureFormat format, EFormatType formatType, EDepthBufferType depthBufferType
		//	, int32 width, int32 height, int32 textureCount = 1, ETextureFilter magnification = ETextureFilter::LINEAR
		//	, ETextureFilter minification = ETextureFilter::LINEAR, bool isGenerateMipmap = false, bool isGenerateMipmapDepth = false, int32 sampleCount = 1)

		for (int32 i = 0; i < swapChainImageViews.size(); ++i)
		{
			std::shared_ptr<jRenderTarget> DepthPtr = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
				{ ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, /*ETextureFilter::LINEAR, ETextureFilter::LINEAR, */false, msaaSamples }));

			std::shared_ptr<jRenderTarget> ColorPtr;
			std::shared_ptr<jRenderTarget> ResolveColorPtr;

			auto SwapChainRTPtr = jRenderTarget::CreateFromTexture<jTexture_Vulkan>(ETextureType::TEXTURE_2D, ETextureFormat::BGRA8
				, swapChainExtent.width, swapChainExtent.height, false, 1, 1, swapChainImages[i], swapChainImageViews[i]);

			if (msaaSamples > 1)
			{
				ColorPtr = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget(
					{ ETextureType::TEXTURE_2D, ETextureFormat::BGRA8, SCR_WIDTH, SCR_HEIGHT, 1, /*ETextureFilter::LINEAR, ETextureFilter::LINEAR, */false, msaaSamples }));

				ResolveColorPtr = SwapChainRTPtr;
			}
			else
			{
				ColorPtr = SwapChainRTPtr;
			}

			const Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
			const Vector2 ClearDepth = Vector2(1.0f, 0.0f);

			jAttachment* color = new jAttachment(ColorPtr, EAttachmentLoadStoreOp::CLEAR_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth);
			jAttachment* depth = new jAttachment(DepthPtr, EAttachmentLoadStoreOp::CLEAR_DONTCARE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth);
			jAttachment* resolve = nullptr;

			if (msaaSamples > 1)
			{
				resolve = new jAttachment(ResolveColorPtr, EAttachmentLoadStoreOp::DONTCARE_STORE, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, ClearDepth);
			}

			auto textureDepth = (jTexture_Vulkan*)depth->RenderTargetPtr->GetTexture();
			auto depthFormat = GetVulkanTextureFormat(textureDepth->Format);
			TransitionImageLayout(textureDepth->Image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);

			jRenderPass_Vulkan* renderPass = new jRenderPass_Vulkan(color, depth, resolve, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });
			renderPass->CreateRenderPass();
			RenderPasses.push_back(renderPass);
		}
	}

	//FrameBufferTest.resize(swapChainImageViews.size());
	//for (int32 i = 0; i < FrameBufferTest.size(); ++i)
	//{
	//	FrameBufferTest[i].SetRenderPass(&RenderPassTest);
	//	ensure(FrameBufferTest[i].CreateFrameBuffer(i));
	//}

	for (int32 i = 0; i < swapChainImageViews.size(); ++i)
	{
		IUniformBufferBlock* UniformBuffer = g_rhi->CreateUniformBufferBlock("jUniformBufferObject", sizeof(jUniformBufferObject));
		UniformBuffer->UpdateBufferData(nullptr, sizeof(jUniformBufferObject));
		UniformBuffers.push_back(UniformBuffer);
	}
	std::weak_ptr<jTexture> Texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName("chalet.jpg"), true, true);
	jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();

	ShaderBindings.UniformBuffers.push_back(jShaderBinding(0, EShaderAccessStageFlag::VERTEX));
	ShaderBindings.Textures.push_back(jShaderBinding(1, EShaderAccessStageFlag::FRAGMENT));
	ShaderBindings.CreateDescriptorSetLayout();
	ShaderBindings.CreatePool();
	ShaderBindingInstances = ShaderBindings.CreateShaderBindingInstance((int32)swapChainImageViews.size());
	for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
	{
		ShaderBindingInstances[i]->UniformBuffers.push_back(UniformBuffers[i]);
		ShaderBindingInstances[i]->Textures.push_back({ .Texture=Texture.lock().get(), .SamplerState=SamplerState });
		ShaderBindingInstances[i]->UpdateShaderBindings();
	}

	ensure(LoadModel());

    // ensure(RecordCommandBuffers());

	QueryPool.Create();

	return true;
}

void jRHI_Vulkan::ReleaseRHI()
{
	jSpirvHelper::Finalize();
}

//std::vector<jAttachment> ColorAttachments;
//jAttachment DepthAttachment;
//jAttachment ColorAttachmentResolve;

void jSamplerStateInfo_Vulkan::Initialize()
{
	SamplerStateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	SamplerStateInfo.magFilter = GetVulkanTextureFilterType(Magnification);
	SamplerStateInfo.minFilter = GetVulkanTextureFilterType(Minification);

	// UV가 [0~1] 범위를 벗어는 경우 처리
	// VK_SAMPLER_ADDRESS_MODE_REPEAT : 반복해서 출력, UV % 1
	// VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : 반복하지만 거울에 비치듯 반대로 출력
	// VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : 범위 밖은 가장자리의 색으로 모두 출력
	// VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : 범위 밖은 반대편 가장자리의 색으로 모두 출력
	// VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : 단색으로 설정함. (samplerInfo.borderColor)
	SamplerStateInfo.addressModeU = GetVulkanTextureAddressMode(AddressU);
	SamplerStateInfo.addressModeV = GetVulkanTextureAddressMode(AddressV);
	SamplerStateInfo.addressModeW = GetVulkanTextureAddressMode(AddressW);
	SamplerStateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	SamplerStateInfo.anisotropyEnable = (MaxAnisotropy > 1);
	SamplerStateInfo.maxAnisotropy = MaxAnisotropy;

	// 이게 true 이면 UV 좌표가 [0, texWidth], [0, texHeight] 가 됨. false 이면 [0, 1] 범위
	SamplerStateInfo.unnormalizedCoordinates = VK_FALSE;

	// compareEnable이 ture 이면, 텍셀을 특정 값과 비교한 뒤 그 결과를 필터링 연산에 사용한다.
	// Percentage-closer filtering(PCF) 에 주로 사용됨.
	SamplerStateInfo.compareEnable = VK_FALSE;
	SamplerStateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	uint32 textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(SCR_WIDTH, SCR_HEIGHT)))) + 1;		// 이것도 수정 필요. SamplerState 는 텍스쳐에 바인딩 해야 할듯 

	SamplerStateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	SamplerStateInfo.mipLodBias = 0.0f;		// Optional
	SamplerStateInfo.minLod = MinLOD;		// Optional
	SamplerStateInfo.maxLod = MaxLOD;

	ensure(vkCreateSampler(g_rhi_vk->device, &SamplerStateInfo, nullptr, &SamplerState) == VK_SUCCESS);
}

void jRasterizationStateInfo_Vulkan::Initialize()
{
	RasterizationStateInfo = {};
	RasterizationStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	RasterizationStateInfo.depthClampEnable = DepthClampEnable;						// 이 값이 VK_TRUE 면 Near나 Far을 벗어나는 영역을 [0.0 ~ 1.0]으로 Clamp 시켜줌.(쉐도우맵에서 유용)
	RasterizationStateInfo.rasterizerDiscardEnable = RasterizerDiscardEnable;		// 이 값이 VK_TRUE 면, 레스터라이저 스테이지를 통과할 수 없음. 즉 Framebuffer 로 결과가 넘어가지 않음.
	RasterizationStateInfo.polygonMode = GetVulkanPolygonMode(PolygonMode);			// FILL, LINE, POINT 세가지가 있음
	RasterizationStateInfo.lineWidth = LineWidth;
	RasterizationStateInfo.cullMode = GetVulkanCullMode(CullMode);
	RasterizationStateInfo.frontFace = GetVulkanFrontFace(FrontFace);
	RasterizationStateInfo.depthBiasEnable = DepthBiasEnable;						// 쉐도우맵 용
	RasterizationStateInfo.depthBiasConstantFactor = DepthBiasConstantFactor;		// Optional
	RasterizationStateInfo.depthBiasClamp = DepthBiasClamp;							// Optional
	RasterizationStateInfo.depthBiasSlopeFactor = DepthBiasSlopeFactor;				// Optional

	// VkPipelineRasterizationStateCreateFlags flags;
}

void jMultisampleStateInfo_Vulkan::Initialize()
{
	MultisampleStateInfo = {};
	MultisampleStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	MultisampleStateInfo.rasterizationSamples = (VkSampleCountFlagBits)SampleCount;
	MultisampleStateInfo.sampleShadingEnable = SampleShadingEnable;			// Sample shading 켬	 (텍스쳐 내부에 있는 aliasing 도 완화 해줌)
	MultisampleStateInfo.minSampleShading = MinSampleShading;
	MultisampleStateInfo.alphaToCoverageEnable = AlphaToCoverageEnable;		// Optional
	MultisampleStateInfo.alphaToOneEnable = AlphaToOneEnable;				// Optional

	// VkPipelineMultisampleStateCreateFlags flags;
	// const VkSampleMask* pSampleMask;
}

FORCEINLINE VkStencilOp GetVulkanStencilOp(EStencilOp type)
{
	using T = VkStencilOp;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EStencilOp::KEEP, VK_STENCIL_OP_KEEP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::ZERO, VK_STENCIL_OP_ZERO),
		CONVERSION_TYPE_ELEMENT(EStencilOp::REPLACE, VK_STENCIL_OP_REPLACE),
		CONVERSION_TYPE_ELEMENT(EStencilOp::INCR, VK_STENCIL_OP_INCREMENT_AND_CLAMP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::INCR_WRAP, VK_STENCIL_OP_INCREMENT_AND_WRAP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::DECR, VK_STENCIL_OP_DECREMENT_AND_CLAMP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::DECR_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP),
		CONVERSION_TYPE_ELEMENT(EStencilOp::INVERT, VK_STENCIL_OP_INVERT)
	);
}

FORCEINLINE VkCompareOp GetVulkanCompareOp(ECompareOp type)
{
	using T = VkCompareOp;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(ECompareOp::NEVER, VK_COMPARE_OP_NEVER),
		CONVERSION_TYPE_ELEMENT(ECompareOp::LESS, VK_COMPARE_OP_LESS),
		CONVERSION_TYPE_ELEMENT(ECompareOp::EQUAL, VK_COMPARE_OP_EQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::LEQUAL, VK_COMPARE_OP_LESS_OR_EQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::GREATER, VK_COMPARE_OP_GREATER),
		CONVERSION_TYPE_ELEMENT(ECompareOp::NOTEQUAL, VK_COMPARE_OP_NOT_EQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::GEQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL),
		CONVERSION_TYPE_ELEMENT(ECompareOp::ALWAYS, VK_COMPARE_OP_ALWAYS)
	);
}

void jStencilOpStateInfo_Vulkan::Initialize()
{
	StencilOpStateInfo = {};
	StencilOpStateInfo.failOp = GetVulkanStencilOp(FailOp);
	StencilOpStateInfo.passOp = GetVulkanStencilOp(PassOp);
	StencilOpStateInfo.depthFailOp = GetVulkanStencilOp(DepthFailOp);
	StencilOpStateInfo.compareOp = GetVulkanCompareOp(CompareOp);
	StencilOpStateInfo.compareMask = CompareMask;
	StencilOpStateInfo.writeMask = WriteMask;
	StencilOpStateInfo.reference = Reference;
}

void jDepthStencilStateInfo_Vulkan::Initialize()
{
	DepthStencilStateInfo = {};
	DepthStencilStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	DepthStencilStateInfo.depthTestEnable = DepthTestEnable;
	DepthStencilStateInfo.depthWriteEnable = DepthWriteEnable;
	DepthStencilStateInfo.depthCompareOp = GetVulkanCompareOp(DepthCompareOp);
	DepthStencilStateInfo.depthBoundsTestEnable = DepthBoundsTestEnable;
	DepthStencilStateInfo.minDepthBounds = MinDepthBounds;		// Optional
	DepthStencilStateInfo.maxDepthBounds = MaxDepthBounds;		// Optional
	DepthStencilStateInfo.stencilTestEnable = StencilTestEnable;
	if (Front)
	{
		DepthStencilStateInfo.front = ((jStencilOpStateInfo_Vulkan*)Front)->StencilOpStateInfo;
	}
	if (Back)
	{
		DepthStencilStateInfo.back = ((jStencilOpStateInfo_Vulkan*)Back)->StencilOpStateInfo;
	}

	// VkPipelineDepthStencilStateCreateFlags    flags;
}

FORCEINLINE VkBlendFactor GetVulkanBlendFactor(EBlendFactor type)
{
	using T = VkBlendFactor;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ZERO, VK_BLEND_FACTOR_ZERO),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE, VK_BLEND_FACTOR_ONE),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_COLOR, VK_BLEND_FACTOR_SRC_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::DST_COLOR, VK_BLEND_FACTOR_DST_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::CONSTANT_COLOR, VK_BLEND_FACTOR_CONSTANT_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::CONSTANT_ALPHA, VK_BLEND_FACTOR_CONSTANT_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::ONE_MINUS_CONSTANT_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA),
		CONVERSION_TYPE_ELEMENT(EBlendFactor::SRC_ALPHA_SATURATE, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE)
	);
}

FORCEINLINE VkBlendOp GetVulkanBlendOp(EBlendOp type)
{
	using T = VkBlendOp;
	GENERATE_STATIC_CONVERSION_ARRAY(
		CONVERSION_TYPE_ELEMENT(EBlendOp::ADD, VK_BLEND_OP_ADD),
		CONVERSION_TYPE_ELEMENT(EBlendOp::SUBTRACT, VK_BLEND_OP_SUBTRACT),
		CONVERSION_TYPE_ELEMENT(EBlendOp::REVERSE_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT),
		CONVERSION_TYPE_ELEMENT(EBlendOp::MIN_VALUE, VK_BLEND_OP_MIN),
		CONVERSION_TYPE_ELEMENT(EBlendOp::MAX_VALUE, VK_BLEND_OP_MAX)
	);
}

FORCEINLINE VkColorComponentFlags GetVulkanBlendOp(EColorMask type)
{
	VkColorComponentFlags result = 0;

	if (EColorMask::NONE == type)
		return result;

	if (EColorMask::ALL == type)
	{
		result = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	}
	else
	{
		if ((int32)EColorMask::R & (int32)type) result |= VK_COLOR_COMPONENT_R_BIT;
		if ((int32)EColorMask::G & (int32)type) result |= VK_COLOR_COMPONENT_G_BIT;
		if ((int32)EColorMask::B & (int32)type) result |= VK_COLOR_COMPONENT_B_BIT;
		if ((int32)EColorMask::A & (int32)type) result |= VK_COLOR_COMPONENT_A_BIT;
	}
	return result;
}

void jBlendingStateInfo_Vulakn::Initialize()
{
	ColorBlendAttachmentInfo = {};
	ColorBlendAttachmentInfo.blendEnable = BlendEnable;
	ColorBlendAttachmentInfo.srcColorBlendFactor = GetVulkanBlendFactor(Src);
	ColorBlendAttachmentInfo.dstColorBlendFactor = GetVulkanBlendFactor(Dest);
	ColorBlendAttachmentInfo.colorBlendOp = GetVulkanBlendOp(BlendOp);
	ColorBlendAttachmentInfo.srcAlphaBlendFactor = GetVulkanBlendFactor(SrcAlpha);
	ColorBlendAttachmentInfo.dstAlphaBlendFactor = GetVulkanBlendFactor(DestAlpha);
	ColorBlendAttachmentInfo.alphaBlendOp = GetVulkanBlendOp(AlphaBlendOp);
	ColorBlendAttachmentInfo.colorWriteMask = GetVulkanBlendOp(ColorWriteMask);
}

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

bool jRHI_Vulkan::LoadModel()
{
	TestCube = jPrimitiveUtil::CreateCube(Vector::ZeroVector, Vector::OneVector, Vector::OneVector, Vector4::ColorRed);
	VertexBuffer = TestCube->RenderObject->VertexBuffer;
	IndexBuffer = TestCube->RenderObject->IndexBuffer;
	return true;

	//tinyobj::attrib_t attrib;
	//std::vector<tinyobj::shape_t> shapes;
	//std::vector<tinyobj::material_t> materials;
	//std::string warn, err;

	//if (!ensure(tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())))
	//	return false;

	//struct jVertexHashFunc
	//{
	//	std::size_t operator()(const Vector& pos, const Vector2& texCoord, const Vector& color) const
	//	{
	//		size_t result = 0;
	//		result = std::hash<float>{}(pos.x);
	//		result ^= std::hash<float>{}(pos.y);
	//		result ^= std::hash<float>{}(pos.z);
	//		result ^= std::hash<float>{}(color.x);
	//		result ^= std::hash<float>{}(color.y);
	//		result ^= std::hash<float>{}(color.z);
	//		result ^= std::hash<float>{}(texCoord.x);
	//		result ^= std::hash<float>{}(texCoord.y);
	//		return result;
	//	}
	//};

	//std::unordered_map<size_t, uint32> uniqueVertices = {};

	//std::vector<float> vertices_temp;
	//std::vector<float> texCoords_temp;
	//std::vector<float> colors_temp;
	//std::vector<uint32> indices;

	//for (const auto& shape : shapes)
	//{
	//	for (const auto& index : shape.mesh.indices)
	//	{
	//		Vector pos = {
	//			attrib.vertices[3 * index.vertex_index + 0],
	//			attrib.vertices[3 * index.vertex_index + 1],
	//			attrib.vertices[3 * index.vertex_index + 2]
	//		};

	//		Vector2 texCoord = {
	//			attrib.texcoords[2 * index.texcoord_index + 0],
	//			1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
	//		};

	//		Vector color = { 1.0f, 1.0f, 1.0f };

	//		const size_t hash = jVertexHashFunc()(pos, texCoord, color);
	//		if (uniqueVertices.count(hash) == 0)
	//		{
	//			uniqueVertices[hash] = static_cast<uint32>(vertices_temp.size() / 3);

	//			vertices_temp.push_back(pos.x);
	//			vertices_temp.push_back(pos.y);
	//			vertices_temp.push_back(pos.z);

	//			texCoords_temp.push_back(texCoord.x);
	//			texCoords_temp.push_back(texCoord.y);

	//			colors_temp.push_back(color.x);
	//			colors_temp.push_back(color.y);
	//			colors_temp.push_back(color.z);
	//		}

	//		indices.push_back(uniqueVertices[hash]);
	//	}
	//}

	//auto vertexStreamData = std::shared_ptr<jVertexStreamData>(new jVertexStreamData());
	//vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	//vertexStreamData->ElementCount = (uint32)(vertices_temp.size() / 3);

	//{
	//	auto streamParam = new jStreamParam<float>();
	//	streamParam->BufferType = EBufferType::STATIC;
	//	streamParam->ElementTypeSize = sizeof(float);
	//	streamParam->ElementType = EBufferElementType::FLOAT;
	//	streamParam->Stride = sizeof(float) * 3;
	//	streamParam->Name = jName("Pos");
	//	streamParam->Data = std::move(vertices_temp);
	//	vertexStreamData->Params.push_back(streamParam);
	//}

	//{
	//	auto streamParam = new jStreamParam<float>();
	//	streamParam->BufferType = EBufferType::STATIC;
	//	streamParam->ElementType = EBufferElementType::FLOAT;
	//	streamParam->ElementTypeSize = sizeof(float);
	//	streamParam->Stride = sizeof(float) * 3;
	//	streamParam->Name = jName("Color");
	//	streamParam->Data = std::move(colors_temp);
	//	vertexStreamData->Params.push_back(streamParam);
	//}

	//{
	//	auto streamParam = new jStreamParam<float>();
	//	streamParam->BufferType = EBufferType::STATIC;
	//	streamParam->ElementType = EBufferElementType::FLOAT;
	//	streamParam->ElementTypeSize = sizeof(float);
	//	streamParam->Stride = sizeof(float) * 2;
	//	streamParam->Name = jName("TexCoord");
	//	streamParam->Data = std::move(texCoords_temp);
	//	vertexStreamData->Params.push_back(streamParam);
	//}
	//VertexBuffer = g_rhi_vk->CreateVertexBuffer(vertexStreamData);

	//auto indexStreamData = std::shared_ptr<jIndexStreamData>(new jIndexStreamData());
	//indexStreamData->ElementCount = static_cast<int32>(indices.size());
	//{
	//	auto streamParam = new jStreamParam<uint32>();
	//	streamParam->BufferType = EBufferType::STATIC;
	//	streamParam->ElementType = EBufferElementType::UINT32;
	//	streamParam->ElementTypeSize = sizeof(uint32);
	//	streamParam->Stride = sizeof(uint32) * 3;
	//	streamParam->Name = jName("Index");
	//	streamParam->Data = std::move(indices);
	//	indexStreamData->Param = streamParam;
	//}
	//IndexBuffer = g_rhi_vk->CreateIndexBuffer(indexStreamData);

	return true;
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

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	g_rhi_vk->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, stagingBuffer, stagingBufferMemory);

	void* data = nullptr;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, streamData->Param->GetBufferData(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	g_rhi_vk->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, indexBuffer->IndexBuffer, indexBuffer->IndexBufferMemory);
	g_rhi_vk->CopyBuffer(stagingBuffer, indexBuffer->IndexBuffer, bufferSize);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
	return indexBuffer;
}

//bool jRHI_Vulkan::RecordCommandBuffers()
//{
//
//}

bool jRHI_Vulkan::CreateSyncObjects()
{
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

#if MULTIPLE_FRAME
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	imageAvailableSemaphores.resize(swapChainImageViews.size());
	renderFinishedSemaphores.resize(swapChainImageViews.size());
	inFlightFences.resize(swapChainImageViews.size());
	imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);

	for (size_t i = 0; i < swapChainImageViews.size(); ++i)
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
	{
		auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false>::Create();
		jMultisampleStateInfo* MultisampleState = nullptr;
		switch (msaaSamples)
		{
		case VK_SAMPLE_COUNT_1_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_1, true, 0.2f, false, false>::Create();
			break;
		case VK_SAMPLE_COUNT_2_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_2, true, 0.2f, false, false>::Create();
			break;
		case VK_SAMPLE_COUNT_4_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_4, true, 0.2f, false, false>::Create();
			break;
		case VK_SAMPLE_COUNT_8_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_8, true, 0.2f, false, false>::Create();
			break;
		case VK_SAMPLE_COUNT_16_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_16, true, 0.2f, false, false>::Create();
			break;
		case VK_SAMPLE_COUNT_32_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_32, true, 0.2f, false, false>::Create();
			break;
		case VK_SAMPLE_COUNT_64_BIT:
			MultisampleState = TMultisampleStateInfo<EMSAASamples::COUNT_64, true, 0.2f, false, false>::Create();
			break;
		default:
			break;
		}
		auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
		auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

		CurrentPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, MultisampleState, DepthStencilState, BlendingState
			, jViewport(0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height), jScissor(0, 0, swapChainExtent.width, swapChainExtent.height));
	}

	jShaderInfo shaderInfo;
	shaderInfo.name = jName("default_test");
	shaderInfo.vs = jName("Shaders/Vulkan/shader_vs.glsl");
	shaderInfo.fs = jName("Shaders/Vulkan/shader_fs.glsl");
	static auto Shader = g_rhi->CreateShader(shaderInfo);

	TestCube->RenderObject->UpdateWorldMatrix();

	//ubo.Model = Matrix::MakeRotate(Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(245.0f)).GetTranspose();
	//ubo.View = jCameraUtil::CreateViewMatrix(Vector(2.0f, 2.0f, 2.0f), Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, 1.0f)).GetTranspose();
	//ubo.Proj = jCameraUtil::CreatePerspectiveMatrix(static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height)
	//	, DegreeToRadian(45.0f), 0.1f, 10.0f).GetTranspose();
	//ubo.Proj.m[1][1] *= -1;

	const Vector mainCameraPos = Vector(2.0f, 2.0f, 2.0f);
	const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
	static auto MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 0.1f, 10.0f, SCR_WIDTH, SCR_HEIGHT, true);
	MainCamera->UpdateCamera();

	jView View;
	View.Camera = MainCamera;

	TestCube->RenderObject->UpdateRenderObjectUniformBuffer(&View);
	TestCube->RenderObject->PrepareShaderBindingInstance();

	class jDrawCommand
	{
	public:
		jDrawCommand(jView* view, jRenderObject* renderObject, jRenderPass* renderPass, jShader* shader, jPipelineStateFixedInfo* pipelineStateFixed, std::vector<const jShaderBindingInstance*> shaderBindingInstances)
			: View(view), RenderObject(renderObject), RenderPass(renderPass), Shader(shader), PipelineStateFixed(pipelineStateFixed), ShaderBindingInstances(shaderBindingInstances)
		{}

		void PrepareToDraw()
		{
			// GetShaderBindings
			RenderObject->GetShaderBindingInstance(ShaderBindingInstances);

			// Bind ShaderBindings
			void* pipelineLayout = g_rhi->CreatePipelineLayout(ShaderBindingInstances);
			std::vector<const jShaderBindings*> shaderBindings;
			for (int32 i = 0; i < ShaderBindingInstances.size(); ++i)
			{
				ShaderBindingInstances[i]->Bind(pipelineLayout, i);
				shaderBindings.push_back(ShaderBindingInstances[i]->ShaderBindings);
			}

			// Create Pipeline
			CurrentPipelineStateInfo = jPipelineStateInfo(PipelineStateFixed, Shader, RenderObject->VertexBuffer, RenderPass, shaderBindings);
			CurrentPipelineStateInfo.CreateGraphicsPipelineState();

			// Bind Pipeline
			CurrentPipelineStateInfo.Bind();
		}
		void Draw()
		{
			// Draw
			RenderObject->Draw(nullptr, Shader, {});
		}

		std::vector<const jShaderBindingInstance*> ShaderBindingInstances;

		jView* View = nullptr;
		jRenderPass* RenderPass = nullptr;
		jShader* Shader = nullptr;
		jRenderObject* RenderObject = nullptr;
		jPipelineStateFixedInfo* PipelineStateFixed = nullptr;
		jPipelineStateInfo CurrentPipelineStateInfo;
	};


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
	if (acquireNextImageResult != VK_SUCCESS)
		return false;

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

	//////////////////////////////////////////////////////////////////////////
	// 여기서 부터 렌더 로직 시작

	//UpdateUniformBuffer(imageIndex);

	auto commandBuffer = CommandBufferManager.GetOrCreateCommandBuffer();
	if (ensure(commandBuffer.Begin()))
	{
		CurrentCommandBuffer = &commandBuffer;

		RenderPasses[imageIndex]->BeginRenderPass(&commandBuffer);

		jDrawCommand command(&View, TestCube->RenderObject, RenderPasses[imageIndex], Shader, &CurrentPipelineStateFixed, { });
		command.PrepareToDraw();
		command.Draw();

		// Finishing up
		RenderPasses[imageIndex]->EndRenderPass();

		ensure(commandBuffer.End());
	}

	// 여기 까지 렌더 로직 끝
	//////////////////////////////////////////////////////////////////////////

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
	submitInfo.pCommandBuffers = &commandBuffer.GetRef();

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
	currenFrame = (currenFrame + 1) % swapChainImageViews.size();
#else
	vkQueueWaitIdle(PresentQueue);
#endif // MULTIPLE_FRAME

	CommandBufferManager.ReturnCommandBuffer(commandBuffer);

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
	for (int32 i = 0; i < RenderPasses.size(); ++i)
	{
		RenderPasses[i]->Release();
		delete RenderPasses[i];
	}
	RenderPasses.clear();

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
		, DegreeToRadian(45.0f), 0.1f, 10.0f).GetTranspose();
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
			g_rhi_vk->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, stagingBuffer, stagingBufferMemory);

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
			g_rhi_vk->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
				, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, stream.VertexBuffer, stream.VertexBufferMemory);

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
    textureMipLevels = static_cast<uint32>(std::floor(std::log2(std::max<int>(width, height)))) + 1;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        , imageSize, stagingBuffer, stagingBufferMemory);

    void* dset = nullptr;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &dset);
    memcpy(dset, data, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

	VkFormat vkTextureFormat = GetVulkanTextureFormat(textureFormat);

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
    texture->Format = textureFormat;
    texture->Type = ETextureType::TEXTURE_2D;
    texture->Width = width;
    texture->Height = height;
	texture->MipLevel = createMipmap ? jTexture::GetMipLevels(width, height) : 1;
    
	texture->Image = TextureImage;
	texture->ImageView = textureImageView;
	texture->ImageMemory = TextureImageMemory;

	return texture;
}

jShader* jRHI_Vulkan::CreateShader(const jShaderInfo& shaderInfo) const
{
	auto shader = new jShader_Vulkan();
	CreateShader(shader, shaderInfo);
	return shader;
}

bool jRHI_Vulkan::CreateShader(jShader* OutShader, const jShaderInfo& shaderInfo) const
{
	auto CreateShaderModule = [](const std::vector<uint32>& code) -> VkShaderModule
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size() * sizeof(uint32);

		// pCode 가 uint32_t* 형이라서 4 byte aligned 된 메모리를 넘겨줘야 함.
		// 다행히 std::vector의 default allocator가 가 메모리 할당시 4 byte aligned 을 이미 하고있어서 그대로 씀.
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule = {};
		ensure(vkCreateShaderModule(g_rhi_vk->device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

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
		jSpirvHelper::GLSLtoSPV(SpirvCode, EShLanguage::EShLangCompute, csText.data());
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
			jSpirvHelper::GLSLtoSPV(SpirvCode, EShLanguage::EShLangVertex, vsText.data());
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
			jSpirvHelper::GLSLtoSPV(SpirvCode, EShLanguage::EShLangGeometry, gsText.data());
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
			jSpirvHelper::GLSLtoSPV(SpirvCode, EShLanguage::EShLangFragment, fsText.data());
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
		CreateImage(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
		imageView = CreateImageView(image, textureFormat, ImageAspectFlagBit, mipLevels);
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		CreateImage2DArray(info.Width, info.Height, info.LayerCount, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
		imageView = CreateImage2DArrayView(image, info.LayerCount, textureFormat, ImageAspectFlagBit, mipLevels);
		break;
	case ETextureType::TEXTURE_CUBE:
		CreateImageCube(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlagBit, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
		imageView = CreateImageCubeView(image, textureFormat, ImageAspectFlagBit, mipLevels);
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
	tex_vk->ImageView = imageView;
	tex_vk->ImageMemory = imageMemory;
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

	const VkImageUsageFlags ImageUsageFlag = (hasDepthAttachment ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_SAMPLED_BIT;
	const VkImageAspectFlags ImageAspectFlag = hasDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	// VK_IMAGE_TILING_LINEAR 설정시 크래시 나서 VK_IMAGE_TILING_OPTIMAL 로 함.
	//const VkImageTiling TilingMode = IsMobile ? VkImageTiling::VK_IMAGE_TILING_OPTIMAL : VkImageTiling::VK_IMAGE_TILING_LINEAR;
	const VkImageTiling TilingMode = VkImageTiling::VK_IMAGE_TILING_OPTIMAL;

	const int32 mipLevels = (info.SampleCount > VK_SAMPLE_COUNT_1_BIT || !info.IsGenerateMipmap) ? 1 : jTexture::GetMipLevels(info.Width, info.Height);		// MipLevel 은 SampleCount 1인 경우만 가능
	JASSERT(info.SampleCount >= 1);

	VkImage image = nullptr;
	VkImageView imageView = nullptr;
	VkDeviceMemory imageMemory = nullptr;

	switch (info.Type)
	{
	case ETextureType::TEXTURE_2D:
		CreateImage(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
		imageView = CreateImageView(image, textureFormat, ImageAspectFlag, mipLevels);
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		CreateImage2DArray(info.Width, info.Height, info.LayerCount, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
		imageView = CreateImage2DArrayView(image, info.LayerCount, textureFormat, ImageAspectFlag, mipLevels);
		break;
	case ETextureType::TEXTURE_CUBE:
		CreateImageCube(info.Width, info.Height, mipLevels, (VkSampleCountFlagBits)info.SampleCount
			, textureFormat, TilingMode, ImageUsageFlag, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
		imageView = CreateImageCubeView(image, textureFormat, ImageAspectFlag, mipLevels);
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
	tex_vk->ImageView = imageView;
	tex_vk->ImageMemory = imageMemory;
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

void jRHI_Vulkan::DrawArrays(EPrimitiveType type, int32 vertStartIndex, int32 vertCount) const
{
	check(CurrentCommandBuffer);
	vkCmdDraw((VkCommandBuffer)CurrentCommandBuffer->GetHandle(), vertCount, 1, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawArraysInstanced(EPrimitiveType type, int32 vertStartIndex, int32 vertCount, int32 instanceCount) const
{
	check(CurrentCommandBuffer);
	vkCmdDraw((VkCommandBuffer)CurrentCommandBuffer->GetHandle(), vertCount, instanceCount, vertStartIndex, 0);
}

void jRHI_Vulkan::DrawElements(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count) const
{
	check(CurrentCommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)CurrentCommandBuffer->GetHandle(), count, 1, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsInstanced(EPrimitiveType type, int32 elementSize, int32 startIndex, int32 count, int32 instanceCount) const
{
	check(CurrentCommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)CurrentCommandBuffer->GetHandle(), count, instanceCount, startIndex, 0, 0);
}

void jRHI_Vulkan::DrawElementsBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) const
{
	check(CurrentCommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)CurrentCommandBuffer->GetHandle(), count, 1, startIndex, baseVertexIndex, 0);
}

void jRHI_Vulkan::DrawElementsInstancedBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount) const
{
	check(CurrentCommandBuffer);
	vkCmdDrawIndexed((VkCommandBuffer)CurrentCommandBuffer->GetHandle(), count, instanceCount, startIndex, baseVertexIndex, 0);
}

jShaderBindings* jRHI_Vulkan::CreateShaderBindings(const std::vector<jShaderBinding>& InUniformBindings, const std::vector<jShaderBinding>& InTextureBindings) const
{
	const auto hash = jShaderBindings::GenerateHash(InUniformBindings, InTextureBindings);

	auto it_find = ShaderBindingPool.find(hash);
	if (ShaderBindingPool.end() != it_find)
		return it_find->second;

	auto NewShaderBinding = new jShaderBindings_Vulkan();

	NewShaderBinding->UniformBuffers = InUniformBindings;
	NewShaderBinding->Textures = InTextureBindings;
	NewShaderBinding->CreateDescriptorSetLayout();
	NewShaderBinding->CreatePool();

	ShaderBindingPool.insert(std::make_pair(hash, NewShaderBinding));

	return NewShaderBinding;
}

jShaderBindingInstance* jRHI_Vulkan::CreateShaderBindingInstance(const std::vector<TShaderBinding<IUniformBufferBlock*>>& InUniformBuffers, const std::vector<TShaderBinding<jTextureBindings>>& InTextures) const
{
	const std::vector<jShaderBinding> UniformBindings(InUniformBuffers.begin(), InUniformBuffers.end());
	const std::vector<jShaderBinding> TextureBindings(InTextures.begin(), InTextures.end());

	auto shaderBindings = CreateShaderBindings(UniformBindings, TextureBindings);
	check(shaderBindings);

	auto shaderBindingInstance = shaderBindings->CreateShaderBindingInstance();
	for (int32 i = 0; i < (int32)InUniformBuffers.size(); ++i)
		shaderBindingInstance->UniformBuffers.push_back(InUniformBuffers[i].Data);
	
	for (int32 i = 0; i < (int32)InTextures.size(); ++i)
		shaderBindingInstance->Textures.push_back(InTextures[i].Data);
	return shaderBindingInstance;
}

void* jRHI_Vulkan::CreatePipelineLayout(const std::vector<const jShaderBindings*>& shaderBindings) const
{
	if (shaderBindings.size() <= 0)
		return 0;

	VkPipelineLayout vkPipelineLayout = nullptr;

	const size_t hash = jShaderBindings::CreateShaderBindingsHash(shaderBindings);

	auto it_find = PipelineLayoutPool.find(hash);
	if (PipelineLayoutPool.end() != it_find)
	{
		vkPipelineLayout = it_find->second;
	}
	else
	{
		std::vector<VkDescriptorSetLayout> DescriptorSetLayouts;
		DescriptorSetLayouts.reserve(shaderBindings.size());
		for (const jShaderBindings* binding : shaderBindings)
		{
			const jShaderBindings_Vulkan* binding_vulkan = (const jShaderBindings_Vulkan*)binding;
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
		if (!ensure(vkCreatePipelineLayout(g_rhi_vk->device, &pipelineLayoutInfo, nullptr, &vkPipelineLayout) == VK_SUCCESS))
			return nullptr;

		PipelineLayoutPool.insert(std::make_pair(hash, vkPipelineLayout));
	}

	return vkPipelineLayout;
}

void* jRHI_Vulkan::CreatePipelineLayout(const std::vector<const jShaderBindingInstance*>& shaderBindingInstances) const
{
	if (shaderBindingInstances.size() <= 0)
		return 0;

	std::vector<const jShaderBindings*> bindings;
	bindings.resize(shaderBindingInstances.size());
	for (int32 i = 0; i < shaderBindingInstances.size(); ++i)
	{
		bindings[i] = shaderBindingInstances[i]->ShaderBindings;
	}
	return CreatePipelineLayout(bindings);
}

IUniformBufferBlock* jRHI_Vulkan::CreateUniformBufferBlock(const char* blockname, size_t size /*= 0*/) const
{
	auto uniformBufferBlock = new jUniformBufferBlock_Vulkan(blockname, size);
	uniformBufferBlock->Init();
	return uniformBufferBlock;
}

jQueryTime* jRHI_Vulkan::CreateQueryTime() const
{
	auto queryTime = new jQueryTime_Vulkan();
	return queryTime;
}

void jRHI_Vulkan::ReleaseQueryTime(jQueryTime* queryTime) const
{
    auto queryTime_gl = static_cast<jQueryTime_OpenGL*>(queryTime);
    // glDeleteQueries(1, &queryTime_gl->QueryId);
    delete queryTime_gl;
}

void jRHI_Vulkan::QueryTimeStampStart(const jQueryTime* queryTimeStamp) const
{
	vkCmdWriteTimestamp((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_rhi_vk->QueryPool.vkQueryPool, ((jQueryTime_Vulkan*)queryTimeStamp)->QueryId);
}

void jRHI_Vulkan::QueryTimeStampEnd(const jQueryTime* queryTimeStamp) const
{
	vkCmdWriteTimestamp((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_rhi_vk->QueryPool.vkQueryPool, ((jQueryTime_Vulkan*)queryTimeStamp)->QueryId + 1);
}

void jRHI_Vulkan::QueryTimeStamp(const jQueryTime* queryTimeStamp) const
{
	check(0);
	//const auto queryTimeStamp_gl = static_cast<const jQueryTime_OpenGL*>(queryTimeStamp);
    //JASSERT(queryTimeStamp_gl->QueryId > 0);
    //glQueryCounter(queryTimeStamp_gl->QueryId, GL_TIMESTAMP);
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
			result = (vkGetQueryPoolResults(g_rhi_vk->device, g_rhi_vk->QueryPool.vkQueryPool, queryTimeStamp_vk->QueryId, 2, sizeof(uint64) * 2, time, sizeof(uint64_t), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
		}
	}

    result = (vkGetQueryPoolResults(g_rhi_vk->device, g_rhi_vk->QueryPool.vkQueryPool, queryTimeStamp_vk->QueryId, 2, sizeof(uint64) * 2, time, sizeof(uint64_t), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
    return (result == VK_SUCCESS);
}

void jRHI_Vulkan::GetQueryTimeStampResult(jQueryTime* queryTimeStamp) const
{
	auto queryTimeStamp_vk = static_cast<jQueryTime_Vulkan*>(queryTimeStamp);
	vkGetQueryPoolResults(g_rhi_vk->device, g_rhi_vk->QueryPool.vkQueryPool, queryTimeStamp_vk->QueryId, 2, sizeof(uint64) * 2, queryTimeStamp_vk->TimeStampStartEnd, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
}

void jRHI_Vulkan::BeginQueryTimeElapsed(const jQueryTime* queryTimeElpased) const
{
    //const auto queryTimeElpased_gl = static_cast<const jQueryTime_OpenGL*>(queryTimeElpased);
    //JASSERT(queryTimeElpased_gl->QueryId > 0);
    ////glBeginQuery(GL_TIME_ELAPSED, queryTimeElpased_gl->QueryId);
	check(0);
}

void jRHI_Vulkan::EndQueryTimeElapsed(const jQueryTime* queryTimeElpased) const
{
   // glEndQuery(GL_TIME_ELAPSED);
	check(0);
}

void jRenderPass_Vulkan::Release()
{
    vkDestroyRenderPass(g_rhi_vk->device, RenderPass, nullptr);
}

size_t jRenderPass_Vulkan::GetHash() const
{
    if (Hash)
        return Hash;

    Hash = __super::GetHash();
    Hash ^= STATIC_NAME_CITY_HASH("ClearValues");
    Hash ^= CityHash64((const char*)ClearValues.data(), sizeof(VkClearValue) * ClearValues.size());
    return Hash;
}

std::unordered_map<size_t, VkRenderPass> s_renderPassPool;
bool jRenderPass_Vulkan::CreateRenderPass()
{
	int32 SampleCount = 0;
	int32 LayerCount = 0;

	const auto renderPassHash = GetHash();
	auto it_find = s_renderPassPool.find(renderPassHash);
	if (s_renderPassPool.end() != it_find)
	{
		RenderPass = it_find->second;

		for (int32 i = 0; i < ColorAttachments.size(); ++i)
		{
			const jAttachment* attachment = ColorAttachments[i];
			check(attachment);
			check(attachment->RenderTargetPtr.get());

			const auto& color = attachment->ClearColor;
			VkClearValue colorClear = {};
			colorClear.color = { color.x, color.y, color.z, color.w };
			ClearValues.push_back(colorClear);

			const auto& RTInfo = attachment->RenderTargetPtr->Info;
			
			check(SampleCount == 0 || SampleCount == RTInfo.SampleCount);
			check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
			SampleCount = RTInfo.SampleCount;
			LayerCount = RTInfo.LayerCount;
		}

		if (DepthAttachment)
		{
			const jAttachment* attachment = DepthAttachment;
			check(attachment);
			check(attachment->RenderTargetPtr.get());

			VkClearValue colorClear = {};
			colorClear.depthStencil = { DepthAttachment->ClearDepth.x, (uint32)DepthAttachment->ClearDepth.y };
			ClearValues.push_back(colorClear);

			const auto& RTInfo = attachment->RenderTargetPtr->Info;

			if (SampleCount == 0)
				SampleCount = RTInfo.SampleCount;
			if (LayerCount == 0)
				LayerCount = RTInfo.LayerCount;
		}
	}
	else
	{
		int32 AttachmentIndex = 0;
		std::vector<VkAttachmentDescription> AttachmentDescs;

		const size_t Attachments = ColorAttachments.size() + (DepthAttachment ? 1 : 0) + (ColorAttachmentResolve ? 1 : 0);
		AttachmentDescs.resize(Attachments);

		std::vector<VkAttachmentReference> colorAttachmentRefs;
		colorAttachmentRefs.resize(ColorAttachments.size());
		for (int32 i = 0; i < ColorAttachments.size(); ++i)
		{
			const jAttachment* attachment = ColorAttachments[i];
			check(attachment);
			check(attachment->RenderTargetPtr.get());

			const auto& RTInfo = attachment->RenderTargetPtr->Info;

			VkAttachmentDescription& colorDesc = AttachmentDescs[AttachmentIndex];
			colorDesc.format = GetVulkanTextureFormat(RTInfo.Format);
			colorDesc.samples = (VkSampleCountFlagBits)RTInfo.SampleCount;
			GetVulkanAttachmentLoadStoreOp(colorDesc.loadOp, colorDesc.storeOp, attachment->LoadStoreOp);
			GetVulkanAttachmentLoadStoreOp(colorDesc.stencilLoadOp, colorDesc.stencilStoreOp, attachment->StencilLoadStoreOp);

			check((ColorAttachmentResolve && ((int32)RTInfo.SampleCount > 1)) || (!ColorAttachmentResolve && (int32)RTInfo.SampleCount == 1));
			check(SampleCount == 0 || SampleCount == RTInfo.SampleCount);
			check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
			SampleCount = RTInfo.SampleCount;
			LayerCount = RTInfo.LayerCount;

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
			if (SampleCount > 1)
			{
				if (DepthAttachment->TransitToShaderReadAtFinal)
					colorDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				else
					colorDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
			else
			{
				colorDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;			// MSAA 안하면 바로 Present 할 것이므로
			}

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

			const auto& color = attachment->ClearColor;
			VkClearValue colorClear = {};
			colorClear.color = { color.x, color.y, color.z, color.w };
			ClearValues.push_back(colorClear);
		}

		if (DepthAttachment)
		{
			check(DepthAttachment->RenderTargetPtr.get());
			const auto& RTInfo = DepthAttachment->RenderTargetPtr->Info;

			VkAttachmentDescription& depthAttachment = AttachmentDescs[AttachmentIndex];
			depthAttachment.format = GetVulkanTextureFormat(RTInfo.Format);
			depthAttachment.samples = (VkSampleCountFlagBits)RTInfo.SampleCount;
			GetVulkanAttachmentLoadStoreOp(depthAttachment.loadOp, depthAttachment.storeOp, DepthAttachment->LoadStoreOp);
			GetVulkanAttachmentLoadStoreOp(depthAttachment.stencilLoadOp, depthAttachment.stencilStoreOp, DepthAttachment->StencilLoadStoreOp);
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			// 최종 쉐이더 리로스 레이아웃 결정 - 쉐이더에서 읽는 리소스로 사용할 경우 처리
			if (DepthAttachment->TransitToShaderReadAtFinal)
				depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			else
				depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;			

			//check((ColorAttachmentResolve && ((int32)RTInfo.SampleCount > 1)) || (!ColorAttachmentResolve && (int32)RTInfo.SampleCount == 1));
			//check(SampleCount == 0 || SampleCount == RTInfo.SampleCount);
			//check(LayerCount == 0 || LayerCount == RTInfo.LayerCount);
			 if (SampleCount == 0)
				SampleCount = RTInfo.SampleCount;
			 if (LayerCount == 0)
				LayerCount = RTInfo.LayerCount;

			VkClearValue colorClear = {};
			colorClear.depthStencil = { DepthAttachment->ClearDepth.x, (uint32)DepthAttachment->ClearDepth.y };
			ClearValues.push_back(colorClear);
		}

		VkAttachmentReference colorAttachmentResolveRef = {};
		if (SampleCount > 1)
		{
			check(ColorAttachmentResolve);
			check(ColorAttachmentResolve->RenderTargetPtr.get());
			const auto& RTInfo = ColorAttachmentResolve->RenderTargetPtr->Info;

			VkAttachmentDescription colorAttachmentResolve = {};
			colorAttachmentResolve.format = GetVulkanTextureFormat(RTInfo.Format);
			colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
			GetVulkanAttachmentLoadStoreOp(colorAttachmentResolve.loadOp, colorAttachmentResolve.storeOp, ColorAttachmentResolve->LoadStoreOp);
			GetVulkanAttachmentLoadStoreOp(colorAttachmentResolve.stencilLoadOp, colorAttachmentResolve.stencilStoreOp, ColorAttachmentResolve->StencilLoadStoreOp);
			colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			AttachmentDescs.emplace_back(colorAttachmentResolve);

			check(RTInfo.SampleCount == 1);
			check(LayerCount == RTInfo.LayerCount);

			colorAttachmentResolveRef.attachment = AttachmentIndex++;		// color attachments + depth attachment
			colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		if (colorAttachmentRefs.size() > 0)
		{
			subpass.colorAttachmentCount = (uint32)colorAttachmentRefs.size();
			subpass.pColorAttachments = &colorAttachmentRefs[0];
		}

		if (DepthAttachment)
		{
            VkAttachmentReference depthAttachmentRef = {};
            depthAttachmentRef.attachment = AttachmentIndex++;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			subpass.pDepthStencilAttachment = &depthAttachmentRef;
		}

		if (SampleCount > 1)
			subpass.pResolveAttachments = &colorAttachmentResolveRef;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(AttachmentDescs.size());
		renderPassInfo.pAttachments = AttachmentDescs.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		
		// 1. Subpasses 들의 이미지 레이아웃 전환은 자동적으로 일어나며, 이런 전환은 subpass dependencies 로 관리 됨.
		// 현재 서브패스를 1개 쓰고 있지만 서브패스 앞, 뒤에 암묵적인 서브페스가 있음.
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
		

		//std::array<VkSubpassDependency, 2> dependencies;

		//dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		//dependencies[0].dstSubpass = 0;
		//dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		//dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		//dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		//dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		//dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		//dependencies[1].srcSubpass = 0;
		//dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		//dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		//dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		//dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		//dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		//dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		//renderPassInfo.dependencyCount = (int32)dependencies.size();
		//renderPassInfo.pDependencies = dependencies.data();

		if (!ensure(vkCreateRenderPass(g_rhi_vk->GetDevice(), &renderPassInfo, nullptr, &RenderPass) == VK_SUCCESS))
			return false;

		s_renderPassPool.insert(std::make_pair(renderPassHash, RenderPass));
	}

	// Create RenderPassInfo
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassInfo.renderPass = RenderPass;

    // 렌더될 영역이며, 최상의 성능을 위해 attachment의 크기와 동일해야함.
	RenderPassInfo.renderArea.offset = { RenderOffset.x, RenderOffset.y };
	RenderPassInfo.renderArea.extent = { (uint32)RenderExtent.x, (uint32)RenderExtent.y };

	RenderPassInfo.clearValueCount = static_cast<uint32_t>(ClearValues.size());
	RenderPassInfo.pClearValues = ClearValues.data();

	// Create framebuffer
	{
		std::vector<VkImageView> ImageViews;

		for (int32 k = 0; k < ColorAttachments.size(); ++k)
		{
			check(ColorAttachments[k]);
			
			const auto* RT = ColorAttachments[k]->RenderTargetPtr.get();
			check(RT);

			const jTexture* texture = RT->GetTexture();
			check(texture);

			ImageViews.push_back((VkImageView)texture->GetHandle());
		}

		if (DepthAttachment)
		{
			const auto* RT = DepthAttachment->RenderTargetPtr.get();
			check(RT);

			const jTexture* texture = RT->GetTexture();
			check(texture);

			ImageViews.push_back((VkImageView)texture->GetHandle());
		}

		if (SampleCount > 1)
		{
			check(ColorAttachmentResolve);

			const auto* RT = ColorAttachmentResolve->RenderTargetPtr.get();
			check(RT);

			const jTexture* texture = RT->GetTexture();
			check(texture);

			ImageViews.push_back((VkImageView)texture->GetHandle());
		}

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = RenderPass;

		// RenderPass와 같은 수와 같은 타입의 attachment 를 사용
		framebufferInfo.attachmentCount = static_cast<uint32>(ImageViews.size());
		framebufferInfo.pAttachments = ImageViews.data();

		framebufferInfo.width = RenderExtent.x;
		framebufferInfo.height = RenderExtent.y;
		framebufferInfo.layers = LayerCount;			// 이미지 배열의 레이어수(3D framebuffer에 사용할 듯)

		if (!ensure(vkCreateFramebuffer(g_rhi_vk->device, &framebufferInfo, nullptr, &FrameBuffer) == VK_SUCCESS))
			return false;
	}

    return true;
}

bool jShaderBindings_Vulkan::CreateDescriptorSetLayout()
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

jShaderBindingInstance* jShaderBindings_Vulkan::CreateShaderBindingInstance() const
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &DescriptorSetLayout;

	jShaderBindingInstance_Vulkan* Instance = new jShaderBindingInstance_Vulkan();
	Instance->ShaderBindings = this;
	if (!ensure(vkAllocateDescriptorSets(g_rhi_vk->device, &allocInfo, &Instance->DescriptorSet) == VK_SUCCESS))
	{
		delete Instance;
		return nullptr;
	}
	CreatedBindingInstances.push_back(Instance);

	return Instance;
}

std::vector<jShaderBindingInstance*> jShaderBindings_Vulkan::CreateShaderBindingInstance(int32 count) const
{
    std::vector<VkDescriptorSetLayout> descSetLayout(count, DescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = DescriptorPool;
    allocInfo.descriptorSetCount = (uint32)descSetLayout.size();
    allocInfo.pSetLayouts = descSetLayout.data();

    std::vector<jShaderBindingInstance*> Instances;

	std::vector<VkDescriptorSet> DescriptorSets;
	DescriptorSets.resize(descSetLayout.size());
    if (!ensure(vkAllocateDescriptorSets(g_rhi_vk->device, &allocInfo, DescriptorSets.data()) == VK_SUCCESS))
        return Instances;

    Instances.resize(DescriptorSets.size());
	for (int32 i = 0; i < DescriptorSets.size(); ++i)		// todo opt
	{
		auto NewBindingInstance = new jShaderBindingInstance_Vulkan();
		CreatedBindingInstances.push_back(NewBindingInstance);
		NewBindingInstance->DescriptorSet = DescriptorSets[i];
		NewBindingInstance->ShaderBindings = this;

		Instances[i] = NewBindingInstance;
	}

    return Instances;
}

size_t jShaderBindings_Vulkan::GetHash() const
{
    size_t result = 0;

    result ^= STATIC_NAME_CITY_HASH("UniformBuffer");
    result ^= CityHash64((const char*)UniformBuffers.data(), sizeof(jShaderBinding) * UniformBuffers.size());

    result ^= STATIC_NAME_CITY_HASH("Texture");
    result ^= CityHash64((const char*)Textures.data(), sizeof(jShaderBinding) * Textures.size());

    return result;
}

void jShaderBindings_Vulkan::CreatePool()
{
	DescriptorPool = g_rhi_vk->ShaderBindingsManager.CreatePool(*this);
}

void jShaderBindingInstance_Vulkan::UpdateShaderBindings()
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

		bufferOffset += (int32)UniformBuffers[i]->GetBufferSize();
	}

	std::vector<VkDescriptorImageInfo> descriptorImages;
	descriptorImages.resize(ShaderBindings->Textures.size());
	for (int32 i = 0; i < ShaderBindings->Textures.size(); ++i)
	{
		descriptorImages[i].imageLayout = Textures[i].Texture->IsDepthFormat() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		descriptorImages[i].imageView = (VkImageView)Textures[i].Texture->GetHandle();
		if (Textures[i].SamplerState)
			descriptorImages[i].sampler = (VkSampler)Textures[i].SamplerState->GetHandle();

		if (!descriptorImages[i].sampler)
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

void jShaderBindingInstance_Vulkan::Bind(void* pipelineLayout, int32 InSlot) const
{
	check(g_rhi_vk->CurrentCommandBuffer);
	check(pipelineLayout);
	vkCmdBindDescriptorSets((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipelineLayout)(pipelineLayout), InSlot, 1, &DescriptorSet, 0, nullptr);
}

bool jCommandBufferManager_Vulkan::CreatePool(uint32 QueueIndex)
{
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = QueueIndex;

	// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 커맨드 버퍼가 새로운 커맨드를 자주 다시 기록한다고 힌트를 줌.
	//											(메모리 할당 동작을 변경할 것임)
	// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 커맨드 버퍼들이 개별적으로 다시 기록될 수 있다.
	//													이 플래그가 없으면 모든 커맨드 버퍼들이 동시에 리셋되야 함.
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;		// Optional

	if (!ensure(vkCreateCommandPool(g_rhi_vk->device, &poolInfo, nullptr, &CommandPool) == VK_SUCCESS))
		return false;

	return true;
}

jCommandBuffer_Vulkan jCommandBufferManager_Vulkan::GetOrCreateCommandBuffer()
{
	if (PendingCommandBuffers.size() > 0)
	{
		for (int32 i = 0; i < (int32)PendingCommandBuffers.size(); ++i)
		{
			const VkResult Result = vkGetFenceStatus(g_rhi_vk->device, PendingCommandBuffers[i].Fence);
			if (Result == VK_SUCCESS)		// VK_SUCCESS 면 Signaled
			{
				jCommandBuffer_Vulkan commandBuffer = PendingCommandBuffers[i];
				PendingCommandBuffers.erase(PendingCommandBuffers.begin() + i);

				UsingCommandBuffers.push_back(commandBuffer);
				commandBuffer.Reset();

				return commandBuffer;
			}
		}
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

	commandBuffer.Fence = g_rhi_vk->FenceManager.GetOrCreateFence();

	UsingCommandBuffers.push_back(commandBuffer);

	return commandBuffer;
}

void jCommandBufferManager_Vulkan::ReturnCommandBuffer(jCommandBuffer_Vulkan commandBuffer)
{
	for (int32 i = 0; i < UsingCommandBuffers.size(); ++i)
	{
		if (UsingCommandBuffers[i].GetHandle() == commandBuffer.GetHandle())
		{
			UsingCommandBuffers.erase(UsingCommandBuffers.begin() + i);
			break;
		}
	}
	PendingCommandBuffers.push_back(commandBuffer);
}

VkDescriptorPool jShaderBindingsManager_Vulkan::CreatePool(const jShaderBindings_Vulkan& bindings, uint32 maxAllocations) const
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

void jVertexBuffer_Vulkan::Bind() const
{
	check(g_rhi_vk->CurrentCommandBuffer);
	vkCmdBindVertexBuffers((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), 0, (uint32)BindInfos.Buffers.size(), &BindInfos.Buffers[0], &BindInfos.Offsets[0]);
}

void jIndexBuffer_Vulkan::Bind() const
{
	VkIndexType IndexType = VK_INDEX_TYPE_UINT16;
	switch (IndexStreamData->Param->ElementType)
	{
	case EBufferElementType::BYTE:
		IndexType = VK_INDEX_TYPE_UINT8_EXT;
		break;
	case EBufferElementType::UINT16:
		IndexType = VK_INDEX_TYPE_UINT16;
		break;
	case EBufferElementType::UINT32:
		IndexType = VK_INDEX_TYPE_UINT32;
		break;
	case EBufferElementType::FLOAT:
		check(0);
		break;
	default:
		break;
	}
	check(g_rhi_vk->CurrentCommandBuffer);
	vkCmdBindIndexBuffer((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), IndexBuffer, 0, IndexType);
}

VkPipeline jPipelineStateInfo::CreateGraphicsPipelineState()
{
	// 미리 만들어 둔게 있으면 사용
	if (vkPipeline)
		return vkPipeline;

	check(PipelineStateFixed);

	Hash = GetHash();
	check(Hash);
	{
		auto it_find = PipelineStatePool.find(Hash);
		if (PipelineStatePool.end() != it_find)
		{
			*this = it_find->second;
			return vkPipeline;
		}
	}

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateVertexInputState();
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = ((jVertexBuffer_Vulkan*)VertexBuffer)->CreateInputAssemblyState();

	// 4. Viewports and scissors
	// SwapChain의 이미지 사이즈가 이 클래스에 정의된 상수 WIDTH, HEIGHT와 다를 수 있다는 것을 기억 해야함.
	// 그리고 Viewports 사이즈는 SwapChain 크기 이하로 마추면 됨.
	// [minDepth ~ maxDepth] 는 [0.0 ~ 1.0] 이며 특별한 경우가 아니면 이 범위로 사용하면 된다.
	// Scissor Rect 영역을 설정해주면 영역내에 있는 Pixel만 레스터라이저를 통과할 수 있으며 나머지는 버려(Discard)진다.
	const auto& Viewports = PipelineStateFixed->Viewports;
	check(Viewports.size());
	std::vector<VkViewport> vkViewports;
	vkViewports.resize(Viewports.size());
	for (int32 i = 0; i < vkViewports.size(); ++i)
	{
		vkViewports[i].x = Viewports[i].X;
		vkViewports[i].y = Viewports[i].Y;
		vkViewports[i].width = Viewports[i].Width;
		vkViewports[i].height = Viewports[i].Height;
		vkViewports[i].minDepth = Viewports[i].MinDepth;
		vkViewports[i].maxDepth = Viewports[i].MaxDepth;
	}

	const auto& Scissors = PipelineStateFixed->Scissors;
	check(Scissors.size());
	std::vector<VkRect2D> vkScissor;
	vkScissor.resize(Scissors.size());
	for (int32 i = 0; i < vkScissor.size(); ++i)
	{
		vkScissor[i].offset.x = Scissors[i].Offset.x;
		vkScissor[i].offset.y = Scissors[i].Offset.y;
		vkScissor[i].extent.width = Scissors[i].Extent.x;
		vkScissor[i].extent.height = Scissors[i].Extent.y;
	}

	// Viewport와 Scissor 를 여러개 설정할 수 있는 멀티 뷰포트를 사용할 수 있기 때문임
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = (uint32)vkViewports.size();
	viewportState.pViewports = &vkViewports[0];
	viewportState.scissorCount = (uint32)vkScissor.size();
	viewportState.pScissors = &vkScissor[0];

	// 2가지 blending 방식을 모두 끌 수도있는데 그렇게 되면, 변경되지 않은 fragment color 값이 그대로 framebuffer에 쓰여짐.
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	// 2). 기존과 새로운 값을 비트 연산으로 결합한다.
	colorBlending.logicOpEnable = VK_FALSE;			// 모든 framebuffer에 사용하는 blendEnable을 VK_FALSE로 했다면 자동으로 logicOpEnable은 꺼진다.
	colorBlending.logicOp = VK_LOGIC_OP_COPY;		// Optional

	// 1). 기존과 새로운 값을 섞어서 최종색을 만들어낸다.
	colorBlending.attachmentCount = 1;						// framebuffer 개수에 맞게 설정해준다.
	colorBlending.pAttachments = &((jBlendingStateInfo_Vulakn*)PipelineStateFixed->BlendingState)->ColorBlendAttachmentInfo;

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
	vkPipelineLayout = (VkPipelineLayout)g_rhi->CreatePipelineLayout(ShaderBindings);

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

	// Shader stage
	check(Shader);
	//pipelineInfo.stageCount = 2;
	//pipelineInfo.pStages = shaderStage;
	pipelineInfo.stageCount = (uint32)((jShader_Vulkan*)Shader)->ShaderStages.size();
	pipelineInfo.pStages = ((jShader_Vulkan*)Shader)->ShaderStages.data();

	// Fixed-function stage
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &((jRasterizationStateInfo_Vulkan*)PipelineStateFixed->RasterizationState)->RasterizationStateInfo;
	pipelineInfo.pMultisampleState = &((jMultisampleStateInfo_Vulkan*)PipelineStateFixed->MultisampleState)->MultisampleStateInfo;
	pipelineInfo.pDepthStencilState = &((jDepthStencilStateInfo_Vulkan*)PipelineStateFixed->DepthStencilState)->DepthStencilStateInfo;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = nullptr;			// Optional
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.renderPass = (VkRenderPass)RenderPass->GetRenderPass();
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
	if (!ensure(vkCreateGraphicsPipelines(g_rhi_vk->device, g_rhi_vk->PipelineCache, 1, &pipelineInfo, nullptr, &vkPipeline) == VK_SUCCESS))
	{
		return nullptr;
	}

	PipelineStatePool.insert(std::make_pair(Hash, *this));

	return vkPipeline;
}


void jPipelineStateInfo::Bind()
{
	check(vkPipeline);
	vkCmdBindPipeline((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
}

void jUniformBufferBlock_Vulkan::Destroy()
{
	vkDestroyBuffer(g_rhi_vk->device, Bufffer, nullptr);
	vkFreeMemory(g_rhi_vk->device, BufferMemory, nullptr);
}

void jUniformBufferBlock_Vulkan::Init()
{
	check(Size);
	VkDeviceSize bufferSize = Size;

	g_rhi_vk->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, Bufffer, BufferMemory);
}

void jUniformBufferBlock_Vulkan::UpdateBufferData(const void* InData, size_t InSize)
{
	check(Size == InSize);
	// 다르면 여기서 추가 작업을 해야 함.

	Size = InSize;

	if (Bufffer && BufferMemory)
	{
		void* data = nullptr;
		vkMapMemory(g_rhi_vk->device, BufferMemory, 0, Size, 0, &data);
		if (InData)
			memcpy(data, InData, InSize);
		else
			memset(data, 0, InSize);
		vkUnmapMemory(g_rhi_vk->device, BufferMemory);
	}
}

void jUniformBufferBlock_Vulkan::ClearBuffer(int32 clearValue)
{
	if (Bufffer && BufferMemory)
	{
		void* data = nullptr;
		vkMapMemory(g_rhi_vk->device, BufferMemory, 0, Size, 0, &data);
		memset(data, clearValue, Size);
		vkUnmapMemory(g_rhi_vk->device, BufferMemory);
	}
}

VkFence jFenceManager_Vulkan::GetOrCreateFence()
{
	if (PendingFences.size() > 0)
	{
		VkFence fence = *PendingFences.begin();
		PendingFences.erase(PendingFences.begin());
		UsingFences.insert(fence);
		return fence;
	}
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkFence newFence = nullptr;
	if (ensure(vkCreateFence(g_rhi_vk->device, &fenceInfo, nullptr, &newFence) == VK_SUCCESS))
		UsingFences.insert(newFence);

	return newFence;
}

//////////////////////////////////////////////////////////////////////////

void jQueryTime_Vulkan::Init()
{
	QueryId = g_rhi_vk->QueryPool.QueryIndex[jProfile_GPU::CurrentWatingResultListIndex];
	g_rhi_vk->QueryPool.QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] += 2;		// Need 2 Queries for Starting, Ending timestamp
}

bool jQueryPool_Vulkan::Create()
{
    VkQueryPoolCreateInfo querPoolCreateInfo = {};
    querPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    querPoolCreateInfo.pNext = nullptr;
    querPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    querPoolCreateInfo.queryCount = 512 * jRHI::MaxWaitingQuerySet;

    const VkResult res = vkCreateQueryPool(g_rhi_vk->device, &querPoolCreateInfo, nullptr, &vkQueryPool);
    return (res == VK_SUCCESS);
}

void jQueryPool_Vulkan::ResetQueryPool(jCommandBuffer* pCommanBuffer /*= nullptr*/)
{
    vkCmdResetQueryPool((VkCommandBuffer)pCommanBuffer->GetHandle(), vkQueryPool, MaxQueryPool * jProfile_GPU::CurrentWatingResultListIndex, MaxQueryPool);
	QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] = MaxQueryPool * jProfile_GPU::CurrentWatingResultListIndex;
}


#endif // USE_VULKAN
