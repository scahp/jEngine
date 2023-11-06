#include "pch.h"
#include "jVulkanFeatureSwitch.h"

bool IsUse_VULKAN_NDC_Y_FLIP()
{
    return (1 && IsUseVulkan());
}
