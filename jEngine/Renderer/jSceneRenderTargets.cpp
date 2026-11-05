#include "pch.h"
#include "jSceneRenderTargets.h"
#include "jAdvancedShadowPass.h"
#include "jBloomDimensions.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jSwapchain.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Shader/jLightingShaderParameters.h"
#include "jOptions.h"
#include "jEngine.h"
#include "jHiZOcclusionPass.h"
#include "jSubsurfacePass.h"
#include "jRasterFeatureDebug.h"
#include "jSkinShading.h"
#include "jTextureSubsurfacePass.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Material/jMaterial.h"

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
std::shared_ptr<jRenderTarget> jSceneRenderTarget::SurfelGI_Resolve_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::AtmosphericShadow_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::VBuffer_RT;
std::shared_ptr<jRenderTarget> jSceneRenderTarget::HitObject_RT;

// todo : remove this.
#include "jPrimitiveUtil.h"
jFullscreenQuadPrimitive* jSceneRenderTarget::GlobalFullscreenPrimitive = nullptr;
//////////////////////////////////////////////////////////////////////////

void jSceneRenderTarget::Create(const jSwapchainImage* InSwapchain, const std::vector<jLight*>* InLights)
{
    auto& diagnostics = MutableRendererFeatureDiagnostics();
    const bool raster = !g_Engine || !g_Engine->Game.IsUsingPathTracingRenderer();
    const bool deferred = raster && gOptions.UseDeferredRenderer;
    // Model selection is independent of the global diffusion switch. Turning
    // scattering off must retain the same pore normals and skin BRDF.
    HasSkinMaterials = false;
    HasTextureSubsurfaceMaterials = false;
    if (deferred)
        for (const auto* object : jObject::GetStaticRenderObject())
            if (object && object->MaterialPtr && object->MaterialPtr->Surface)
            {
                const auto& surface = *object->MaterialPtr->Surface;
                HasSkinMaterials |= surface.ShadingModel == jSurfaceShadingModel::SkinKelemen;
                HasTextureSubsurfaceMaterials |= surface.ShadingModel == jSurfaceShadingModel::SkinKelemen
                    && surface.SubsurfaceSpace == jSubsurfaceSpace::Texture;
            }
    const uint32 requested = gOptions.RequestedMSAASamples;
    const uint64 bytesPerPixel = 32u + (HasSkinMaterials ? 12u : 0u)
        + (jSubsurface::HasEffect(gOptions.Subsurface) ? 16u + (HasTextureSubsurfaceMaterials ? 8u : 0u) : 0u);
    const bool withinBudget = SCR_WIDTH > 0 && SCR_HEIGHT > 0 && SCR_WIDTH <= 8192 && SCR_HEIGHT <= 8192 &&
        uint64(SCR_WIDTH) * uint64(SCR_HEIGHT) * bytesPerPixel * (uint64(requested) + 1u) <= 512ull * 1024ull * 1024ull;
    const auto applied = g_rhi->SelectMSAASamples(deferred && withinBudget ? requested : 1);
    diagnostics.AppliedMSAASamples = static_cast<int32>(applied);
    diagnostics.MSAASupportReason = requested == 1 ? "Disabled (1 sample)." : !deferred
        ? "Per-sample MSAA requires the Deferred renderer; using 1 sample."
        : !withinBudget ? "The deferred MSAA attachment budget is 512 MiB; using 1 sample."
        : static_cast<uint32>(applied) != requested
        ? "The requested deferred attachment sample count is unsupported; using 1 sample."
        : "Per-sample deferred lighting; HDR colours resolve after lighting.";
    jSubsurface::Settings subsurface;
    std::string validationError;
    const bool subsurfaceActive = deferred && jSubsurface::ValidateSettings(gOptions.Subsurface, subsurface, validationError)
        && jSubsurface::HasEffect(subsurface);
    diagnostics.SubsurfaceSupportReason = !gOptions.Subsurface.Enabled ? "Disabled."
        : !deferred ? "Screen-space diffusion requires the Deferred renderer."
        : !validationError.empty() ? validationError : !subsurfaceActive ? "Zero diffusion strength or radius." : "Ready.";
    UseSubpass = deferred && gOptions.UseSubpass && applied == EMSAASamples::COUNT_1 && !subsurfaceActive && !HasSkinMaterials;

    jRenderTargetInfo ColorRTInfo = {
        .Type = ETextureType::TEXTURE_2D,
        .Format = ETextureFormat::R11G11B10F,
        .Width = SCR_WIDTH,
        .Height = SCR_HEIGHT,
        .LayerCount = 1,
        .IsGenerateMipmap = false,
        .SampleCount = g_rhi->GetSelectedMSAASamples(),
        .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        .TextureCreateFlag = (applied == EMSAASamples::COUNT_1 ? ETextureCreateFlag::UAV : ETextureCreateFlag::NONE) | ETextureCreateFlag::TransferSrc,
        .ResourceName = jNameStatic("ColorPtr")
    };
    ColorPtr = jRenderTargetPool::GetRenderTarget(ColorRTInfo);

    {
        int32 Width = jBloomDimensions::Downsample(SCR_WIDTH, 4);
        int32 Height = jBloomDimensions::Downsample(SCR_HEIGHT, 4);

        jRenderTargetInfo BloomSetupRTInfo = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::RGBA16F,
            .Width = Width,
            .Height = Height,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = EMSAASamples::COUNT_1,
            .RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .ResourceName = jNameStatic("BloomSetup")
        };
        BloomSetup = jRenderTargetPool::GetRenderTarget(BloomSetupRTInfo);

        wchar_t TempStr[1024];
        for (int32 i = 0; i < _countof(DownSample); ++i)
        {
            Width = jBloomDimensions::Downsample(Width, 2);
            Height = jBloomDimensions::Downsample(Height, 2);

            wsprintf(TempStr, TEXT("DownSample[%d]"), i);
            jRenderTargetInfo DownSampleRTInfo = {
                .Type = ETextureType::TEXTURE_2D,
                .Format = ETextureFormat::RGBA16F,
                .Width = Width,
                .Height = Height,
                .LayerCount = 1,
                .IsGenerateMipmap = false,
                .SampleCount = EMSAASamples::COUNT_1,
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
                .SampleCount = EMSAASamples::COUNT_1,
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
        .IsUseAsSubpassInput = UseSubpass,
        .IsMemoryless = gOptions.UseMemoryless && UseSubpass,
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
        .SampleCount = EMSAASamples::COUNT_1,
        .RTClearValue = jRTClearValue(0.0f, 0),
        .TextureCreateFlag = ETextureCreateFlag::UAV,
        .ResourceName = jNameStatic("LinearDepthPtr")
    };
    LinearDepthPtr = jRenderTargetPool::GetRenderTarget(LinearDepthRTInfo);

    if (!FinalColorPtr)
    {
        FinalColorPtr = jRenderTarget::CreateFromTexture(InSwapchain->TexturePtr);
    }

    {
        const int32 AtmosphericResolutionPercent = Clamp(atoi(gOptions.AtmosphereResolution), 1, 100);
        const int32 AtmosphericWidth = Max(1, (SCR_WIDTH * AtmosphericResolutionPercent + 99) / 100);
        const int32 AtmosphericHeight = Max(1, (SCR_HEIGHT * AtmosphericResolutionPercent + 99) / 100);
        jRenderTargetInfo Info = {
            .Type = ETextureType::TEXTURE_2D,
            .Format = ETextureFormat::R16F,
            .Width = AtmosphericWidth,
            .Height = AtmosphericHeight,
            .LayerCount = 1,
            .IsGenerateMipmap = false,
            .SampleCount = EMSAASamples::COUNT_1,
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
    ETextureFormat GBufferTexFormat[_countof(GBuffer)] = { ETextureFormat::R11G11B10F, ETextureFormat::R11G11B10F, ETextureFormat::RGBA16F, ETextureFormat::R11G11B10F, ETextureFormat::RG16F };
    if (HasSkinMaterials)
    {
        GBufferTexFormat[0] = ETextureFormat::RGBA16F;
        GBufferTexFormat[1] = ETextureFormat::RGBA16F;
        GBufferTexFormat[4] = ETextureFormat::RGBA16F;
    }
    for (int32 i = 0; i < _countof(GBuffer); ++i)
    {
        const bool UseAsSubpassInput = UseSubpass;
        const bool IsMemoryless = gOptions.UseMemoryless && UseSubpass
            && i != int32(EGBufferType::EMISSIVE) && i != 4;

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
            .RTClearValue = i == 4 ? jRTClearValue(1.0f, 1.0f, 0.0f, 0.0f) : jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            .TextureCreateFlag = ETextureCreateFlag::NONE,
            .ResourceName = jName(TempStr)
        };
        GBuffer[i] = jRenderTargetPool::GetRenderTarget(Info);
    }
    if (subsurfaceActive)
    {
        jRenderTargetInfo info = {.Type = ETextureType::TEXTURE_2D, .Format = ETextureFormat::RGBA16F,
            .Width = SCR_WIDTH, .Height = SCR_HEIGHT, .SampleCount = applied,
            .RTClearValue = jRTClearValue(0.f, 0.f, 0.f, 0.f), .TextureCreateFlag = ETextureCreateFlag::TransferSrc,
            .ResourceName = jNameStatic("SubsurfaceMaterialMask")};
        SubsurfaceMask = jRenderTargetPool::GetRenderTarget(info);
        info.ResourceName = jNameStatic("SubsurfaceDiffuseLighting");
        DiffuseLighting = jRenderTargetPool::GetRenderTarget(info);
        if (HasTextureSubsurfaceMaterials)
        {
            info.ResourceName = jNameStatic("SkinSurfaceUVAndIdentity");
            SkinUV = jRenderTargetPool::GetRenderTarget(info);
        }
    }
}

void jSceneRenderTarget::Return()
{
    if (SubsurfaceMask) SubsurfaceMask->Return();
    if (DiffuseLighting) DiffuseLighting->Return();
    if (SkinUV) SkinUV->Return();
    for (auto& target : SkinIntermediateTargets) if (target) target->Return();
    for (auto& target : MultisampleTargets) if (target) target->Return();
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
    if (SurfelGI_Resolve_RT)
        SurfelGI_Resolve_RT->Return();
}

void jSceneRenderTarget::ReleasePersistentResources()
{
    jAdvancedShadows::ReleaseResources();
    jHiZOcclusion::ReleaseResources();
    jSubsurface::ReleaseResources();
    jSkinShading::ReleaseResources();
    jTextureSubsurface::ReleaseResources();
    jRasterFeatureDebug::ReleaseResources();
    auto ReturnAndResetRenderTarget = [](std::shared_ptr<jRenderTarget>& InOutRenderTarget)
    {
        if (InOutRenderTarget)
        {
            InOutRenderTarget->Return();
            InOutRenderTarget.reset();
        }
    };

    ReturnAndResetRenderTarget(IrradianceMap);
    ReturnAndResetRenderTarget(FilteredEnvMap);
    ReturnAndResetRenderTarget(CubeEnvMap);
    HistoryBuffer.reset();
    HistoryDepthBuffer.reset();
    GaussianV.reset();
    GaussianH.reset();
    ReturnAndResetRenderTarget(AOProjection);
    ReturnAndResetRenderTarget(GIProjection);
    ReturnAndResetRenderTarget(SSGI_RT);
    for (std::shared_ptr<jRenderTarget>& AccumRT : SSGI_Accum_RT)
        ReturnAndResetRenderTarget(AccumRT);
    ReturnAndResetRenderTarget(SurfelGI_Debug_RT);
    ReturnAndResetRenderTarget(SurfelGI_Attempt_RT);
    ReturnAndResetRenderTarget(SurfelGI_Resolve_RT);
    ReturnAndResetRenderTarget(AtmosphericShadow_RT);
    ReturnAndResetRenderTarget(VBuffer_RT);
    ReturnAndResetRenderTarget(HitObject_RT);
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
    if (InUseAsSubpassInput)
    {
        jSceneSubpassInputShaderParameters Parameters;
        Parameters.GBuffer0 = { GBuffer[0]->GetTexture() };
        Parameters.GBuffer1 = { GBuffer[1]->GetTexture() };
        Parameters.GBuffer2 = { GBuffer[2]->GetTexture() };
        Parameters.GBuffer3 = { GBuffer[3]->GetTexture() };
        Parameters.GBuffer4 = { GBuffer[4]->GetTexture() };
        Parameters.DepthTexture = { DepthPtr->GetTexture() };
        return jShaderParameterSet::CreateShaderBindingInstance(Parameters, EShaderAccessStageFlag::FRAGMENT, jShaderBindingInstanceType::SingleFrame);
    }
    else if (GBuffer[0]->Info.SampleCount != EMSAASamples::COUNT_1)
    {
        jSceneMultisampleTexturesShaderParameters p;
        p.GBuffer0 = {GBuffer[0]->GetTexture()}; p.GBuffer1 = {GBuffer[1]->GetTexture()};
        p.GBuffer2 = {GBuffer[2]->GetTexture()}; p.GBuffer3 = {GBuffer[3]->GetTexture()};
        p.GBuffer4 = {GBuffer[4]->GetTexture()}; p.DepthTexture = {DepthPtr->GetTexture()};
        return jShaderParameterSet::CreateShaderBindingInstance(p, EShaderAccessStageFlag::FRAGMENT, jShaderBindingInstanceType::SingleFrame);
    }
    else
    {
        const jSamplerStateInfo* GBufferSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();
        jSceneTexturesShaderParameters Parameters;
        Parameters.GBuffer0 = { GBuffer[0]->GetTexture(), GBufferSamplerStateInfo };
        Parameters.GBuffer1 = { GBuffer[1]->GetTexture(), GBufferSamplerStateInfo };
        Parameters.GBuffer2 = { GBuffer[2]->GetTexture(), GBufferSamplerStateInfo };
        Parameters.GBuffer3 = { GBuffer[3]->GetTexture(), GBufferSamplerStateInfo };
        Parameters.GBuffer4 = { GBuffer[4]->GetTexture(), GBufferSamplerStateInfo };
        Parameters.DepthTexture = { DepthPtr->GetTexture(), GBufferSamplerStateInfo };
        return jShaderParameterSet::CreateShaderBindingInstance(Parameters, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame);
    }
}
