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
std::shared_ptr<jRenderTarget> jSceneRenderTarget::GIProjection;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::SSGI_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::SSGI_Accum_RT[3];
std::shared_ptr<jRenderTarget> jSceneRenderTarget::SurfelGI_Debug_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::SurfelGI_Attempt_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::AtmosphericShadow_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::VBuffer_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::HitObject_RT;

// todo : remove this.
#include "jPrimitiveUtil.h"
jFullscreenQuadPrimitive* jSceneRenderTarget::GlobalFullscreenPrimitive = nullptr;
//////////////////////////////////////////////////////////////////////////

void jSceneRenderTarget::Create(const jSwapchainImage* InSwapchain, const std::vector<jLight*>* InLights)
{
    jRenderTargetInfo ColorRTInfo = {
        .Type = ETextureType::TEXTURE_2D,
        .Format = ETextureFormat::R11G11B10F,
        .Width = SCR_WIDTH,
        .Height = SCR_HEIGHT,
        .LayerCount = 1,
        .IsGenerateMipmap = false,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("ColorPtr")
    };
    ColorPtr = jRenderTargetPool::GetRenderTarget(ColorRTInfo);

    {
        int32 Width = SCR_WIDTH / 4;
        int32 Height = SCR_HEIGHT / 4;

        jRenderTargetInfo BloomSetupRTInfo = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::RGBA16F,
            .Width = Width,
            .Height = Height,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = g_rhi->GetSelectedMSAASamples(),
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .ResourceName = jNameStatic("BloomSetup")
        };
        BloomSetup = jRenderTargetPool::GetRenderTarget(BloomSetupRTInfo);

        wchar_t TempStr[1024];
        for (int32 i = 0; i < _countof(DownSample); ++i)
        {
            Width /= 2;
            Height /= 2;

            wsprintf(TempStr, TEXT("DownSample[%d]"), i);
            jRenderTargetInfo DownSampleRTInfo = {
                .Type = ETextureType::TEXTURE_2D,
                .Format = ETextureFormat::RGBA16F,
                .Width = Width,
                .Height = Height,
                .LayerCount = 1,
                .IsGenerateMipmap = false,
                .SampleCount = g_rhi->GetSelectedMSAASamples(),
                .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                .ResourceName = jName(TempStr)
            };

            DownSample[i] = jRenderTargetPool::GetRenderTarget(DownSampleRTInfo);
        }

        for (int32 i = 0; i < _countof(UpSample); ++i)
        {
            Width *= 2;
            Height *= 2;

            wsprintf(TempStr, TEXT("UpSample[%d]"), i);
            jRenderTargetInfo UpSampleRTInfo = {
                .Type = ETextureType::TEXTURE_2D,
                .Format = ETextureFormat::RGBA16F,
                .Width = Width,
                .Height = Height,
                .LayerCount = 1,
                .IsGenerateMipmap = false,
                .SampleCount = g_rhi->GetSelectedMSAASamples(),
                .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                .ResourceName = jName(TempStr)
            };

            UpSample[i] = jRenderTargetPool::GetRenderTarget(UpSampleRTInfo);
        }
    }

    jRenderTargetInfo DepthRTInfo = {
        .Type = ETextureType::TEXTURE_2D,
        .Format = ETextureFormat::D24_S8,
        .Width = SCR_WIDTH,
        .Height = SCR_HEIGHT,
        .LayerCount = 1,
        .IsGenerateMipmap = false,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .IsUseAsSubpassInput = gOptions.UseSubpass,
        .IsMemoryless = gOptions.UseMemoryless,
        .RTClearValue = jRTClearValue(1.0f, 0),
        .ResourceName = jNameStatic("DepthPtr")
    };
    DepthPtr = jRenderTargetPool::GetRenderTarget(DepthRTInfo);

    if (DepthPtr)
    {
        const int32 DepthWidth = DepthPtr->Info.Width;
        const int32 DepthHeight = DepthPtr->Info.Height;
        if (!jSceneRenderTarget::HistoryDepthBuffer || jSceneRenderTarget::HistoryDepthBuffer->Width != DepthWidth || jSceneRenderTarget::HistoryDepthBuffer->Height != DepthHeight)
        {
            jSceneRenderTarget::HistoryDepthBuffer = g_rhi->Create2DTexture((uint32)DepthWidth, (uint32)DepthHeight, (uint32)1, (uint32)1
                , ETextureFormat::R16F, ETextureCreateFlag::UAV, EResourceLayout::UAV);
        }
    }

    jRenderTargetInfo LinearDepthRTInfo = {
        .Type = ETextureType::TEXTURE_2D,
        .Format = ETextureFormat::R32F,
        .Width = SCR_WIDTH,
        .Height = SCR_HEIGHT,
        .LayerCount = 1,
        .IsGenerateMipmap = false,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .RTClearValue = jRTClearValue(0.0f, 0),
        .TextureCreateFlag = ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("LinearDepthPtr")
    };
    LinearDepthPtr = jRenderTargetPool::GetRenderTarget(LinearDepthRTInfo);

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
        jRenderTargetInfo Info = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::R16F,
            .Width = SCR_WIDTH / 2,
            .Height = SCR_HEIGHT / 2,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = g_rhi->GetSelectedMSAASamples(),
            .IsUseAsSubpassInput = false,
            .IsMemoryless = false,
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::UAV,
            .ResourceName = jNameStatic("AtmosphericShadowing")
        };
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
                jRenderTargetInfo Info = {
                    .Type = ETextureType::TEXTURE_2D,
                    .Format = ETextureFormat::D16,
                    .Width = jDirectionalLight::SM_Width,
                    .Height = jDirectionalLight::SM_Height,
                    .LayerCount = 1,
                    .IsGenerateMipmap = false,
                    .SampleCount = EMSAASamples::COUNT_1,
                    .RTClearValue = RTClearValue,
                    .ResourceName = jNameStatic("DirectionalLight_ShadowMap")
                };
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            else if (light->Type == ELightType::POINT)
            {
                jRenderTargetInfo Info = {
                    .Type = ETextureType::TEXTURE_CUBE,
                    .Format = ETextureFormat::D16,
                    .Width = jPointLight::SM_Width,
                    .Height = jPointLight::SM_Height,
                    .LayerCount = 6,
                    .IsGenerateMipmap = false,
                    .SampleCount = EMSAASamples::COUNT_1,
                    .RTClearValue = RTClearValue,
                    .ResourceName = jNameStatic("PointLight_ShadowMap")
                };
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            else if (light->Type == ELightType::SPOT)
            {
                jRenderTargetInfo Info = {
                    .Type = ETextureType::TEXTURE_2D,
                    .Format = ETextureFormat::D16,
                    .Width = jSpotLight::SM_Width,
                    .Height = jSpotLight::SM_Height,
                    .LayerCount = 1,
                    .IsGenerateMipmap = false,
                    .SampleCount = EMSAASamples::COUNT_1,
                    .RTClearValue = RTClearValue,
                    .ResourceName = jNameStatic("SpotLight_ShadowMap")
                };
                ShadowMapPtr = jRenderTargetPool::GetRenderTarget(Info);
            }
            LightShadowMapPtr.insert(std::make_pair(light, ShadowMapPtr));
        }
    }

    wchar_t TempStr[256] = { 0, };
    ETextureFormat GBufferTexFormat[_countof(GBuffer)] = { ETextureFormat::R11G11B10F, ETextureFormat::R11G11B10F, ETextureFormat::RGBA16F };
    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        const bool UseAsSubpassInput = gOptions.UseSubpass;
        const bool IsMemoryless = gOptions.UseMemoryless && gOptions.UseSubpass;

        wsprintf(TempStr, TEXT("GBuffer[%d]"), i);
        jRenderTargetInfo Info = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = GBufferTexFormat[i],
            .Width = SCR_WIDTH,
            .Height = SCR_HEIGHT,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = g_rhi->GetSelectedMSAASamples(),
            .IsUseAsSubpassInput = UseAsSubpassInput,
            .IsMemoryless = IsMemoryless,
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .TextureCreateFlag = ETextureCreateFlag::NONE,
            .ResourceName = jName(TempStr)
        };
        GBuffer[i] = jRenderTargetPool::GetRenderTarget(Info);
    }
}

void jSceneRenderTarget::Return()
{
    if (ColorPtr)
        ColorPtr->Return();
    if (DepthPtr)
        DepthPtr->Return();
    if (LinearDepthPtr)
        LinearDepthPtr->Return();
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
    if (GIProjection)
        GIProjection->Return();
    if (SSGI_RT)
        SSGI_RT->Return();
    if (SurfelGI_Debug_RT)
        SurfelGI_Debug_RT->Return();
    if (SurfelGI_Attempt_RT)
        SurfelGI_Attempt_RT->Return();
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
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

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
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllocator.Alloc<jTextureResource>(DepthPtr->GetTexture(), ShadowSamplerStateInfo)));
    }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
}
