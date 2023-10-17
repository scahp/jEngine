#include "pch.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jSwapchain.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "jOptions.h"

void jSceneRenderTarget::Create(const jSwapchainImage* InSwapchain, const std::vector<jLight*>* InLights)
{
    ColorPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) });

    {
        int32 Width = SCR_WIDTH / 4;
        int32 Height = SCR_HEIGHT / 4;

        BloomSetup = jRenderTargetPool::GetRenderTarget(
            { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) });

        for (int32 i = 0; i < _countof(DownSample); ++i)
        {
            Width /= 2;
            Height /= 2;

            DownSample[i] = jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) });
        }

        for (int32 i = 0; i < _countof(UpSample); ++i)
        {
            Width *= 2;
            Height *= 2;

            UpSample[i] = jRenderTargetPool::GetRenderTarget(
                { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) });
        }
    }

    DepthPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(1.0f, 0) });

    if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
    {
        check(InSwapchain);
        ResolvePtr = jRenderTarget::CreateFromTexture(InSwapchain->TexturePtr);
    }

    if (!FinalColorPtr)
    {
        FinalColorPtr = jRenderTarget::CreateFromTexture(InSwapchain->TexturePtr);
    }

    if (InLights)
    {
        for (int32 i = 0; i < InLights->size(); ++i)
        {
            jLight* light = InLights->at(i);

            const bool IsUseReverseZShadow = USE_REVERSEZ_PERSPECTIVE_SHADOW && (light->IsUseRevereZPerspective());
            const jRTClearValue RTClearValue = IsUseReverseZShadow ? jRTClearValue(0.0f, 0) : jRTClearValue(1.0f, 0);

            // todo ShadowMap 크기나 스펙을 Light 에서 정의할 수 있게 하는건 어떨까?
            std::shared_ptr<jRenderTarget> ShadowMapPtr;
            if (light->Type == ELightType::DIRECTIONAL)
            {
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(
                    { ETextureType::TEXTURE_2D, ETextureFormat::D16, jDirectionalLight::SM_Width, jDirectionalLight::SM_Height, 1, false, EMSAASamples::COUNT_1, RTClearValue });
            }
            else if (light->Type == ELightType::POINT)
            {
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(
                    { ETextureType::TEXTURE_CUBE, ETextureFormat::D16, jPointLight::SM_Width, jPointLight::SM_Height, 6, false, EMSAASamples::COUNT_1,RTClearValue });
            }
            else if (light->Type == ELightType::SPOT)
            {
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(
                    { ETextureType::TEXTURE_2D, ETextureFormat::D16, jSpotLight::SM_Width, jSpotLight::SM_Height, 1, false, EMSAASamples::COUNT_1, RTClearValue });
            }
            LightShadowMapPtr.insert(std::make_pair(light, ShadowMapPtr));
        }
    }

    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        const bool UseAsSubpassInput = gOptions.UseSubpass;
        const bool IsMemoryless = gOptions.UseMemoryless && gOptions.UseSubpass;
        GBuffer[i] = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, SCR_WIDTH, SCR_HEIGHT
            , 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), UseAsSubpassInput, IsMemoryless });
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
    for(auto it : LightShadowMapPtr)
    {
        if (it.second)
            it.second->Return();
    }
    if (BloomSetup)
        BloomSetup->Return();
    for (int32 i = 0; i < _countof(DownSample); ++i)
    {
        if (DownSample[i])
            DownSample[i]->Return();
    }
    for (int32 i = 0; i < _countof(UpSample); ++i)
    {
        if (UpSample[i])
            UpSample[i]->Return();
    }
    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        if (GBuffer[i])
            GBuffer[i]->Return();
    }
}

std::shared_ptr<jRenderTarget> jSceneRenderTarget::GetShadowMap(const jLight* InLight) const
{
    auto it_find = LightShadowMapPtr.find(InLight);
    if (LightShadowMapPtr.end() != it_find)
    {
        return it_find->second;
    }
    return std::shared_ptr<jRenderTarget>();
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
            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::SUBPASS_INPUT_ATTACHMENT, EShaderAccessStageFlag::FRAGMENT
                , ResourceInlineAllocator.Alloc<jTextureResource>(GBuffer[i]->GetTexture(), nullptr));
        }
        else
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllocator.Alloc<jTextureResource>(GBuffer[i]->GetTexture(), ShadowSamplerStateInfo));
        }
    }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
}
