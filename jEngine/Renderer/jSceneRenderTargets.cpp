#include "pch.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jSwapchain.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"

void jSceneRenderTarget::Create(const jSwapchainImage* image)
{
    ColorPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::BGRA8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->GetSelectedMSAASamples() });

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
}

bool jSceneRenderTarget::IsValid() const
{
    return ColorPtr || DepthPtr || ResolvePtr || DirectionalLightShadowMapPtr || CubeShadowMapPtr;
}
