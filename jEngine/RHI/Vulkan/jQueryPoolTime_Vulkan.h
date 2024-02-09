#pragma once
#include "../jRHI.h"

extern const uint32 MaxQueryTimeCount;

//////////////////////////////////////////////////////////////////////////
// jQueryPoolTime_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jQueryPoolTime_Vulkan : public jQueryPool
{
    jQueryPoolTime_Vulkan(ECommandBufferType InType) : CommandBufferType(InType) {}
    virtual ~jQueryPoolTime_Vulkan() 
    {
        ReleaseInstance();
    }

    virtual bool Create() override;
    virtual void ResetQueryPool(jCommandBuffer* pCommanBuffer = nullptr);
    virtual void Release() override;
    virtual void* GetHandle() const override { return QueryPool; }
    virtual int32 GetUsedQueryCount(int32 InFrameIndex) const override;

    virtual bool CanWholeQueryResult() const override { return true; }
    virtual std::vector<uint64> GetWholeQueryResult(int32 InFrameIndex, int32 InCount) const override;
    virtual std::vector<uint64> GetWholeQueryResult(int32 InFrameIndex) const override;

    void ReleaseInstance();

    ECommandBufferType CommandBufferType = ECommandBufferType::GRAPHICS;
    VkQueryPool QueryPool = nullptr;
    int32 QueryIndex[jRHI::MaxWaitingQuerySet] = { 0, };
};

//////////////////////////////////////////////////////////////////////////
// jQueryTime_Vulkan
//////////////////////////////////////////////////////////////////////////
struct jQueryTime_Vulkan : public jQuery
{
    jQueryTime_Vulkan(ECommandBufferType InCmdBufferType) : CmdBufferType(InCmdBufferType) {}
    virtual ~jQueryTime_Vulkan() {}
    virtual void Init() override;
    
    virtual void BeginQuery(const jCommandBuffer* commandBuffer) const override;
    virtual void EndQuery(const jCommandBuffer* commandBuffer) const override;

    virtual bool IsQueryTimeStampResult(bool isWaitUntilAvailable) const override;
    virtual void GetQueryResult() const override;
    virtual void GetQueryResultFromQueryArray(int32 InWatingResultIndex, const std::vector<uint64>& wholeQueryArray) const override;

    virtual uint64 GetElpasedTime() const override { return TimeStampStartEnd[1] - TimeStampStartEnd[0]; }

    mutable uint64 TimeStampStartEnd[2] = { 0, 0 };
    uint32 QueryId = 0;
    ECommandBufferType CmdBufferType = ECommandBufferType::GRAPHICS;
};