#pragma once

#include "jVulkanFeatureSwitch.h"

namespace jVulkanDeviceUtil
{

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
    VK_KHR_MAINTENANCE3_EXTENSION_NAME,
#if USE_VARIABLE_SHADING_RATE_TIER2
    VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME,
#endif
    VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
    VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME,
    
    // VK_KHR_acceleration_structure
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,

    // VK_KHR_ray_tracing_pipeline
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,

    // VK_KHR_spirv_1_4
    VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,    
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