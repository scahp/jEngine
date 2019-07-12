#pragma once
#include "jShadowTypes.h"
#include "jPostProcess.h"

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
struct IShaderStorageBufferObject;
struct IAtomicCounterBuffer;
struct jTexture;
struct jRenderTarget;
class jObject;

class jGame
{
public:
	jGame();
	~jGame();

	void ProcessInput();
	void Setup();

	void SpawnHairObjects();
	void SpawnTestPrimitives();

	void Update(float deltaTime);
	void OnMouseMove(int32 xOffset, int32 yOffset);
	void Teardown();
	void UpdateSettings();

	jDirectionalLight* DirectionalLight = nullptr;
	jPointLight* PointLight = nullptr;
	jSpotLight* SpotLight = nullptr;
	jLight* AmbientLight = nullptr;
	jCamera* MainCamera = nullptr;

	jObject* Sphere = nullptr;

	jRenderer* Renderer = nullptr;

	std::map<EShadowType, jRenderer*> ShadowRendererMap;
	EShadowType ShadowType = EShadowType::MAX;

	jRenderer* DeferredRenderer = nullptr;
	jRenderer* ForwardRenderer = nullptr;
};

