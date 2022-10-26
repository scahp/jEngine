#include "pch.h"
#include "jMemoryPool.h"

void* jMemory::GetMappedPointer() const
{
    return SubMemoryAllocator->MappedPointer;
}

void* jMemory::GetMemory() const
{
    return SubMemoryAllocator->GetMemory();
}

//////////////////////////////////////////////////////////////////////////
// jMemory
void jMemory::Free()
{
    SubMemoryAllocator->Free(*this);
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
    const uint64 AlignedRequestedSize = Align(InRequstedSize, Alignment);

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

        AllocMem.Range.Offset = Align(SubMemoryRange.Offset, Alignment);
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
    const EPoolSizeType PoolSizeType = GetMemorySize(InSize);

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
