#include "pch.h"
#include "jFenceManager_Vulkan.h"

void jFence_Vulkan::Release()
{
    if (VK_NULL_HANDLE != Fence)
        vkDestroyFence(g_rhi_vk->Device, Fence, nullptr);
}

void jFence_Vulkan::WaitForFence(uint64 InTimeoutNanoSec)
{
    check(VK_NULL_HANDLE != Fence);
    vkWaitForFences(g_rhi_vk->Device, 1, &Fence, true, UINT64_MAX);
}

bool jFence_Vulkan::IsComplete() const
{
    const VkResult Result = vkGetFenceStatus(g_rhi_vk->Device, Fence);
    return (Result == VK_SUCCESS);		// VK_SUCCESS 면 Signaled
}

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
        iter->Release();
    }
    UsingFences.clear();

    for (auto& iter : PendingFences)
    {
        iter->Release();
    }
    PendingFences.clear();
}
