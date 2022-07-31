#include "pch.h"
#include "jFenceManager.h"

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
    if (ensure(vkCreateFence(g_rhi_vk->Device, &fenceInfo, nullptr, &newFence) == VK_SUCCESS))
        UsingFences.insert(newFence);

    return newFence;
}