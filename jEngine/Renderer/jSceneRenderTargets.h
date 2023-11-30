#pragma once

struct jRenderTarget;
class jSwapchainImage;

struct jSceneRenderTarget
{
    // 임시
    static std::shared_ptr<jRenderTarget> IrradianceMap;
    static jTexture* OriginHDR;
    static std::shared_ptr<jRenderTarget> FilteredEnvMap;
    static std::shared_ptr<jRenderTarget> CubeEnvMap;
    //////////////////////////////////////////////////////////////////////////

    std::shared_ptr<jRenderTarget> ColorPtr;
    std::shared_ptr<jRenderTarget> DepthPtr;
    std::shared_ptr<jRenderTarget> ResolvePtr;
    
    std::shared_ptr<jRenderTarget> GBuffer[3];

    std::map<const jLight*, std::shared_ptr<jRenderTarget>> LightShadowMapPtr;

    std::shared_ptr<jRenderTarget> FinalColorPtr;

    std::shared_ptr<jRenderTarget> BloomSetup;
    std::shared_ptr<jRenderTarget> DownSample[3];
    std::shared_ptr<jRenderTarget> UpSample[3];

    void Create(const jSwapchainImage* InSwapchain, const std::vector<jLight*>* InLights = nullptr);
    void Return();
    std::shared_ptr<jRenderTarget> GetShadowMap(const jLight* InLight) const;
    std::shared_ptr<jShaderBindingInstance> PrepareGBufferShaderBindingInstance(bool InUseAsSubpassInput) const;
};
