#pragma once

#include <cstdint>
#include <functional>
#include <string>

class jEditor;
class jEditorProject;
class jEditorAssetRegistry;
struct jEditorAssetRecord;
struct ImVec2;

// Main/render-owner API. Tick performs at most one completed thumbnail upload
// before UI drawing. Render defers all catalog mutations until its list is done.
namespace jEditorAssetPanel
{
enum class Action { Activate, SelectInstances, ReplaceSelection, SetStartupScene };
using AssetAction = std::function<void(Action, const jEditorAssetRecord&)>;
void Tick(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry);
void Render(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry,
    std::string& selectedId, bool canReplaceSelection, const AssetAction& action);
void Reveal(const jEditorAssetRecord& asset);
void RenderTexturePreview(const jEditorProject* project, const jEditorAssetRegistry* registry,
    const std::string& sourcePath, const ImVec2& size);
void Shutdown(); // Before UI renderer/RHI teardown; joins the sole CPU thumbnail job.
}
