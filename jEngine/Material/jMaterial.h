#pragma once
#include "RHI/jRHIType.h"
#include <optional>
#include <string>

struct jTexture;
struct jShaderBindingInstance;
struct jSamplerStateInfo;

struct jMaterialData
{
    void* GetData() const { return (void*)Data.data(); }
    uint32 GetDataSizeInBytes() const { return (uint32)Data.size(); }

    std::vector<uint8> Data;
};

enum class jSurfaceAlphaMode : uint32 { Opaque, Mask, Blend };

struct jSurfaceShading
{
    float Transmission = 0.f, Ior = 1.5f, ClearCoat = 0.f, ClearCoatGloss = 0.f;
    float Subsurface = 0.f, Anisotropy = 0.f, SpecularTint = 0.f, Sheen = 0.f, SheenTint = 0.f;
    jSurfaceAlphaMode AlphaMode = jSurfaceAlphaMode::Opaque;
    float AlphaCutoff = .5f;
    bool DoubleSided = false;
    bool LegacyPackedMap = false;
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
        Emission,
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
    std::string SourceMaterialName;
    std::optional<jSurfaceShading> Surface;

    // Authored instances clone before editing; imported prototypes stay shared.
    bool UseMaterialProperties = false;
    Vector4 BaseColorOpacity = Vector4(1.0f);
    Vector Emissive = Vector(0.0f);
    float MetallicFactor = 0.0f;
    float RoughnessFactor = 0.5f;
    uint64 Revision = 0;
    std::vector<std::shared_ptr<jTexture>> OwnedTextures;

    bool bUseSphericalMap = false;
    bool bRaytracingAlphaTest = false;
    float RaytracingAlphaCutoff = 0.5f;

    const std::shared_ptr<jShaderBindingInstance>& CreateShaderBindingInstance();
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = nullptr;
    mutable bool NeedToUpdateShaderBindingInstance = true;
};
