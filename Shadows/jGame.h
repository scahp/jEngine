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
struct jRenderTarget;

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
	
	jShader* ExpDeepShadowMapGenShader = nullptr;
	jShader* DeepShadowMapGenShader = nullptr;
	jShader* Hair_Shader = nullptr;
	jShader* ExpDeepShadowFull_Shader = nullptr;
	jShader* DeepShadowFull_Shader = nullptr;
	jShader* DeepShadowAA_Shader = nullptr;
	jShader* Deferred_Shader = nullptr;
	jShader* ExpDeferred_Shader = nullptr;

	class jObject* Sphere = nullptr;

	jRenderer* Renderer = nullptr;

	std::map<EShadowType, jRenderer*> ShadowRendererMap;
	EShadowType ShadowType = EShadowType::MAX;
};

