#include "pch.h"
#include "jMaterial.h"
#include "RHI/jRHI.h"

jShaderBindingInstance* jMaterial::CreateShaderBindingInstance()
{
    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

    // for (int32 i = 0; i < (int32)EMaterialTextureType::Max; ++i)
    for (int32 i = 0; i < (int32)EMaterialTextureType::Max; ++i)
    {
        TextureData& TextureData = TexData[i];
        const jTexture* Texture = TextureData.GetTexture();

        if (!Texture)
        {
            if ((int32)EMaterialTextureType::NormalSampler == i)
                Texture = GNormalTexture;
            else
                Texture = GWhiteTexture;
        }

        ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jTextureResource>(Texture, nullptr));
    }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray);
}
