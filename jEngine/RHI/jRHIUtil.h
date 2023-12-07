struct jTexture;

namespace jRHIUtil
{

// Convert Spheremap(TwoMirrorBall) to Cubemap
std::shared_ptr<jRenderTarget> ConvertToCubeMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jName InTwoMirrorBallSphereMapFilePath);
std::shared_ptr<jRenderTarget> ConvertToCubeMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jTexture* InTwoMirrorBallSphereMap);

// Convert Cubemap to IrradianceMap
std::shared_ptr<jRenderTarget> GenerateIrradianceMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jTexture* InCubemap);

// Convert Cubemap to FilteredEnvironmentMap
std::shared_ptr<jRenderTarget> GenerateFilteredEnvironmentMap(jName InDestFilePath, Vector2i InDestTextureSize
    , std::shared_ptr<jRenderFrameContext> InRenderFrameContextPtr, jTexture* InCubemap);


}