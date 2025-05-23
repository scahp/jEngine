﻿#pragma once
#include "../jFenceManager.h"

class jFence_DX12 : public jFence
{
public:
    constexpr static uint64 InitialFenceValue = uint64(0);

    virtual ~jFence_DX12() {}
    virtual void* GetHandle() const override { return Fence.Get(); }
    virtual void Release() override;
    virtual void WaitForFence(uint64 InTimeoutNanoSec = UINT64_MAX) override;
    virtual bool IsValid() const override { return (Fence && FenceEvent); }
    virtual bool IsComplete() const override;
    bool IsComplete(uint64 InFenceValue) const;
    void WaitForFenceValue(uint64 InFenceValue, uint64 InTimeoutNanoSec = UINT64_MAX);

    uint64 SignalWithNextFenceValue(ID3D12CommandQueue* InCommandQueue, bool bWaitUntilExecuteComplete = false);

    ComPtr<ID3D12Fence> Fence;
    HANDLE FenceEvent = nullptr;

    jMutexLock FenceValueLock;
    uint64 FenceValue = InitialFenceValue;
};

class jFenceManager_DX12 : public jFenceManager
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
