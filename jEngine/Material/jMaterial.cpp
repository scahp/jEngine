#include "pch.h"
#include "jMaterial.h"
#include "RHI/jRHI.h"

jTexture* jMaterial::GetTexture(EMaterialTextureType InType) const
{
    check(EMaterialTextureType::Albedo <= InType);
    check(EMaterialTextureType::Max > InType);

    if (!TexData[(int32)InType].Texture)
    {
        if (InType == EMaterialTextureType::Normal)
            return GNormalTexture.get();
        return GBlackTexture.get();
    }

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
                    Texture = GNormalTexture.get();
                else if ((int32)EMaterialTextureType::Env == i)
                    Texture = GWhiteCubeTexture.get();
                else
                    Texture = GWhiteTexture.get();
            }

            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllactor.Alloc<jTextureResource>(Texture, nullptr)));
        }

        if (MaterialDataPtr && MaterialDataPtr->GetData() && MaterialDataPtr->GetDataSizeInBytes() > 0)
        {
            MaterialDataUniformBufferPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("MaterialDataUniformBuffer"), jLifeTimeType::MultiFrame, MaterialDataPtr->GetDataSizeInBytes());
            MaterialDataUniformBufferPtr->UpdateBufferData(MaterialDataPtr->GetData(), MaterialDataPtr->GetDataSizeInBytes());
			ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_GRAPHICS
				, ResourceInlineAllactor.Alloc<jUniformBufferResource>(MaterialDataUniformBufferPtr.get())));
        }

        if (ShaderBindingInstance)
            ShaderBindingInstance->Free();

        ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
    }
    return ShaderBindingInstance;
}
