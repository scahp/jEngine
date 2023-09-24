#pragma once
#include "jRHIType_DX12.h"

struct jRingBuffer_DX12;

struct jDescriptor_DX12
{
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = {};
    uint32 Index = uint32(-1);
    class jDescriptorHeap_DX12* DescriptorHeap = nullptr;

    void Free();

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

        Descriptor.DescriptorHeap = this;
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
    void ProcessPendingDescriptorPoolFree();

    // 이번 프레임에만 사용할 Descriptor 생성
    //jDescriptor_DX12 OneFrameCreateConstantBufferView(jRingBuffer_DX12* InBuffer, uint64 InOffset, uint32 InSize, ETextureFormat InFormat = ETextureFormat::MAX);
    //jDescriptor_DX12 OneFrameCreateShaderResourceView(jRingBuffer_DX12* InBuffer, uint64 InOffset, uint32 InStride, uint32 InNumOfElement, ETextureFormat InFormat = ETextureFormat::MAX);

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

struct jDescriptorBlock_DX12
{
    static constexpr int32 MaxDescriptorsInBlock = 2000;
    class jOnlineDescriptorHeapBlocks_DX12* DescriptorHeapBlocks = nullptr;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;
    int32 Index = 0;
    int32 AllocatedSize = 0;
    jDescriptor_DX12 Descriptors[MaxDescriptorsInBlock];
};

class jOnlineDescriptorHeap_DX12;
class jOnlineDescriptorHeapBlocks_DX12
{
public:
    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;

    void Initialize(EDescriptorHeapTypeDX12 InHeapType, uint32 InNumOfDescriptorsInBlock = jDescriptorBlock_DX12::MaxDescriptorsInBlock, uint32 InNumOfBlock = 100);
    void Release();

    jOnlineDescriptorHeap_DX12* Alloc()
    {
        jScopedLock s(&DescriptorBlockLock);

        if (FreeLists.empty())
            return nullptr;

        uint32 FreeListIndex = *FreeLists.begin();
        FreeLists.erase(FreeLists.begin());

        return OnlineDescriptorHeap[FreeListIndex];
    }
    void Free(uint32 InIndex)
    {
        jScopedLock s(&DescriptorBlockLock);

        check(!FreeLists.contains(InIndex));
        FreeLists.insert(InIndex);
    }

    ComPtr<ID3D12DescriptorHeap> Heap;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandleStart = { };
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandleStart = { };
    uint32 DescriptorSize = 0;
    uint32 NumOfDescriptors = 0;
    std::set<uint32> FreeLists;
    
    std::vector<jOnlineDescriptorHeap_DX12*> OnlineDescriptorHeap;
    std::vector<jDescriptorBlock_DX12> DescriptorBlocks;

    mutable jMutexLock DescriptorBlockLock;
};

class jOnlineDescriptorHeap_DX12
{
public:
    void Initialize(jDescriptorBlock_DX12* InDescriptorBlocks)
    {
        DescriptorBlocks = InDescriptorBlocks;
        if (DescriptorBlocks)
        {
            CPUHandle = DescriptorBlocks->Descriptors[0].CPUHandle;
            GPUHandle = DescriptorBlocks->Descriptors[0].GPUHandle;
            Heap = DescriptorBlocks->DescriptorHeapBlocks->Heap.Get();
        }
    }
    void Release()
    {
        if (DescriptorBlocks)
        {
            check(DescriptorBlocks->DescriptorHeapBlocks);
            DescriptorBlocks->DescriptorHeapBlocks->Free(DescriptorBlocks->Index);
            DescriptorBlocks = nullptr;
        }
    }
    jDescriptor_DX12 Alloc()
    {
        if (NumOfAllocated < jDescriptorBlock_DX12::MaxDescriptorsInBlock)
            return DescriptorBlocks->Descriptors[NumOfAllocated++];

        return jDescriptor_DX12();
    }
    void Reset()
    {
        NumOfAllocated = 0;
    }
    int32 GetNumOfAllocated() const { return NumOfAllocated; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(size_t InIndex) const { return D3D12_GPU_DESCRIPTOR_HANDLE(GPUHandle.ptr + InIndex * DescriptorBlocks->DescriptorHeapBlocks->DescriptorSize); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return CPUHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const { return GPUHandle; }
    ID3D12DescriptorHeap* GetHeap() const { return Heap; }

private:
    ID3D12DescriptorHeap* Heap = nullptr;
    jDescriptorBlock_DX12* DescriptorBlocks = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = { };
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = { };
    int32 NumOfAllocated = 0;
};

class jOfflineDescriptorHeap_DX12
{
public:
    void Initialize(EDescriptorHeapTypeDX12 InHeapType)
    {
        check(!IsInitialized);

        HeapType = InHeapType;
        CurrentHeap = CreateDescriptorHeap();
        IsInitialized = true;
    }

    jDescriptor_DX12 Alloc()
    {
        if (!IsInitialized)
            return jDescriptor_DX12();

        if (!CurrentHeap)
        {
            CurrentHeap = CreateDescriptorHeap();
            check(CurrentHeap);
        }

        jDescriptor_DX12 NewDescriptor = CurrentHeap->Alloc();
        if (!NewDescriptor.IsValid())
        {
            CurrentHeap = CreateDescriptorHeap();
            check(CurrentHeap);

            NewDescriptor = CurrentHeap->Alloc();
            check(NewDescriptor.IsValid());
        }

        return NewDescriptor;
    }

    void Free(const jDescriptor_DX12& InDescriptor)
    {
        check(InDescriptor.DescriptorHeap);
        InDescriptor.DescriptorHeap->Free(InDescriptor.Index);
    }

    void Release()
    {
        if (!IsInitialized)
            return;

        for (int32 i = 0; i < Heap.size(); ++i)
        {
            delete Heap[i];
        }
        Heap.clear();
    }

private:
    jDescriptorHeap_DX12* CreateDescriptorHeap()
    {
        auto DescriptorHeap = new jDescriptorHeap_DX12();
        check(DescriptorHeap);

        DescriptorHeap->Initialize(HeapType, false);

        Heap.push_back(DescriptorHeap);
        return DescriptorHeap;
    }

    bool IsInitialized = false;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;

    jDescriptorHeap_DX12* CurrentHeap = nullptr;
    std::vector<jDescriptorHeap_DX12*> Heap;
};