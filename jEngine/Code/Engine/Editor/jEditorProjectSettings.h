#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include "Renderer/Features/jSceneFeatureSettings.h"

class jEditor;
class jEditorProject;
struct jEditorProjectConfig;
enum class EInputKey : std::uint16_t;

// Project renderer defaults and editor camera bindings, owned by the UI thread.
// No generic gameplay/physics input system is implied by these camera actions.
namespace jEditorProjectSettings
{
// Known settings: render.deferred, render.surfelgi, render.surfelgi.inline,
// render.ssgi, render.bloom, render.vsync (true/false); render.surfelgi.intensity
// (0..8), render.surfelgi.inline.rays (1..16); render.shadow.filter (0=existing,
// 1=Poisson PCF), render.shadow.radiusTexels (finite 0..4); input.camera.forward/back/left/
// right/up/down (portable single key names). Unknown setting entries are preserved.
// Scene features use render.environment.*, render.ppr.* and render.atmosphere.*;
// the codecs below share their strict scalar/vector/path validation.
bool Validate(const jEditorProjectConfig& config, std::string& error,
    const std::filesystem::path& projectRoot = {});
bool Validate(const jEditorProject& project, std::string& error);
// Validate everything before applying any gOptions, VSync or active bindings.
bool Apply(const jEditorProject& project, jEditor& editor, std::string& error);
// Save a validated candidate atomically before publishing config/runtime settings.
// Unknown settings from the current project survive candidate omission.
bool Save(jEditorProject& project, jEditor& editor, const jEditorProjectConfig& candidate,
    std::string& error);
// Shared scene-feature codecs. Read validates paths/loads only the bounded .jsh
// coefficient asset when a root is supplied; raw HDR projection is never here.
// Both functions are transactional and preserve unrelated settings.
bool ReadSceneFeatures(const jEditorProjectConfig& config, const std::filesystem::path& projectRoot,
    jSceneFeatures::Settings& output, std::string& shAsset, std::string& error);
bool WriteSceneFeatures(jEditorProjectConfig& config, const jSceneFeatures::Settings& input,
    const std::string& shAsset, std::string& error);
void SetVisible(bool visible);
bool IsVisible();
void Render(jEditor& editor, jEditorProject* project);
// Root consumes this at a safe frame boundary, then calls RHI RecreateSwapChain.
// The settings UI itself never mutates a swapchain while rendering.
bool ConsumePresentModeChange();
// No movement while Ctrl/Alt/Shift is held. Caller still gates viewport/text focus.
bool ActionDown(const char* actionName, EInputKey fallback);
EInputKey Binding(const char* actionName, EInputKey fallback);
const char* KeyName(EInputKey key);
bool ParseKey(const std::string& name, EInputKey& key);
} // namespace jEditorProjectSettings
