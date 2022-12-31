#pragma once
#include "Core/jResourceContainer.h"

struct IUniformBufferBlock;
struct jRenderFrameContext;
struct jTexture;
struct jSamplerStateInfo;

struct jShaderBindingResource : public std::enable_shared_from_this<jShaderBindingResource>
{
    virtual ~jShaderBindingResource() {}
};

struct jUniformBufferResource : public jShaderBindingResource
{
    jUniformBufferResource() = default;
    jUniformBufferResource(const IUniformBufferBlock* InUniformBuffer) : UniformBuffer(InUniformBuffer) {}
    virtual ~jUniformBufferResource() {}
    const IUniformBufferBlock* UniformBuffer = nullptr;
};

struct jBufferResource : public jShaderBindingResource
{
    jBufferResource() = default;
    jBufferResource(const jBuffer* InBuffer) : Buffer(InBuffer) {}
    virtual ~jBufferResource() {}
    const jBuffer* Buffer = nullptr;
};

struct jTextureResource : public jShaderBindingResource
{
    jTextureResource() = default;
    jTextureResource(const jTexture* InTexture, const jSamplerStateInfo* InSamplerState)
        : Texture(InTexture), SamplerState(InSamplerState) {}
    virtual ~jTextureResource() {}
    const jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
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
    jShaderBinding() = default;
    jShaderBinding(const int32 bindingPoint, const EShaderBindingType bindingType
        , const EShaderAccessStageFlag accessStageFlags, const jShaderBindingResource* InResource = nullptr)
        : BindingPoint(bindingPoint), BindingType(bindingType), AccessStageFlags(accessStageFlags), Resource(InResource)
    {
        check(EShaderBindingType::SUBPASS_INPUT_ATTACHMENT != bindingType || accessStageFlags == EShaderAccessStageFlag::FRAGMENT);        // SubpassInputAttachment must have the stageflag 0.

        GetHash();
    }

    FORCEINLINE size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = BindingPoint ^ (uint32)BindingType ^ (uint32)AccessStageFlags;
        return Hash;
    }

    void CloneWithoutResource(jShaderBinding& OutReslut) const
    {
        OutReslut.BindingPoint = BindingPoint;
        OutReslut.BindingType = BindingType;
        OutReslut.AccessStageFlags = AccessStageFlags;
        OutReslut.Hash = Hash;
    }

    mutable size_t Hash = 0;

    int32 BindingPoint = 0;
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
        check(NumOfInlineData > NumOfData);
        const int32 AddressOffset = (NumOfData) * sizeof(jShaderBinding);
        new (&Data[0] + AddressOffset) jShaderBinding(args...);
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
        return *this;
    }

    FORCEINLINE const jShaderBinding* operator[] (int32 InIndex) const
    {
        check(InIndex < NumOfData);
        return (jShaderBinding*)(&Data[InIndex * sizeof(jShaderBinding)]);
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

    uint8 Data[NumOfInlineData * sizeof(jShaderBinding)];
    int32 NumOfData = 0;
};

template <typename T>
struct TShaderBinding : public jShaderBinding
{
    TShaderBinding(const int32 bindingPoint, const EShaderBindingType bindingType, const EShaderAccessStageFlag accessStageFlags, const T& InData)
        : jShaderBinding(bindingPoint, bindingType, accessStageFlags), Data(InData)
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

    const struct jShaderBindingsLayout* ShaderBindingsLayouts = nullptr;
    
    virtual void Initialize(const jShaderBindingArray& InShaderBindingArray) {}
    virtual void UpdateShaderBindings(const jShaderBindingArray& InShaderBindingArray) {}
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
    virtual void* GetHandle() const { return nullptr; }
    virtual const std::vector<uint32>* GetDynamicOffsets() const { return nullptr; }
    virtual void Free() {}
    virtual jShaderBindingInstanceType GetType() const { return Type; }
    virtual void SetType(const jShaderBindingInstanceType InType) { Type = InType; }

private:
    jShaderBindingInstanceType Type = jShaderBindingInstanceType::SingleFrame;
};

using jShaderBindingInstanceArray = jResourceContainer<const jShaderBindingInstance*>;

struct jShaderBindingsLayout
{
    virtual ~jShaderBindingsLayout() {}

    virtual bool Initialize(const jShaderBindingArray& InShaderBindingArray) { return false; }
    virtual jShaderBindingInstance* CreateShaderBindingInstance(const jShaderBindingArray& InShaderBindingArray, const jShaderBindingInstanceType InType) const { return nullptr; }
    virtual size_t GetHash() const;
    virtual const jShaderBindingArray& GetShaderBindingsLayout() const { return ShaderBindingArray; }
    virtual void* GetHandle() const { return nullptr; }

    mutable size_t Hash = 0;

protected:
    jShaderBindingArray ShaderBindingArray;     // Resource 정보는 비어있음
};

using jShaderBindingsLayoutArray = jResourceContainer<const jShaderBindingsLayout*>;
