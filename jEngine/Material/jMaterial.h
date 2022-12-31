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
        DiffuseSampler = 0,
        NormalSampler,
        OpacitySampler,
        Max
    };

    struct TextureData
    {
        jName Name;
        jTexture* Texture = nullptr;
        ETextureAddressMode TextureAddressModeU = ETextureAddressMode::REPEAT;
        ETextureAddressMode TextureAddressModeV = ETextureAddressMode::REPEAT;

        const jTexture* GetTexture() const
        {
            return Texture;
        }
    };

    TextureData TexData[static_cast<int32>(EMaterialTextureType::Max)];

    jShaderBindingInstance* CreateShaderBindingInstance();
    jShaderBindingInstance* ShaderBindingInstance = nullptr;
    mutable bool NeedToUpdateShaderBindingInstance = true;
};
