#pragma once

struct IUniformBufferBlock;
struct jRenderFrameContext;

struct jShaderBindingResource : public std::enable_shared_from_this<jShaderBindingResource>
{
    virtual ~jShaderBindingResource() {}
};

struct jUniformBufferResource : public jShaderBindingResource
{
    jUniformBufferResource() = default;
    jUniformBufferResource(const IUniformBufferBlock* InUniformBuffer) : UniformBuffer(InUniformBuffer) {}
    const IUniformBufferBlock* UniformBuffer = nullptr;
};

struct jTextureResource : public jShaderBindingResource
{
    jTextureResource() = default;
    jTextureResource(const jTexture* InTexture, const jSamplerStateInfo* InSamplerState)
        : Texture(InTexture), SamplerState(InSamplerState) {}
    const jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
};

struct jShaderBinding
{
    jShaderBinding() = default;
    jShaderBinding(const int32 bindingPoint, const EShaderBindingType bindingType
        , const EShaderAccessStageFlag accessStageFlags, std::shared_ptr<jShaderBindingResource> resourcePtr = nullptr)
        : BindingPoint(bindingPoint), BindingType(bindingType), AccessStageFlags(accessStageFlags), ResourcePtr(resourcePtr)
    { }

    FORCEINLINE size_t GetHash() const
    {
        size_t result = 0;
        result ^= CityHash64((const char*)&BindingPoint, sizeof(BindingPoint));
        result ^= CityHash64((const char*)&BindingType, sizeof(BindingType));
        result ^= CityHash64((const char*)&AccessStageFlags, sizeof(AccessStageFlags));
        return result;
    }

    int32 BindingPoint = 0;
    EShaderBindingType BindingType = EShaderBindingType::UNIFORMBUFFER;
    EShaderAccessStageFlag AccessStageFlags = EShaderAccessStageFlag::ALL_GRAPHICS;

    std::shared_ptr<jShaderBindingResource> ResourcePtr;
};

template <typename T>
struct TShaderBinding : public jShaderBinding
{
    TShaderBinding(const int32 bindingPoint, const EShaderBindingType bindingType, const EShaderAccessStageFlag accessStageFlags, const T& InData)
        : jShaderBinding(bindingPoint, bindingType, accessStageFlags), Data(InData)
    { }
    T Data = T();
};

struct jShaderBindingInstance
{
    virtual ~jShaderBindingInstance() {}

    const struct jShaderBindingLayout* ShaderBindings = nullptr;
    
    virtual void UpdateShaderBindings(std::vector<jShaderBinding> InShaderBindings) {}
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
};

struct jShaderBindingLayout
{
    static size_t GenerateHash(const std::vector<jShaderBinding>& shaderBindings);
    FORCEINLINE static size_t CreateShaderBindingLayoutHash(const std::vector<const jShaderBindingLayout*>& shaderBindings)
    {
        size_t result = 0;
        for (auto& bindings : shaderBindings)
        {
            result ^= bindings->GetHash();
        }
        return result;
    }

    virtual ~jShaderBindingLayout() {}

    virtual bool CreateDescriptorSetLayout() { return false; }
    virtual void CreatePool() {}

    std::vector<jShaderBinding> ShaderBindings;

    FORCEINLINE int32 GetNextBindingIndex() const { return (int32)(ShaderBindings.size()); };

    virtual jShaderBindingInstance* CreateShaderBindingInstance() const { return nullptr; }
    virtual std::vector<jShaderBindingInstance*> CreateShaderBindingInstance(int32 count) const { return {}; }

    virtual size_t GetHash() const;

    mutable size_t Hash = 0;
};