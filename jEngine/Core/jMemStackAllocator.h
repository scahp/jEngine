#pragma once

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
        const uint64 allocOffset = Align<uint64>(Offset, DEFAULT_ALIGNMENT);
        if (allocOffset + InNumOfBytes <= DataSize)
        {
            Offset = allocOffset + InNumOfBytes;
            return (Address + allocOffset);
        }
        return nullptr;
    }
};

class jPageAllocator
{
public:
    static constexpr uint64 PageSize = 1024 * 4;
    static constexpr uint64 MaxMemoryChunkSize = PageSize - sizeof(jMemoryChunk);

    static jPageAllocator* Get()
    {
        static jPageAllocator* s_pageAllocator = nullptr;
        if (!s_pageAllocator)
            s_pageAllocator = new jPageAllocator();

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
                return Chunk;
            }
        }

        uint8* NewLowMemory = static_cast<uint8*>(malloc(PageSize));
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
        const uint64 NeedToAllocateBytes = InNumOfBytes + sizeof(jMemoryChunk);

        if (FreeChunkBigSize)
        {
            jMemoryChunk* ChunkPrev = nullptr;
            jMemoryChunk* CurChunk = FreeChunkBigSize;
            while (CurChunk)
            {
                if (CurChunk->DataSize >= NeedToAllocateBytes)
                {
                    if (ChunkPrev)
                        ChunkPrev->Next = CurChunk->Next;

                    return CurChunk;
                }

                ChunkPrev = CurChunk;
                CurChunk = CurChunk->Next;
            }
        }

        uint8* NewLowMemory = static_cast<uint8*>(malloc(NeedToAllocateBytes));
        jMemoryChunk* NewChunk = new (NewLowMemory) jMemoryChunk(NewLowMemory, sizeof(jMemoryChunk), NeedToAllocateBytes);
        return NewChunk;
    }

    void FreeBigSize(jMemoryChunk* InChunk)
    {
        InChunk->Offset = sizeof(jMemoryChunk);

        if (FreeChunkBigSize)
        {
            jMemoryChunk* ChunkPrev = nullptr;
            jMemoryChunk* CurChunk = FreeChunkBigSize;
            while (CurChunk)
            {
                if (CurChunk->DataSize >= InChunk->DataSize)
                {
                    if (ChunkPrev)
                        ChunkPrev->Next = InChunk;
                    InChunk->Next = CurChunk;
                    return;
                }

                ChunkPrev = CurChunk;
                CurChunk = CurChunk->Next;
            }

            if (ChunkPrev)
            {
                ChunkPrev->Next = InChunk;
                return;
            }
        }

        FreeChunkBigSize = InChunk;
    }

    void Flush()
    {
        jScopedLock s(&Lock);
        while (FreeChunk)
        {
            jMemoryChunk* Next = FreeChunk->Next;
            delete[] FreeChunk;
            FreeChunk = Next;
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
        static jMemStack* s_memStack = nullptr;
        if (!s_memStack)
            s_memStack = new jMemStack();

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