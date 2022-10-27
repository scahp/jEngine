#pragma once

namespace jVulkanDeviceUtil
{

#if _DEBUG
static constexpr bool EnableValidationLayers = false;
#else
static constexpr bool EnableValidationLayers = false;
#endif

struct QueueFamilyIndices
{
    std::optional<uint32> GraphicsFamily;
    std::optional<uint32> ComputeFamily;
    std::optional<uint32> PresentFamily;

    bool IsComplete() const
    {
        return GraphicsFamily.has_value() && ComputeFamily.has_value() && PresentFamily.has_value();
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR Capabilities;
    std::vector<VkSurfaceFormatKHR> Formats;
    std::vector<VkPresentModeKHR> PresentModes;
};

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
    //, "VK_LAYER_LUNARG_api_dump"		// display api call
};

const std::vector<const char*> DeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME,
    VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);
void PopulateDebutMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo
    , const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
VkSampleCountFlagBits GetMaxUsableSampleCount(VkPhysicalDevice physicalDevice);
VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
VkExtent2D ChooseSwapExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities);
SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
std::vector<const char*> GetRequiredInstanceExtensions();
bool CheckValidationLayerSupport();

}