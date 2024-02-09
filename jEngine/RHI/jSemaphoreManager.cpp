#include "pch.h"
#include "jSemaphoreManager.h"

jSemaphoreManager_Vulkan::~jSemaphoreManager_Vulkan()
{
    Release();
}

void jSemaphoreManager_Vulkan::Release()
{
    for (int32 i = 0; i < (int32)ESemaphoreType::MAX; ++i)
    {
        auto& CurUsingSemaphores = UsingSemaphore[(int32)i];
        for (auto& iter : CurUsingSemaphores)
        {
            vkDestroySemaphore(g_rhi_vk->Device, (VkSemaphore)iter->GetHandle(), nullptr);
            delete iter;
        }
        CurUsingSemaphores.clear();

        auto& CurPendingSemaphores = PendingSemaphore[(int32)i];
        for (auto& iter : CurPendingSemaphores)
        {
            vkDestroySemaphore(g_rhi_vk->Device, (VkSemaphore)iter->GetHandle(), nullptr);
            delete iter;
        }
        CurPendingSemaphores.clear();
    }
}

jSemaphore* jSemaphoreManager_Vulkan::GetOrCreateSemaphore(ESemaphoreType InType)
{
    auto& CurPendingSemaphores = PendingSemaphore[(int32)InType];
    if (CurPendingSemaphores.size() > 0)
    {
        jSemaphore* semaphore = *CurPendingSemaphores.begin();
        CurPendingSemaphores.erase(CurPendingSemaphores.begin());
        UsingSemaphore[(int32)InType].insert(semaphore);
        return semaphore;
    }
    
    jSemaphore_Vulkan* newSemaphore = new jSemaphore_Vulkan(InType);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (InType == ESemaphoreType::TIMELINE)
    {
        VkSemaphoreTypeCreateInfo createTypeInfo{};
        createTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        createTypeInfo.pNext = nullptr;
        createTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        createTypeInfo.initialValue = 0;
        semaphoreInfo.pNext = &createTypeInfo;
    }

    if (ensure(VK_SUCCESS == vkCreateSemaphore(g_rhi_vk->Device, &semaphoreInfo, nullptr, &newSemaphore->Semaphore)))
        UsingSemaphore[(int32)InType].insert(newSemaphore);
    else
        check(0);

    return newSemaphore;
}

