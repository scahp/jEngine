#pragma once
#include "RHI/jRHIType.h"

struct jTexture;
struct jShaderBindingInstance;

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

    TextureData TexData[static_cast<int32>(EMaterialTextureType::Max)];

    const std::shared_ptr<jShaderBindingInstance>& CreateShaderBindingInstance();
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = nullptr;
    mutable bool NeedToUpdateShaderBindingInstance = true;
};
