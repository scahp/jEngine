#pragma once
#include "jRHI.h"
#include "jRenderer.h"
#include "jGBuffer.h"
#include "jPipeline.h"

class jCamera;

class jForwardRenderer : public jRenderer
{
public:
	jForwardRenderer() = default;
	virtual ~jForwardRenderer();

	typedef void (*RenderPassFunc)(jCamera*);

	virtual void Setup();
	virtual void Teardown();

	virtual void ShadowPrePass(const jCamera* camera) override;
	virtual void RenderPass(const jCamera* camera) override;
	virtual void PostProcessPass(const jCamera* camera) override;
	virtual void PostRenderPass(const jCamera* camera) override;

	virtual void UpdateSettings() {}
};

