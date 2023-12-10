#include "pch.h"
#include "jMemoryPool.h"

void* jMemory::GetMappedPointer() const
{
    return SubMemoryAllocator->GetMappedPointer();
}

void* jMemory::GetMemory() const
{
    return SubMemoryAllocator->GetMemory();
}

//////////////////////////////////////////////////////////////////////////
// jMemory
void jMemory::Free()
{
    g_rhi->GetMemoryPool()->Free(*this);
    Reset();
}

void jMemory::Reset()
{
    Buffer = nullptr;
    Range.Offset = 0;
    Range.DataSize = 0;
    SubMemoryAllocator = nullptr;
}

//////////////////////////////////////////////////////////////////////////
// jSubMemoryAllocator
jMemory jSubMemoryAllocator::Alloc(uint64 InRequstedSize)
{
    jScopedLock s(&Lock);

    jMemory AllocMem;
    const uint64 AlignedRequestedSize = (Alignment > 0) ? Align(InRequstedSize, Alignment) : InRequstedSize;

    for (int32 i = 0; i < (int32)FreeLists.size(); ++i)
    {
        if (FreeLists[i].DataSize >= AlignedRequestedSize)
        {
            AllocMem.Buffer = GetBuffer();
            check(AllocMem.Buffer);

            AllocMem.Range.Offset = FreeLists[i].Offset;
            AllocMem.Range.DataSize = AlignedRequestedSize;
            AllocMem.SubMemoryAllocator = this;
            FreeLists.erase(FreeLists.begin() + i);
            return AllocMem;
        }
    }

    if ((SubMemoryRange.Offset + AlignedRequestedSize) <= SubMemoryRange.DataSize)
    {
        AllocMem.Buffer = GetBuffer();
        check(AllocMem.Buffer);

        AllocMem.Range.Offset = (Alignment > 0) ? Align(SubMemoryRange.Offset, Alignment) : SubMemoryRange.Offset;
        AllocMem.Range.DataSize = AlignedRequestedSize;
        AllocMem.SubMemoryAllocator = this;
        
        SubMemoryRange.Offset += AlignedRequestedSize;
        AllAllocatedLists.push_back(AllocMem.Range);

        check(AllocMem.Range.Offset + AllocMem.Range.DataSize <= SubMemoryRange.DataSize);
    }
    return AllocMem;
}

jMemory jMemoryPool::Alloc(EVulkanBufferBits InUsages, EVulkanMemoryBits InProperties, uint64 InSize)
{
    jScopedLock s(&Lock);
    const EPoolSizeType PoolSizeType = GetPoolSizeType(InSize);

    std::vector<jSubMemoryAllocator*>& SubMemoryAllocators = MemoryPools[(int32)PoolSizeType];
    for (auto& iter : SubMemoryAllocators)
    {
        if (!iter->IsMatchType(InUsages, InProperties))
            continue;

        const jMemory& alloc = iter->Alloc(InSize);
        if (alloc.IsValid())
            return alloc;
    }

    // 새로운 메모리 추가
    jSubMemoryAllocator* NewSubMemoryAllocator = CreateSubMemoryAllocator();
    SubMemoryAllocators.push_back(NewSubMemoryAllocator);

    // 지원가능 최대 메모리 크기를 넘어서는 SubMemoryAllocator 전체를 사용하도록 함
    const uint64 SubMemoryAllocatorSize = (PoolSizeType == EPoolSizeType::MAX) ? InSize : SubMemorySize[(uint64)PoolSizeType];

    NewSubMemoryAllocator->Initialize(InUsages, InProperties, SubMemoryAllocatorSize);
    const jMemory& alloc = NewSubMemoryAllocator->Alloc(InSize);
    check(alloc.IsValid());

    return alloc;
}

void jMemoryPool::Free(const jMemory& InFreeMemory)
{
    jScopedLock s(&Lock);

    const int32 CurrentFrameNumber = g_rhi->GetCurrentFrameNumber();
    const int32 OldestFrameToKeep = CurrentFrameNumber - NumOfFramesToWaitBeforeReleasing;

    // ProcessPendingMemoryFree
    {
        // Check it is too early
        if (CurrentFrameNumber >= CanReleasePendingFreeMemoryFrameNumber)
        {
            // Release pending memory
            int32 i = 0;
            for (; i < PendingFree.size(); ++i)
            {
                jPendingFreeMemory& PendingFreeMemory = PendingFree[i];
                if (PendingFreeMemory.FrameIndex < OldestFrameToKeep)
                {
                    check(PendingFreeMemory.Memory.SubMemoryAllocator);
                    PendingFreeMemory.Memory.SubMemoryAllocator->Free(PendingFreeMemory.Memory);
                }
                else
                {
                    CanReleasePendingFreeMemoryFrameNumber = PendingFreeMemory.FrameIndex + NumOfFramesToWaitBeforeReleasing + 1;
                    break;
                }
            }
            if (i > 0)
            {
                const size_t RemainingSize = (PendingFree.size() - i);
                if (RemainingSize > 0)
                {
                    memcpy(&PendingFree[0], &PendingFree[i], sizeof(jPendingFreeMemory) * RemainingSize);
                }
                PendingFree.resize(RemainingSize);
            }
        }
    }

    PendingFree.emplace_back(jPendingFreeMemory(CurrentFrameNumber, InFreeMemory));
}
