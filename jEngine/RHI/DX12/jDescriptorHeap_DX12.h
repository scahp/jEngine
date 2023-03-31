#pragma once
#include "jRHIType_DX12.h"

struct jDescriptor_DX12
{
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = {};
    uint32 Index = uint32(-1);

    bool IsValid() const { return (Index != -1); }
};

class jDescriptorHeap_DX12
{
public:
    void Initialize(EDescriptorHeapTypeDX12 InHeapType, bool InShaderVisible, uint32 InNumOfDescriptors = 1024);
    void Release();

    jDescriptor_DX12 Alloc()
    {
        jScopedLock s(&DescriptorLock);

        if (Pools.empty())
            return jDescriptor_DX12();

        jDescriptor_DX12 Descriptor;
        Descriptor.Index = *Pools.begin();
        Pools.erase(Pools.begin());

        Descriptor.CPUHandle = CPUHandleStart;
        Descriptor.CPUHandle.ptr += Descriptor.Index * DescriptorSize;

        Descriptor.GPUHandle = GPUHandleStart;
        Descriptor.GPUHandle.ptr += Descriptor.Index * DescriptorSize;
        return Descriptor;
    }
    void Free(uint32 Index)
    {
        jScopedLock s(&DescriptorLock);

        check(!Pools.contains(Index));
        Pools.insert(Index);
    }

    ComPtr<ID3D12DescriptorHeap> Heap;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandleStart = { };
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandleStart = { };
    uint32 DescriptorSize = 0;
    uint32 NumOfDescriptors = 0;
    std::set<uint32> Pools;
    mutable jMutexLock DescriptorLock;
};
