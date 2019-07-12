#pragma once
#include "jRenderer.h"
#include "jRHI.h"
#include "jShader.h"

class jCamera;
class jObject;
struct Vector;
class jLight;
struct jShader;

class jShadowVolumeRenderer : public jRenderer
{
public:
	jShadowVolumeRenderer();
	virtual ~jShadowVolumeRenderer();

	bool CanSkipShadowObject(const jCamera* camera, const jObject* object, const Vector* lightPos, const Vector* lightDir, const jLight* light) const;

	virtual void Setup() override;
	virtual void RenderPass(const jCamera* camera) override;
	virtual void DebugRenderPass(const jCamera* camera) override;
	virtual void UpdateSettings() override;

	jShaderInfo ShadowVolumeInfinityFarShaderInfo;
	jShader* ShadowVolumeInfinityFarShader = nullptr;

	bool ShowSilhouette_DirectionalLight = false;
	bool ShowSilhouette_PointLight = false;
	bool ShowSilhouette_SpotLight = false;

};

