#include "pch.h"
#include "jSemaphoreManager.h"

jSemaphoreManager_Vulkan::~jSemaphoreManager_Vulkan()
{
    Release();
}

void jSemaphoreManager_Vulkan::Release()
{
    for (auto& iter : UsingSemaphore)
    {
        vkDestroySemaphore(g_rhi_vk->Device, (VkSemaphore)iter->GetHandle(), nullptr);
        delete iter;
    }
    UsingSemaphore.clear();

    for (auto& iter : PendingSemaphore)
    {
        vkDestroySemaphore(g_rhi_vk->Device, (VkSemaphore)iter->GetHandle(), nullptr);
        delete iter;
    }
    PendingSemaphore.clear();
}

jSemaphore* jSemaphoreManager_Vulkan::GetOrCreateSemaphore()
{
    if (PendingSemaphore.size() > 0)
    {
        jSemaphore* semaphore = *PendingSemaphore.begin();
        PendingSemaphore.erase(PendingSemaphore.begin());
        UsingSemaphore.insert(semaphore);
        return semaphore;
    }
    
    jSemaphore_Vulkan* newSemaphore = new jSemaphore_Vulkan();
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (ensure(VK_SUCCESS == vkCreateSemaphore(g_rhi_vk->Device, &semaphoreInfo, nullptr, &newSemaphore->Semaphore)))
        UsingSemaphore.insert(newSemaphore);
    else
        check(0);

    return newSemaphore;
}

