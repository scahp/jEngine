#include "pch.h"
#include "jVulkanDeviceUtil.h"

namespace jVulkanDeviceUtil
{

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
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

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);

    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

jVulkanDeviceUtil::QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
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
        }
        
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            indices.computeFamily = i;
        }
        
        if (indices.IsComplete())
            break;

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

bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures = {};
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    // 현재는 Geometry Shader 지원 여부만 판단
    if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || !deviceFeatures.geometryShader)
        return false;

    QueueFamilyIndices indices = FindQueueFamilies(device, surface);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device, surface);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.IsComplete() && extensionsSupported && swapChainAdequate && deviceFeatures.samplerAnisotropy;
}

VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice physicalDevice)
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

VkExtent2D ChooseSwapExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities)
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

jVulkanDeviceUtil::SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
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

std::vector<const char*> GetRequiredExtensions()
{
    uint32 glfwExtensionCount = 0;
    const char** glfwExtensions = nullptr;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (jVulkanDeviceUtil::EnableValidationLayers)
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

}