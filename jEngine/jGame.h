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
class jDirectionalLightPrimitive;
class jMeshObject;
class jPointLightPrimitive;
class jSpotLightPrimitive;

class jGame
{
public:
    static jBuffer* ScratchASBuffer;
    static jBuffer* TopLevelASBuffer;
    static jBuffer* InstanceUploadBuffer;
	static jObject* Sphere;
	static std::function<void(void* InCmdBuffer)> UpdateTopLevelAS;


	jGame();
	~jGame();

	void ProcessInput(float deltaTime);
	void Setup();

	enum class ESpawnedType
	{
		None = 0,
		TestPrimitive,
		CubePrimitive,
		InstancingPrimitive,
		IndirectDrawPrimitive,
	};

	void SpawnObjects(ESpawnedType spawnType);

	void RemoveSpawnedObjects();
	void SpawnTestPrimitives();
	void SapwnCubePrimitives();
	void SpawnInstancingPrimitives();
	void SpawnIndirectDrawPrimitives();

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

	jDirectionalLightPrimitive* DirectionalLightInfo = nullptr;
	jPointLightPrimitive* PointLightInfo = nullptr;
	jSpotLightPrimitive* SpotLightInfo = nullptr;
	jUIQuadPrimitive* DirectionalLightShadowMapUIDebug = nullptr;
	jMeshObject* Sponza = nullptr;

	std::vector<jObject*> SpawnedObjects;

	std::future<void> ResourceLoadCompleteEvent;
	std::vector<jObject*> CompletedAsyncLoadObjects;
	jMutexLock AsyncLoadLock;
};

