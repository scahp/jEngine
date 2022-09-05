#pragma once
#include "../jRHI.h"

extern const uint32 MaxQueryOcclusionCount;

//////////////////////////////////////////////////////////////////////////
// jQueryPoolOcclusion_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jQueryPoolOcclusion_Vulkan : public jQueryPool
{
    virtual ~jQueryPoolOcclusion_Vulkan()
    {
        ReleaseInstance();
    }

    virtual bool Create() override;
    virtual void ResetQueryPool(jCommandBuffer* pCommanBuffer = nullptr);
    virtual void Release() override;
    virtual void* GetHandle() const override { return QueryPool; }

    virtual bool CanWholeQueryResult() const override { return true; }
    virtual std::vector<uint64> GetWholeQueryResult(int32 InFrameIndex, int32 InCount) const override;
    virtual std::vector<uint64> GetWholeQueryResult(int32 InFrameIndex) const override;

    virtual int32 GetUsedQueryCount(int32 InFrameIndex) const override;

    void ReleaseInstance();

    VkQueryPool QueryPool = nullptr;
    int32 QueryIndex[jRHI::MaxWaitingQuerySet] = { 0, };
};

//////////////////////////////////////////////////////////////////////////
// jQueryOcclusion_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jQueryOcclusion_Vulkan : public jQuery
{
    virtual ~jQueryOcclusion_Vulkan() {}
    virtual void Init() override;

    virtual void BeginQuery(const jCommandBuffer* commandBuffer) const override;
    virtual void EndQuery(const jCommandBuffer* commandBuffer) const override;
    virtual void GetQueryResultFromQueryArray(int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryArray) const override;

    uint64 GetQueryResult();

    uint32 QueryId = 0;
    mutable uint64 Result = 0;
};
