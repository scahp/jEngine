#include "pch.h"
#include "jMaterial.h"
#include "jMaterialWater.h"
#include "RHI/jRHI.h"
#include "Shader/jCommonShaderParameters.h"
#include <cstring>
#include <type_traits>

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
    const bool frameMaterial = Water && Water->Enabled;
    const uint64 frameNumber = g_rhi->GetCurrentFrameNumber();
    if (NeedToUpdateShaderBindingInstance || !ShaderBindingInstance || frameMaterial != MappingBindingIsSingleFrame
        || (frameMaterial && MappingUniformFrame != frameNumber))
    {
        NeedToUpdateShaderBindingInstance = false;

        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
        jMaterialShaderParameters Parameters;

        auto GetResolvedTexture = [this](EMaterialTextureType InType) -> jTexture*
        {
            const jTexture* Texture = TexData[(int32)InType].GetTexture();
            if (!Texture)
            {
                if (InType == EMaterialTextureType::Normal)
                    Texture = GNormalTexture.get();
                else if (InType == EMaterialTextureType::Env)
                    Texture = GWhiteCubeTexture.get();
                else
                    Texture = GWhiteTexture.get();
            }
            return const_cast<jTexture*>(Texture);
        };

        auto CreateTextureParameter = [this, &GetResolvedTexture](EMaterialTextureType InType)
        {
            jShaderParameterTexture2D Result;
            Result.Texture = GetResolvedTexture(InType);
            Result.SamplerState = GetTextureSamplerState(InType);
            return Result;
        };

        Parameters.DiffuseTexture = CreateTextureParameter(EMaterialTextureType::Albedo);
        Parameters.NormalTexture = CreateTextureParameter(EMaterialTextureType::Normal);
        Parameters.RMTexture = CreateTextureParameter(EMaterialTextureType::Metallic);
        Parameters.EnvTexture.Texture = GetResolvedTexture(EMaterialTextureType::Env);
        Parameters.EnvTexture.SamplerState = GetTextureSamplerState(EMaterialTextureType::Env);
        jShaderParameterTexture2D* mappingParameters[] = {&Parameters.MappingTexture0, &Parameters.MappingTexture1,
            &Parameters.MappingTexture2, &Parameters.MappingTexture3, &Parameters.MappingTexture4, &Parameters.MappingTexture5,
            &Parameters.MappingTexture6, &Parameters.MappingTexture7};
        for (size_t i = 0; i < MappingTextures.size(); ++i)
        {
            mappingParameters[i]->Texture = MappingTextures[i] ? MappingTextures[i].get() : GWhiteTexture.get();
            mappingParameters[i]->SamplerState = GetTextureSamplerState(EMaterialTextureType::Albedo);
        }
        jMaterialGraphUniformBuffer graphParameters;
        // Vector4's user-provided default constructor intentionally leaves its
        // components untouched, so aggregate braces do not zero this payload.
        static_assert(std::is_trivially_copyable_v<jMaterialGraphUniformBuffer>);
        std::memset(&graphParameters, 0, sizeof(graphParameters));
        const size_t valueCount = MappingProgram && MappingProgram->Active
            ? std::min<size_t>(MappingProgram->ValueCount, MappingValues.size()) : 0;
        for (size_t i = 0; i < valueCount; ++i) graphParameters.Values[i] = MappingValues[i];
        graphParameters.OutputOverridesMask = MappingOutputOverridesMask;
        const auto surface = Surface.value_or(jSurfaceShading{});
        const auto bounded = [](float value, float low, float high, float fallback)
        { return std::isfinite(value) ? std::clamp(value, low, high) : fallback; };
        graphParameters.MaterialSubsurfaceColorWeight = Vector4(
            bounded(surface.SubsurfaceColor[0], 0, 1, 1), bounded(surface.SubsurfaceColor[1], 0, 1, 1),
            bounded(surface.SubsurfaceColor[2], 0, 1, 1), bounded(surface.Subsurface, 0, 1, 0));
        graphParameters.MaterialSubsurfaceParameters = Vector4(bounded(surface.SubsurfaceThicknessWorld, 0, 10000, 1),
            float(surface.SubsurfaceProfile < 8 ? surface.SubsurfaceProfile : 0), 0, 0);
        const uint32 shadingModel = uint32(surface.ShadingModel) <= 1 ? uint32(surface.ShadingModel) : 0;
        const uint32 subsurfaceSpace = uint32(surface.SubsurfaceSpace) <= 1 ? uint32(surface.SubsurfaceSpace) : 0;
        graphParameters.MaterialSkinParameters = Vector4(float(shadingModel | (subsurfaceSpace << 8)),
            bounded(surface.SpecularWeight, 0, 8, 1), bounded(surface.DiffuseOcclusion, 0, 1, 1),
            bounded(surface.PreScatterWeight, 0, 1, 1));
        FillMaterialWaterShaderParameters(Water.get(), graphParameters);
        MappingUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("MaterialGraphParameters"), frameMaterial ? jLifeTimeType::OneFrame : jLifeTimeType::MultiFrame, sizeof(graphParameters)));
        MappingUniformBuffer->UpdateBufferData(&graphParameters, sizeof(graphParameters));
        Parameters.MaterialGraphParam.Buffer = MappingUniformBuffer;

        jShaderParameterSet::BuildShaderBindings(Parameters, EShaderAccessStageFlag::ALL_GRAPHICS, ShaderBindingArray, ResourceInlineAllactor);

        if (ShaderBindingInstance && !MappingBindingIsSingleFrame)
            ShaderBindingInstance->Free();

        MappingBindingIsSingleFrame = frameMaterial;
        MappingUniformFrame = frameNumber;
        ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray,
            frameMaterial ? jShaderBindingInstanceType::SingleFrame : jShaderBindingInstanceType::MultiFrame);
    }
    return ShaderBindingInstance;
}
