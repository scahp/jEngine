#pragma once

#define USE_VK_MEMORY_POOL 1

class jSubMemoryAllocator;

// Memory range in sub memory
struct jRange
{
    uint64 Offset = 0;
    uint64 DataSize = 0;
};

struct jMemory
{
    FORCEINLINE bool IsValid() const { return Buffer; }
    FORCEINLINE void* GetBuffer() const { return Buffer; }
    void* GetMappedPointer() const;
    void* GetMemory() const;
    void Free();
    void Reset();
    void* Buffer = nullptr;
    jRange Range;
    jSubMemoryAllocator* SubMemoryAllocator = nullptr;
};

class jSubMemoryAllocator
{
public:
    friend class jMemoryPool;

    virtual ~jSubMemoryAllocator() {}

    virtual void Initialize(EVulkanBufferBits InUsage, EVulkanMemoryBits InProperties, uint64 InSize) = 0;
    virtual void* GetBuffer() const { return nullptr; }
    virtual void* GetMemory() const { return nullptr; }
    virtual void* GetMappedPointer() const { return MappedPointer; }
    virtual jMemory Alloc(uint64 InRequstedSize);
    virtual bool IsMatchType(EVulkanBufferBits InUsages, EVulkanMemoryBits InProperties) const
    {
        return (Usages == InUsages) && (Properties == InProperties);
    }

protected:
    virtual void Free(const jMemory& InFreeMemory)
    {
        jScopedLock s(&Lock);
        
        // If All of the allocated memory returned then clear allocated list to use all area of the submemory allocator
        if (AllAllocatedLists.size() == FreeLists.size() + 1)
        {
            AllAllocatedLists.clear();
            FreeLists.clear();
        }
        else
        {
            FreeLists.push_back(InFreeMemory.Range);
        }
    }

    jMutexLock Lock;
    void* MappedPointer = nullptr;
    std::vector<jRange> FreeLists;
    std::vector<jRange> AllAllocatedLists;
    jRange SubMemoryRange;
    EVulkanBufferBits Usages = EVulkanBufferBits::TRANSFER_SRC;
    EVulkanMemoryBits Properties = EVulkanMemoryBits::DEVICE_LOCAL;
    uint64 Alignment = 16;
};

class jMemoryPool
{
public:
    virtual ~jMemoryPool() {}
    enum class EPoolSizeType : uint8
    {
        E128,
        E256,
        E512,
        E1K,
        E2K,
        E4K,
        E8K,
        E16K,
        MAX
    };

    //enum class EPoolSize : uint64
    constexpr static uint64 MemorySize[(int32)EPoolSizeType::MAX] =
    {
        128,            // E128
        256,            // E256
        512,            // E512
        1024,           // E1K 
        2048,           // E2K 
        4096,           // E4K 
        8192,           // E8K 
        16 * 1024,      // E16K
    };

    constexpr static uint64 SubMemorySize[(int32)EPoolSizeType::MAX] =
    {
        128 * 1024,
        128 * 1024,
        256 * 1024,
        256 * 1024,
        512 * 1024,
        512 * 1024,
        1024 * 1024,
        1024 * 1024,
    };

    struct jPendingFreeMemory
    {
        jPendingFreeMemory() = default;
        jPendingFreeMemory(int32 InFrameIndex, const jMemory& InMemory) : FrameIndex(InFrameIndex), Memory(InMemory) {}

        int32 FrameIndex = 0;
        jMemory Memory;
    };

    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;

    virtual jSubMemoryAllocator* CreateSubMemoryAllocator() const = 0;

    // 적절한 PoolSize 선택 함수
    virtual EPoolSizeType GetPoolSizeType(uint64 InSize) const
    {
        for (int32 i = 0; i < (int32)EPoolSizeType::MAX; ++i)
        {
            if (MemorySize[i] > InSize)
            {
                return (EPoolSizeType)i;
            }
        }
        return EPoolSizeType::MAX;
    }

    virtual jMemory Alloc(EVulkanBufferBits InUsages, EVulkanMemoryBits InProperties, uint64 InSize);

    virtual void Free(const jMemory& InFreeMemory);

    jMutexLock Lock;
    std::vector<jSubMemoryAllocator*> MemoryPools[(int32)EPoolSizeType::MAX + 1];
    std::vector<jPendingFreeMemory> PendingFree;
    int32 CanReleasePendingFreeMemoryFrameNumber = 0;
};