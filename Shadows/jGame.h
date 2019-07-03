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
	void Update(float deltaTime);
	void OnMouseMove(int32 xOffset, int32 yOffset);
	void Teardown();
	void UpdateSettings();

	jDirectionalLight* DirectionalLight = nullptr;
	jPointLight* PointLight = nullptr;
	jSpotLight* SpotLight = nullptr;
	jLight* AmbientLight = nullptr;
	jCamera* MainCamera = nullptr;

	IShaderStorageBufferObject* ssbo = nullptr;
	IShaderStorageBufferObject* startElementBuf = nullptr;
	IShaderStorageBufferObject* linkedListEntryDepthAlphaNext = nullptr;
	IShaderStorageBufferObject* linkedListEntryNeighbors = nullptr;
	IAtomicCounterBuffer* atomicBuffer = nullptr;

	struct shader_data_t
	{
		float camera_position[4] = { 1, 2, 3, 4 };
		float light_position[4] = { 5, 6, 7, 8 };
		float light_diffuse[4] = { 9, 10, 11, 12 };
	} shader_data;

	static constexpr int32 linkedlistDepth = 50;
	static constexpr auto linkedListDepthSize = SM_WIDTH * SM_HEIGHT * linkedlistDepth;

	jObject* Sphere = nullptr;

	jRenderer* Renderer = nullptr;

	std::map<EShadowType, jRenderer*> ShadowRendererMap;
	EShadowType ShadowType = EShadowType::MAX;

	jPostprocessChain PostProcessChain;
};

