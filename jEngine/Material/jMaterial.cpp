#include "pch.h"
#include "jMaterial.h"
#include "RHI/jRHI.h"

jTexture* jMaterial::GetTexture(EMaterialTextureType InType) const
{
    check(EMaterialTextureType::Albedo <= InType);
    check(EMaterialTextureType::Max > InType);
    return TexData[(int32)InType].Texture;
}

const std::shared_ptr<jShaderBindingInstance>& jMaterial::CreateShaderBindingInstance()
{
    if (NeedToUpdateShaderBindingInstance)
    {
        NeedToUpdateShaderBindingInstance = false;

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        for (int32 i = 0; i < (int32)EMaterialTextureType::Max; ++i)
        {
            const TextureData& TextureData = TexData[i];
            const jTexture* Texture = TextureData.GetTexture();

            if (!Texture)
            {
                if ((int32)EMaterialTextureType::Normal == i)
                    Texture = GNormalTexture;
                else if ((int32)EMaterialTextureType::Env == i)
                    Texture = GWhiteCubeTexture;
                else
                    Texture = GWhiteTexture;
            }

            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllactor.Alloc<jTextureResource>(Texture, nullptr));
        }

        if (ShaderBindingInstance)
            ShaderBindingInstance->Free();

        ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
    }
    return ShaderBindingInstance;
}
