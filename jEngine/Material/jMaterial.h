#pragma once
#include "RHI/jRHIType.h"

struct jTexture;
struct jShaderBindingInstance;
struct jSamplerStateInfo;

struct jMaterialData
{
    void* GetData() const { return (void*)Data.data(); }
    uint32 GetDataSizeInBytes() const { return (uint32)Data.size(); }

    std::vector<uint8> Data;
};

class jMaterial
{
public:
    virtual ~jMaterial() {}

    enum class EMaterialTextureType : int8
    {
        Albedo = 0,
        Normal,
        //Opacity,
#if USE_SPONZA_PBR
        //BaseColor,
        Metallic,
        //Roughness,
#endif
        Env,
        Max
    };

    struct TextureData
    {
        jName Name;
        jName FilePath;
        jTexture* Texture = nullptr;
        ETextureAddressMode TextureAddressModeU = ETextureAddressMode::REPEAT;
        ETextureAddressMode TextureAddressModeV = ETextureAddressMode::REPEAT;
        ETextureFilter MinificationFilter = ETextureFilter::LINEAR_MIPMAP_LINEAR;
        ETextureFilter MagnificationFilter = ETextureFilter::LINEAR;
        float MaxAnisotropy = 1.0f;
        mutable jSamplerStateInfo* SamplerState = nullptr;
        mutable ETextureAddressMode CachedSamplerAddressModeU = ETextureAddressMode::MAX;
        mutable ETextureAddressMode CachedSamplerAddressModeV = ETextureAddressMode::MAX;
        mutable ETextureFilter CachedMinificationFilter = ETextureFilter::MAX;
        mutable ETextureFilter CachedMagnificationFilter = ETextureFilter::MAX;
        mutable float CachedMaxAnisotropy = -1.0f;

        const jTexture* GetTexture() const
        {
            return Texture;
        }
    };

    bool HasAlbedoTexture() const { return TexData[(int32)EMaterialTextureType::Albedo].Texture; }
    bool IsUseSphericalMap() const { return bUseSphericalMap; }
    bool IsUseSRGBAlbedoTexture() const { return TexData[(int32)EMaterialTextureType::Albedo].Texture ? TexData[(int32)EMaterialTextureType::Albedo].Texture->sRGB : false; }
    bool IsRaytracingAlphaTestEnabled() const { return bRaytracingAlphaTest; }
    jTexture* GetTexture(EMaterialTextureType InType) const;
    template <typename T> T* GetTexture(EMaterialTextureType InType) const { return (T*)(GetTexture(InType)); }
    jSamplerStateInfo* GetTextureSamplerState(EMaterialTextureType InType) const;

    TextureData TexData[static_cast<int32>(EMaterialTextureType::Max)];
    
    // Raw material payload used by RT/path tracing paths that manage their own uniform buffers.
    std::shared_ptr<jMaterialData> MaterialDataPtr;

    bool bUseSphericalMap = false;
    bool bRaytracingAlphaTest = false;
    float RaytracingAlphaCutoff = 0.5f;

    const std::shared_ptr<jShaderBindingInstance>& CreateShaderBindingInstance();
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = nullptr;
    mutable bool NeedToUpdateShaderBindingInstance = true;
};
