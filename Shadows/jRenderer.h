#pragma once
#include "jRHI.h"

class jCamera;

class jRenderer
{
public:
	jRenderer();
	virtual ~jRenderer();

	typedef void (*RenderPassFunc)(jCamera*);

	virtual void Setup();
	virtual void Teardown();
	virtual void Reset();

	virtual void ShadowPrePass(jCamera* camera);
	virtual void DebugShadowPrePass(jCamera* camera);
	virtual void RenderPass(jCamera* camera);
	virtual void DebugRenderPass(jCamera* camera);
	virtual void UIPass(jCamera* camera);
	virtual void DebugUIPass(jCamera* camera);

	virtual void Render(jCamera* camera);
	virtual void UpdateSettings() {}

	jRenderTarget* GBuffer = nullptr;
};

