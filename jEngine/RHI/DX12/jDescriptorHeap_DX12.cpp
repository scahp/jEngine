#include "pch.h"
#include "jDescriptorHeap_DX12.h"

void jDescriptorHeap_DX12::Initialize(EDescriptorHeapTypeDX12 InHeapType, bool InShaderVisible, uint32 InNumOfDescriptors /*= 1024*/)
{
    jScopedLock s(&DescriptorLock);

    check(!InShaderVisible || (InShaderVisible && (InHeapType != EDescriptorHeapTypeDX12::RTV && InHeapType != EDescriptorHeapTypeDX12::DSV)));

    const auto HeapTypeDX12 = GetDX12DescriptorHeapType(InHeapType);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { };
    heapDesc.NumDescriptors = InNumOfDescriptors;
    heapDesc.Type = HeapTypeDX12;
    heapDesc.Flags = InShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    if (JFAIL(g_rhi_dx12->Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&Heap))))
        return;

    CPUHandleStart = Heap->GetCPUDescriptorHandleForHeapStart();
    if (InShaderVisible)
        GPUHandleStart = Heap->GetGPUDescriptorHandleForHeapStart();
    else
        GPUHandleStart.ptr = (UINT64)-1;

    DescriptorSize = g_rhi_dx12->Device->GetDescriptorHandleIncrementSize(HeapTypeDX12);
    NumOfDescriptors = InNumOfDescriptors;

    for (uint32 i = 0; i < NumOfDescriptors; ++i)
    {
        Pools.insert(i);
    }
}

void jDescriptorHeap_DX12::Release()
{
    jScopedLock s(&DescriptorLock);

    Heap->Release();
    CPUHandleStart = { };
    GPUHandleStart = { };
    DescriptorSize = 0;
    NumOfDescriptors = 0;
    Pools.clear();
}
