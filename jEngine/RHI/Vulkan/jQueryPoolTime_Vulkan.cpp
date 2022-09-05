#include "pch.h"
#include "jQueryPoolTime_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"

const uint32 MaxQueryTimeCount = 512;

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

    const VkResult res = vkCreateQueryPool(g_rhi_vk->Device, &querPoolCreateInfo, nullptr, &QueryPool);
    return (res == VK_SUCCESS);
}

void jQueryPoolTime_Vulkan::ResetQueryPool(jCommandBuffer* pCommanBuffer /*= nullptr*/)
{
    vkCmdResetQueryPool((VkCommandBuffer)pCommanBuffer->GetHandle(), QueryPool, MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex, MaxQueryTimeCount);
    QueryIndex[jProfile_GPU::CurrentWatingResultListIndex] = MaxQueryTimeCount * jProfile_GPU::CurrentWatingResultListIndex;
}

void jQueryPoolTime_Vulkan::Release()
{
    ReleaseInstance();
}

int32 jQueryPoolTime_Vulkan::GetUsedQueryCount(int32 InFrameIndex) const
{
    return QueryIndex[InFrameIndex] - InFrameIndex * MaxQueryTimeCount;
}

std::vector<uint64> jQueryPoolTime_Vulkan::GetWholeQueryResult(int32 InFrameIndex, int32 InCount) const
{
    std::vector<uint64> result;
    result.resize(InCount);
    vkGetQueryPoolResults(g_rhi_vk->Device, QueryPool, InFrameIndex * MaxQueryTimeCount, InCount
        , sizeof(uint64) * InCount, result.data(), sizeof(uint64), VK_QUERY_RESULT_64_BIT);
    return result;
}

std::vector<uint64> jQueryPoolTime_Vulkan::GetWholeQueryResult(int32 InFrameIndex) const
{
    std::vector<uint64> result;
    result.resize(MaxQueryTimeCount);
    vkGetQueryPoolResults(g_rhi_vk->Device, QueryPool, InFrameIndex * MaxQueryTimeCount, MaxQueryTimeCount
        , sizeof(uint64) * MaxQueryTimeCount, result.data(), sizeof(uint64), VK_QUERY_RESULT_64_BIT);
    return result;
}

void jQueryPoolTime_Vulkan::ReleaseInstance()
{
    if (QueryPool)
    {
        vkDestroyQueryPool(g_rhi_vk->Device, QueryPool, nullptr);
        QueryPool = nullptr;
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

void jQueryTime_Vulkan::BeginQuery(const jCommandBuffer* commandBuffer) const
{
    check(commandBuffer);
    vkCmdWriteTimestamp((VkCommandBuffer)commandBuffer->GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_rhi_vk->QueryPoolTime->QueryPool, QueryId);
}

void jQueryTime_Vulkan::EndQuery(const jCommandBuffer* commandBuffer) const
{
    check(commandBuffer);
    vkCmdWriteTimestamp((VkCommandBuffer)commandBuffer->GetHandle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_rhi_vk->QueryPoolTime->QueryPool, QueryId + 1);
}

bool jQueryTime_Vulkan::IsQueryTimeStampResult(bool isWaitUntilAvailable) const
{
    uint64 time[2] = { 0, 0 };
    VkResult result = VK_RESULT_MAX_ENUM;
    if (isWaitUntilAvailable)
    {
        while (result == VK_SUCCESS)
        {
            result = (vkGetQueryPoolResults(g_rhi_vk->Device, g_rhi_vk->QueryPoolTime->QueryPool, QueryId, 2, sizeof(uint64) * 2, time, sizeof(uint64), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
        }
    }

    result = (vkGetQueryPoolResults(g_rhi_vk->Device, g_rhi_vk->QueryPoolTime->QueryPool, QueryId, 2, sizeof(uint64) * 2, time, sizeof(uint64), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
    return (result == VK_SUCCESS);
}

void jQueryTime_Vulkan::GetQueryResult() const
{
    vkGetQueryPoolResults(g_rhi_vk->Device, g_rhi_vk->QueryPoolTime->QueryPool, QueryId, 2, sizeof(uint64) * 2, TimeStampStartEnd, sizeof(uint64), VK_QUERY_RESULT_64_BIT);
}

void jQueryTime_Vulkan::GetQueryResultFromQueryArray(int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryArray) const
{
    const uint32 queryStart = (QueryId) - InWatingResultIndex * MaxQueryTimeCount;
    const uint32 queryEnd = (QueryId + 1) - InWatingResultIndex * MaxQueryTimeCount;
    check(queryStart >= 0 && queryStart < MaxQueryTimeCount);
    check(queryEnd >= 0 && queryEnd < MaxQueryTimeCount);

    TimeStampStartEnd[0] = wholeQueryArray[queryStart];
    TimeStampStartEnd[1] = wholeQueryArray[queryEnd];
}