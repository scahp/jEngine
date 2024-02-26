#pragma once
#include "RHI/jRHIType.h"

struct jTexture;
struct jShaderBindingInstance;

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

        const jTexture* GetTexture() const
        {
            return Texture;
        }
    };

    bool HasAlbedoTexture() const { return TexData[(int32)EMaterialTextureType::Albedo].Texture; }
    bool IsUseSphericalMap() const { return bUseSphericalMap; }
    bool IsUseSRGBAlbedoTexture() const { return TexData[(int32)EMaterialTextureType::Albedo].Texture ? TexData[(int32)EMaterialTextureType::Albedo].Texture->sRGB : false; }
    jTexture* GetTexture(EMaterialTextureType InType) const;
    template <typename T> T* GetTexture(EMaterialTextureType InType) const { return (T*)(GetTexture(InType)); }

    TextureData TexData[static_cast<int32>(EMaterialTextureType::Max)];
    
    // Material specific uniform buffer data which is depend on material what data was set.
    std::shared_ptr<jMaterialData> MaterialDataPtr;
    std::shared_ptr<IUniformBufferBlock> MaterialDataUniformBufferPtr;

    bool bUseSphericalMap = false;

    const std::shared_ptr<jShaderBindingInstance>& CreateShaderBindingInstance();
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = nullptr;
    mutable bool NeedToUpdateShaderBindingInstance = true;
};
