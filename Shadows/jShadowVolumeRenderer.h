#pragma once
#include "jRenderer.h"
#include "jRHI.h"

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

	bool CanSkipShadowObject(jCamera* camera, jObject* object, Vector* lightPos, Vector* lightDir, jLight* light) const;

	virtual void Setup() override;
	virtual void RenderPass(jCamera* camera) override;
	virtual void DebugRenderPass(jCamera* camera) override;
	virtual void UpdateSettings() override;

	jShaderInfo ShadowVolumeInfinityFarShaderInfo;
	jShader* ShadowVolumeInfinityFarShader = nullptr;

	bool ShowSilhouette_DirectionalLight = false;
	bool ShowSilhouette_PointLight = false;
	bool ShowSilhouette_SpotLight = false;

};

