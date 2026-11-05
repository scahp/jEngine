#pragma once
#include <memory>
#include <string>
#include <cstdint>

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
class jMeshObject;
class jAssetImportService;
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
    void SyncRaytracingScene();
    void ClearImportedSceneForAuthoring();
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
    // Owner-thread API. Requests only queue work; Update applies it before Draw.
    ESceneRenderPipeline GetActiveSceneRenderPipeline() const;
    bool CanSwitchSceneRenderPipeline(ESceneRenderPipeline target, std::string& reason) const;
    bool RequestSceneRenderPipeline(ESceneRenderPipeline target, std::string& reason);
    bool IsSceneRenderPipelineSwitchPending() const { return SceneRenderPipelineSwitchPending; }
	const char* GetSceneRecommendedLoaderName(int32 InIndex) const;
	const char* GetSelectedPathTracingSceneLoaderName() const;
	const char* GetActivePathTracingSceneLoaderName() const;
	ESceneLoader GetSelectedPathTracingSceneLoader() const;
	bool IsUsingPathTracingRenderer() const;
	void SetSelectedPathTracingSceneIndex(int32 InIndex);
	void SetSelectedPathTracingSceneLoader(ESceneLoader InLoader);
	void RequestLoadSelectedPathTracingScene();
	bool CanLoadSelectedPathTracingScene() const;
    bool ConfigureSceneBrowserStartup(const std::string& sceneId, const std::string& loader, std::string& error);
    bool StartupSceneUsesPathTracing();
    bool IsSceneBrowserRestartRequested() const { return PathTracingSceneBrowser.RestartIndex >= 0; }
    std::vector<std::string> GetSceneBrowserRestartArguments() const;
    void RequestSceneMaterialRefresh() { SceneMaterialRefreshPending = true; }
    bool IsSceneImportPending() const { return PendingImportId != 0 || SceneMaterialRefreshPending; }
    const std::string& GetSceneImportStatus() const { return SceneImportStatus; }
    void CancelSceneImport();
    bool WaitForSceneImportForTesting(uint32 timeoutMilliseconds = 30000);

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
        int32 RestartIndex = -1;
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
    void EnsureSceneBrowserRasterPreviewLights();
    void ClearSceneBrowserRasterPreviewLights();
	int32 FindPathTracingSceneIndexById(const std::string& InSceneId) const;
	void LoadPathTracingSceneByIndex(int32 InIndex, bool InRebuildRaytracingScene, jMeshObject* preparedModel = nullptr);
    void ProcessSceneImports();
    void ProcessSceneRenderPipelineSwitch();

    std::unique_ptr<jAssetImportService> AssetImports;
    ESceneRenderPipeline PendingSceneRenderPipeline = ESceneRenderPipeline::Deferred;
    bool SceneRenderPipelineSwitchPending = false;
    std::uint64_t PendingImportId = 0;
    bool SceneMaterialRefreshPending = false;
    bool SceneMaterialRefreshAfterRaster = false;
    bool RefreshSceneMaterialLibrary(std::string& error);
    std::string PendingImportSceneId;
    std::string SceneImportStatus;
	jPathTracingSceneBrowserState PathTracingSceneBrowser;
	std::vector<jObject*> SceneBrowserLoadedObjects;
	std::vector<jLight*> SceneBrowserLoadedLights;
    // Temporary raster approximations of legacy analytic emitters, never authored.
    std::vector<jLight*> SceneBrowserRasterPreviewLights;
};
