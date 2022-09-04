#include "pch.h"
#include "jQueryPoolTime_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"

//////////////////////////////////////////////////////////////////////////
// jQueryPoolTime_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jQueryPoolTime_Vulkan::Create()
{
    VkQueryPoolCreateInfo querPoolCreateInfo = {};
    querPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    querPoolCreateInfo.pNext = nullptr;
    querPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    querPoolCreateInfo.queryCount = MaxQueryTimeCount * jRHI::MaxWaitingQuerySet;

    const VkResult res = vkCreateQueryPool(g_rhi_vk->Device, &querPoolCreateInfo, nullptr, &vkQueryPool);
    return (res == VK_SUCCESS);
}

void jQueryPoolTime_Vulkan::ResetQueryPool(jCommandBuffer* pCommanBuffer /*= nullptr*/)
{
    vkCmdResetQueryPool((VkCommandBuffer)pCommanBuffer->GetHandle(), vkQueryPool, MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex, MaxQueryTimeCount);
    QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] = MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex;
}

void jQueryPoolTime_Vulkan::Release()
{
    ReleaseInstance();
}

void jQueryPoolTime_Vulkan::ReleaseInstance()
{
    if (vkQueryPool)
    {
        vkDestroyQueryPool(g_rhi_vk->Device, vkQueryPool, nullptr);
        vkQueryPool = nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// jQueryTime_Vulkan
//////////////////////////////////////////////////////////////////////////
void jQueryTime_Vulkan::Init()
{
    QueryId = g_rhi_vk->QueryPoolTime->QueryIndex[jProfile_GPU::CurrentWatingResultListIndex];
    g_rhi_vk->QueryPoolTime->QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] += 2;		// Need 2 Queries for Starting, Ending timestamp
}
