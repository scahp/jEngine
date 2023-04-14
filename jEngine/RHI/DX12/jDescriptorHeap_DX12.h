#pragma once
#include "jRHIType_DX12.h"

struct jRingBuffer_DX12;

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
    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;
    struct PendingForFree
    {
        uint32 DescriptorIndex = UINT_MAX;
        uint32 FrameIndex = 0;
    };

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
    jDescriptor_DX12 OneFrameAlloc()
    {
        jDescriptor_DX12 NewAlloc = Alloc();
        Free(NewAlloc.Index, NumOfFramesToWaitBeforeReleasing);
        return NewAlloc;
    }
    void Free(uint32 InIndex)
    {
        jScopedLock s(&DescriptorLock);

        check(!Pools.contains(InIndex));
        Pools.insert(InIndex);
    }
    void Free(uint32 InIndex, uint32 InDelayFrames);

    // 이번 프레임에만 사용할 Descriptor 생성
    jDescriptor_DX12 OneFrameCreateConstantBufferView(jRingBuffer_DX12* InBuffer, uint64 InOffset, uint32 InSize, ETextureFormat InFormat = ETextureFormat::MAX);
    jDescriptor_DX12 OneFrameCreateShaderResourceView(jRingBuffer_DX12* InBuffer, uint64 InOffset, uint32 InStride, uint32 InNumOfElement, ETextureFormat InFormat = ETextureFormat::MAX);

    ComPtr<ID3D12DescriptorHeap> Heap;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandleStart = { };
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandleStart = { };
    uint32 DescriptorSize = 0;
    uint32 NumOfDescriptors = 0;
    std::set<uint32> Pools;
    mutable jMutexLock DescriptorLock;

    std::vector<PendingForFree> PendingFree;
    int32 CanReleasePendingFreeShaderBindingInstanceFrameNumber = 0;
};
