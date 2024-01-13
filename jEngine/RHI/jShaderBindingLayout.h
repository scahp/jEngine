#pragma once
#include "Core/jResourceContainer.h"

struct IUniformBufferBlock;
struct jRenderFrameContext;
struct jTexture;
struct jSamplerStateInfo;

// Single resources
struct jShaderBindingResource : public std::enable_shared_from_this<jShaderBindingResource>
{
    virtual ~jShaderBindingResource() {}
    virtual const void* GetResource() const { return nullptr; }
    virtual int32 NumOfResource() const { return 1; }
    virtual bool IsBindless() const { return false; }
};

struct jUniformBufferResource : public jShaderBindingResource
{
    jUniformBufferResource() = default;
    jUniformBufferResource(const IUniformBufferBlock* InUniformBuffer) : UniformBuffer(InUniformBuffer) {}
    virtual ~jUniformBufferResource() {}
    virtual const void* GetResource() const override { return UniformBuffer; }

    const IUniformBufferBlock* UniformBuffer = nullptr;
};

struct jBufferResource : public jShaderBindingResource
{
    jBufferResource() = default;
    jBufferResource(const jBuffer* InBuffer) : Buffer(InBuffer) {}
    virtual ~jBufferResource() {}
    virtual const void* GetResource() const override { return Buffer; }

    const jBuffer* Buffer = nullptr;
};

struct jSamplerResource : public jShaderBindingResource
{
    jSamplerResource() = default;
    jSamplerResource(const jSamplerStateInfo* InSamplerState)
        : SamplerState(InSamplerState) {}
    virtual ~jSamplerResource() {}
    virtual const void* GetResource() const override { return SamplerState; }

    const jSamplerStateInfo* SamplerState = nullptr;
};

struct jTextureResource : public jSamplerResource
{
    jTextureResource() = default;
    jTextureResource(const jTexture* InTexture, const jSamplerStateInfo* InSamplerState, int32 InMipLevel = 0)
        : jSamplerResource(InSamplerState), Texture(InTexture), MipLevel(InMipLevel) {}
    virtual ~jTextureResource() {}
    virtual const void* GetResource() const override { return Texture; }

    const jTexture* Texture = nullptr;
    const int32 MipLevel = 0;
};

struct jTextureArrayResource : public jShaderBindingResource
{
    jTextureArrayResource() = default;
    jTextureArrayResource(const jTexture** InTextureArray, const int32 InNumOfTexure)
        : TextureArray(InTextureArray), NumOfTexure(InNumOfTexure) {}
    virtual ~jTextureArrayResource() {}
    virtual const void* GetResource() const override { return TextureArray; }
    virtual int32 NumOfResource() const override { return NumOfTexure; }

    const jTexture** TextureArray = nullptr;
    const int32 NumOfTexure = 1;
};
//////////////////////////////////////////////////////////////////////////

// Bindless resources, It contain multiple resources at once.
struct jUniformBufferResourceBindless : public jShaderBindingResource
{
    jUniformBufferResourceBindless() = default;
    jUniformBufferResourceBindless(const std::vector<const IUniformBufferBlock*>& InUniformBuffers) : UniformBuffers(InUniformBuffers) {}
    virtual ~jUniformBufferResourceBindless() {}
    virtual const void* GetResource(int32 InIndex) const { return UniformBuffers[InIndex]; }
    virtual int32 GetNumOfResources() const { return (int32)UniformBuffers.size(); }
    virtual bool IsBindless() const { return true; }

    std::vector<const IUniformBufferBlock*> UniformBuffers;
};

struct jBufferResourceBindless : public jShaderBindingResource
{
    jBufferResourceBindless() = default;
    jBufferResourceBindless(const std::vector<const jBuffer*>& InBuffers) : Buffers(InBuffers) {}
    virtual ~jBufferResourceBindless() {}
    virtual const void* GetResource(int32 InIndex) const { return Buffers[InIndex]; }
    virtual bool IsBindless() const { return true; }

    std::vector<const jBuffer*> Buffers;
};

struct jSamplerResourceBindless : public jShaderBindingResource
{
    jSamplerResourceBindless() = default;
    jSamplerResourceBindless(const std::vector<const jSamplerStateInfo*>& InSamplerStates)
        : SamplerStates(InSamplerStates) {}
    virtual ~jSamplerResourceBindless() {}
    virtual const void* GetResource(int32 InIndex) const { return SamplerStates[InIndex]; }
    virtual bool IsBindless() const { return true; }

    std::vector<const jSamplerStateInfo*> SamplerStates;
};

struct jTextureResourceBindless : public jShaderBindingResource
{
    struct jTextureBindData
    {
        jTextureBindData() = default;
        jTextureBindData(jTexture* InTexture, jSamplerStateInfo* InSamplerState, int32 InMipLevel = 0)
            : Texture(InTexture), SamplerState(InSamplerState), MipLevel(InMipLevel)
        {}

        jTexture* Texture = nullptr;
        jSamplerStateInfo* SamplerState = nullptr;
        int32 MipLevel = 0;
    };

    jTextureResourceBindless() = default;
    jTextureResourceBindless(const std::vector<jTextureBindData>& InTextureBindData)
        : TextureBindDatas(InTextureBindData) {}
    virtual ~jTextureResourceBindless() {}
    virtual const void* GetResource(int32 InIndex) const { return &TextureBindDatas[0]; }
    virtual bool IsBindless() const { return true; }

    std::vector<jTextureBindData> TextureBindDatas;
};

struct jTextureArrayResourceBindless : public jShaderBindingResource
{
    struct jTextureArrayBindData
    {
        jTexture** TextureArray = nullptr;
        int32 InNumOfTexure = 0;
    };

    jTextureArrayResourceBindless() = default;
    jTextureArrayResourceBindless(const std::vector<jTextureArrayBindData>& InTextureArrayBindDatas)
        : TextureArrayBindDatas(InTextureArrayBindDatas) {}
    virtual ~jTextureArrayResourceBindless() {}
    virtual const void* GetResource(int32 InIndex) const { return &TextureArrayBindDatas[InIndex]; }
    virtual int32 NumOfResource(int32 InIndex) const { return TextureArrayBindDatas[InIndex].InNumOfTexure; }
    virtual bool IsBindless() const { return true; }

    std::vector<jTextureArrayBindData> TextureArrayBindDatas;
};
//////////////////////////////////////////////////////////////////////////

struct jShaderBindingResourceInlineAllocator
{
    template <typename T, typename... T1>
    T* Alloc(T1... args)
    {
        check((Offset + sizeof(T)) < sizeof(Data));

        T* AllocatedAddress = new (&Data[0] + Offset) T(args...);
        Offset += sizeof(T);
        return AllocatedAddress;
    }

    void Reset()
    {
        Offset = 0;
    }

    uint8 Data[1024];
    int32 Offset = 0;
};

struct jShaderBinding
{
    static constexpr int32 APPEND_LAST = -1;        // BindingPoint is appending last

    jShaderBinding() = default;
    jShaderBinding(const int32 InBindingPoint, const int32 InNumOfDescriptors, const EShaderBindingType InBindingType
        , const EShaderAccessStageFlag InAccessStageFlags, const jShaderBindingResource* InResource = nullptr, bool InIsInline = false)
        : BindingPoint(InBindingPoint), NumOfDescriptors(InNumOfDescriptors), BindingType(InBindingType)
        , AccessStageFlags(InAccessStageFlags), Resource(InResource), IsInline(InIsInline)
    {
        check(EShaderBindingType::SUBPASS_INPUT_ATTACHMENT != InBindingType || InAccessStageFlags == EShaderAccessStageFlag::FRAGMENT);        // SubpassInputAttachment must have the stageflag 0.

        GetHash();
    }

    FORCEINLINE size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = CityHash64((int64)IsInline);
        Hash = CityHash64WithSeed((int64)BindingPoint, Hash);
        Hash = CityHash64WithSeed((int64)NumOfDescriptors, Hash);
        Hash = CityHash64WithSeed((int64)BindingType, Hash);
        Hash = CityHash64WithSeed((int64)AccessStageFlags, Hash);

        return Hash;
    }

    void CloneWithoutResource(jShaderBinding& OutReslut) const
    {
        OutReslut.IsInline = IsInline;
        OutReslut.BindingPoint = BindingPoint;
        OutReslut.NumOfDescriptors = NumOfDescriptors;
        OutReslut.BindingType = BindingType;
        OutReslut.AccessStageFlags = AccessStageFlags;
        OutReslut.Hash = Hash;
    }

    bool IsBindless() const { return NumOfDescriptors >= UINT_MAX; }

    mutable size_t Hash = 0;

    bool IsInline = false;
    int32 BindingPoint = 0;
    int32 NumOfDescriptors = 1;
    EShaderBindingType BindingType = EShaderBindingType::UNIFORMBUFFER;
    EShaderAccessStageFlag AccessStageFlags = EShaderAccessStageFlag::ALL_GRAPHICS;

    // std::shared_ptr<jShaderBindingResource> ResourcePtr;
    const jShaderBindingResource* Resource = nullptr;
};

struct jShaderBindingArray
{
    static constexpr int32 NumOfInlineData = 10;

    template <typename... T>
    void Add(T... args)
    {
        static_assert(std::is_trivially_copyable<jShaderBinding>::value, "jShaderBinding should be trivially copyable");

        check(NumOfInlineData > NumOfData);
        new (&Data[NumOfData]) jShaderBinding(args...);
        ++NumOfData;
    }

    size_t GetHash() const
    {
        size_t Hash = 0;
        jShaderBinding* Address = (jShaderBinding*)&Data[0];
        for (int32 i = 0; i < NumOfData; ++i)
        {
            Hash ^= ((Address + i)->GetHash() << i);
        }
        return Hash;
    }

    FORCEINLINE jShaderBindingArray& operator = (const jShaderBindingArray& In)
    {
        memcpy(&Data[0], &In.Data[0], sizeof(jShaderBinding) * In.NumOfData);
        NumOfData = In.NumOfData;
        return *this;
    }

    FORCEINLINE const jShaderBinding* operator[] (int32 InIndex) const
    {
        check(InIndex < NumOfData);
        return (jShaderBinding*)(&Data[InIndex]);
    }

    FORCEINLINE void CloneWithoutResource(jShaderBindingArray& OutResult) const
    {
        memcpy(&OutResult.Data[0], &Data[0], sizeof(jShaderBinding) * NumOfData);

        for (int32 i = 0; i < NumOfData; ++i)
        {
            jShaderBinding* SrcAddress = (jShaderBinding*)&Data[i];
            jShaderBinding* DstAddress = (jShaderBinding*)&OutResult.Data[i];
            SrcAddress->CloneWithoutResource(*DstAddress);
        }
        OutResult.NumOfData = NumOfData;
    }

    jShaderBinding Data[NumOfInlineData];
    int32 NumOfData = 0;
};

template <typename T>
struct TShaderBinding : public jShaderBinding
{
    TShaderBinding(const int32 InBindingPoint, const int32 InNumOfDescriptors, const EShaderBindingType InBindingType, const EShaderAccessStageFlag InAccessStageFlags, const T& InData)
        : jShaderBinding(InBindingPoint, InNumOfDescriptors, InBindingType, InAccessStageFlags), Data(InData)
    { }
    T Data = T();
};

//struct jShaderBindingArray
//{
//    static constexpr int32 NumOfInlineBindings = 10;
//    struct Allocator
//    {
//        uint8* InlineStrage[NumOfInlineBindings] = {};
//        uint8* GetAddress() const { return InlineStrage[0]; }
//        void SetHeapMemeory(uint8* InHeap) { InlineStrage[0] = InHeap; }
//    };
//    Allocator Data{};
//    uint64 AllocateOffset = 0;
//
//    template <typename T>
//    void Add(const TShaderBinding<T>& InShaderBinding)
//    {
//        const bool NotEnoughInlineStorage = (sizeof(Data) < AllocateOffset + sizeof(InShaderBinding));
//        if (NotEnoughInlineStorage)
//        {
//            // 추가 메모리 할당
//            check(0);
//        }
//
//        memcpy(Data.GetAddress() + AllocateOffset, InShaderBinding, sizeof(InShaderBinding));
//    }
//};

enum class jShaderBindingInstanceType : uint8
{
    SingleFrame = 0,
    MultiFrame,
    Max
};

struct jShaderBindingInstance : public std::enable_shared_from_this<jShaderBindingInstance>
{
    virtual ~jShaderBindingInstance() {}

    const struct jShaderBindingLayout* ShaderBindingsLayouts = nullptr;

    virtual void Initialize(const jShaderBindingArray& InShaderBindingArray) {}
    virtual void UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray) {}
    virtual void* GetHandle() const { return nullptr; }
    virtual const std::vector<uint32>* GetDynamicOffsets() const { return nullptr; }
    virtual void Free() {}
    virtual jShaderBindingInstanceType GetType() const { return Type; }
    virtual void SetType(const jShaderBindingInstanceType InType) { Type = InType; }

private:
    jShaderBindingInstanceType Type = jShaderBindingInstanceType::SingleFrame;
};

// todo : MemStack for jShaderBindingInstanceArray to allocate fast memory
using jShaderBindingInstanceArray = jResourceContainer<const jShaderBindingInstance*>;
using jShaderBindingInstancePtrArray = std::vector<std::shared_ptr<jShaderBindingInstance>>;

struct jShaderBindingLayout
{
    virtual ~jShaderBindingLayout() {}

    virtual bool Initialize(const jShaderBindingArray& InShaderBindingArray) { return false; }
    virtual std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const { return nullptr; }
    virtual size_t GetHash() const;
    virtual const jShaderBindingArray& GetShaderBindingsLayout() const { return ShaderBindingArray; }
    virtual void* GetHandle() const { return nullptr; }

    mutable size_t Hash = 0;

protected:
    jShaderBindingArray ShaderBindingArray;     // Resource 정보는 비어있음
};

using jShaderBindingLayoutArray = jResourceContainer<const jShaderBindingLayout*>;

namespace Test
{
    uint32 GetCurrentFrameNumber();
}

// To pending deallocation for MultiFrame GPU Data, Because Avoding deallocation inflighting GPU Data (ex. jShaderBindingInstance, IUniformBufferBlock)
template <typename T>
struct jDeallocatorMultiFrameResource
{
    static constexpr int32 NumOfFramesToWaitBeforeReleasing = 3;
    struct jPendingFreeData
    {
        jPendingFreeData() = default;
        jPendingFreeData(int32 InFrameIndex, std::shared_ptr<T> InDataPtr) : FrameIndex(InFrameIndex), DataPtr(InDataPtr) {}

        int32 FrameIndex = 0;
        std::shared_ptr<T> DataPtr = nullptr;
    };
    std::vector<jPendingFreeData> PendingFree;
    int32 CanReleasePendingFreeDataFrameNumber = 0;
    std::function<void(std::shared_ptr<T>)> FreeDelegate;

    void Free(std::shared_ptr<T> InDataPtr)
    {
        const int32 CurrentFrameNumber = Test::GetCurrentFrameNumber();
        const int32 OldestFrameToKeep = CurrentFrameNumber - NumOfFramesToWaitBeforeReleasing;

        // ProcessPendingDescriptorPoolFree
        {
            // Check it is too early
            if (CurrentFrameNumber >= CanReleasePendingFreeDataFrameNumber)
            {
                // Release pending memory
                int32 i = 0;
                for (; i < PendingFree.size(); ++i)
                {
                    jPendingFreeData& PendingFreeData = PendingFree[i];
                    if (PendingFreeData.FrameIndex < OldestFrameToKeep)
                    {
                        // Return to pending descriptor set
                        check(PendingFreeData.DataPtr);
                        if (FreeDelegate)
                            FreeDelegate(PendingFreeData.DataPtr);
                    }
                    else
                    {
                        CanReleasePendingFreeDataFrameNumber = PendingFreeData.FrameIndex + NumOfFramesToWaitBeforeReleasing + 1;
                        break;
                    }
                }
                if (i > 0)
                {
                    const size_t RemainingSize = (PendingFree.size() - i);
                    if (RemainingSize > 0)
                    {
                        for (int32 k = 0; k < RemainingSize; ++k)
                            PendingFree[k] = PendingFree[i + k];
                    }
                    PendingFree.resize(RemainingSize);
                }
            }
        }

        PendingFree.emplace_back(jPendingFreeData(CurrentFrameNumber, InDataPtr));
    }
};

using jDeallocatorMultiFrameShaderBindingInstance = jDeallocatorMultiFrameResource<jShaderBindingInstance>;
using jDeallocatorMultiFrameUniformBufferBlock = jDeallocatorMultiFrameResource<IUniformBufferBlock>;