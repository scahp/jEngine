﻿#include "pch.h"
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
    if (LastFenceValue == uint64(-1))
        return;

    check(Fence);
    if (Fence->GetCompletedValue() > LastFenceValue)
        return;

    check(FenceEvent);
    WaitForSingleObjectEx(FenceEvent, (DWORD)(InTimeoutNanoSec / 1000000), false);;
    LastFenceValue = uint64(-1);
}

bool jFence_DX12::SetFenceValue(uint64 InFenceValue)
{
    LastFenceValue = InFenceValue;
    if (Fence->GetCompletedValue() > InFenceValue)
        return false;

    JFAIL(Fence->SetEventOnCompletion(InFenceValue, FenceEvent));
    return true;
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
    if (JOK(g_rhi_dx12->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&newFence->Fence))))
    {
        newFence->FenceEvent = CreateEvent(nullptr, false, false, nullptr);
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