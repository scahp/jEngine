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

	static bool PreventReturnRenderTarget;

	static void ReleaseForRecreateSwapchain()
	{
		PreventReturnRenderTarget = true;

        RenderTargetResourceMap.clear();
        RenderTargetHashVariableMap.clear();

		PreventReturnRenderTarget = false;
	}

	static void Release()
	{
		if (g_EyeAdaptationARTPtr)
		{
			g_EyeAdaptationARTPtr->Return();
			g_EyeAdaptationARTPtr.reset();
		}
		if (g_EyeAdaptationBRTPtr)
		{
			g_EyeAdaptationBRTPtr->Return();
			g_EyeAdaptationBRTPtr.reset();
		}

        jRenderTargetPool::PreventReturnRenderTarget = true;	// After Released, RT can't be returned.

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

