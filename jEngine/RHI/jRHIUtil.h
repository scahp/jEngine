struct jTexture;
struct jRenderFrameContext;
struct jShader;

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

typedef std::function<void(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)> FuncBindingShaderResources;
typedef std::function<jShader*(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)> FuncCreateShaders;
typedef std::function<void(jRasterizationStateInfo*& OutRasterState, jBlendingStateInfo*& OutBlendState, jDepthStencilStateInfo*& OutDepthStencilState)> FuncCreateFixedPipelineStates;

void CreateDefaultFixedPipelineStates(jRasterizationStateInfo*& OutRasterState, jBlendingStateInfo*& OutBlendState, jDepthStencilStateInfo*& OutDepthStencilState);

void DispatchCompute(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jTexture* RenderTarget
                    , FuncBindingShaderResources InFuncBindingShaderResources, FuncCreateShaders InFuncCreateShaders);

void DrawFullScreen(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, std::shared_ptr<jRenderTarget> InRenderTargetPtr
	                , FuncBindingShaderResources InFuncBindingShaderResources, FuncCreateShaders InFuncCreateShaders, FuncCreateFixedPipelineStates InFuncCreateFixedPipelineStates = CreateDefaultFixedPipelineStates);

void DrawQuad(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, std::shared_ptr<jRenderTarget> InRenderTargetPtr, Vector4i InRect
	, FuncBindingShaderResources InFuncBindingShaderResources, FuncCreateShaders InFuncCreateShaders, FuncCreateFixedPipelineStates InFuncCreateFixedPipelineStates = CreateDefaultFixedPipelineStates);

}