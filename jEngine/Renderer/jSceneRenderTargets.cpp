#include "pch.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jSwapchain.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "jOptions.h"

// 임시
std::shared_ptr<jRenderTarget> jSceneRenderTarget::IrradianceMap;
jTexture* jSceneRenderTarget::OriginHDR = nullptr;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::FilteredEnvMap;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::CubeEnvMap;
jTexture* jSceneRenderTarget::IrradianceMap2 = nullptr;
jTexture* jSceneRenderTarget::FilteredEnvMap2 = nullptr;
jTexture* jSceneRenderTarget::CubeEnvMap2 = nullptr;
std::shared_ptr<jTexture> jSceneRenderTarget::HistoryBuffer;
std::shared_ptr<jTexture> jSceneRenderTarget::HistoryDepthBuffer;
std::shared_ptr<jTexture> jSceneRenderTarget::GaussianV;
std::shared_ptr<jTexture> jSceneRenderTarget::GaussianH;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::AOProjection;

// todo : remove this.
#include "jPrimitiveUtil.h"
jFullscreenQuadPrimitive* jSceneRenderTarget::GlobalFullscreenPrimitive = nullptr;
//////////////////////////////////////////////////////////////////////////

void jSceneRenderTarget::Create(const jSwapchainImage* InSwapchain, const std::vector<jLight*>* InLights)
{
    jRenderTargetInfo ColorRTInfo = { ETextureType::TEXTURE_2D, ETextureFormat::R11G11B10F, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) };
    ColorRTInfo.ResourceName = TEXT("ColorPtr");
    ColorRTInfo.TextureCreateFlag = ETextureCreateFlag::UAV;
    ColorPtr = jRenderTargetPool::GetRenderTarget(ColorRTInfo);

    {
        int32 Width = SCR_WIDTH / 4;
        int32 Height = SCR_HEIGHT / 4;

        jRenderTargetInfo BloomSetupRTInfo = { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) };
        BloomSetupRTInfo.ResourceName = TEXT("BloomSetup");
        BloomSetup = jRenderTargetPool::GetRenderTarget(BloomSetupRTInfo);

        wchar_t TempStr[1024];
        for (int32 i = 0; i < _countof(DownSample); ++i)
        {
            Width /= 2;
            Height /= 2;

            wsprintf(TempStr, TEXT("DownSample[%d]"), i);
            jRenderTargetInfo DownSampleRTInfo = { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) };
            DownSampleRTInfo.ResourceName = TempStr;

            DownSample[i] = jRenderTargetPool::GetRenderTarget(DownSampleRTInfo);
        }

        for (int32 i = 0; i < _countof(UpSample); ++i)
        {
            Width *= 2;
            Height *= 2;

            wsprintf(TempStr, TEXT("UpSample[%d]"), i);
            jRenderTargetInfo UpSampleRTInfo = { ETextureType::TEXTURE_2D, ETextureFormat::RGBA16F, Width, Height, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f) };
            UpSampleRTInfo.ResourceName = TempStr;

            UpSample[i] = jRenderTargetPool::GetRenderTarget(UpSampleRTInfo);
        }
    }

    jRenderTargetInfo DepthRTInfo = { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(1.0f, 0) };
    DepthRTInfo.ResourceName = TEXT("DepthPtr");
    DepthRTInfo.IsUseAsSubpassInput = gOptions.UseSubpass;
    DepthRTInfo.IsMemoryless = gOptions.UseMemoryless;
    DepthPtr = jRenderTargetPool::GetRenderTarget(DepthRTInfo);

    if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
    {
        check(InSwapchain);
        ResolvePtr = jRenderTarget::CreateFromTexture(InSwapchain->TexturePtr);
    }

    if (!FinalColorPtr)
    {
        FinalColorPtr = jRenderTarget::CreateFromTexture(InSwapchain->TexturePtr);
    }

    {
        jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, SCR_WIDTH / 2, SCR_HEIGHT / 2
                   , 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), ETextureCreateFlag::RTV | ETextureCreateFlag::UAV, false, false };
        Info.ResourceName = L"AtmosphericShadowing";
        AtmosphericShadowing = jRenderTargetPool::GetRenderTarget(Info);
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
                jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::D16, jDirectionalLight::SM_Width, jDirectionalLight::SM_Height, 1, false, EMSAASamples::COUNT_1, RTClearValue };
                Info.ResourceName = TEXT("DirectionalLight_ShadowMap");
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            else if (light->Type == ELightType::POINT)
            {
                jRenderTargetInfo Info = { ETextureType::TEXTURE_CUBE, ETextureFormat::D16, jPointLight::SM_Width, jPointLight::SM_Height, 6, false, EMSAASamples::COUNT_1, RTClearValue };
                Info.ResourceName = TEXT("PointLight_ShadowMap");
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            else if (light->Type == ELightType::SPOT)
            {
                jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::D16, jSpotLight::SM_Width, jSpotLight::SM_Height, 1, false, EMSAASamples::COUNT_1, RTClearValue };
                Info.ResourceName = TEXT("SpotLight_ShadowMap");
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            LightShadowMapPtr.insert(std::make_pair(light, ShadowMapPtr));
        }
    }

    wchar_t TempStr[256] = { 0, };
    ETextureFormat GBufferTexFormat[_countof(GBuffer)] = { ETextureFormat::RGBA16F, ETextureFormat::RGBA8, ETextureFormat::RG16F };
    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        const bool UseAsSubpassInput = gOptions.UseSubpass;
        const bool IsMemoryless = gOptions.UseMemoryless && gOptions.UseSubpass;

        jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, GBufferTexFormat[i], SCR_WIDTH, SCR_HEIGHT
            , 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f), ETextureCreateFlag::NONE, UseAsSubpassInput, IsMemoryless };

        wsprintf(TempStr, TEXT("GBuffer[%d]"), i);
        Info.ResourceName = TempStr;
        GBuffer[i] = jRenderTargetPool::GetRenderTarget(Info);
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
    if (AtmosphericShadowing)
        AtmosphericShadowing->Return();
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

std::shared_ptr<jShaderBindingInstance> jSceneRenderTarget::PrepareGBufferShaderBindingInstance(bool InUseAsSubpassInput) const
{
    int32 BindingPoint = 0;
    jShaderBindingArray ShaderBindingArray;
    jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        if (InUseAsSubpassInput)
        {
            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::SUBPASS_INPUT_ATTACHMENT, EShaderAccessStageFlag::FRAGMENT
                , ResourceInlineAllocator.Alloc<jTextureResource>(GBuffer[i]->GetTexture(), nullptr)));
        }
        else
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

            ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllocator.Alloc<jTextureResource>(GBuffer[i]->GetTexture(), ShadowSamplerStateInfo)));
        }
    }

    if (InUseAsSubpassInput)
    {
        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::SUBPASS_INPUT_ATTACHMENT, EShaderAccessStageFlag::FRAGMENT
            , ResourceInlineAllocator.Alloc<jTextureResource>(DepthPtr->GetTexture(), nullptr)));
    }
    else
    {
        const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllocator.Alloc<jTextureResource>(DepthPtr->GetTexture(), ShadowSamplerStateInfo)));
    }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
}
