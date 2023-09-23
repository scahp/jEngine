#pragma once
#include "../jFenceManager.h"

class jFence_Vulkan : public jFence
{
public:
    virtual ~jFence_Vulkan() {}
    virtual void* GetHandle() const override { return Fence; }
    virtual void Release() override;
    virtual void WaitForFence(uint64 InTimeoutNanoSec = UINT64_MAX) override;
    virtual bool IsValid() const override { return !!Fence; }
    virtual bool IsComplete() const override;

    VkFence Fence = nullptr;
};

class jFenceManager_Vulkan : public jFenceManager
{
public:
    virtual jFence* GetOrCreateFence() override;
    virtual void ReturnFence(jFence* fence) override
    {
        UsingFences.erase(fence);
        PendingFences.insert(fence);
    }
    virtual void Release() override;

    robin_hood::unordered_set<jFence*> UsingFences;
    robin_hood::unordered_set<jFence*> PendingFences;
};
