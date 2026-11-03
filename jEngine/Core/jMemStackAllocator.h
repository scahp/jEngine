#pragma once

#include <cstdlib>
#include <limits>
#include <new>

#define ENABLE_ALLOCATOR_LOG 0
#define DEFAULT_ALIGNMENT 16

struct jMemoryChunk
{
    jMemoryChunk() = default;
    jMemoryChunk(uint8* InAddress, uint64 InOffset, uint64 InDataSize)
        : Address(InAddress), Offset(InOffset), DataSize(InDataSize) { }

    uint8* Address = nullptr;
    uint64 Offset = 0;
    uint64 DataSize = 0;

    jMemoryChunk* Next = nullptr;

    FORCEINLINE void* Alloc(size_t InNumOfBytes)
    {
        if (Offset > DataSize)
            return nullptr;

        const uint64 Padding = (DEFAULT_ALIGNMENT - Offset % DEFAULT_ALIGNMENT) % DEFAULT_ALIGNMENT;
        const uint64 Available = DataSize - Offset;
        if (Padding > Available || InNumOfBytes > Available - Padding)
            return nullptr;

        const uint64 AllocOffset = Offset + Padding;
        Offset = AllocOffset + InNumOfBytes;
        return Address + AllocOffset;
    }
};

class jPageAllocator
{
public:
    static constexpr uint64 PageSize = 1024 * 4;
    static constexpr uint64 MaxMemoryChunkSize = PageSize - sizeof(jMemoryChunk);

    static jPageAllocator* Get()
    {
        static jPageAllocator* const s_pageAllocator = new jPageAllocator();
        return s_pageAllocator;
    }

    jMemoryChunk* Allocate()
    {
        {
            jScopedLock s(&Lock);
            if (FreeChunk)
            {
                jMemoryChunk* Chunk = FreeChunk;
                FreeChunk = FreeChunk->Next;
                Chunk->Next = nullptr;
                return Chunk;
            }
        }

        uint8* NewLowMemory = static_cast<uint8*>(malloc(PageSize));
        if (!NewLowMemory)
            throw std::bad_alloc();
        jMemoryChunk* NewMemoryChunk = new (NewLowMemory) jMemoryChunk(NewLowMemory, sizeof(jMemoryChunk), jPageAllocator::PageSize);
        return NewMemoryChunk;
    }

    void Free(jMemoryChunk* InChunk)
    {
        jScopedLock s(&Lock);

        InChunk->Offset = sizeof(jMemoryChunk);
        InChunk->Next = FreeChunk;
        FreeChunk = InChunk;
    }

    jMemoryChunk* AllocateBigSize(uint64 InNumOfBytes)
    {
        if (InNumOfBytes > std::numeric_limits<size_t>::max() - sizeof(jMemoryChunk))
            throw std::bad_alloc();
        const uint64 NeedToAllocateBytes = InNumOfBytes + sizeof(jMemoryChunk);

        {
            jScopedLock s(&Lock);
            jMemoryChunk** ChunkLink = &FreeChunkBigSize;
            while (*ChunkLink)
            {
                jMemoryChunk* CurChunk = *ChunkLink;
                if (CurChunk->DataSize >= NeedToAllocateBytes)
                {
                    *ChunkLink = CurChunk->Next;
                    CurChunk->Next = nullptr;
                    return CurChunk;
                }
                ChunkLink = &CurChunk->Next;
            }
        }

        uint8* NewLowMemory = static_cast<uint8*>(malloc(NeedToAllocateBytes));
        if (!NewLowMemory)
            throw std::bad_alloc();
        jMemoryChunk* NewChunk = new (NewLowMemory) jMemoryChunk(NewLowMemory, sizeof(jMemoryChunk), NeedToAllocateBytes);
        return NewChunk;
    }

    void FreeBigSize(jMemoryChunk* InChunk)
    {
        jScopedLock s(&Lock);
        InChunk->Offset = sizeof(jMemoryChunk);

        jMemoryChunk** ChunkLink = &FreeChunkBigSize;
        while (*ChunkLink && (*ChunkLink)->DataSize < InChunk->DataSize)
            ChunkLink = &(*ChunkLink)->Next;
        InChunk->Next = *ChunkLink;
        *ChunkLink = InChunk;
    }

    void Flush()
    {
        jScopedLock s(&Lock);
        while (FreeChunk)
        {
            jMemoryChunk* Next = FreeChunk->Next;
            std::free(FreeChunk);
            FreeChunk = Next;
        }
        while (FreeChunkBigSize)
        {
            jMemoryChunk* Next = FreeChunkBigSize->Next;
            std::free(FreeChunkBigSize);
            FreeChunkBigSize = Next;
        }
    }

    jMutexLock Lock;
    jMemoryChunk* FreeChunk = nullptr;
    jMemoryChunk* FreeChunkBigSize = nullptr;

private:
    jPageAllocator() = default;
    jPageAllocator(const jPageAllocator&) = delete;
    jPageAllocator(jPageAllocator&&) = delete;
    jPageAllocator& operator = (const jPageAllocator& In) = delete;
};

class jMemStack
{
public:
    static jMemStack* Get()
    {
        // Allocation and Flush on this frame arena remain owned by the frame thread.
        static jMemStack* const s_memStack = new jMemStack();
        return s_memStack;
    }

    template <typename T>
    T* Alloc()
    {
        return (T*)Alloc(sizeof(T));
    }

    void* Alloc(uint64 InNumOfBytes)
    {
        if (InNumOfBytes >= jPageAllocator::MaxMemoryChunkSize)
        {
            jMemoryChunk* NewChunk = jPageAllocator::Get()->AllocateBigSize(InNumOfBytes);
            check(NewChunk);

            NewChunk->Next = BigSizeChunk;
            BigSizeChunk = NewChunk;
            
            void* AllocatedMemory = BigSizeChunk->Alloc(InNumOfBytes);
            check(AllocatedMemory);
            return AllocatedMemory;
        }

        if (TopMemoryChunk)
        {
            void* AllocatedMemory = TopMemoryChunk->Alloc(InNumOfBytes);
            if (AllocatedMemory)
            {
                return AllocatedMemory;
            }
        }

        jMemoryChunk* NewChunk = jPageAllocator::Get()->Allocate();
        check(NewChunk);

        NewChunk->Next = TopMemoryChunk;
        TopMemoryChunk = NewChunk;

        void* AllocatedMemory = TopMemoryChunk->Alloc(InNumOfBytes);
        check(AllocatedMemory);
        return AllocatedMemory;
    }

    void Flush()
    {
        while (TopMemoryChunk)
        {
            jMemoryChunk* Next = TopMemoryChunk->Next;
            jPageAllocator::Get()->Free(TopMemoryChunk);
            TopMemoryChunk = Next;
        }

        while (BigSizeChunk)
        {
            jMemoryChunk* Next = BigSizeChunk->Next;
            jPageAllocator::Get()->FreeBigSize(BigSizeChunk);
            BigSizeChunk = Next;
        }
    }

private:
    jMemoryChunk* TopMemoryChunk = nullptr;
    jMemoryChunk* BigSizeChunk = nullptr;
};

template <typename T>
class jMemStackAllocator
{
public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T value_type;

    pointer allocate(size_t InNumOfElement)
    {
        if (InNumOfElement > std::numeric_limits<size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
#if ENABLE_ALLOCATOR_LOG
        pointer allocatedAddress = static_cast<pointer>(jMemStack::Get()->Alloc(InNumOfElement * sizeof(T)));
        std::cout << "Called jMemstackAllocator::allocate with 0x" << allocatedAddress << " address and " << InNumOfElement << " elements" << std::endl;
        return allocatedAddress;
#else
        return static_cast<pointer>(jMemStack::Get()->Alloc(InNumOfElement * sizeof(T)));
#endif
    }

    void deallocate(pointer InAddress, size_t InNumOfElement)
    {
#if ENABLE_ALLOCATOR_LOG
        std::cout << "Called jMemstackAllocator::deallocate with 0x" << InAddress << " address and " << InNumOfElement << " elements" << std::endl;
#endif
        //free(InAddress);
    }

    void construct(pointer InAddress, const const_reference InInitialValue)
    {
#if ENABLE_ALLOCATOR_LOG
        std::cout << "Called jMemstackAllocator::construct with 0x" << InAddress << " address and initial value " << InInitialValue << std::endl;
#endif
        new (static_cast<void*>(InAddress)) T(InInitialValue);
    }

    void construct(pointer InAddress)
    {
#if ENABLE_ALLOCATOR_LOG
        std::cout << "Called jMemstackAllocator::construct with 0x" << InAddress << " address" << std::endl;
#endif
        new(static_cast<void*>(InAddress)) T();
    }

    void destroy(pointer InAddress)
    {
#if ENABLE_ALLOCATOR_LOG
        std::cout << "Called jMemstackAllocator::destroy with 0x" << InAddress << " address" << std::endl;
#endif
        InAddress->~T();
    }
};
