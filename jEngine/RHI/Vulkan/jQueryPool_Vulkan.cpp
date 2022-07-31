#include "pch.h"
#include "jQueryPool_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"

//////////////////////////////////////////////////////////////////////////
// jQueryPool_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jQueryPool_Vulkan::Create()
{
    VkQueryPoolCreateInfo querPoolCreateInfo = {};
    querPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    querPoolCreateInfo.pNext = nullptr;
    querPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    querPoolCreateInfo.queryCount = MaxQueryTimeCount * jRHI::MaxWaitingQuerySet;

    const VkResult res = vkCreateQueryPool(g_rhi_vk->Device, &querPoolCreateInfo, nullptr, &vkQueryPool);
    return (res == VK_SUCCESS);
}

void jQueryPool_Vulkan::ResetQueryPool(jCommandBuffer* pCommanBuffer /*= nullptr*/)
{
    vkCmdResetQueryPool((VkCommandBuffer)pCommanBuffer->GetHandle(), vkQueryPool, MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex, MaxQueryTimeCount);
    QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] = MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex;
}

//////////////////////////////////////////////////////////////////////////
// jQueryTime_Vulkan
//////////////////////////////////////////////////////////////////////////
void jQueryTime_Vulkan::Init()
{
    QueryId = g_rhi_vk->QueryPool.QueryIndex[jProfile_GPU::CurrentWatingResultListIndex];
    g_rhi_vk->QueryPool.QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] += 2;		// Need 2 Queries for Starting, Ending timestamp
}
