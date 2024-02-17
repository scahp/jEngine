#pragma once

struct jRenderTarget;
class jSwapchainImage;

enum class EGBufferType : uint8
{
    NORMAL = 0,
    ALBEDO = 1,
    VELOCITY = 2,
    MAX,
};

struct jSceneRenderTarget : public std::enable_shared_from_this<jSceneRenderTarget>
{
    virtual ~jSceneRenderTarget()
    {
        Return();
    }

    // 임시
    static std::shared_ptr<jRenderTarget> IrradianceMap;
    static jTexture* OriginHDR;
    static std::shared_ptr<jRenderTarget> FilteredEnvMap;
    static std::shared_ptr<jRenderTarget> CubeEnvMap;
    static jTexture* IrradianceMap2;
    static jTexture* FilteredEnvMap2;
    static jTexture* CubeEnvMap2;
    static class jFullscreenQuadPrimitive* GlobalFullscreenPrimitive;
    static std::shared_ptr<jTexture> HistoryBuffer;
    static std::shared_ptr<jTexture> HistoryDepthBuffer;
    static std::shared_ptr<jTexture> GaussianV;
    static std::shared_ptr<jTexture> GaussianH;
    static std::shared_ptr<jRenderTarget> AOProjection;
    //////////////////////////////////////////////////////////////////////////

    std::shared_ptr<jRenderTarget> ColorPtr;
    std::shared_ptr<jRenderTarget> DepthPtr;
    std::shared_ptr<jRenderTarget> ResolvePtr;
    
    std::shared_ptr<jRenderTarget> GBuffer[3];

    const std::shared_ptr<jRenderTarget>& GetGBuffer(EGBufferType InType)
    {
        check((int32)InType < _countof(GBuffer));
        return GBuffer[(int32)InType];
    }

    std::map<const jLight*, std::shared_ptr<jRenderTarget>> LightShadowMapPtr;

    std::shared_ptr<jRenderTarget> FinalColorPtr;
    std::shared_ptr<jRenderTarget> AtmosphericShadowing;
    std::shared_ptr<jRenderTarget> BloomSetup;
    std::shared_ptr<jRenderTarget> DownSample[3];
    std::shared_ptr<jRenderTarget> UpSample[3];


    void Create(const jSwapchainImage* InSwapchain, const std::vector<jLight*>* InLights = nullptr);
    void Return();
    std::shared_ptr<jRenderTarget> GetShadowMap(const jLight* InLight) const;
    std::shared_ptr<jShaderBindingInstance> PrepareGBufferShaderBindingInstance(bool InUseAsSubpassInput) const;
};
