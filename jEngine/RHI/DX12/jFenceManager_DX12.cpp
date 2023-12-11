#include "pch.h"
#include "jFenceManager_DX12.h"

void jFence_DX12::Release()
{
    if (Fence)
    {
        Fence->Release();
        Fence = nullptr;
    }

    if (FenceEvent)
    {
        CloseHandle(FenceEvent);
        FenceEvent = nullptr;
    }
}

void jFence_DX12::WaitForFence(uint64 InTimeoutNanoSec)
{
    WaitForFenceValue(FenceValue, InTimeoutNanoSec);
}

void jFence_DX12::WaitForFenceValue(uint64 InFenceValue, uint64 InTimeoutNanoSec)
{
    if (InFenceValue == InitialFenceValue)
        return;

    check(Fence);
    if (UINT64_MAX == InTimeoutNanoSec)
    {
        while (Fence->GetCompletedValue() < InFenceValue) 
        {
            Sleep(0);
        }
    }
    else
    {
        std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();

        while (Fence->GetCompletedValue() < InFenceValue)
        {
            std::chrono::nanoseconds elapsed_nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now() - lastTime);
            if (elapsed_nanoseconds.count() >= (int64)InTimeoutNanoSec)
                return;

            Sleep(0);
        }
    }
}

bool jFence_DX12::IsComplete() const
{
    return (Fence->GetCompletedValue() >= FenceValue);
}

bool jFence_DX12::IsComplete(uint64 InFenceValue) const
{
    return (Fence->GetCompletedValue() >= InFenceValue);
}

uint64 jFence_DX12::SignalWithNextFenceValue(ID3D12CommandQueue* InCommandQueue, bool bWaitUntilExecuteComplete)
{
    {
        jScopedLock s(&FenceValueLock);

        const auto NewFenceValue = FenceValue + 1;
        if (JFAIL(InCommandQueue->Signal(Fence.Get(), NewFenceValue)))
            return FenceValue;

        FenceValue = NewFenceValue;
    }

    JFAIL(Fence->SetEventOnCompletion(FenceValue, FenceEvent));
    if (bWaitUntilExecuteComplete)
    {
        WaitForSingleObjectEx(FenceEvent, INFINITE, false);
    }
    return FenceValue;
}

jFence* jFenceManager_DX12::GetOrCreateFence()
{
    if (PendingFences.size() > 0)
    {
        jFence* fence = *PendingFences.begin();
        PendingFences.erase(PendingFences.begin());
        UsingFences.insert(fence);
        return fence;
    }

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    jFence_DX12* newFence = new jFence_DX12();
    if (JOK(g_rhi_dx12->Device->CreateFence(jFence_DX12::InitialFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&newFence->Fence))))
    {
        newFence->FenceEvent = ::CreateEvent(nullptr, false, false, nullptr);
        if (newFence->FenceEvent)
        {
            UsingFences.insert(newFence);
            return newFence;
        }
    }
    check(0);

    delete newFence;
    return nullptr;
}

void jFenceManager_DX12::Release()
{
    for (auto& iter : UsingFences)
    {
        iter->Release();
    }
    UsingFences.clear();

    for (auto& iter : PendingFences)
    {
        iter->Release();
    }
    PendingFences.clear();
}
