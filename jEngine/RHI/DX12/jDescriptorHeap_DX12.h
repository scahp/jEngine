#pragma once
#include "jRHIType_DX12.h"

struct jRingBuffer_DX12;

struct jDescriptor_DX12
{
    static const jDescriptor_DX12 Invalid;

    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = {};
    uint32 Index = uint32(-1);
    std::weak_ptr<class jDescriptorHeap_DX12> DescriptorHeap;

    void Free();
    bool IsValid() const { return (Index != -1); }
};

class jDescriptorHeap_DX12 : public std::enable_shared_from_this<jDescriptorHeap_DX12>
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

        Descriptor.DescriptorHeap = shared_from_this();
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
    class jOnlineDescriptorHeapBlocks_DX12* DescriptorHeapBlocks = nullptr;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;
    int32 Index = 0;
    int32 AllocatedSize = 0;
    std::vector<jDescriptor_DX12> Descriptors;
};

class jOnlineDescriptorHeap_DX12;

// 한개의 Heap 을 여러개의 Block 으로 쪼개서 관리하는 클래스
// - Block 이름은 jOnlineDescriptorHeap_DX12 임.
class jOnlineDescriptorHeapBlocks_DX12
{
public:
    static constexpr int32 DescriptorsInBlock = 5000;
    static constexpr int32 TotalHeapSize = 500000;
    static constexpr int32 SamplerDescriptorsInBlock = 100;
    static constexpr int32 SamplerTotalHeapSize = 2000;

    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;

    struct jFreeData
    {
        bool IsValid() const
        {
            return Index != -1;
        }
        uint32 ReleasedFrame = 0;
        int32 Index = -1;
    };

    struct jFreeDataLessReleasedFrameFirstComp
    {
        bool operator() (const jFreeData& InA, const jFreeData& InB) const
        {
            return ((uint64)InA.ReleasedFrame << 32 | (uint64)InA.Index) < ((uint64)InB.ReleasedFrame << 32 | (uint64)InB.Index);
        }
    };

    void Initialize(EDescriptorHeapTypeDX12 InHeapType, uint32 InTotalHeapSize, uint32 InDescriptorsInBlock);
    void Release();

    jOnlineDescriptorHeap_DX12* Alloc();
    void Free(int32 InIndex);

    ComPtr<ID3D12DescriptorHeap> Heap;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandleStart = { };
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandleStart = { };
    uint32 DescriptorSize = 0;
    uint32 NumOfDescriptors = 0;
    std::set<jFreeData, jFreeDataLessReleasedFrameFirstComp> FreeLists;
    
    std::vector<jOnlineDescriptorHeap_DX12*> OnlineDescriptorHeap;
    std::vector<jDescriptorBlock_DX12> DescriptorBlocks;

    mutable jMutexLock DescriptorBlockLock;
};

// CommandList 당 한개씩 가지게 되는 OnlineDescriptorHeap, jOnlineDescriptorHeapBlocks_DX12 으로 부터 할당 받음.
// - CommandList 당 Block 을 할당하는 방식으로 여러개의 CommandList 가 OnlineDescriptor 에서 Allocation 경쟁 하는 부분을 피함.
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
        }
    }
    jDescriptor_DX12 Alloc()
    {
        if (NumOfAllocated < DescriptorBlocks->Descriptors.size())
            return DescriptorBlocks->Descriptors[NumOfAllocated++];

        return jDescriptor_DX12();
    }
    void Reset()
    {
        NumOfAllocated = 0;
    }
    bool CanAllocate(int32 InSize) const
    {
        return (DescriptorBlocks->Descriptors.size() - NumOfAllocated) >= InSize;
    }
    int32 GetNumOfAllocated() const { return NumOfAllocated; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(size_t InIndex) const { return D3D12_GPU_DESCRIPTOR_HANDLE(GPUHandle.ptr + InIndex * DescriptorBlocks->DescriptorHeapBlocks->DescriptorSize); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return CPUHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const { return GPUHandle; }
    ID3D12DescriptorHeap* GetHeap() const { return Heap; }

    uint32 GetDescriptorSize() const 
    {
        check(DescriptorBlocks);
        check(DescriptorBlocks->DescriptorHeapBlocks);
        return DescriptorBlocks->DescriptorHeapBlocks->DescriptorSize;
    }

private:
    ID3D12DescriptorHeap* Heap = nullptr;
    jDescriptorBlock_DX12* DescriptorBlocks = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = { };
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = { };
    int32 NumOfAllocated = 0;
};

// OnlineDescriptorHeapBlock 을 관리하는 객체, 필요한 경우 추가 DescriptorHeapBlock 을 할당함.
class jOnlineDescriptorManager
{
public:
    jOnlineDescriptorHeap_DX12* Alloc(EDescriptorHeapTypeDX12 InType)
    {
        std::vector<jOnlineDescriptorHeapBlocks_DX12*>& DescriptorHeapBlocks = OnlineDescriptorHeapBlocks[(int32)InType];

        // 기존 HeapBlock 에서 할당 가능한지 확인
        for (int32 i = 0; i < (int32)DescriptorHeapBlocks.size(); ++i)
        {
            check(DescriptorHeapBlocks[i]);
            jOnlineDescriptorHeap_DX12* AllocatedBlocks = DescriptorHeapBlocks[i]->Alloc();
            if (AllocatedBlocks)
            {
                return AllocatedBlocks;
            }
        }

        // 기존 HeapBlock 이 가득 찬 상태라 HeapBlock 추가
        auto SelectedHeapBlocks = new jOnlineDescriptorHeapBlocks_DX12();

        switch (InType)
        {
        case EDescriptorHeapTypeDX12::CBV_SRV_UAV:
        {
            SelectedHeapBlocks->Initialize(EDescriptorHeapTypeDX12::CBV_SRV_UAV
                , jOnlineDescriptorHeapBlocks_DX12::TotalHeapSize, jOnlineDescriptorHeapBlocks_DX12::DescriptorsInBlock);
            break;
        }
        case EDescriptorHeapTypeDX12::SAMPLER:
        {
            SelectedHeapBlocks->Initialize(EDescriptorHeapTypeDX12::SAMPLER
                , jOnlineDescriptorHeapBlocks_DX12::SamplerTotalHeapSize, jOnlineDescriptorHeapBlocks_DX12::SamplerDescriptorsInBlock);
            break;
        }
        default:
            check(0);
            break;
        }

        DescriptorHeapBlocks.push_back(SelectedHeapBlocks);

        // 할당 할 수 있는 HeapBlock 이 더 많은 것을 앞쪽에 배치함
        std::sort(DescriptorHeapBlocks.begin(), DescriptorHeapBlocks.end(), [](jOnlineDescriptorHeapBlocks_DX12* InA, jOnlineDescriptorHeapBlocks_DX12* InB){
            return InA->FreeLists.size() > InB->FreeLists.size();
        });

        jOnlineDescriptorHeap_DX12* AllocatedBlocks = SelectedHeapBlocks->Alloc();
        check(AllocatedBlocks);
        return AllocatedBlocks;
    }
    void Free(jOnlineDescriptorHeap_DX12* InDescriptorHeap)
    {
        check(InDescriptorHeap);
        InDescriptorHeap->Release();
    }

    void Release()
    {
        for(int32 i=0;i<(int32)EDescriptorHeapTypeDX12::MAX;++i)
        {
            for(jOnlineDescriptorHeapBlocks_DX12* iter : OnlineDescriptorHeapBlocks[i])
            {
                delete iter;
            }
            OnlineDescriptorHeapBlocks[i].clear();
        }
    }

    std::vector<jOnlineDescriptorHeapBlocks_DX12*> OnlineDescriptorHeapBlocks[(int32)EDescriptorHeapTypeDX12::MAX];
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
            if (Heap.size() > 0)
            {
                // 할당 할 수 있는 Heap 이 더 많은 것을 앞쪽에 배치함
                std::sort(Heap.begin(), Heap.end(), [](const std::shared_ptr<jDescriptorHeap_DX12>& InA, const std::shared_ptr<jDescriptorHeap_DX12>& InB) {
                    return InA->Pools.size() > InB->Pools.size();
                    });

                if (Heap[0]->Pools.size() > 0)
                {
                    CurrentHeap = Heap[0];

                    NewDescriptor = CurrentHeap->Alloc();
                    check(NewDescriptor.IsValid());
                    return NewDescriptor;
                }
            }

            CurrentHeap = CreateDescriptorHeap();
            check(CurrentHeap);

            NewDescriptor = CurrentHeap->Alloc();
            check(NewDescriptor.IsValid());
        }

        return NewDescriptor;
    }

    void Free(const jDescriptor_DX12& InDescriptor)
    {
        if (!InDescriptor.DescriptorHeap.expired())
            InDescriptor.DescriptorHeap.lock()->Free(InDescriptor.Index);
    }

    void Release()
    {
        if (!IsInitialized)
            return;

        Heap.clear();
    }

private:
    std::shared_ptr<jDescriptorHeap_DX12> CreateDescriptorHeap()
    {
        auto DescriptorHeap = std::make_shared<jDescriptorHeap_DX12>();
        check(DescriptorHeap);

        DescriptorHeap->Initialize(HeapType, false);

        Heap.push_back(DescriptorHeap);
        return DescriptorHeap;
    }

    bool IsInitialized = false;
    EDescriptorHeapTypeDX12 HeapType = EDescriptorHeapTypeDX12::CBV_SRV_UAV;

    std::shared_ptr<jDescriptorHeap_DX12> CurrentHeap;
    std::vector<std::shared_ptr<jDescriptorHeap_DX12>> Heap;
};