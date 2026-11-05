#pragma once
#include <filesystem>
#include <string>
#include <functional>
class jEditor;
bool IsAuthoringWorkspaceVisible();
void SetAuthoringWorkspaceVisible(bool visible);
void SetAuthoringRuntimeMode(bool runtime);
bool IsAuthoringRuntimeMode();
void SetAuthoringExecutablePath(const std::filesystem::path& path);
bool IsAuthoringViewportHovered();
bool IsAuthoringViewportFocused();
float GetAuthoringSceneAspect();
bool MapAuthoringViewportPoint(double& x, double& y);
void RevealAuthoringSelection(const jEditor& editor);
bool OpenAuthoringProject(jEditor& editor, const std::filesystem::path& project, std::string& error);
std::filesystem::path GetAuthoringProjectFile();
bool LoadAuthoringScene(jEditor& editor, const std::filesystem::path& scene, std::string& error);
void TickAuthoringWorkspace(jEditor& editor, float deltaTime);
void PrepareAuthoringUiFrame();
void RenderAuthoringWorkspace(jEditor& editor);
void ShutdownAuthoringWorkspace();
bool RequestAuthoringWindowClose();
void RequestAuthoringSceneReplacement(std::function<void()> action);
void SetTestPlatformWindows(bool enabled);
void SetAuthoringIntegrationDrawHook(std::function<void()> draw);
void SetAuthoringTestStateDirectory(const std::filesystem::path& directory);
