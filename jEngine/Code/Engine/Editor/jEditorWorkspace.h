#pragma once
#include <filesystem>
#include <string>
#include <functional>
class jEditor;
struct ImDrawList;
struct ImVec2;
bool IsAuthoringWorkspaceVisible();
void SetAuthoringWorkspaceVisible(bool visible);
void SetAuthoringToolsVisible(bool visible);
bool IsAuthoringViewportOnly();
void RequestAuthoringViewportOnlyToggle();
void ApplyAuthoringViewportOnlyToggle(jEditor& editor);
void SetAuthoringRuntimeMode(bool runtime);
bool IsAuthoringRuntimeMode();
void SetAuthoringExecutablePath(const std::filesystem::path& path);
bool IsAuthoringViewportHovered();
bool IsAuthoringViewportFocused();
float GetAuthoringSceneAspect();
// Current visible Scene/Game image in ImGui screen coordinates.
bool GetAuthoringViewportRect(float& x, float& y, float& width, float& height);
bool MapAuthoringViewportPoint(double& x, double& y);
void RevealAuthoringSelection(const jEditor& editor);
// Current visible Scene/Game renderer combo in ImGui screen coordinates.
bool GetAuthoringRendererControlRect(float& x, float& y, float& width, float& height);
// Visible popup choice; renderer is jGame::ESceneRenderPipeline (0, 1 or 2).
bool GetAuthoringRendererChoiceRect(int renderer, float& x, float& y, float& width, float& height);
bool OpenAuthoringProject(jEditor& editor, const std::filesystem::path& project, std::string& error);
std::filesystem::path GetAuthoringProjectFile();
bool LoadAuthoringScene(jEditor& editor, const std::filesystem::path& scene, std::string& error);
void TickAuthoringWorkspace(jEditor& editor, float deltaTime);
void PrepareAuthoringUiFrame();
void RenderAuthoringWorkspace(jEditor& editor);
void DrawSceneLoadingOverlay(ImDrawList& draw, const ImVec2& position, const ImVec2& size);
void KeepAuthoringDockSpaceAlive();
void UpdateAuthoringViewportOnlyInput(jEditor& editor);
void ShutdownAuthoringWorkspace();
bool RequestAuthoringWindowClose();
void RequestAuthoringSceneReplacement(std::function<void()> action);
void SetTestPlatformWindows(bool enabled);
void SetAuthoringIntegrationDrawHook(std::function<void()> draw);
void SetAuthoringTestStateDirectory(const std::filesystem::path& directory);
