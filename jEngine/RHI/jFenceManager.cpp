#include "pch.h"
#include "jFenceManager.h"

jFence* jFenceManager_Vulkan::GetOrCreateFence()
{
    if (PendingFences.size() > 0)
    {
        jFence* fence = *PendingFences.begin();
        PendingFences.erase(PendingFences.begin());
        UsingFences.insert(fence);
        return fence;
    }

    jFence_Vulkan* newFence = new jFence_Vulkan();
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (ensure(vkCreateFence(g_rhi_vk->Device, &fenceInfo, nullptr, &newFence->Fence) == VK_SUCCESS))
        UsingFences.insert(newFence);
    else
        check(0);

    return newFence;
}

void jFenceManager_Vulkan::Release()
{
    for (auto& iter : UsingFences)
    {
        vkDestroyFence(g_rhi_vk->Device, (VkFence)iter->GetHandle(), nullptr);
    }
    UsingFences.clear();

    for (auto& iter : PendingFences)
    {
        vkDestroyFence(g_rhi_vk->Device, (VkFence)iter->GetHandle(), nullptr);
    }
    PendingFences.clear();
}
