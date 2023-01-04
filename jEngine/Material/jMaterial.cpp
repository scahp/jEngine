#include "pch.h"
#include "jMaterial.h"
#include "RHI/jRHI.h"

jShaderBindingInstance* jMaterial::CreateShaderBindingInstance()
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
                else
                    Texture = GWhiteTexture;
            }

            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllactor.Alloc<jTextureResource>(Texture, nullptr));
        }

        if (ShaderBindingInstance)
            ShaderBindingInstance->Free();

        ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
    }
    return ShaderBindingInstance;
}
