#pragma once

extern std::shared_ptr<jRenderTarget> g_EyeAdaptationARTPtr;
extern std::shared_ptr<jRenderTarget> g_EyeAdaptationBRTPtr;

class jRenderTargetPool
{
public:
	jRenderTargetPool();
	~jRenderTargetPool();

	static std::shared_ptr<jRenderTarget> GetRenderTarget(const jRenderTargetInfo& info);
	static void ReturnRenderTarget(jRenderTarget* renderTarget);

	static void ReleaseForRecreateSwapchain()
	{
        RenderTargetResourceMap.clear();
        RenderTargetHashVariableMap.clear();
	}

	static void Release()
	{
        g_EyeAdaptationARTPtr->Return();
        g_EyeAdaptationBRTPtr->Return();
        g_EyeAdaptationARTPtr.reset();
        g_EyeAdaptationBRTPtr.reset();

		RenderTargetResourceMap.clear();
		RenderTargetHashVariableMap.clear();
	}

	struct jRenderTargetPoolResource
	{
		bool IsUsing = false;
		std::shared_ptr<jRenderTarget> RenderTargetPtr;
	};
	static std::map<size_t, std::list<jRenderTargetPoolResource> > RenderTargetResourceMap;
	static std::map<jRenderTarget*, size_t> RenderTargetHashVariableMap;

	static struct jTexture* GetNullTexture(ETextureType type);
};

