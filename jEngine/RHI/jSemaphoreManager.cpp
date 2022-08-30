#include "pch.h"
#include "jSemaphoreManager.h"

jSemaphoreManager_Vulkan::~jSemaphoreManager_Vulkan()
{
    ReleaseInternal();
}

void jSemaphoreManager_Vulkan::ReleaseInternal()
{
    for (auto& iter : UsingSemaphore)
        vkDestroySemaphore(g_rhi_vk->Device, iter, nullptr);
    UsingSemaphore.clear();

    for (auto& iter : PendingSemaphore)
        vkDestroySemaphore(g_rhi_vk->Device, iter, nullptr);
    PendingSemaphore.clear();
}

VkSemaphore jSemaphoreManager_Vulkan::GetOrCreateSemaphore()
{
    if (PendingSemaphore.size() > 0)
    {
        VkSemaphore semaphore = *PendingSemaphore.begin();
        PendingSemaphore.erase(PendingSemaphore.begin());
        UsingSemaphore.insert(semaphore);
        return semaphore;
    }

    VkSemaphore semaphore;
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (ensure(VK_SUCCESS == vkCreateSemaphore(g_rhi_vk->Device, &semaphoreInfo, nullptr, &semaphore)))
        UsingSemaphore.insert(semaphore);
    else
        check(0);

    return semaphore;
}

