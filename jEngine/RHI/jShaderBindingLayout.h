#pragma once
#include "Core/jResourceContainer.h"

struct IUniformBufferBlock;
struct jRenderFrameContext;
struct jTexture;
struct jSamplerStateInfo;

struct jShaderBindingResource : public std::enable_shared_from_this<jShaderBindingResource>
{
    virtual ~jShaderBindingResource() {}
    virtual const void* GetResource() const { return nullptr; }
    virtual int32 NumOfResource() const { return 1; }
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
    jTextureResource(const jTexture* InTexture, const jSamplerStateInfo* InSamplerState)
        : jSamplerResource(InSamplerState), Texture(InTexture) {}
    virtual ~jTextureResource() {}
    virtual const void* GetResource() const override { return Texture; }

    const jTexture* Texture = nullptr;
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

        struct jHashTemp
        {
            bool IsInline = false;
            int32 BindingPoint = 0;
            int32 NumOfDescriptors = 1;
            EShaderBindingType BindingType = EShaderBindingType::UNIFORMBUFFER;
            EShaderAccessStageFlag AccessStageFlags = EShaderAccessStageFlag::ALL_GRAPHICS;
        };

        jHashTemp temp;
        temp.IsInline = IsInline;
        temp.BindingPoint = BindingPoint;
        temp.NumOfDescriptors = NumOfDescriptors;
        temp.BindingType = BindingType;
        temp.AccessStageFlags = AccessStageFlags;

        Hash = CityHash64((const char*)&temp, sizeof(temp));

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

struct jShaderBindingInstance
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
