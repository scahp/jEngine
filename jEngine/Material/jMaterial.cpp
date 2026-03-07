#include "pch.h"
#include "jMaterial.h"
#include "RHI/jRHI.h"

namespace
{
    FORCEINLINE ETextureAddressMode GetSafeTextureAddressMode(ETextureAddressMode InMode)
    {
        return (InMode == ETextureAddressMode::MAX) ? ETextureAddressMode::REPEAT : InMode;
    }

    FORCEINLINE ETextureFilter GetSafeTextureFilter(ETextureFilter InFilter, ETextureFilter InDefault)
    {
        return (InFilter == ETextureFilter::MAX) ? InDefault : InFilter;
    }
}

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

jSamplerStateInfo* jMaterial::GetTextureSamplerState(EMaterialTextureType InType) const
{
    check(EMaterialTextureType::Albedo <= InType);
    check(EMaterialTextureType::Max > InType);

    const TextureData& TextureDataRef = TexData[(int32)InType];
    const ETextureAddressMode AddressU = GetSafeTextureAddressMode(TextureDataRef.TextureAddressModeU);
    const ETextureAddressMode AddressV = GetSafeTextureAddressMode(TextureDataRef.TextureAddressModeV);
    const ETextureFilter MinificationFilter = GetSafeTextureFilter(TextureDataRef.MinificationFilter, ETextureFilter::LINEAR_MIPMAP_LINEAR);
    const ETextureFilter MagnificationFilter = GetSafeTextureFilter(TextureDataRef.MagnificationFilter, ETextureFilter::LINEAR);
    const float MaxAnisotropy = (TextureDataRef.MaxAnisotropy < 1.0f) ? 1.0f : ((TextureDataRef.MaxAnisotropy > 16.0f) ? 16.0f : TextureDataRef.MaxAnisotropy);
    const bool NeedUpdateSampler = (!TextureDataRef.SamplerState)
        || (TextureDataRef.CachedSamplerAddressModeU != AddressU)
        || (TextureDataRef.CachedSamplerAddressModeV != AddressV)
        || (TextureDataRef.CachedMinificationFilter != MinificationFilter)
        || (TextureDataRef.CachedMagnificationFilter != MagnificationFilter)
        || (TextureDataRef.CachedMaxAnisotropy != MaxAnisotropy);

    if (NeedUpdateSampler)
    {
        jSamplerStateInfo SamplerStateInitializer;
        SamplerStateInitializer.Minification = MinificationFilter;
        SamplerStateInitializer.Magnification = MagnificationFilter;
        SamplerStateInitializer.AddressU = AddressU;
        SamplerStateInitializer.AddressV = AddressV;
        SamplerStateInitializer.AddressW = ETextureAddressMode::REPEAT;
        SamplerStateInitializer.MipLODBias = 0.0f;
        SamplerStateInitializer.MaxAnisotropy = MaxAnisotropy;
        SamplerStateInitializer.IsEnableComparisonMode = false;
        SamplerStateInitializer.ComparisonFunc = ECompareOp::NEVER;
        SamplerStateInitializer.TextureComparisonMode = ETextureComparisonMode::NONE;
        SamplerStateInitializer.MinLOD = 0.0f;
        SamplerStateInitializer.MaxLOD = FLT_MAX;
        SamplerStateInitializer.GetHash();
        TextureDataRef.SamplerState = g_rhi->CreateSamplerState(SamplerStateInitializer);
        TextureDataRef.CachedSamplerAddressModeU = AddressU;
        TextureDataRef.CachedSamplerAddressModeV = AddressV;
        TextureDataRef.CachedMinificationFilter = MinificationFilter;
        TextureDataRef.CachedMagnificationFilter = MagnificationFilter;
        TextureDataRef.CachedMaxAnisotropy = MaxAnisotropy;
    }

    return TextureDataRef.SamplerState;
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
            const TextureData& TextureDataRef = TexData[i];
            const jTexture* Texture = TextureDataRef.GetTexture();

            if (!Texture)
            {
                if ((int32)EMaterialTextureType::Normal == i)
                    Texture = GNormalTexture.get();
                else if ((int32)EMaterialTextureType::Env == i)
                    Texture = GWhiteCubeTexture.get();
                else
                    Texture = GWhiteTexture.get();
            }

            jSamplerStateInfo* SamplerState = GetTextureSamplerState((EMaterialTextureType)i);
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllactor.Alloc<jTextureResource>(Texture, SamplerState)));
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
