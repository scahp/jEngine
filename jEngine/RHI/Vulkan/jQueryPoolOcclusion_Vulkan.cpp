#include "pch.h"
#include "jQueryPoolOcclusion_Vulkan.h"
#include "Profiler/jPerformanceProfile.h"

const uint32 MaxQueryOcclusionCount = 512;

//////////////////////////////////////////////////////////////////////////
// jQueryPoolOcclusion_Vulkan
//////////////////////////////////////////////////////////////////////////
bool jQueryPoolOcclusion_Vulkan::Create()
{
    VkQueryPoolCreateInfo querPoolCreateInfo = {};
    querPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    querPoolCreateInfo.pNext = nullptr;
    querPoolCreateInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
    querPoolCreateInfo.queryCount = MaxQueryOcclusionCount * jRHI::MaxWaitingQuerySet;

    for (int32 i = 0; i < jRHI::MaxWaitingQuerySet; ++i)
        QueryIndex[i] = MaxQueryOcclusionCount * i;

    const VkResult res = vkCreateQueryPool(g_rhi_vk->Device, &querPoolCreateInfo, nullptr, &QueryPool);
    return (res == VK_SUCCESS);
}

void jQueryPoolOcclusion_Vulkan::ResetQueryPool(jCommandBuffer* pCommanBuffer /*= nullptr*/)
{
    vkCmdResetQueryPool((VkCommandBuffer)pCommanBuffer->GetHandle(), QueryPool, MaxQueryOcclusionCount * g_rhi_vk->CurrenFrameIndex, MaxQueryOcclusionCount);
    QueryIndex[g_rhi_vk->CurrenFrameIndex] = MaxQueryOcclusionCount * g_rhi_vk->CurrenFrameIndex;
}

void jQueryPoolOcclusion_Vulkan::Release()
{
    ReleaseInstance();
}

std::vector<uint64> jQueryPoolOcclusion_Vulkan::GetWholeQueryResult(int32 InFrameIndex, int32 InCount) const
{
    std::vector<uint64> queryResult;
    if (InCount <= 0)
        return queryResult;

    queryResult.resize(InCount);
    VkResult result = vkGetQueryPoolResults(g_rhi_vk->Device, QueryPool, InFrameIndex * MaxQueryOcclusionCount, InCount
        , sizeof(uint64) * InCount, queryResult.data(), sizeof(uint64), VK_QUERY_RESULT_64_BIT);
    ensure(result == VK_SUCCESS);
    return queryResult;
}

std::vector<uint64> jQueryPoolOcclusion_Vulkan::GetWholeQueryResult(int32 InFrameIndex) const
{
    std::vector<uint64> queryResult;
    queryResult.resize(MaxQueryOcclusionCount);
    VkResult result = vkGetQueryPoolResults(g_rhi_vk->Device, QueryPool, InFrameIndex * MaxQueryOcclusionCount, MaxQueryOcclusionCount
        , sizeof(uint64) * MaxQueryOcclusionCount, queryResult.data(), sizeof(uint64), VK_QUERY_RESULT_64_BIT);
    ensure(result == VK_SUCCESS);
    return queryResult;
}

int32 jQueryPoolOcclusion_Vulkan::GetUsedQueryCount(int32 InFrameIndex) const
{
    return QueryIndex[InFrameIndex] - InFrameIndex * MaxQueryOcclusionCount;
}

//////////////////////////////////////////////////////////////////////////
// jQueryOcclusion_Vulkan
//////////////////////////////////////////////////////////////////////////
void jQueryOcclusion_Vulkan::Init()
{
    QueryId = g_rhi_vk->QueryPoolOcclusion->QueryIndex[g_rhi_vk->CurrenFrameIndex];
    g_rhi_vk->QueryPoolOcclusion->QueryIndex[g_rhi_vk->CurrenFrameIndex] += 1;		// Need 1 Query for sample count.

    Result = 0;
}

void jQueryPoolOcclusion_Vulkan::ReleaseInstance()
{
    if (QueryPool)
    {
        vkDestroyQueryPool(g_rhi_vk->Device, QueryPool, nullptr);
        QueryPool = nullptr;
    }
}

void jQueryOcclusion_Vulkan::BeginQuery(const jCommandBuffer* commandBuffer) const
{
    vkCmdBeginQuery((VkCommandBuffer)commandBuffer->GetHandle(), (VkQueryPool)g_rhi_vk->QueryPoolOcclusion->GetHandle(), QueryId, 0);
}

void jQueryOcclusion_Vulkan::EndQuery(const jCommandBuffer* commandBuffer) const
{
    vkCmdEndQuery((VkCommandBuffer)commandBuffer->GetHandle(), (VkQueryPool)g_rhi_vk->QueryPoolOcclusion->GetHandle(), QueryId);
}

uint64 jQueryOcclusion_Vulkan::GetQueryResult()
{
    vkGetQueryPoolResults(g_rhi_vk->Device, g_rhi_vk->QueryPoolOcclusion->QueryPool, QueryId, 1, sizeof(uint64), &Result, sizeof(uint64), VK_QUERY_RESULT_64_BIT);
    return Result;
}

void jQueryOcclusion_Vulkan::GetQueryResultFromQueryArray(int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryTimeStampArray) const
{
    if (wholeQueryTimeStampArray.size() && (QueryId >= InWatingResultIndex * MaxQueryOcclusionCount))
    {
        const uint32 queryIndex = QueryId - InWatingResultIndex * MaxQueryOcclusionCount;
        Result = wholeQueryTimeStampArray[queryIndex];
    }
}