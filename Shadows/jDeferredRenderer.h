#pragma once
#include "jRHI.h"
#include "jRenderer.h"
#include "jGBuffer.h"
#include "jPipeline.h"

class jCamera;

class jDeferredRenderer : public jRenderer
{
public:
	jDeferredRenderer(const jRenderTargetInfo& geometryBufferInfo);
	virtual ~jDeferredRenderer();

	typedef void (*RenderPassFunc)(jCamera*);

	virtual void Setup();
	virtual void Teardown();

	virtual void ShadowPrePass(const jCamera* camera) override;
	virtual void RenderPass(const jCamera* camera) override;
	virtual void PostProcessPass(const jCamera* camera) override;
	virtual void PostRenderPass(const jCamera* camera) override;

	virtual void UpdateSettings() {}

private:
	jGBuffer GBuffer;
	jRenderTargetInfo GeometryBufferInfo;
	std::list<jObject*> ShadowPassObjects;
	std::list<jObject*> RenderPassObjects;

};

