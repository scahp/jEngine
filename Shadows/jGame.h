#pragma once
#include "jShadowTypes.h"

class jRHI;
extern jRHI* g_rhi;

class jDirectionalLight;
class jLight;
class jCamera;
struct jShader;
class jPointLight;
class jSpotLight;
class jRenderer;

class jGame
{
public:
	jGame();
	~jGame();

	void ProcessInput();
	void Setup();
	void Update(float deltaTime);
	void OnMouseMove(int32 xOffset, int32 yOffset);
	void Teardown();
	void UpdateSettings();

	jDirectionalLight* DirectionalLight = nullptr;
	jPointLight* PointLight = nullptr;
	jSpotLight* SpotLight = nullptr;
	jLight* AmbientLight = nullptr;
	jCamera* MainCamera = nullptr;
	jShader* SimpleShader = nullptr;
	jShader* ShadowMapShader = nullptr;
	jShader* ShadowMapOmniShader = nullptr;
	jShader* BaseShader = nullptr;
	jShader* ShadowVolumeBaseShader = nullptr;
	jShader* ShadowVolumeInfinityFarShader = nullptr;

	class jObject* Sphere = nullptr;

	jRenderer* Renderer = nullptr;

	std::map<EShadowType, jRenderer*> ShadowRendererMap;
	EShadowType ShadowType = EShadowType::MAX;
};

