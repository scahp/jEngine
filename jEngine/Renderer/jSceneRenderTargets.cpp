#include "pch.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jSwapchain.h"

void jSceneRenderTarget::Create(const jSwapchainImage* image)
{
    ColorPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::BGRA8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->MsaaSamples });

    DepthPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, false, g_rhi_vk->MsaaSamples });

    if (g_rhi_vk->MsaaSamples > 1)
    {
        check(image);
        ResolvePtr = jRenderTarget::CreateFromTexture(image->TexturePtr);
    }

    if (!FinalColorPtr)
    {
        FinalColorPtr = jRenderTarget::CreateFromTexture(image->TexturePtr);
    }

    DirectionalLightShadowMapPtr = jRenderTargetPool::GetRenderTarget(
        { ETextureType::TEXTURE_2D, ETextureFormat::D24_S8, SCR_WIDTH, SCR_HEIGHT, 1, false, 1 });    
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
}

bool jSceneRenderTarget::IsValid() const
{
    return ColorPtr || DepthPtr || ResolvePtr || DirectionalLightShadowMapPtr;
}
