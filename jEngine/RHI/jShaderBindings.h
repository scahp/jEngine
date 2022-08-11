#pragma once

struct IUniformBufferBlock;
struct jRenderFrameContext;

struct jShaderBinding
{
    jShaderBinding() = default;
    jShaderBinding(const int32 bindingPoint, const EShaderAccessStageFlag accessStageFlags)
        : BindingPoint(bindingPoint), AccessStageFlags(accessStageFlags)
    { }

    FORCEINLINE size_t GetHash() const
    {
        size_t result = 0;
        result ^= CityHash64((const char*)&BindingPoint, sizeof(BindingPoint));
        result ^= CityHash64((const char*)&AccessStageFlags, sizeof(AccessStageFlags));
        return result;
    }

    int32 BindingPoint = 0;
    EShaderAccessStageFlag AccessStageFlags = EShaderAccessStageFlag::ALL_GRAPHICS;
};

template <typename T>
struct TShaderBinding : public jShaderBinding
{
    TShaderBinding(const int32 bindingPoint, const EShaderAccessStageFlag accessStageFlags, const T& InData)
        : jShaderBinding(bindingPoint, accessStageFlags), Data(InData)
    { }
    T Data = T();
};

struct jTextureBindings
{
    const jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
};

struct jShaderBindingInstance
{
    virtual ~jShaderBindingInstance() {}

    const struct jShaderBindings* ShaderBindings = nullptr;
    std::vector<IUniformBufferBlock*> UniformBuffers;
    std::vector<jTextureBindings> Textures;

    virtual void UpdateShaderBindings() {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, void* pipelineLayout, int32 InSlot = 0) const {}
};

struct jShaderBindings
{
    static size_t GenerateHash(const std::vector<jShaderBinding>& InUniformBuffers, const std::vector<jShaderBinding>& InTextures);
    FORCEINLINE static size_t CreateShaderBindingsHash(const std::vector<const jShaderBindings*>& shaderBindings)
    {
        size_t result = 0;
        for (auto& bindings : shaderBindings)
        {
            result ^= bindings->GetHash();
        }
        return result;
    }

    virtual ~jShaderBindings() {}

    virtual bool CreateDescriptorSetLayout() { return false; }
    virtual void CreatePool() {}

    std::vector<jShaderBinding> UniformBuffers;
    std::vector<jShaderBinding> Textures;

    FORCEINLINE int32 GetNextBindingIndex() const { return (int32)(UniformBuffers.size() + Textures.size()); };

    virtual jShaderBindingInstance* CreateShaderBindingInstance() const { return nullptr; }
    virtual std::vector<jShaderBindingInstance*> CreateShaderBindingInstance(int32 count) const { return {}; }

    virtual size_t GetHash() const;

    mutable size_t Hash = 0;
};