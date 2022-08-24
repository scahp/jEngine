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

struct jShaderBinding
{
    jShaderBinding() = default;
    jShaderBinding(const int32 bindingPoint, const EShaderBindingType bindingType
        , const EShaderAccessStageFlag accessStageFlags, std::shared_ptr<jShaderBindingResource> resourcePtr = nullptr)
        : BindingPoint(bindingPoint), BindingType(bindingType), AccessStageFlags(accessStageFlags), ResourcePtr(resourcePtr)
    { }

    FORCEINLINE size_t GetHash() const
    {
        size_t result = CityHash64((const char*)&BindingPoint, sizeof(BindingPoint));
        result = CityHash64WithSeed((const char*)&BindingType, sizeof(BindingType), result);
        result = CityHash64WithSeed((const char*)&AccessStageFlags, sizeof(AccessStageFlags), result);
        return result;
    }

    void CloneWithoutResource(jShaderBinding& OutReslut) const
    {
        OutReslut.BindingPoint = BindingPoint;
        OutReslut.BindingType = BindingType;
        OutReslut.AccessStageFlags = AccessStageFlags;
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

    const struct jShaderBindingsLayout* ShaderBindingsLayouts = nullptr;
    
    virtual void Initialize(const std::vector<jShaderBinding>& InShaderBindings) {}
    virtual void UpdateShaderBindings(const std::vector<jShaderBinding>& InShaderBindings) {}
    virtual void BindGraphics(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
    virtual void BindCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
};

struct jShaderBindingsLayout
{
    static size_t GenerateHash(const std::vector<jShaderBinding>& shaderBindings);
    FORCEINLINE static size_t CreateShaderBindingLayoutHash(const std::vector<const jShaderBindingsLayout*>& shaderBindings)
    {
        size_t result = 0;
        for (auto& bindings : shaderBindings)
        {
            result = CityHash64WithSeed(bindings->GetHash(), result);
        }
        return result;
    }

    virtual ~jShaderBindingsLayout() {}

    virtual bool Initialize(const std::vector<jShaderBinding>& shaderBindings) { return false; }
    virtual jShaderBindingInstance* CreateShaderBindingInstance(const std::vector<jShaderBinding>& InShaderBindings) const { return nullptr; }
    virtual size_t GetHash() const;
    virtual const std::vector<jShaderBinding>& GetShaderBindingsLayout() const { return ShaderBindings; }

    mutable size_t Hash = 0;

protected:
    std::vector<jShaderBinding> ShaderBindings;     // Resource 정보는 비어있음
};