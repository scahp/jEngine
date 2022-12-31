#include "pch.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jSwapchain.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "jOptions.h"

void jSceneRenderTarget::Create(const jSwapchainImage* image)
{
    ColorPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->GetSelectedMSAASamples() });

    {
        int32 Width = SCR_WIDTH / 4;
        int32 Height = SCR_HEIGHT / 4;

        BloomSetup = jRenderTargetPool::GetRenderTarget(
            { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi_vk->GetSelectedMSAASamples() });

        for (int32 i = 0; i < _countof(DownSample); ++i)
        {
            Width /= 2;
            Height /= 2;

            DownSample[i] = jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi_vk->GetSelectedMSAASamples() });
        }

        for (int32 i = 0; i < _countof(UpSample); ++i)
        {
            Width *= 2;
            Height *= 2;

            UpSample[i] = jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi_vk->GetSelectedMSAASamples() });
        }
    }

    DepthPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->GetSelectedMSAASamples() });

    if ((int32)g_rhi_vk->GetSelectedMSAASamples() > 1)
    {
        check(image);
        ResolvePtr = jRenderTarget::CreateFromTexture(image->TexturePtr);
    }

    if (!FinalColorPtr)
    {
        FinalColorPtr = jRenderTarget::CreateFromTexture(image->TexturePtr);
    }

    DirectionalLightShadowMapPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, jDirectionalLight::SM_Width, jDirectionalLight::SM_Height, 1, false, EMSAASamples::COUNT_1 });

    CubeShadowMapPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_CUBE, ETextureFormat::D24_S8, jPointLight::SM_Width, jPointLight::SM_Height, 6, false, EMSAASamples::COUNT_1 });

    SpotLightShadowMapPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, jSpotLight::SM_Width, jSpotLight::SM_Height, 1, false, EMSAASamples::COUNT_1 });

    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        const bool UseAsSubpassInput = gOptions.UseSubpass;
        const bool IsMemoryless = gOptions.UseMemoryless && gOptions.UseSubpass;
        GBuffer[i] = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, SCR_WIDTH, SCR_HEIGHT
            , 1, false, g_rhi_vk->GetSelectedMSAASamples(), UseAsSubpassInput, IsMemoryless });
    }
}

void jSceneRenderTarget::Return()
{
    if (ColorPtr)
        ColorPtr->Return();
    if (DepthPtr)
        DepthPtr->Return();
    if (ResolvePtr)
        ResolvePtr->Return();
    if (DirectionalLightShadowMapPtr)
        DirectionalLightShadowMapPtr->Return();
    if (CubeShadowMapPtr)
        CubeShadowMapPtr->Return();
    if (SpotLightShadowMapPtr)
        SpotLightShadowMapPtr->Return();
    if (BloomSetup)
        BloomSetup->Return();
    for (int32 i = 0; i < _countof(DownSample); ++i)
        DownSample[i]->Return();
    for (int32 i = 0; i < _countof(UpSample); ++i)
        UpSample[i]->Return();
    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        if (GBuffer[i])
            GBuffer[i]->Return();
    }
}

jShaderBindingInstance* jSceneRenderTarget::PrepareGBufferShaderBindingInstance(bool InUseAsSubpassInput) const
{
    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        if (InUseAsSubpassInput)
        {
            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::SUBPASS_INPUT_ATTACHMENT, EShaderAccessStageFlag::FRAGMENT
                , ResourceInlineAllocator.Alloc<jTextureResource>(GBuffer[i]->GetTexture(), nullptr));
        }
        else
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), true, ECompareOp::LESS>::Create();

            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllocator.Alloc<jTextureResource>(GBuffer[i]->GetTexture(), ShadowSamplerStateInfo));
        }
    }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
}
