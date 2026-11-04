#pragma once

#include <cstdint>
#include <functional>
#include <string>

class jEditor;
class jEditorProject;
class jEditorAssetRegistry;
struct ImVec2;

// Main/render-owner API. Tick performs at most one completed thumbnail upload
// before UI drawing. Render defers all catalog mutations until its list is done.
namespace jEditorAssetPanel
{
using ActivateAsset = std::function<void(const std::string& assetId)>;
void Tick(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry);
void Render(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry,
    std::string& selectedId, const ActivateAsset& activate = {});
void RenderTexturePreview(const jEditorProject* project, const jEditorAssetRegistry* registry,
    const std::string& sourcePath, const ImVec2& size);
void Shutdown(); // Before UI renderer/RHI teardown; joins the sole CPU thumbnail job.
}
