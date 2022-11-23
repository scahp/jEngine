#pragma once

struct jRenderTarget;
class jSwapchainImage;

struct jSceneRenderTarget
{
    std::shared_ptr<jRenderTarget> ColorPtr;
    std::shared_ptr<jRenderTarget> DepthPtr;
    std::shared_ptr<jRenderTarget> ResolvePtr;
    
    std::shared_ptr<jRenderTarget> GBuffer[3];

    std::shared_ptr<jRenderTarget> DirectionalLightShadowMapPtr;
    std::shared_ptr<jRenderTarget> CubeShadowMapPtr;
    std::shared_ptr<jRenderTarget> SpotLightShadowMapPtr;

    std::shared_ptr<jRenderTarget> FinalColorPtr;

    std::shared_ptr<jRenderTarget> BloomSetup;
    std::shared_ptr<jRenderTarget> DownSample[3];
    std::shared_ptr<jRenderTarget> UpSample[3];

    void Create(const jSwapchainImage* image);
    void Return();
    jShaderBindingInstance* PrepareGBufferShaderBindingInstance(bool InUseAsSubpassInput) const;
};
