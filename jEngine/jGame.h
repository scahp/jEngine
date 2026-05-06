#pragma once

#include <string>
#include <vector>

#include "Math/Vector.h"

class jRHI;
extern jRHI* g_rhi;

class jLight;
class jCamera;
struct jShader;
class jRenderer;
struct jFrameBuffer;
struct IAtomicCounterBuffer;
struct jTexture;
struct jFrameBuffer;
class jObject;
class jPipelineSet;
class jCascadeDirectionalLight;

class jGame
{
public:
	enum class ESceneRenderPipeline
	{
		Deferred = 0,
		Forward,
		PathTracing,
	};

	enum class ESceneLoader
	{
		Recommended = 0,
		Model,
		PathTracing,
	};

	struct jLoadableSceneDesc
	{
		std::string SceneId;
		std::string FilePath;
		std::string DisplayName;
		std::string Extension;
		ESceneRenderPipeline RenderPipeline = ESceneRenderPipeline::Deferred;
		ESceneLoader RecommendedLoader = ESceneLoader::Model;
		ESceneLoader SelectedLoader = ESceneLoader::Recommended;
	};

	jGame();
	~jGame();

	void ProcessInput(float deltaTime);
	void Setup();

	void SpawnGraphTestFunc();	// Test

	void Update(float deltaTime);
	void Draw();

	void OnMouseButton();
	void OnMouseMove(int32 xOffset, int32 yOffset);
	void Resize(int32 width, int32 height);
	void Release();

	const std::vector<jLoadableSceneDesc>& GetLoadablePathTracingScenes() const;
	int32 GetSelectedPathTracingSceneIndex() const;
	int32 GetActivePathTracingSceneIndex() const;
	const char* GetSelectedPathTracingSceneName() const;
	const char* GetActivePathTracingSceneName() const;
	const char* GetSceneRenderPipelineName(int32 InIndex) const;
	const char* GetActiveSceneRenderPipelineName() const;
	const char* GetSceneRecommendedLoaderName(int32 InIndex) const;
	const char* GetSelectedPathTracingSceneLoaderName() const;
	const char* GetActivePathTracingSceneLoaderName() const;
	ESceneLoader GetSelectedPathTracingSceneLoader() const;
	bool IsUsingPathTracingRenderer() const;
	void SetSelectedPathTracingSceneIndex(int32 InIndex);
	void SetSelectedPathTracingSceneLoader(ESceneLoader InLoader);
	void RequestLoadSelectedPathTracingScene();
	bool CanLoadSelectedPathTracingScene() const;

	jCamera* MainCamera = nullptr;

private:
	struct jPathTracingSceneBrowserState
	{
		bool Initialized = false;
		std::string RootFolder = "Resource";
		std::string SettingsFile = "jengine.ini";
		std::vector<jLoadableSceneDesc> Scenes;
		std::string LastLoadedSceneId;
		ESceneLoader LastLoadedLoader = ESceneLoader::Model;
		int32 SelectedIndex = -1;
		int32 ActiveIndex = -1;
		int32 PendingLoadIndex = -1;
		ESceneRenderPipeline ActiveRenderPipeline = ESceneRenderPipeline::Deferred;
		ESceneLoader ActiveLoader = ESceneLoader::Model;
	};

	void InitializePathTracingSceneBrowser();
	void RefreshPathTracingSceneCatalog();
	void LoadPathTracingSceneBrowserSettings();
	void SavePathTracingSceneBrowserSettings() const;
	void ApplyScenePlacementPreset(int32 InIndex);
	void ClearSceneBrowserLoadedObjects();
	void ClearSceneBrowserLoadedLights();
	int32 FindPathTracingSceneIndexById(const std::string& InSceneId) const;
	void LoadPathTracingSceneByIndex(int32 InIndex, bool InRebuildRaytracingScene);

	jPathTracingSceneBrowserState PathTracingSceneBrowser;
	std::vector<jObject*> SceneBrowserLoadedObjects;
	std::vector<jLight*> SceneBrowserLoadedLights;
};


