#pragma once

class jRHI;
extern jRHI* g_rhi;

class jDirectionalLight;
class jLight;
class jCamera;
struct jShader;
class jPointLight;
class jSpotLight;
class jRenderer;
struct jFrameBuffer;
struct IShaderStorageBufferObject;
struct IAtomicCounterBuffer;
struct jTexture;
struct jFrameBuffer;
class jObject;
class jPipelineSet;
class jCascadeDirectionalLight;
class jUIQuadPrimitive;

class jGame
{
public:
	jGame();
	~jGame();

	void ProcessInput();
	void Setup();

	enum class ESpawnedType
	{
		None = 0,
		TestPrimitive,
		CubePrimitive
	};

	void SpawnObjects(ESpawnedType spawnType);

	void RemoveSpawnedObjects();
	void SpawnTestPrimitives();
	void SapwnCubePrimitives();

	void SpawnGraphTestFunc();	// Test

	ESpawnedType SpawnedType = ESpawnedType::None;

	void Update(float deltaTime);
	void Draw();

	void OnMouseButton();
	void OnMouseMove(int32 xOffset, int32 yOffset);
	void Resize(int32 width, int32 height);
	void Release();

	jDirectionalLight* DirectionalLight = nullptr;
	jDirectionalLight* NormalDirectionalLight = nullptr;
	jCascadeDirectionalLight* CascadeDirectionalLight = nullptr;
	jPointLight* PointLight = nullptr;
	jSpotLight* SpotLight = nullptr;
	jLight* AmbientLight = nullptr;
	jCamera* MainCamera = nullptr;

	jObject* DirectionalLightInfo = nullptr;
	jObject* PointLightInfo = nullptr;
	jObject* SpotLightInfo = nullptr;
	jUIQuadPrimitive* DirectionalLightShadowMapUIDebug = nullptr;

	std::vector<jObject*> SpawnedObjects;
};

