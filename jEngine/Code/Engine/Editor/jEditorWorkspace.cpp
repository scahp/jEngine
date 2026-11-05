#include "pch.h"
#include "jEditorWorkspace.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "jEditorTools.h"
#include "jEditorUiRenderer.h"
#include "jEditorExtensions.h"
#include "jEditorPackaging.h"
#include "jEditorAssetPanel.h"
#include "jEditorCapture.h"
#include "jEditorProjectSettings.h"
#include "jEditorWater.h"
#include "jSceneFeatureTools.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jEditorAssetPath.h"
#include "FileLoader/jImageFileLoader.h"
#include "FileLoader/jModelUploadSession.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "imgui_internal.h"
#include "Core/jLog.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "jEngine.h"
#include "jOptions.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHI.h"
#include <fstream>
#include <future>
#include <ctime>
#include <set>

namespace
{
namespace fs = std::filesystem;
using namespace jEditorAuthoring;
using Json = nlohmann::json;
enum class Dialog { None, Save, Open, NewProject, OpenProject, Import, SavePrefab, OpenPrefab, Package };
struct Row { EntityId Id; int Depth; bool Children; std::string Label; };
struct AssetPlacement
{
    std::string Id, Source, Error;
    uint64_t Revision = 0;
    std::unique_ptr<Document> Prefab;
    std::optional<jEditor::PlacementBounds> Bounds;
    std::optional<jEditor::PlacementHit> Drop;
};
struct ModelReplacement
{
    struct Target { EntityId Id; std::string Source, AssetId; };
    jEditorAssetRecord Asset;
    std::vector<Target> Targets;
};
struct CaptureDialog
{
    bool Open = false, Video = false, WasBusy = false;
    int Resolution = 0, Width = 1920, Height = 1080, Fps = 30;
    jEditorCapture::Format StillFormat = jEditorCapture::Format::Png;
    char File[2048]{};
    std::string Error;
};
struct RendererChoiceRect { ImVec2 Position{}, Size{}; bool Visible = false; };
struct Workspace
{
    bool Visible = true, Runtime = false, ViewHovered = false, ViewFocused = false;
    bool ViewportOnly = false, ToggleViewportOnly = false;
    bool RendererControlVisible = false, ViewToolbarOwnsInput = false;
    bool FixedLayout = true, ShowAssets = true, ShowLogs = true, ShowProfiler = false, ShowTools = false;
    bool PaintTerrainInScene = false;
    bool SurfaceSnap = true, SuppressEditorEscape = false;
    Vec3 DuplicateOffset{1, 0, 0};
    std::optional<AssetPlacement> Placement;
    std::optional<ModelReplacement> Replacement;
    bool FocusAssets = false;
    ImGuiTextFilter ReplacementFilter;
    CaptureDialog Capture;
    bool Docking = true, PlatformWindows = false, DocksInitialized = false;
    ImGuiID DockSpaceId = 0;
    std::optional<bool> TestPlatformWindows;
    std::function<void()> IntegrationDraw;
    bool ShowSettings = false, ShowKeyboardShortcuts = false, LoadLayout = true, ResetLayout = false, AutoSave = true;
    float LeftWidth = 245, RightWidth = 325, BottomHeight = 205, FontScale = 1;
    float AutoSaveSeconds = 0, WatchSeconds = 0;
    ImVec2 ViewPosition{}, ViewSize{};
    ImVec2 RendererControlPosition{}, RendererControlSize{};
    std::array<RendererChoiceRect, 3> RendererChoices{};
    std::optional<jGame::ESceneRenderPipeline> RendererSwitchTarget;
    std::string RendererSwitchError;
    uint64_t RowRevision = ~uint64_t(0), AssetRevision = ~uint64_t(0);
    uint64_t RowSignature = ~uint64_t(0);
    EntityId HierarchyActive = 0, RangeAnchor = 0, RevealHierarchy = 0, RowSelected = 0;
    ImGuiTextFilter EntityFilter, AssetFilter, LogFilter, ShortcutFilter;
    std::set<EntityId> Collapsed;
    std::vector<Row> Rows;
    std::vector<size_t> AssetRows;
    std::vector<std::string> ReimportRequests;
    EntityId PropertyOwner = 0;
    bool OwnPropertyTransaction = false;
    std::string CachedEntityFilter, CachedAssetFilter, Status;
    Dialog Modal = Dialog::None;
    bool OpenModal = false, UnsavedPrompt = false;
    bool CloseApproved = false;
    std::function<void()> Pending;
    char Path[1024] = {}, Name[128] = "My Project", AssetDestination[256] = "Assets";
    std::unique_ptr<jEditorProject> Project;
    std::unique_ptr<jEditorAssetRegistry> Registry;
    std::string SelectedAsset;
    std::future<std::pair<jEditorPackageReport, std::string>> Packaging;
    std::array<float, 240> FrameTimes{};
    int FrameOffset = 0, MinimumLevel = 0;
    char Command[512] = {};
    fs::path LayoutFile = ".jeditor/workspace.json";
    std::string ImGuiFile = ".jeditor/imgui.ini";
    fs::path ExecutablePath;
    Json Layout = Json::object();
};
Workspace& State() { static Workspace state; return state; }
jCamera* Camera() { return g_Engine ? g_Engine->Game.MainCamera : nullptr; }
void Message(const std::string& value, bool error = false)
{
    State().Status = value;
    jLog::Get().Write(error ? EjLogLevel::Error : EjLogLevel::Info, "Editor", value);
}
const char* RendererLabel(jGame::ESceneRenderPipeline renderer)
{
    switch (renderer)
    {
    case jGame::ESceneRenderPipeline::Deferred: return "Deferred";
    case jGame::ESceneRenderPipeline::Forward: return "Forward";
    case jGame::ESceneRenderPipeline::PathTracing: return "Path Tracing";
    }
    return "Unknown";
}
bool ViewportRendererToolbar()
{
    auto& s = State();
    auto* game = g_Engine ? &g_Engine->Game : nullptr;
    const auto active = game ? game->GetActiveSceneRenderPipeline() : jGame::ESceneRenderPipeline::Deferred;
    if (game && s.RendererSwitchTarget && !game->IsSceneRenderPipelineSwitchPending())
    {
        const auto target = *s.RendererSwitchTarget; s.RendererSwitchTarget.reset();
        if (active == target)
        {
            s.RendererSwitchError.clear();
            Message(std::string("Renderer: ") + RendererLabel(active));
        }
        else
        {
            s.RendererSwitchError = std::string("Renderer switch to ") + RendererLabel(target)
                + " was not applied. Active: " + RendererLabel(active) + ". See Console for details.";
            Message(s.RendererSwitchError, true);
        }
    }
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float labelWidth = ImGui::CalcTextSize("Renderer").x + ImGui::GetStyle().ItemSpacing.x;
    const float comboWidth = ImGui::CalcTextSize("Path Tracing").x + 2 * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight();
    const bool inlineLabel = available >= labelWidth + comboWidth;
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Renderer");
    if (inlineLabel) ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.0f, std::min(comboWidth, ImGui::GetContentRegionAvail().x)));
    ImGui::BeginDisabled(!game || game->IsSceneRenderPipelineSwitchPending());
    const bool popup = ImGui::BeginCombo("##ViewportRenderer", game ? RendererLabel(active) : "Unavailable");
    if (popup)
    {
        const jGame::ESceneRenderPipeline choices[] = {jGame::ESceneRenderPipeline::Deferred,
            jGame::ESceneRenderPipeline::Forward, jGame::ESceneRenderPipeline::PathTracing};
        for (const auto choice : choices)
        {
            std::string reason;
            const bool supported = game && game->CanSwitchSceneRenderPipeline(choice, reason);
            const bool selected = ImGui::Selectable(RendererLabel(choice), active == choice, supported ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled);
            auto& choiceRect = s.RendererChoices[static_cast<size_t>(choice)];
            choiceRect.Position = ImGui::GetItemRectMin();
            const auto choiceEnd = ImGui::GetItemRectMax();
            choiceRect.Size = ImVec2(choiceEnd.x - choiceRect.Position.x, choiceEnd.y - choiceRect.Position.y);
            choiceRect.Visible = ImGui::IsItemVisible();
            if (selected)
            {
                // Draw only queues the request. The owner update performs the GPU/lifecycle work.
                if (game->RequestSceneRenderPipeline(choice, reason))
                {
                    s.RendererSwitchError.clear();
                    if (game->IsSceneRenderPipelineSwitchPending())
                    {
                        s.RendererSwitchTarget = choice;
                        Message(std::string("Renderer switch queued: ") + RendererLabel(choice));
                    }
                    else Message(std::string("Renderer: ") + RendererLabel(active));
                }
                else { s.RendererSwitchError = reason; Message(reason, true); }
            }
            if (active == choice) ImGui::SetItemDefaultFocus();
            if (!supported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
                ImGui::TextUnformatted(reason.empty() ? "Renderer unavailable" : reason.c_str());
                ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            }
        }
        ImGui::EndCombo();
    }
    // EndCombo restores the parent combo's item data; inside its popup these
    // getters would describe a popup item rather than the control in Scene.
    s.RendererControlPosition = ImGui::GetItemRectMin();
    const auto controlEnd = ImGui::GetItemRectMax();
    s.RendererControlSize = ImVec2(controlEnd.x - s.RendererControlPosition.x, controlEnd.y - s.RendererControlPosition.y);
    s.RendererControlVisible = ImGui::IsItemVisible();
    const bool comboActive = ImGui::IsItemActive();
    const bool comboHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::EndDisabled();
    if (game && game->IsSceneRenderPipelineSwitchPending())
    {
        const char* pending = "Switch pending";
        if (start.x + available - controlEnd.x >= ImGui::CalcTextSize(pending).x + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
        ImGui::TextDisabled("%s", pending);
        if (comboHovered) ImGui::SetTooltip("The active renderer is shown until the owner update applies this request.");
    }
    else if (!s.RendererSwitchError.empty()) ImGui::TextWrapped("%s", s.RendererSwitchError.c_str());
    ImGui::Separator();
    const ImVec2 end(start.x + available, ImGui::GetCursorScreenPos().y);
    const bool toolbarHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseHoveringRect(start, end);
    if (popup || comboActive || (toolbarHovered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))) s.ViewToolbarOwnsInput = true;
    return toolbarHovered || popup || comboActive || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
}
void ShowDialog(Dialog dialog, const fs::path& path = {})
{
    auto& s = State(); s.Modal = dialog; s.OpenModal = true;
    const auto text = jEditorAssetPath::ToUtf8(path);
    std::snprintf(s.Path, sizeof(s.Path), "%s", text.c_str());
}
void FinishInspectorEdit(jEditor& editor)
{
    auto& s = State();
    if (s.OwnPropertyTransaction && editor.GetDocument().HasTransaction()) editor.GetDocument().CommitTransaction();
    s.OwnPropertyTransaction = false; s.PropertyOwner = 0;
}
void FinishWorkspaceEdit(jEditor& editor)
{
    editor.CancelViewportDuplicateDrag();
    FinishInspectorEdit(editor);
    jEditorTools::FinishTerrainStroke(editor);
}
void ClearAssetPlacement(jEditor& editor)
{
    State().Placement.reset();
    State().Replacement.reset();
    editor.ReleasePlacementModels();
}
void SyncWorkspaceInput()
{
    const auto& io = ImGui::GetIO();
    // Platform windows send input through ImGui's backend.
    for (int key = 0; key < 26; ++key) SetKeyDownState(static_cast<EInputKey>(int(EInputKey::A) + key), ImGui::IsKeyDown(static_cast<ImGuiKey>(int(ImGuiKey_A) + key)));
    for (int key = 0; key < 10; ++key) SetKeyDownState(static_cast<EInputKey>(int(EInputKey::NUM0) + key), ImGui::IsKeyDown(static_cast<ImGuiKey>(int(ImGuiKey_0) + key)));
    const std::pair<EInputKey, ImGuiKey> mapped[] = {{EInputKey::DELETE_KEY, ImGuiKey_Delete}, {EInputKey::ESCAPE, ImGuiKey_Escape}, {EInputKey::SPACE, ImGuiKey_Space}, {EInputKey::ENTER, ImGuiKey_Enter}, {EInputKey::TAB, ImGuiKey_Tab}, {EInputKey::LEFT, ImGuiKey_LeftArrow}, {EInputKey::RIGHT, ImGuiKey_RightArrow}, {EInputKey::UP, ImGuiKey_UpArrow}, {EInputKey::DOWN, ImGuiKey_DownArrow}};
    for (const auto& [engineKey, uiKey] : mapped) SetKeyDownState(engineKey, ImGui::IsKeyDown(uiKey));
    if (State().SuppressEditorEscape) SetKeyDownState(EInputKey::ESCAPE, false);
    SetKeyDownState(EInputKey::CTRL, io.KeyCtrl || io.KeySuper); SetKeyDownState(EInputKey::ALT, io.KeyAlt); SetKeyDownState(EInputKey::SHIFT, io.KeyShift);
    SetKeyDownState(EInputKey::PLUS, ImGui::IsKeyDown(ImGuiKey_Equal) || ImGui::IsKeyDown(ImGuiKey_KeypadAdd)); SetKeyDownState(EInputKey::MINUS, ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_KeypadSubtract));
    g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down = io.MouseDown[0];
    g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down = io.MouseDown[1];
}
void GuardUnsaved(jEditor& editor, std::function<void()> action)
{
    FinishWorkspaceEdit(editor);
    ClearAssetPlacement(editor);
    if (editor.GetDocument().IsDirty()) { State().Pending = std::move(action); State().UnsavedPrompt = true; }
    else action();
}
void CopySelection(jEditor& editor)
{
    FinishWorkspaceEdit(editor);
    const auto text = editor.GetDocument().SelectionJson(editor.GetSelectedEntities());
    if (text.empty()) Message(editor.GetDocument().LastError(), true);
    else ImGui::SetClipboardText(text.c_str());
}
void PasteSelection(jEditor& editor)
{
    const char* text = ImGui::GetClipboardText();
    if (!text) return;
    const size_t length = strnlen(text, 16 * 1024 * 1024 + 1);
    if (length > 16 * 1024 * 1024) { Message("Clipboard selection exceeds 16 MiB", true); return; }
    FinishWorkspaceEdit(editor);
    const auto roots = editor.GetDocument().PasteSelectionJson(std::string(text, length));
    if (roots.empty()) Message(editor.GetDocument().LastError(), true);
    else editor.SetSelectedEntities(roots);
}
fs::path SceneDefault(jEditor& editor)
{
    if (!editor.GetDocument().Path().empty()) return editor.GetDocument().Path();
    return editor.GetProjectRoot() / "Scenes/Untitled.jscene";
}
bool SaveScene(jEditor& editor, const fs::path& path)
{
    FinishWorkspaceEdit(editor);
    try { if (path.has_parent_path()) fs::create_directories(path.parent_path()); }
    catch (const std::exception& error) { Message(error.what(), true); return false; }
    if (!editor.GetDocument().Save(path)) { Message(editor.GetDocument().LastError(), true); return false; }
    Message("Saved " + jEditorAssetPath::ToUtf8(path));
    if (State().Registry) State().Registry->ScanAsync();
    return true;
}
void SetStartupScene(jEditor& editor, const fs::path& path)
{
    auto& s = State();
    if (!s.Project || path.empty()) return;
    std::error_code ec;
    const auto relative = fs::relative(path, s.Project->GetRoot(), ec);
    std::string error;
    if (ec || !jEditorProject::ResolveSafe(s.Project->GetRoot(), relative, error, true))
    { Message(error.empty() ? "Choose a saved scene inside this project." : error, true); return; }
    auto config = s.Project->GetConfig();
    config.StartupScene = jEditorAssetPath::ToUtf8(relative);
    if (jEditorProjectSettings::Save(*s.Project, editor, config, error)) Message("Startup scene: " + config.StartupScene);
    else Message(error, true);
}
void LoadLayout()
{
    auto& s = State(); s.LoadLayout = false;
    try
    {
        std::ifstream in(s.LayoutFile);
        if (in) in >> s.Layout;
        if (!s.Layout.is_object()) s.Layout = Json::object();
        s.LeftWidth = std::clamp(s.Layout.value("leftWidth", 245.0f), 180.0f, 700.0f);
        s.RightWidth = std::clamp(s.Layout.value("rightWidth", 325.0f), 240.0f, 800.0f);
        s.BottomHeight = std::clamp(s.Layout.value("bottomHeight", 205.0f), 100.0f, 600.0f);
        s.FontScale = std::clamp(s.Layout.value("fontScale", 1.0f), 0.8f, 2.0f);
        s.FixedLayout = s.Layout.value("fixedLayout", true);
        s.AutoSave = s.Layout.value("autoSave", true);
        s.Docking = s.Layout.value("docking", true);
        s.PlatformWindows = s.Layout.value("platformWindows", false);
    }
    catch (const std::exception& e) { Message(std::string("Layout settings: ") + e.what(), true); }
}
void SaveLayout()
{
    auto& s = State();
    s.Layout["leftWidth"] = s.LeftWidth; s.Layout["rightWidth"] = s.RightWidth;
    s.Layout["bottomHeight"] = s.BottomHeight; s.Layout["fontScale"] = s.FontScale;
    s.Layout["fixedLayout"] = s.FixedLayout; s.Layout["autoSave"] = s.AutoSave;
    s.Layout["docking"] = s.Docking; s.Layout["platformWindows"] = s.PlatformWindows;
    try
    {
        fs::create_directories(s.LayoutFile.parent_path());
        auto temporary = s.LayoutFile; temporary += ".tmp";
        std::ofstream out(temporary); out << s.Layout.dump(2); out.close();
        if (!out) throw std::runtime_error("Could not write layout settings");
        std::error_code ec; fs::rename(temporary, s.LayoutFile, ec);
        if (ec) { fs::copy_file(temporary, s.LayoutFile, fs::copy_options::overwrite_existing); fs::remove(temporary); }
        Message("Layout saved");
        ImGui::SaveIniSettingsToDisk(s.ImGuiFile.c_str());
    }
    catch (const std::exception& e) { Message(e.what(), true); }
}
void FinishProperties(Document& document, std::span<const Entity> edits, const char* label)
{
    if (ImGui::IsItemActivated() && !document.HasTransaction())
    {
        if (document.BeginTransaction(label)) { State().OwnPropertyTransaction = true; State().PropertyOwner = document.SelectedId(); }
    }
    if (!edits.empty() && !document.UpdateMany(edits, label)) Message(document.LastError(), true);
    if (ImGui::IsItemDeactivated() && State().OwnPropertyTransaction)
    {
        if (document.HasTransaction()) document.CommitTransaction();
        State().OwnPropertyTransaction = false; State().PropertyOwner = 0;
    }
}
void FinishProperty(Document& document, Entity& entity, bool changed, const char* label)
{
    FinishProperties(document, std::span(&entity, changed ? 1 : 0), label);
}
void FinishMaterialProperty(Document& document, Entity& entity, bool changed, const char* label)
{
    if (changed) entity.Material.OverrideEnabled = true;
    FinishProperty(document, entity, changed, label);
}
void Vec3Field(Document& document, Entity& entity, const char* label, Vec3& value, float speed = .25f)
{
    FinishProperty(document, entity, ImGui::DragFloat3(label, &value.X, speed), label);
}
void AddMenu(jEditor& editor)
{
    for (int i = 0; i <= static_cast<int>(EntityKind::Water); ++i)
    {
        auto kind = static_cast<EntityKind>(i);
        if (kind == EntityKind::Model) continue;
        if (ImGui::MenuItem(KindName(kind))) editor.CreateEntity(kind);
    }
}
void BuildRows(Document& document)
{
    auto& s = State(); const std::string filter(s.EntityFilter.InputBuf);
    const auto selected = filter.empty() ? 0 : document.SelectedId();
    if (s.RowRevision == document.Revision() && s.CachedEntityFilter == filter && s.RowSelected == selected) return;
    s.RowRevision = document.Revision();
    s.RowSelected = selected;
    uint64_t signature = std::hash<std::string>{}(filter);
    const auto mix = [&](uint64_t value) { signature ^= value + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2); };
    mix(selected);
    std::map<EntityId, std::vector<EntityId>> children;
    for (const auto& [id, entity] : document.Entities()) { mix(id); mix(entity.Parent); mix(std::hash<std::string>{}(entity.Name)); }
    for (auto id : s.Collapsed) mix(id);
    if (s.RowSignature == signature && s.CachedEntityFilter == filter) return;
    s.RowSignature = signature; s.Rows.clear(); s.CachedEntityFilter = filter;
    for (const auto& [id, entity] : document.Entities()) children[entity.Parent].push_back(id);
    std::set<EntityId> matches;
    if (!filter.empty()) for (const auto& [id, e] : document.Entities())
        if (id == selected || s.EntityFilter.PassFilter(e.Name.c_str()))
            for (EntityId at = id; at; ) { matches.insert(at); const auto* p = document.Find(at); at = p ? p->Parent : 0; }
    std::function<void(EntityId, int)> visit = [&](EntityId parent, int depth)
    {
        for (auto id : children[parent])
        {
            if (!filter.empty() && !matches.count(id)) continue;
            const auto& e = *document.Find(id); const bool hasChildren = !children[id].empty();
            s.Rows.push_back({id, depth, hasChildren, e.Name + "##entity" + std::to_string(id)});
            if (hasChildren && (!s.Collapsed.count(id) || !filter.empty())) visit(id, depth + 1);
        }
    }; visit(0, 0);
}
void SelectHierarchyRow(jEditor& editor, int index)
{
    auto& s = State();
    const auto id = s.Rows[index].Id;
    const auto& io = ImGui::GetIO();
    const bool toggle = io.KeyCtrl || io.KeySuper;
    const auto anchor = std::find_if(s.Rows.begin(), s.Rows.end(), [&](const Row& row) { return row.Id == s.RangeAnchor; });
    if (io.KeyShift && anchor != s.Rows.end())
    {
        const int first = static_cast<int>(anchor - s.Rows.begin());
        auto selected = toggle ? editor.GetSelectedEntities() : std::vector<EntityId>();
        std::erase(selected, id);
        for (int i = std::min(first, index); i <= std::max(first, index); ++i)
            if (i != index) selected.push_back(s.Rows[i].Id);
        selected.push_back(id);
        editor.SetSelectedEntities(selected);
    }
    else
    {
        if (toggle) editor.ToggleEntitySelection(id);
        else editor.SelectEntity(id);
        s.RangeAnchor = id;
    }
}
void Hierarchy(jEditor& editor)
{
    auto& s = State(); auto& document = editor.GetDocument();
    if (ImGui::Button("+ Add")) ImGui::OpenPopup("AddEntity");
    if (ImGui::BeginPopup("AddEntity")) { AddMenu(editor); ImGui::EndPopup(); }
    ImGui::SameLine(); if (ImGui::Button("Duplicate")) editor.DuplicateSelected();
    s.EntityFilter.Draw("Search##entities", -1);
    if (ImGui::Button("Scene root", ImVec2(-1, 0))) editor.SelectEntity(0);
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_ENTITY"))
            if (payload->DataSize == sizeof(EntityId)) document.Reparent(*static_cast<const EntityId*>(payload->Data), 0);
        ImGui::EndDragDropTarget();
    }
    if (s.HierarchyActive != document.SelectedId())
        s.RangeAnchor = s.RevealHierarchy = document.SelectedId();
    if (s.RevealHierarchy)
    {
        for (const auto* entity = document.Find(s.RevealHierarchy); entity && entity->Parent; entity = document.Find(entity->Parent))
            if (s.Collapsed.erase(entity->Parent)) s.RowRevision = ~uint64_t(0);
    }
    BuildRows(document);
    ImGui::TextDisabled("%zu objects | %zu selected", document.Entities().size(), editor.GetSelectedEntities().size());
    ImGui::BeginChild("EntityRows", ImVec2(0, 0), false);
    ImGuiListClipper clip; clip.Begin(static_cast<int>(s.Rows.size()));
    if (s.RevealHierarchy)
    {
        const auto row = std::find_if(s.Rows.begin(), s.Rows.end(), [&](const Row& entry) { return entry.Id == s.RevealHierarchy; });
        if (row != s.Rows.end()) clip.IncludeItemByIndex(static_cast<int>(row - s.Rows.begin()));
    }
    while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
    {
        const auto row = s.Rows[i]; const auto* found = document.Find(row.Id); if (!found) continue;
        const Entity rowEntity = *found; const auto* e = &rowEntity;
        ImGui::PushID(static_cast<int>(row.Id)); ImGui::Indent(row.Depth * 12.0f);
        if (row.Children)
        {
            if (ImGui::ArrowButton("Expand", s.Collapsed.count(row.Id) ? ImGuiDir_Right : ImGuiDir_Down))
            { if (!s.Collapsed.erase(row.Id)) s.Collapsed.insert(row.Id); s.RowRevision = ~uint64_t(0); }
            ImGui::SameLine();
        }
        const auto& selected = editor.GetSelectedEntities();
        const bool active = std::find(selected.begin(), selected.end(), row.Id) != selected.end();
        if (!e->Visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.5f, .5f, .5f, 1));
        if (ImGui::Selectable(row.Label.c_str(), active, 0, ImVec2(0, ImGui::GetFrameHeight()))) SelectHierarchyRow(editor, i);
        if (row.Id == s.RevealHierarchy) ImGui::SetScrollHereY();
        if (!e->Visible) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) editor.FocusSelected(Camera());
        if (!document.IsEffectivelyLocked(row.Id) && ImGui::BeginDragDropSource())
        { ImGui::SetDragDropPayload("JENGINE_ENTITY", &row.Id, sizeof(row.Id)); ImGui::TextUnformatted(e->Name.c_str()); ImGui::EndDragDropSource(); }
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_ENTITY"))
                if (payload->DataSize == sizeof(EntityId) && !document.Reparent(*static_cast<const EntityId*>(payload->Data), row.Id)) Message(document.LastError(), true);
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem())
        {
            if (!active) { editor.SelectEntity(row.Id); s.RangeAnchor = row.Id; }
            if (ImGui::MenuItem("Focus", "F")) editor.FocusSelected(Camera());
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) editor.DuplicateSelected();
            Entity edit = *e;
            if (ImGui::MenuItem("Visible", nullptr, edit.Visible)) { edit.Visible = !edit.Visible; document.Update(edit); }
            if (ImGui::MenuItem("Locked", nullptr, edit.Locked)) { edit.Locked = !edit.Locked; document.Update(edit); }
            if (ImGui::MenuItem("Save prefab")) ShowDialog(Dialog::SavePrefab, editor.GetProjectRoot() / "Assets/Prefabs/Object.jprefab");
            if (ImGui::MenuItem("Delete", "Delete")) editor.DeleteSelected();
            ImGui::EndPopup();
        }
        ImGui::Unindent(row.Depth * 12.0f); ImGui::PopID();
    }
    ImGui::EndChild();
    s.RevealHierarchy = 0;
    s.HierarchyActive = document.SelectedId();
}
const jEditorAssetRecord* AssetDropTarget()
{
    const jEditorAssetRecord* asset = nullptr;
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_ASSET"))
        {
            auto& s = State();
            if (s.Registry && payload->DataSize > 0)
            {
                const std::string id(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize) - 1);
                asset = s.Registry->FindById(id);
            }
        }
        ImGui::EndDragDropTarget();
    }
    return asset;
}
void TextureSlot(Document& document, Entity& entity, const char* label, std::string& path,
    const std::string* inherited = nullptr, bool* overridden = nullptr)
{
    const auto& shown = overridden && !*overridden ? *inherited : path;
    auto& s = State();
    ImGui::BeginGroup();
    if (!shown.empty())
    {
        jEditorAssetPanel::RenderTexturePreview(s.Project.get(), s.Registry.get(), shown, ImVec2(56, 56));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    if (!entity.Material.OverrideEnabled && !shown.empty()) ImGui::TextDisabled("Override inactive");
    else if (overridden) ImGui::TextDisabled("%s", *overridden ? "Instance override" : "Shared value");
    ImGui::TextWrapped("%s", shown.empty() ? "None (drop a texture)" : shown.c_str());
    ImGui::EndGroup();
    ImGui::EndGroup();
    if (const auto* asset = AssetDropTarget(); asset && asset->Kind == jEditorAssetKind::Texture)
    {
        path = asset->SourcePath;
        if (overridden) *overridden = true;
        entity.Material.OverrideEnabled = true;
        document.Update(entity, "Assign texture");
    }
}
template <typename T>
void ResetMaterialParameter(Document& document, Entity& entity, const char* label, bool& overridden,
    T& value, const T& inherited)
{
    if (!overridden) return;
    ImGui::PushID(label);
    if (ImGui::SmallButton("Use shared value"))
    {
        overridden = false;
        value = inherited;
        document.Update(entity, "Reset material parameter");
    }
    ImGui::PopID();
}
void MaterialInspector(jEditor& editor, Entity& entity)
{
    auto& document = editor.GetDocument();
    const auto& assetPath = entity.Material.GraphAsset;
    ImGui::TextWrapped("Shared material: %s", entity.Material.GraphOverrides ? assetPath.c_str() : "Drop a .jmaterial here");
    if (const auto* asset = AssetDropTarget(); asset && asset->Kind == jEditorAssetKind::Material)
    {
        std::string error;
        if (jEditorTools::AssignSharedMaterial(editor, entity.Id, asset->SourcePath, error))
            entity = *document.Find(entity.Id);
        else Message(error, true);
    }
    auto& material = entity.Material;
    if (entity.Kind == EntityKind::Model && ImGui::Checkbox("Override imported material", &material.OverrideEnabled))
        document.Update(entity, "Toggle material override");

    auto defaults = material;
    if (defaults.GraphOverrides) defaults.GraphOverrides.emplace();
    defaults = editor.ResolveMaterial(defaults);
    auto* flags = material.GraphOverrides ? &*material.GraphOverrides : nullptr;
    auto color = flags && !flags->BaseColor ? defaults.BaseColor : material.BaseColor;
    const bool colorChanged = ImGui::ColorEdit3("Base color", &color.X);
    if (colorChanged) { material.BaseColor = color; if (flags) flags->BaseColor = true; }
    FinishMaterialProperty(document, entity, colorChanged, "Material color");
    if (flags) ResetMaterialParameter(document, entity, "Base color", flags->BaseColor, material.BaseColor, defaults.BaseColor);

    FinishMaterialProperty(document, entity, ImGui::ColorEdit3("Emissive", &material.Emissive.X, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float), "Emissive");
    auto scalar = [&](const char* label, float& value, float inherited, bool* overridden, float minimum)
    {
        float shown = overridden && !*overridden ? inherited : value;
        const bool changed = ImGui::SliderFloat(label, &shown, minimum, 1);
        if (changed) { value = shown; if (overridden) *overridden = true; }
        FinishMaterialProperty(document, entity, changed, label);
        if (overridden) ResetMaterialParameter(document, entity, label, *overridden, value, inherited);
    };
    scalar("Metallic", material.Metallic, defaults.Metallic, flags ? &flags->Metallic : nullptr, 0);
    scalar("Roughness", material.Roughness, defaults.Roughness, flags ? &flags->Roughness : nullptr, .01f);
    FinishMaterialProperty(document, entity, ImGui::SliderFloat("Opacity", &material.Opacity, 0, 1), "Opacity");
    TextureSlot(document, entity, "Base color texture", material.BaseColorTexture, &defaults.BaseColorTexture,
        flags ? &flags->BaseColorTexture : nullptr);
    if (flags) ResetMaterialParameter(document, entity, "Base color texture", flags->BaseColorTexture,
        material.BaseColorTexture, defaults.BaseColorTexture);
    TextureSlot(document, entity, "Normal texture", material.NormalTexture, &defaults.NormalTexture,
        flags ? &flags->NormalTexture : nullptr);
    if (flags) ResetMaterialParameter(document, entity, "Normal texture", flags->NormalTexture,
        material.NormalTexture, defaults.NormalTexture);
    if (ImGui::TreeNode("Subsurface material"))
    {
        auto shown = material;
        if (flags)
        {
            if (!flags->SubsurfaceWeight) shown.SubsurfaceWeight = defaults.SubsurfaceWeight;
            if (!flags->SubsurfaceColor) shown.SubsurfaceColor = defaults.SubsurfaceColor;
            if (!flags->SubsurfaceThickness) shown.SubsurfaceThicknessWorld = defaults.SubsurfaceThicknessWorld;
            if (!flags->SubsurfaceProfile) shown.SubsurfaceProfile = defaults.SubsurfaceProfile;
        }
        else if (!material.SubsurfaceOverrideEnabled)
        {
            const auto* object = editor.ObjectForEntity(entity.Id);
            if (object && !object->RenderObjects.empty() && object->RenderObjects[0]->MaterialPtr && object->RenderObjects[0]->MaterialPtr->Surface)
            {
                const auto& surface = *object->RenderObjects[0]->MaterialPtr->Surface;
                shown.SubsurfaceWeight = surface.Subsurface;
                shown.SubsurfaceColor = {surface.SubsurfaceColor[0], surface.SubsurfaceColor[1], surface.SubsurfaceColor[2]};
                shown.SubsurfaceThicknessWorld = surface.SubsurfaceThicknessWorld; shown.SubsurfaceProfile = surface.SubsurfaceProfile;
            }
        }
        const auto finishSSS = [&](bool changed, bool* overridden, const char* label)
        {
            if (changed)
            {
                material.SubsurfaceOverrideEnabled = true;
                material.SubsurfaceWeight = shown.SubsurfaceWeight; material.SubsurfaceColor = shown.SubsurfaceColor;
                material.SubsurfaceThicknessWorld = shown.SubsurfaceThicknessWorld; material.SubsurfaceProfile = shown.SubsurfaceProfile;
                if (overridden) *overridden = true;
            }
            FinishMaterialProperty(document, entity, changed, label);
        };
        finishSSS(ImGui::SliderFloat("Subsurface weight", &shown.SubsurfaceWeight, 0, 1), flags ? &flags->SubsurfaceWeight : nullptr, "Subsurface weight");
        if (flags) ResetMaterialParameter(document, entity, "SSS weight", flags->SubsurfaceWeight, material.SubsurfaceWeight, defaults.SubsurfaceWeight);
        finishSSS(ImGui::ColorEdit3("Subsurface color", &shown.SubsurfaceColor.X), flags ? &flags->SubsurfaceColor : nullptr, "Subsurface color");
        if (flags) ResetMaterialParameter(document, entity, "SSS color", flags->SubsurfaceColor, material.SubsurfaceColor, defaults.SubsurfaceColor);
        finishSSS(ImGui::DragFloat("Thickness (world)", &shown.SubsurfaceThicknessWorld, .01f, 0, 10000, "%.3g", ImGuiSliderFlags_AlwaysClamp),
            flags ? &flags->SubsurfaceThickness : nullptr, "Subsurface thickness");
        if (flags) ResetMaterialParameter(document, entity, "SSS thickness", flags->SubsurfaceThickness, material.SubsurfaceThicknessWorld, defaults.SubsurfaceThicknessWorld);
        static const auto profiles = [] { std::array<const char*, 8> names{}; for (size_t i = 0; i < names.size(); ++i) names[i] = jSubsurface::Profiles[i].Name; return names; }();
        int profile = int(shown.SubsurfaceProfile);
        const bool profileChanged = ImGui::Combo("Diffusion profile", &profile, profiles.data(), int(profiles.size()));
        if (profileChanged) shown.SubsurfaceProfile = uint32_t(profile);
        finishSSS(profileChanged, flags ? &flags->SubsurfaceProfile : nullptr, "Subsurface profile");
        if (flags) ResetMaterialParameter(document, entity, "SSS profile", flags->SubsurfaceProfile, material.SubsurfaceProfile, defaults.SubsurfaceProfile);
        if (material.SubsurfaceOverrideEnabled && ImGui::Button("Inherit subsurface"))
        {
            material.SubsurfaceOverrideEnabled = false;
            if (flags) flags->SubsurfaceWeight = flags->SubsurfaceColor = flags->SubsurfaceThickness = flags->SubsurfaceProfile = false;
            document.Update(entity, "Inherit subsurface material");
        }
        ImGui::TextWrapped("Deferred surface diffusion requires Project Settings > Enable Deferred SSS. Thickness uses scene world units; 0 disables diffusion. Presets approximate surface scattering, not hidden-surface transmission. PT keeps its existing subsurface approximation.");
        const auto& reason = GetRendererFeatureDiagnostics().SubsurfaceSupportReason;
        if (!reason.empty()) ImGui::TextWrapped("%s", reason.c_str());
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Skin shading"))
    {
        auto shown = material;
        if (flags)
        {
            if (!flags->ShadingModel) shown.ShadingModel = defaults.ShadingModel;
            if (!flags->SubsurfaceSpace) shown.SubsurfaceSpace = defaults.SubsurfaceSpace;
            if (!flags->SpecularWeight) shown.SpecularWeight = defaults.SpecularWeight;
            if (!flags->DiffuseOcclusion) shown.DiffuseOcclusion = defaults.DiffuseOcclusion;
            if (!flags->PreScatterWeight) shown.PreScatterWeight = defaults.PreScatterWeight;
        }
        else if (!material.SkinOverrideEnabled)
        {
            const auto* object = editor.ObjectForEntity(entity.Id);
            if (object && !object->RenderObjects.empty() && object->RenderObjects[0]->MaterialPtr && object->RenderObjects[0]->MaterialPtr->Surface)
            {
                const auto& surface = *object->RenderObjects[0]->MaterialPtr->Surface;
                shown.ShadingModel = surface.ShadingModel; shown.SubsurfaceSpace = surface.SubsurfaceSpace;
                shown.SpecularWeight = surface.SpecularWeight; shown.DiffuseOcclusion = surface.DiffuseOcclusion;
                shown.PreScatterWeight = surface.PreScatterWeight;
            }
        }
        const auto finishSkin = [&](bool changed, bool* overridden, const char* label)
        {
            if (changed)
            {
                material.SkinOverrideEnabled = true;
                material.ShadingModel = shown.ShadingModel; material.SubsurfaceSpace = shown.SubsurfaceSpace;
                material.SpecularWeight = shown.SpecularWeight; material.DiffuseOcclusion = shown.DiffuseOcclusion;
                material.PreScatterWeight = shown.PreScatterWeight;
                if (overridden) *overridden = true;
            }
            FinishMaterialProperty(document, entity, changed, label);
        };
        int model = int(shown.ShadingModel), space = int(shown.SubsurfaceSpace);
        const bool modelChanged = ImGui::Combo("Shading model", &model, "Standard PBR\0Skin Kelemen\0");
        if (modelChanged) shown.ShadingModel = jMaterialMapping::ShadingModel(model);
        finishSkin(modelChanged, flags ? &flags->ShadingModel : nullptr, "Shading model");
        if (flags) ResetMaterialParameter(document, entity, "Shading model", flags->ShadingModel, material.ShadingModel, defaults.ShadingModel);
        const bool spaceChanged = ImGui::Combo("Subsurface space", &space, "Screen\0Texture (UV)\0");
        if (spaceChanged) shown.SubsurfaceSpace = jMaterialMapping::SubsurfaceSpace(space);
        finishSkin(spaceChanged, flags ? &flags->SubsurfaceSpace : nullptr, "Subsurface space");
        if (flags) ResetMaterialParameter(document, entity, "Subsurface space", flags->SubsurfaceSpace, material.SubsurfaceSpace, defaults.SubsurfaceSpace);
        finishSkin(ImGui::SliderFloat("Specular weight", &shown.SpecularWeight, 0, 8), flags ? &flags->SpecularWeight : nullptr, "Specular weight");
        if (flags) ResetMaterialParameter(document, entity, "Specular weight", flags->SpecularWeight, material.SpecularWeight, defaults.SpecularWeight);
        finishSkin(ImGui::SliderFloat("Diffuse occlusion", &shown.DiffuseOcclusion, 0, 1), flags ? &flags->DiffuseOcclusion : nullptr, "Diffuse occlusion");
        if (flags) ResetMaterialParameter(document, entity, "Diffuse occlusion", flags->DiffuseOcclusion, material.DiffuseOcclusion, defaults.DiffuseOcclusion);
        finishSkin(ImGui::SliderFloat("Pre-scatter weight", &shown.PreScatterWeight, 0, 1), flags ? &flags->PreScatterWeight : nullptr, "Pre-scatter weight");
        if (flags) ResetMaterialParameter(document, entity, "Pre-scatter weight", flags->PreScatterWeight, material.PreScatterWeight, defaults.PreScatterWeight);
        if (material.SkinOverrideEnabled && ImGui::Button("Inherit skin shading"))
        {
            material.SkinOverrideEnabled = false;
            if (flags) flags->ShadingModel = flags->SubsurfaceSpace = flags->SpecularWeight = flags->DiffuseOcclusion = flags->PreScatterWeight = false;
            document.Update(entity, "Inherit skin shading");
        }
        ImGui::TextWrapped("Skin Kelemen interprets Roughness as Beckmann m. Texture diffusion uses UVs and requires its renderer support; these material controls alone do not reproduce the original full Skin pipeline.");
        ImGui::TreePop();
    }
    if (ImGui::Button("Material graph")) State().ShowTools = true;
}
bool HasMaterial(EntityKind kind)
{
    return kind == EntityKind::Cube || kind == EntityKind::Sphere || kind == EntityKind::Model
        || kind == EntityKind::Terrain || kind == EntityKind::Water;
}
bool IsLight(EntityKind kind)
{
    return kind == EntityKind::PointLight || kind == EntityKind::SpotLight || kind == EntityKind::DirectionalLight;
}
bool CanReplaceModels(const Document& document, std::span<const EntityId> ids)
{
    return !document.IsPlaying() && !ids.empty() && std::all_of(ids.begin(), ids.end(), [&](EntityId id) {
        const auto* entity = document.Find(id);
        return entity && entity->Kind == EntityKind::Model && !document.IsEffectivelyLocked(id);
    });
}
void RequestModelReplacement(jEditor& editor, const jEditorAssetRecord& asset, std::span<const EntityId> ids)
{
    auto& s = State(); auto& document = editor.GetDocument();
    if (s.Placement || s.Replacement) { Message("Finish the pending asset operation first.", true); return; }
    if (!CanReplaceModels(document, ids) || asset.Kind != jEditorAssetKind::Model || asset.Missing || !asset.MissingDependencies.empty()) return;
    ModelReplacement replacement;
    replacement.Asset = asset;
    for (const auto id : ids)
    {
        const auto& entity = *document.Find(id);
        replacement.Targets.push_back({id, entity.SourceAsset, entity.AssetId});
    }
    FinishWorkspaceEdit(editor);
    s.Replacement = std::move(replacement);
    Message("Preparing replacement model " + asset.SourcePath);
}
void AdvanceModelReplacement(jEditor& editor)
{
    auto& s = State(); auto& document = editor.GetDocument();
    if (!s.Replacement || document.HasTransaction()) return;
    const auto& replacement = *s.Replacement;
    const auto* asset = s.Registry ? s.Registry->FindById(replacement.Asset.Id) : nullptr;
    if (!asset || asset->Missing || !asset->MissingDependencies.empty() ||
        asset->Revision != replacement.Asset.Revision || asset->SourcePath != replacement.Asset.SourcePath)
    {
        Message("Replacement asset changed. Select it again to continue.", true);
        ClearAssetPlacement(editor);
        return;
    }
    std::vector<EntityId> ids;
    for (const auto& target : replacement.Targets)
    {
        const auto* entity = document.Find(target.Id);
        if (!entity || entity->AssetId != target.AssetId || entity->SourceAsset != target.Source)
        {
            Message("Replacement cancelled because a target changed.", true);
            ClearAssetPlacement(editor);
            return;
        }
        ids.push_back(target.Id);
    }
    const auto result = editor.ReplaceModelAssets(ids, asset->SourcePath, asset->Id, asset->Revision);
    if (result == jEditor::PlacementReadiness::Pending) return;
    if (result == jEditor::PlacementReadiness::Ready) Message("Model asset set to " + asset->SourcePath);
    else Message(editor.LastError(), true);
    ClearAssetPlacement(editor);
}
const jEditorAssetRecord* FindModelAsset(const Entity& entity)
{
    const auto& s = State();
    if (!s.Registry || entity.Kind != EntityKind::Model) return nullptr;
    return entity.AssetId.empty() ? s.Registry->FindBySourcePath(entity.SourceAsset) : s.Registry->FindById(entity.AssetId);
}
void ModelAssetActions(jEditor& editor, std::span<const EntityId> ids)
{
    auto& s = State(); auto& document = editor.GetDocument();
    if (!s.Registry || ids.empty() || !std::all_of(ids.begin(), ids.end(), [&](EntityId id) {
        return document.Find(id)->Kind == EntityKind::Model;
    })) return;
    const auto& active = *document.Find(document.SelectedId());
    const auto* asset = FindModelAsset(active);
    const auto& path = asset ? asset->SourcePath : active.SourceAsset;
    if (!path.empty()) ImGui::TextWrapped("%s", path.c_str());
    ImGui::BeginDisabled(!asset);
    if (ImGui::Button(ids.size() == 1 ? "Find in Assets" : "Find Active in Assets"))
    {
        jEditorAssetPanel::Reveal(*asset);
        s.SelectedAsset = asset->Id;
        s.ShowAssets = s.FocusAssets = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.Placement || s.Replacement || !CanReplaceModels(document, ids));
    if (ImGui::Button("Replace Asset")) { s.ReplacementFilter.Clear(); ImGui::OpenPopup("Replace model asset"); }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("Replace model asset"))
    {
        ImGui::Text("Replace asset on %zu model%s", ids.size(), ids.size() == 1 ? "" : "s");
        s.ReplacementFilter.Draw("Search##replacement", 320);
        std::vector<const jEditorAssetRecord*> models;
        for (const auto& model : s.Registry->GetAssets())
            if (model.Kind == jEditorAssetKind::Model && !model.Missing && model.MissingDependencies.empty()
                && s.ReplacementFilter.PassFilter(model.SourcePath.c_str())) models.push_back(&model);
        ImGui::BeginChild("Replacement models", ImVec2(380, 200), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (models.empty()) ImGui::TextDisabled("No available models match.");
        ImGuiListClipper clip; clip.Begin(static_cast<int>(models.size()));
        while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
        {
            const auto& model = *models[i];
            ImGui::PushID(model.Id.c_str());
            if (ImGui::Selectable(model.SourcePath.c_str(), asset && model.Id == asset->Id, ImGuiSelectableFlags_DontClosePopups))
            {
                RequestModelReplacement(editor, model, ids);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}
template<class Read, class Write, class Widget>
void BatchField(Document& document, std::span<const EntityId> ids, const char* label, Read read, Write write, Widget widget)
{
    if (ids.empty()) return;
    auto value = read(*document.Find(ids.back()));
    const bool mixed = std::any_of(ids.begin(), ids.end(), [&](EntityId id) { return read(*document.Find(id)) != value; });
    if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    const bool changed = widget(label, value);
    if (mixed) ImGui::PopItemFlag();
    std::vector<Entity> edits;
    if (changed)
    {
        edits.reserve(ids.size());
        for (const auto id : ids)
        {
            edits.push_back(*document.Find(id));
            write(edits.back(), value);
        }
    }
    FinishProperties(document, edits, label);
    if (mixed) { ImGui::SameLine(); ImGui::TextDisabled("Mixed"); }
}
void MultiInspector(jEditor& editor, std::span<const EntityId> ids)
{
    auto& document = editor.GetDocument();
    const auto all = [&](auto predicate) {
        return std::all_of(ids.begin(), ids.end(), [&](EntityId id) { return predicate(*document.Find(id)); });
    };
    const auto check = [](const char* label, bool& value) { return ImGui::Checkbox(label, &value); };
    const auto drag = [](float speed, float min, float max) {
        return [=](const char* label, float& value) { return ImGui::DragFloat(label, &value, speed, min, max, "%.3f", ImGuiSliderFlags_AlwaysClamp); };
    };
    const auto color = [](const char* label, Vec3& value) {
        return ImGui::ColorEdit3(label, &value.X, ImGuiColorEditFlags_NoInputs);
    };
    const auto field = [&](const char* label, auto group, auto member, auto widget) {
        BatchField(document, ids, label, [=](const Entity& e) { return (e.*group).*member; },
            [=](Entity& e, const auto& value) { (e.*group).*member = value; }, widget);
    };
    ImGui::Text("%zu objects selected", ids.size());
    ModelAssetActions(editor, ids);
    std::vector<EntityId> lockTargets;
    for (const auto id : ids)
    {
        const auto parent = document.Find(id)->Parent;
        if (!parent || !document.IsEffectivelyLocked(parent)) lockTargets.push_back(id);
    }
    BatchField(document, lockTargets, "Locked", [](const Entity& e) { return e.Locked; },
        [](Entity& e, bool value) { e.Locked = value; }, check);
    const bool locked = !all([&](const Entity& e) { return !document.IsEffectivelyLocked(e.Id); });
    if (locked) ImGui::TextDisabled("Unlock selection to edit shared properties.");
    ImGui::BeginDisabled(locked);
    BatchField(document, ids, "Visible", [](const Entity& e) { return e.Visible; },
        [](Entity& e, bool value) { e.Visible = value; }, check);
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto axes = [&](const char* label, Vec3 Transform::* member, float speed) {
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);
            const auto axis = [&](const char* name, float Vec3::* component) {
                BatchField(document, ids, name, [=](const Entity& e) { return (e.Local.*member).*component; },
                    [=](Entity& e, float value) { (e.Local.*member).*component = value; }, drag(speed, 0, 0));
            };
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .56f);
            axis("X", &Vec3::X); axis("Y", &Vec3::Y); axis("Z", &Vec3::Z);
            ImGui::PopItemWidth();
            ImGui::PopID();
        };
        axes("Position", &Transform::Position, .25f);
        axes("Rotation (deg)", &Transform::RotationDegrees, .25f);
        axes("Scale", &Transform::Scale, .01f);
        if (ImGui::Button("Reset transform"))
        {
            std::vector<Entity> edits;
            for (const auto id : ids) { edits.push_back(*document.Find(id)); edits.back().Local = {}; }
            if (!document.UpdateMany(edits, "Reset transforms")) Message(document.LastError(), true);
        }
    }
    if (all([](const Entity& e) { return IsLight(e.Kind); }) && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        field("Color", &Entity::Light, &LightProperties::Color, color);
        field("Intensity", &Entity::Light, &LightProperties::Intensity, drag(.1f, 0, 100000));
        if (all([](const Entity& e) { return e.Kind != EntityKind::DirectionalLight; }))
            field("Range", &Entity::Light, &LightProperties::Range, drag(1, .001f, 100000));
        if (all([](const Entity& e) { return e.Kind == EntityKind::SpotLight; }))
        {
            float maxInner = 89.9f;
            for (const auto id : ids) maxInner = std::min(maxInner, document.Find(id)->Light.OuterConeDegrees);
            if (maxInner > 0) field("Inner cone", &Entity::Light, &LightProperties::InnerConeDegrees, drag(.1f, 0, maxInner));
            float minOuter = 0;
            for (const auto id : ids) minOuter = std::max(minOuter, document.Find(id)->Light.InnerConeDegrees);
            if (minOuter < 89.9f) field("Outer cone", &Entity::Light, &LightProperties::OuterConeDegrees, drag(.1f, minOuter, 89.9f));
        }
        field("Cast shadows", &Entity::Light, &LightProperties::CastShadows, check);
    }
    if (all([](const Entity& e) { return e.Kind == EntityKind::Camera; }) && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        field("Vertical FOV", &Entity::Camera, &CameraProperties::VerticalFovDegrees, drag(.25f, 5, 170));
        float maxNear = 10000;
        for (const auto id : ids) maxNear = std::min(maxNear, std::nextafter(document.Find(id)->Camera.FarPlane, 0.f));
        if (maxNear > .001f) field("Near", &Entity::Camera, &CameraProperties::NearPlane, drag(.01f, .001f, maxNear));
        float minFar = .01f;
        for (const auto id : ids) minFar = std::max(minFar, std::nextafter(document.Find(id)->Camera.NearPlane, FLT_MAX));
        if (minFar < 1000000) field("Far", &Entity::Camera, &CameraProperties::FarPlane, drag(1, minFar, 1000000));
        field("Primary", &Entity::Camera, &CameraProperties::Primary, check);
    }
    if (all([](const Entity& e) { return HasMaterial(e.Kind); }) && ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (all([](const Entity& e) { return e.Kind == EntityKind::Model; }))
            field("Override imported material", &Entity::Material, &MaterialProperties::OverrideEnabled, check);
        const bool imported = !all([](const Entity& e) { return e.Kind != EntityKind::Model || e.Material.OverrideEnabled; });
        if (imported) ImGui::TextWrapped("Enable material overrides on selected models to edit these values.");
        ImGui::BeginDisabled(imported);
        const auto material = [&](const char* label, auto member, bool MaterialGraphOverrides::* flag, auto widget) {
            BatchField(document, ids, label, [&](const Entity& e) { return editor.ResolveMaterial(e.Material).*member; },
                [=](Entity& e, const auto& value) {
                    e.Material.*member = value;
                    e.Material.OverrideEnabled = true;
                    if (flag && e.Material.GraphOverrides) (*e.Material.GraphOverrides).*flag = true;
                }, widget);
        };
        material("Base color", &MaterialProperties::BaseColor, &MaterialGraphOverrides::BaseColor, color);
        material("Emissive", &MaterialProperties::Emissive, nullptr, [](const char* label, Vec3& value) {
            return ImGui::ColorEdit3(label, &value.X, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
        });
        material("Metallic", &MaterialProperties::Metallic, &MaterialGraphOverrides::Metallic, drag(.01f, 0, 1));
        material("Roughness", &MaterialProperties::Roughness, &MaterialGraphOverrides::Roughness, drag(.01f, .01f, 1));
        material("Opacity", &MaterialProperties::Opacity, nullptr, drag(.01f, 0, 1));
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    if (ImGui::Button("Duplicate")) editor.DuplicateSelected(); ImGui::SameLine();
    if (ImGui::Button("Delete")) editor.DeleteSelected();
    ImGui::EndDisabled();
}
void Inspector(jEditor& editor)
{
    auto& document = editor.GetDocument(); const auto* selected = document.Find(editor.GetSelectedEntityId());
    if (!selected) { ImGui::TextWrapped("Select an object in Hierarchy or the scene."); return; }
    auto ids = editor.GetSelectedEntities();
    std::erase_if(ids, [&](EntityId id) { return !document.Find(id); });
    if (ids.size() > 1) { MultiInspector(editor, ids); return; }
    Entity e = *selected; ImGui::PushID(static_cast<int>(e.Id));
    char name[256]; std::snprintf(name, sizeof(name), "%s", e.Name.c_str());
    const bool renamed = ImGui::InputText("Name", name, sizeof(name)); if (renamed) e.Name = name;
    FinishProperty(document, e, renamed, "Rename object");
    ImGui::TextDisabled("%s | ID %llu", KindName(e.Kind), static_cast<unsigned long long>(e.Id));
    bool visible = e.Visible; if (ImGui::Checkbox("Visible", &visible)) { e.Visible = visible; document.Update(e, "Set visibility"); }
    ImGui::SameLine(); bool locked = e.Locked; if (ImGui::Checkbox("Locked", &locked)) { e.Locked = locked; document.Update(e, "Set lock"); }
    ModelAssetActions(editor, std::span(&e.Id, 1));
    ImGui::BeginDisabled(document.IsEffectivelyLocked(e.Id));
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Vec3Field(document, e, "Position", e.Local.Position);
        Vec3Field(document, e, "Rotation (deg)", e.Local.RotationDegrees);
        Vec3Field(document, e, "Scale", e.Local.Scale, .01f);
        if (ImGui::Button("Reset transform")) { e.Local = {}; document.Update(e, "Reset transform"); }
    }
    if (HasMaterial(e.Kind))
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) MaterialInspector(editor, e);
    if (e.Kind == EntityKind::Cube || e.Kind == EntityKind::Sphere || e.Kind == EntityKind::Model
        || e.Kind == EntityKind::ParticleEmitter || e.Kind == EntityKind::Terrain || e.Kind == EntityKind::Water)
        if (ImGui::CollapsingHeader("Shadow casting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool overrideCasting = e.ShadowCaster.has_value();
            if (ImGui::Checkbox("Override shadow casting", &overrideCasting))
            {
                if (overrideCasting)
                {
                    const auto* object = editor.ObjectForEntity(e.Id);
                    e.ShadowCaster = object ? !object->SkipShadowMapGen : true;
                }
                else e.ShadowCaster.reset();
                document.Update(e, "Override shadow casting");
            }
            if (e.ShadowCaster)
            {
                bool casts = *e.ShadowCaster;
                const bool changed = ImGui::Checkbox("Cast shadows", &casts);
                if (changed) e.ShadowCaster = casts;
                FinishProperty(document, e, changed, "Object shadow casting");
            }
            else ImGui::TextDisabled("Inherits source / prototype shadow casting.");
            ImGui::TextWrapped("Disabling casting keeps this object visible as a shadow receiver.");
        }
    if (e.Kind == EntityKind::Water)
        jEditorWater::RenderInspector(editor, e.Id,
            [&](Entity& candidate, const char* label, bool changed) { FinishProperty(document, candidate, changed, label); });
    if (IsLight(e.Kind))
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            FinishProperty(document, e, ImGui::ColorEdit3("Color", &e.Light.Color.X), "Light color");
            FinishProperty(document, e, ImGui::DragFloat("Intensity", &e.Light.Intensity, .1f, 0, 100000), "Light intensity");
            FinishProperty(document, e, ImGui::DragFloat("Range", &e.Light.Range, 1, .001f, 100000), "Light range");
            if (e.Kind == EntityKind::SpotLight)
            {
                FinishProperty(document, e, ImGui::SliderFloat("Inner cone", &e.Light.InnerConeDegrees, 0, 89), "Inner cone");
                FinishProperty(document, e, ImGui::SliderFloat("Outer cone", &e.Light.OuterConeDegrees, .01f, 89), "Outer cone");
            }
            FinishProperty(document, e, ImGui::Checkbox("Cast shadows", &e.Light.CastShadows), "Shadow setting");
            bool overrideShadows = e.Light.ShadowOverride.has_value();
            if (ImGui::Checkbox("Override shadow settings", &overrideShadows))
            {
                if (overrideShadows)
                {
                    const auto kind = e.Kind == EntityKind::DirectionalLight ? jAdvancedShadows::LightKind::Directional
                        : e.Kind == EntityKind::PointLight ? jAdvancedShadows::LightKind::Point : jAdvancedShadows::LightKind::Spot;
                    std::string reason;
                    auto settings = gOptions.AdvancedShadows;
                    if (!jAdvancedShadows::ValidateForLight(settings, kind, reason))
                    {
                        settings = {};
                        ImGui::TextWrapped("Inherited shadow settings are incompatible with this light; the override starts from engine defaults.");
                    }
                    e.Light.ShadowOverride = settings;
                }
                else e.Light.ShadowOverride.reset();
                document.Update(e, "Override shadow settings");
            }
            if (e.Light.ShadowOverride)
            {
                const auto kind = e.Kind == EntityKind::DirectionalLight ? jAdvancedShadows::LightKind::Directional
                    : e.Kind == EntityKind::PointLight ? jAdvancedShadows::LightKind::Point : jAdvancedShadows::LightKind::Spot;
                std::string error;
                auto settings = *e.Light.ShadowOverride;
                ImGui::BeginGroup();
                const bool changed = jEditorProjectSettings::RenderAdvancedShadowControls(settings, error, kind);
                ImGui::EndGroup();
                if (changed) e.Light.ShadowOverride = settings;
                FinishProperty(document, e, changed, "Light shadow settings");
            }
            else ImGui::TextDisabled("Inherits project / global shadow settings.");
            ImGui::TextWrapped("Raster shadows only; Path Tracing and HWRT ray shadows use their own visibility.");
        }
    if (e.Kind == EntityKind::Camera && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        FinishProperty(document, e, ImGui::SliderFloat("Vertical FOV", &e.Camera.VerticalFovDegrees, 5, 170), "Camera FOV");
        FinishProperty(document, e, ImGui::DragFloat("Near", &e.Camera.NearPlane, .01f, .001f, 10000), "Camera near");
        FinishProperty(document, e, ImGui::DragFloat("Far", &e.Camera.FarPlane, 1, .01f, 1000000), "Camera far");
        FinishProperty(document, e, ImGui::Checkbox("Primary", &e.Camera.Primary), "Primary camera");
    }
    ImGui::Separator();
    if (ImGui::Button("Duplicate")) editor.DuplicateSelected(); ImGui::SameLine();
    if (ImGui::Button("Delete")) editor.DeleteSelected();
    ImGui::EndDisabled(); ImGui::PopID();
}
void ActivateAsset(jEditor& editor, const jEditorAssetRecord& asset)
{
    if (editor.GetDocument().IsPlaying()) return;
    if (asset.Kind == jEditorAssetKind::Model)
    {
        FinishWorkspaceEdit(editor);
        if (!editor.AddModel(asset.SourcePath, asset.Id, asset.Revision)) Message(editor.LastError(), true);
    }
    else if (asset.Kind == jEditorAssetKind::Prefab || asset.Kind == jEditorAssetKind::Scene)
    {
        std::string error; const auto file = State().Registry->Resolve(asset.Id, error);
        if (!file) { Message(error, true); return; }
        if (asset.Kind == jEditorAssetKind::Scene)
            GuardUnsaved(editor, [&editor, path = *file] { std::string error; if (!LoadAuthoringScene(editor, path, error)) Message(error, true); });
        else
        {
            FinishWorkspaceEdit(editor);
            const auto id = editor.GetDocument().InstantiatePrefab(*file);
            if (id) editor.SelectEntity(id);
            else Message(editor.GetDocument().LastError(), true);
        }
    }
}
void Assets(jEditor& editor)
{
    auto& s = State();
    if (!s.Project || !s.Registry) { ImGui::TextWrapped("Create or open a project from File to browse assets."); return; }
    const auto canReplace = !s.Placement && !s.Replacement && CanReplaceModels(editor.GetDocument(), editor.GetSelectedEntities());
    jEditorAssetPanel::Render(editor, *s.Project, *s.Registry, s.SelectedAsset, canReplace,
        [&editor](jEditorAssetPanel::Action action, const jEditorAssetRecord& asset) {
            auto& state = State(); auto& document = editor.GetDocument();
            if (document.IsPlaying()) return;
            switch (action)
            {
            case jEditorAssetPanel::Action::Activate: ActivateAsset(editor, asset); break;
            case jEditorAssetPanel::Action::SelectInstances:
            {
                std::vector<EntityId> ids;
                for (const auto& [id, entity] : document.Entities())
                    if (entity.Kind == EntityKind::Model && (entity.AssetId == asset.Id ||
                        (entity.AssetId.empty() && entity.SourceAsset == asset.SourcePath))) ids.push_back(id);
                if (!ids.empty())
                {
                    FinishInspectorEdit(editor);
                    editor.SetSelectedEntities(ids);
                    state.RevealHierarchy = document.SelectedId();
                }
                Message(std::to_string(ids.size()) + " instances of " + asset.SourcePath);
                break;
            }
            case jEditorAssetPanel::Action::ReplaceSelection: RequestModelReplacement(editor, asset, editor.GetSelectedEntities()); break;
            case jEditorAssetPanel::Action::SetStartupScene:
            {
                std::string error;
                if (const auto path = state.Registry->Resolve(asset.Id, error)) SetStartupScene(editor, *path);
                else Message(error, true);
                break;
            }
            }
        });
}

Vector AsVector(const Vec3& value) { return Vector(value.X, value.Y, value.Z); }
Vec3 AsPosition(const Vector& value) { return {value.x, value.y, value.z}; }
Vector PlacementPosition(const jEditor::PlacementHit& hit, const jEditor::PlacementBounds& bounds)
{
    const Vector minimum = AsVector(bounds.Min), maximum = AsVector(bounds.Max);
    const Vector normal = AsVector(hit.Normal);
    const Vector extent = (maximum - minimum) * .5f;
    const Vector absoluteNormal(std::abs(normal.x), std::abs(normal.y), std::abs(normal.z));
    return AsVector(hit.Point) + normal * extent.DotProduct(absoluteNormal) - (minimum + maximum) * .5f;
}
std::optional<jEditor::PlacementHit> CursorPlacement(jEditor& editor, const jCamera& camera)
{
    const auto& s = State();
    const auto mouse = ImGui::GetMousePos();
    const float x = 2 * (mouse.x - s.ViewPosition.x) / s.ViewSize.x - 1;
    const float y = 1 - 2 * (mouse.y - s.ViewPosition.y) / s.ViewSize.y;
    const Matrix clipToWorld = camera.GetInverseViewProjectionMatrix();
    const Vector nearPoint = clipToWorld.TransformPoint(Vector(x, y, 0));
    const Vector direction = clipToWorld.TransformPoint(Vector(x, y, .9f)) - nearPoint;
    const float range = camera.IsInfinityFar ? FLT_MAX : (clipToWorld.TransformPoint(Vector(x, y, 1)) - nearPoint).Length();
    return editor.FindPlacementHit(AsPosition(nearPoint), AsPosition(direction), range, s.SurfaceSnap);
}
void DrawPlacement(const jCamera& camera, const std::optional<jEditor::PlacementHit>& hit,
    const AssetPlacement& placement)
{
    const auto& s = State();
    auto& draw = *ImGui::GetWindowDrawList();
    const ImVec2 min = s.ViewPosition, max(min.x + s.ViewSize.x, min.y + s.ViewSize.y);
    draw.PushClipRect(min, max, true);
    const Matrix vp = camera.GetViewProjectionMatrix();
    const auto project = [&](const Vector& point) -> std::optional<ImVec2> {
        const auto clip = vp.Transform(Vector4(point.x, point.y, point.z, 1));
        if (clip.w <= .0001f || clip.z < 0 || clip.z > clip.w) return {};
        const ImVec2 projected(min.x + (clip.x / clip.w + 1) * .5f * s.ViewSize.x,
            min.y + (1 - clip.y / clip.w) * .5f * s.ViewSize.y);
        return std::isfinite(projected.x) && std::isfinite(projected.y) ? std::optional(projected) : std::nullopt;
    };
    const ImU32 color = !placement.Error.empty() || !hit ? IM_COL32(255, 114, 99, 255)
        : placement.Bounds ? IM_COL32(92, 228, 190, 255) : IM_COL32(255, 210, 104, 255);
    ImVec2 label(min.x + 8, min.y + 8);
    if (hit)
    {
        if (const auto point = project(AsVector(hit->Point)))
        {
            draw.AddCircle(*point, 6, color, 12, 2);
            draw.AddLine(ImVec2(point->x - 10, point->y), ImVec2(point->x + 10, point->y), color);
            draw.AddLine(ImVec2(point->x, point->y - 10), ImVec2(point->x, point->y + 10), color);
        }
        if (placement.Bounds)
        {
            const auto origin = PlacementPosition(*hit, *placement.Bounds);
            const auto low = AsVector(placement.Bounds->Min) + origin, high = AsVector(placement.Bounds->Max) + origin;
            std::array<std::optional<ImVec2>, 8> corners;
            for (int i = 0; i < 8; ++i)
                corners[i] = project(Vector(i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z));
            for (int i = 0; i < 8; ++i) for (const int edge : {1, 2, 4})
                if (!(i & edge) && corners[i] && corners[i | edge]) draw.AddLine(*corners[i], *corners[i | edge], color, 2);
        }
    }
    const char* text = !placement.Error.empty() ? placement.Error.c_str()
        : !hit ? "Aim at a surface or the Y=0 work plane"
        : placement.Drop ? "Preparing placement  |  Esc to cancel"
        : !placement.Bounds ? "Model bounds load on drop  |  Esc to cancel"
        : hit->Surface ? "Surface  |  Drop to place  |  Esc to cancel"
        : "Work plane Y=0  |  Drop to place  |  Esc to cancel";
    draw.AddRectFilled(ImVec2(label.x - 4, label.y - 2),
        ImVec2(label.x + ImGui::CalcTextSize(text).x + 4, label.y + ImGui::GetTextLineHeight() + 2), IM_COL32(16, 16, 16, 225));
    draw.AddText(label, color, text);
    draw.PopClipRect();
}
jEditor::PlacementReadiness UpdatePlacementBounds(jEditor& editor, AssetPlacement& placement, bool prepare)
{
    using Readiness = jEditor::PlacementReadiness;
    if (!placement.Error.empty()) return Readiness::Failed;
    if (placement.Bounds) return Readiness::Ready;
    jEditor::PlacementBounds bounds;
    const auto state = placement.Prefab ? editor.PrefabPlacementBounds(*placement.Prefab, bounds, prepare)
        : editor.ModelPlacementBounds(placement.Source, placement.Id, placement.Revision, bounds, prepare);
    if (state == Readiness::Ready) placement.Bounds = bounds;
    if (state == Readiness::Failed) placement.Error = editor.LastError();
    return state;
}
void AdvancePlacement(jEditor& editor)
{
    auto& s = State();
    if (!s.Placement || !s.Placement->Drop) return;
    auto& placement = *s.Placement;
    const auto* asset = s.Registry ? s.Registry->FindById(placement.Id) : nullptr;
    if (!asset || asset->Missing || asset->Revision != placement.Revision || asset->SourcePath != placement.Source)
    {
        Message("Asset changed during placement. Drag the current asset again.", true);
        ClearAssetPlacement(editor);
        return;
    }
    const auto ready = UpdatePlacementBounds(editor, placement, true);
    if (ready == jEditor::PlacementReadiness::Pending || editor.GetDocument().HasTransaction()) return;
    if (ready == jEditor::PlacementReadiness::Failed) Message(placement.Error, true);
    else
    {
        const auto position = AsPosition(PlacementPosition(*placement.Drop, *placement.Bounds));
        if (placement.Prefab)
        {
            auto& doc = editor.GetDocument();
            const auto id = doc.InstantiatePrefabJson(placement.Prefab->PrefabJson(placement.Prefab->SelectedId()), 0, position);
            if (id) editor.SelectEntity(id);
            else Message(doc.LastError(), true);
        }
        else if (!editor.AddModel(placement.Source, placement.Id, placement.Revision, position)) Message(editor.LastError(), true);
    }
    ClearAssetPlacement(editor);
}
void RenderAssetPlacement(jEditor& editor, const jCamera& camera)
{
    auto& s = State();
    if (s.Replacement) return;
    if (s.Placement && s.Placement->Drop)
    {
        DrawPlacement(camera, s.Placement->Drop, *s.Placement);
        return;
    }
    const auto* payload = ImGui::GetDragDropPayload();
    if (!s.Registry || !payload || !payload->IsDataType("JENGINE_ASSET") || payload->DataSize < 2) return;
    const std::string id(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize) - 1);
    const auto* asset = s.Registry->FindById(id);
    if (!asset || asset->Missing || (asset->Kind != jEditorAssetKind::Model && asset->Kind != jEditorAssetKind::Prefab)) return;
    if (!ImGui::BeginDragDropTarget()) return;
    const auto* accepted = ImGui::AcceptDragDropPayload("JENGINE_ASSET", ImGuiDragDropFlags_AcceptPeekOnly);
    const bool delivery = accepted && accepted->IsDelivery();
    ImGui::EndDragDropTarget();
    if (!accepted) return;
    if (!s.Placement || s.Placement->Id != asset->Id || s.Placement->Revision != asset->Revision || s.Placement->Source != asset->SourcePath)
    {
        auto& placement = s.Placement.emplace();
        placement.Id = asset->Id;
        placement.Source = asset->SourcePath;
        placement.Revision = asset->Revision;
        if (asset->Kind == jEditorAssetKind::Prefab)
        {
            const auto path = s.Registry->Resolve(asset->Id, placement.Error);
            if (path)
            {
                placement.Prefab = std::make_unique<Document>();
                if (!placement.Prefab->InstantiatePrefab(*path)) placement.Error = placement.Prefab->LastError();
            }
        }
    }
    auto& placement = *s.Placement;
    const auto hit = CursorPlacement(editor, camera);
    UpdatePlacementBounds(editor, placement, false);
    if (!delivery) { DrawPlacement(camera, hit, placement); return; }
    if (hit && placement.Error.empty())
    {
        placement.Drop = hit;
        AdvancePlacement(editor);
        if (s.Placement) DrawPlacement(camera, hit, *s.Placement);
    }
    else
    {
        Message(placement.Error.empty() ? "No placement surface in range." : placement.Error, true);
        ClearAssetPlacement(editor);
    }
}
void SceneToolbar(jEditor& editor)
{
    auto& s = State();
    auto& placement = editor.Placement;
    const auto& style = ImGui::GetStyle();
    const float comboWidth = std::max(80.0f, ImGui::CalcTextSize("Rotate").x + ImGui::GetFrameHeight() + 2 * style.FramePadding.x);
    const float buttonWidths = ImGui::CalcTextSize("World").x + ImGui::CalcTextSize("Steps").x +
        ImGui::CalcTextSize("Ground").x + ImGui::CalcTextSize("Arrange").x + 8 * style.FramePadding.x;
    const float checkboxWidths = ImGui::CalcTextSize("Grid").x + ImGui::CalcTextSize("Surface").x +
        2 * (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x);
    const bool compact = ImGui::GetContentRegionAvail().x < comboWidth + buttonWidths + checkboxWidths + 6 * style.ItemSpacing.x;
    if (compact)
    {
        if (ImGui::Button("Scene tools")) ImGui::OpenPopup("Scene tool controls");
        if (!ImGui::BeginPopup("Scene tool controls")) return;
    }
    const auto sameLine = [&] { if (!compact) ImGui::SameLine(); };
    const auto edit = [&](auto command) {
        FinishWorkspaceEdit(editor);
        if (editor.GetDocument().HasTransaction()) Message("Finish the active transform first.", true);
        else if (!command()) Message(editor.LastError(), true);
    };
    int tool = placement.GizmoOperation == ImGuizmo::ROTATE ? 1 : placement.GizmoOperation == ImGuizmo::SCALE ? 2 : 0;
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo("##Scene transform tool", &tool, "Move\0Rotate\0Scale\0"))
        placement.GizmoOperation = tool == 1 ? ImGuizmo::ROTATE : tool == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move / Rotate / Scale (Shift+W / E / R)\nAlt-drag the move gizmo to duplicate");
    sameLine();
    const bool local = placement.GizmoMode == ImGuizmo::LOCAL;
    if (ImGui::Button(local ? "Local" : "World")) placement.GizmoMode = local ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Transform axes (Q)");
    sameLine();
    ImGui::Checkbox("Grid", &placement.UseSnap);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grid snap for transforms and asset placement (Ctrl+G)");
    sameLine();
    if (ImGui::Button("Steps")) ImGui::OpenPopup("Grid steps");
    if (ImGui::BeginPopup("Grid steps"))
    {
        ImGui::DragFloat3("Move", placement.TranslationSnap, .1f, .001f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Rotate (deg)", &placement.RotationSnap, 1.0f, .1f, 180.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat3("Scale", placement.ScaleSnap, .01f, .001f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndPopup();
    }
    sameLine();
    ImGui::Checkbox("Surface", &s.SurfaceSnap);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Place on visible cubes, spheres and terrain. The Y=0 work plane is the fallback.");
    sameLine();
    ImGui::BeginDisabled(!editor.GetSelectedEntityId());
    if (ImGui::Button("Ground")) edit([&] { return editor.PlaceSelectionOnGround(); });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Place selection on the ground below");
    sameLine();
    if (ImGui::Button("Arrange")) ImGui::OpenPopup("Arrange selection");
    if (ImGui::BeginPopup("Arrange selection"))
    {
        ImGui::SetNextItemWidth(160);
        ImGui::InputFloat3("World offset", &s.DuplicateOffset.X);
        if (ImGui::Button("Duplicate & Move")) edit([&] { return editor.DuplicateAndMove(s.DuplicateOffset); });
        if (ImGui::MenuItem("Repeat Last Offset", nullptr, false, editor.CanRepeatDuplicateOffset()))
            edit([&] { return editor.RepeatDuplicateOffset(); });
        const auto axes = [&](const char* label, size_t minimum, auto command) {
            if (!ImGui::BeginMenu(label, editor.GetSelectedEntities().size() >= minimum)) return;
            const char* names[] = {"World X", "World Y", "World Z"};
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(names[i])) edit([&] { return command(static_cast<jEditor::WorldAxis>(i)); });
            ImGui::EndMenu();
        };
        axes("Align to last selected", 2, [&](auto axis) { return editor.AlignSelection(axis); });
        axes("Distribute evenly", 3, [&](auto axis) { return editor.DistributeSelection(axis); });
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if (compact) ImGui::EndPopup();
}

fs::path CaptureDestination(jEditor& editor, const CaptureDialog& dialog)
{
    auto path = jEditorAssetPath::FromUtf8(dialog.File);
    if (path.empty() || path.filename().empty() || path.filename() == "." || path.filename() == "..") return {};
    if (path.is_relative()) path = fs::absolute(editor.GetProjectRoot() / path);
    return path.replace_extension(jEditorCapture::Extension(dialog.Video ? jEditorCapture::Format::Mp4 : dialog.StillFormat));
}
void OpenCapture(jEditor& editor, bool video)
{
    auto& dialog = State().Capture;
    dialog.Video = video;
    dialog.Open = true;
    dialog.Error.clear();
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char date[32]{};
    std::strftime(date, sizeof(date), "%Y%m%d-%H%M%S", &local);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const auto format = video ? jEditorCapture::Format::Mp4 : dialog.StillFormat;
    const std::string name = std::string(video ? "Recording-" : "Screenshot-") + date + "-" + std::to_string(milliseconds)
        + jEditorCapture::Extension(format);
    const auto path = jEditorAssetPath::ToUtf8(editor.GetProjectRoot() / "Captures" / name);
    std::snprintf(dialog.File, sizeof(dialog.File), "%s", path.c_str());
}
void CaptureMenu(jEditor& editor)
{
    using jEditorCapture::Format;
    using jEditorCapture::Phase;
    const auto& status = jEditorCapture::GetStatus();
    const bool idle = status.State == Phase::Idle;
    auto& workspace = State();
    if (idle && workspace.Capture.WasBusy)
        workspace.Status = status.Error.empty() ? "Saved " + jEditorAssetPath::ToUtf8(status.File.filename()) : status.Error;
    workspace.Capture.WasBusy = !idle;
    if (ImGui::BeginMenu("Capture"))
    {
        if (ImGui::MenuItem("Screenshot...", nullptr, false, idle && g_rhi)) OpenCapture(editor, false);
        if (ImGui::MenuItem("Record video...", nullptr, false, idle && g_rhi && jEditorCapture::Supports(Format::Mp4)))
            OpenCapture(editor, true);
        if (ImGui::MenuItem("Stop recording", nullptr, false, status.State == Phase::Recording)) jEditorCapture::Stop();
        if (!jEditorCapture::Supports(Format::Mp4)) ImGui::TextDisabled("MP4 recording is available on Windows.");
        if (idle && !status.File.empty())
        {
            ImGui::Separator();
            if (!status.Error.empty()) ImGui::TextWrapped("%s", status.Error.c_str());
            else
            {
                const auto path = jEditorAssetPath::ToUtf8(status.File);
                ImGui::TextDisabled("Last capture");
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420);
                ImGui::TextUnformatted(path.c_str());
                ImGui::PopTextWrapPos();
                if (ImGui::MenuItem("Copy output path")) ImGui::SetClipboardText(path.c_str());
            }
        }
        ImGui::EndMenu();
    }
    if (status.State == Phase::Recording)
    {
        const int seconds = int(status.Seconds);
        ImGui::TextColored(ImVec4(1.f, .32f, .27f, 1.f), "REC %02d:%02d", seconds / 60, seconds % 60);
        if (ImGui::SmallButton("Stop REC")) jEditorCapture::Stop();
    }
    else if (!idle) ImGui::TextDisabled("%s", status.State == Phase::Saving ? "Saving capture..." : "Capturing...");
}
void CaptureOptions(jEditor& editor)
{
    using jEditorCapture::Format;
    auto& dialog = State().Capture;
    if (dialog.Open) { ImGui::OpenPopup("Capture output"); dialog.Open = false; }
    if (!ImGui::BeginPopupModal("Capture output", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(editor.GetDocument().IsPlaying() ? "Capture Game" : "Capture Scene");
    ImGui::TextDisabled("Fits the rendered view to the output size. UI and selection guides are excluded.");
    constexpr const char* sizes[] = {"Current render", "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Custom"};
    constexpr int dimensions[][2] = {{0, 0}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("Resolution", &dialog.Resolution, sizes, IM_ARRAYSIZE(sizes));
    int width = SCR_WIDTH, height = SCR_HEIGHT;
    if (dialog.Resolution == IM_ARRAYSIZE(sizes) - 1)
    {
        ImGui::SetNextItemWidth(100); ImGui::InputInt("Width", &dialog.Width, 0);
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputInt("Height", &dialog.Height, 0);
        width = dialog.Width; height = dialog.Height;
    }
    else if (dialog.Resolution)
    {
        width = dimensions[dialog.Resolution][0]; height = dimensions[dialog.Resolution][1];
    }
    else
    {
        if (dialog.Video) { width &= ~1; height &= ~1; }
        ImGui::TextDisabled("%d x %d", width, height);
    }
    if (dialog.Video)
    {
        ImGui::TextUnformatted("MP4 (H.264)");
        ImGui::SetNextItemWidth(110); ImGui::InputInt("FPS", &dialog.Fps, 1, 10);
    }
    else
    {
        struct StillFormat { Format Id; const char* Label; };
        constexpr StillFormat formats[] = {{Format::Png, "PNG"}, {Format::Jpeg, "JPEG"}, {Format::Bmp, "BMP"}};
        const auto shown = std::find_if(std::begin(formats), std::end(formats), [&](auto value) { return value.Id == dialog.StillFormat; });
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("Image format", shown->Label))
        {
            for (const auto& format : formats)
            {
                if (!jEditorCapture::Supports(format.Id) || !ImGui::Selectable(format.Label, format.Id == dialog.StillFormat)) continue;
                dialog.StillFormat = format.Id;
                try
                {
                    const auto path = jEditorAssetPath::ToUtf8(CaptureDestination(editor, dialog));
                    std::snprintf(dialog.File, sizeof(dialog.File), "%s", path.c_str());
                }
                catch (const std::exception& error) { dialog.Error = error.what(); }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SetNextItemWidth(440);
    ImGui::InputText("Output file", dialog.File, sizeof(dialog.File));
    if (!dialog.Error.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440);
        ImGui::TextColored(ImVec4(1.f, .35f, .3f, 1.f), "%s", dialog.Error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::Button(dialog.Video ? "Start recording" : "Save screenshot"))
    {
        try
        {
            jEditorCapture::Options options{CaptureDestination(editor, dialog), width, height, dialog.Fps,
                dialog.Video ? Format::Mp4 : dialog.StillFormat};
            if (jEditorCapture::Start(std::move(options), dialog.Error))
            {
                dialog.WasBusy = true;
                ImGui::CloseCurrentPopup();
            }
        }
        catch (const std::exception& error) { dialog.Error = error.what(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void KeyboardShortcuts()
{
    auto& s = State();
    if (!s.ShowKeyboardShortcuts) return;
    ImGui::SetNextWindowSize(ImVec2(680, 465), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard Shortcuts", &s.ShowKeyboardShortcuts)) { ImGui::End(); return; }
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##Shortcut filter", "Filter by action, key, or area",
        s.ShortcutFilter.InputBuf, sizeof(s.ShortcutFilter.InputBuf))) s.ShortcutFilter.Build();
    ImGui::TextDisabled("Camera keys follow the active project settings.");

    struct Shortcut { const char* Area; const char* Action; const char* Keys; };
#ifdef __APPLE__
#define JEDITOR_SHORTCUT_MOD "Cmd"
#else
#define JEDITOR_SHORTCUT_MOD "Ctrl"
#endif
    constexpr Shortcut shortcuts[] = {
        {"View", "Toggle viewport only", "F11"},
        {"View", "Toggle rendering panels", "F10"},
        {"View", "Cycle console visibility", "Backtick"},
        {"View", "Close console", "Esc"},
        {"Scene", "New scene", JEDITOR_SHORTCUT_MOD "+N"},
        {"Scene", "Open scene", JEDITOR_SHORTCUT_MOD "+O"},
        {"Scene", "Save scene", JEDITOR_SHORTCUT_MOD "+S"},
        {"Scene", "Save scene as", JEDITOR_SHORTCUT_MOD "+Shift+S"},
        {"Selection", "Focus selection", "F"},
        {"Selection", "Clear selection or cancel placement", "Esc"},
        {"Selection", "Select previous or next object", "Up / Down"},
        {"Selection", "Toggle object selection", JEDITOR_SHORTCUT_MOD "+Click"},
        {"Selection", "Select a range in Hierarchy", "Shift+Click"},
        {"Edit", "Undo", JEDITOR_SHORTCUT_MOD "+Z"},
        {"Edit", "Redo", JEDITOR_SHORTCUT_MOD "+Y / " JEDITOR_SHORTCUT_MOD "+Shift+Z"},
        {"Edit", "Copy selection", JEDITOR_SHORTCUT_MOD "+C"},
        {"Edit", "Paste selection", JEDITOR_SHORTCUT_MOD "+V"},
        {"Edit", "Duplicate selection", JEDITOR_SHORTCUT_MOD "+D"},
        {"Edit", "Delete selection", "Delete"},
        {"Transform", "Move gizmo", "Shift+W"},
        {"Transform", "Rotate gizmo", "Shift+E"},
        {"Transform", "Scale gizmo", "Shift+R"},
        {"Transform", "Toggle world or local axes", "Q"},
        {"Transform", "Toggle grid snapping", JEDITOR_SHORTCUT_MOD "+G"},
        {"Transform", "Duplicate and move with the Move gizmo", "Alt+Drag"},
        {"Placement", "Choose point light", "L"},
        {"Placement", "Choose spot light", "K"},
        {"Placement", "Choose directional light", "J"},
        {"Placement", "Choose cube", "N"},
        {"Placement", "Choose sphere", "M"},
        {"Placement", "Place the chosen object type", "P"},
    };
#undef JEDITOR_SHORTCUT_MOD

    std::vector<Shortcut> matches;
    const auto add = [&](const Shortcut& shortcut) {
        if (s.ShortcutFilter.IsActive())
        {
            const std::string text = std::string(shortcut.Area) + " " + shortcut.Action + " " + shortcut.Keys;
            if (!s.ShortcutFilter.PassFilter(text.c_str())) return;
        }
        matches.push_back(shortcut);
    };
    if (IsUseDX12()) add({"View", "Toggle exclusive fullscreen", "Alt+Enter"});
    for (const auto& shortcut : shortcuts) add(shortcut);
    using namespace jEditorProjectSettings;
    for (const auto& action : CameraActions())
        if (const char* key = KeyName(Binding(action.Name, action.Default))) add({"Camera", action.Label, key});
    add({"Camera", "Look around", "Right mouse drag"});
    add({"Camera", "Orbit around focus", "Alt+Left mouse drag"});
    add({"Camera", "Increase camera speed", "= / Numpad +"});
    add({"Camera", "Decrease camera speed", "- / Numpad -"});

    if (matches.empty()) ImGui::TextDisabled("No shortcuts match.");
    else if (ImGui::BeginTable("Shortcut list", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthFixed, 95.f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 180.f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& shortcut : matches)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", shortcut.Area);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(shortcut.Action);
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(shortcut.Keys);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void Logs()
{
    auto& s = State(); ImGui::Combo("Minimum level", &s.MinimumLevel, "Trace\0Debug\0Info\0Warning\0Error\0Fatal\0");
    s.LogFilter.Draw("Search##logs", -1);
    auto recent = jLog::Get().SnapshotRecent(); std::vector<size_t> rows; rows.reserve(recent.size());
    for (size_t i = 0; i < recent.size(); ++i) if (static_cast<int>(recent[i]->Level) >= s.MinimumLevel && (s.LogFilter.PassFilter(recent[i]->Message.c_str()) || s.LogFilter.PassFilter(recent[i]->Category.c_str()))) rows.push_back(i);
    ImGui::BeginChild("LogRows", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip; clip.Begin(static_cast<int>(rows.size()));
    while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
    {
        const auto& record = recent[rows[i]];
        if (record->Level >= EjLogLevel::Error) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .35f, .3f, 1));
        else if (record->Level == EjLogLevel::Warning) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .8f, .25f, 1));
        ImGui::Text("[%s] [%s] %s", jLogLevelName(record->Level), record->Category.c_str(), record->Message.c_str());
        if (record->Level >= EjLogLevel::Warning) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("UTC %lld ms | thread %llu | sequence %llu", static_cast<long long>(record->TimestampMilliseconds), static_cast<unsigned long long>(record->ThreadID), static_cast<unsigned long long>(record->Sequence));
    }
    ImGui::EndChild();
    if (ImGui::InputText("Command", s.Command, sizeof(s.Command), ImGuiInputTextFlags_EnterReturnsTrue)) { jConsole::Get().ExecuteCommand(s.Command); s.Command[0] = 0; }
}
void Profiler(jEditor& editor)
{
    auto& s = State(); const auto stats = jLog::Get().GetStats(); const auto image = jImageFileLoader::GetInstance().GetStats();
    ImGui::PlotLines("Frame ms", s.FrameTimes.data(), static_cast<int>(s.FrameTimes.size()), s.FrameOffset, nullptr, 0, FLT_MAX, ImVec2(-1, 85));
    ImGui::Text("Scene %zu | Render objects %zu | Imports %zu", editor.GetDocument().Entities().size(), jObject::GetStaticRenderObject().size(), editor.PendingImportCount());
    ImGui::Text("Image cache %zu | In-flight %zu | Coalesced %zu", image.Cached, image.InFlight, image.CoalescedWaits);
    ImGui::Text("Log queue %zu | Drops %llu | Sink errors %llu", stats.Queued, static_cast<unsigned long long>(stats.Dropped), static_cast<unsigned long long>(stats.SinkErrors));
    ImGui::TextWrapped("%s", editor.ImportStatus().c_str());
}
void StartPackage(jEditor& editor, const fs::path& output)
{
    auto& s = State();
    if (!s.Project) { Message("Open a project before packaging.", true); return; }
    if (s.Packaging.valid()) { Message("A package build is already active.", true); return; }
    if (s.Project->GetConfig().StartupScene.empty()) { Message("Set a Startup Scene before building a package.", true); return; }
    const auto startup = s.Project->GetRoot() / jEditorAssetPath::FromUtf8(s.Project->GetConfig().StartupScene);
    std::error_code ec;
    if (editor.GetDocument().IsDirty() && fs::equivalent(editor.GetDocument().Path(), startup, ec) &&
        !SaveScene(editor, editor.GetDocument().Path())) return;
    const auto project = *s.Project;
    const auto binary = s.ExecutablePath;
    const auto resource = fs::current_path() / "Resource";
    s.Packaging = std::async(std::launch::async, [project, output, binary, resource]() mutable
    {
        jEditorPackageReport report; std::string error;
        jEditorAssetRegistry registry(project);
        if (!registry.Scan(error)) return std::make_pair(report, error);
        jEditorPackageRequest request; request.Project = &project; request.Registry = &registry;
        request.OutputDirectory = output; request.StartupScene = project.GetConfig().StartupScene;
        if (!jEditorPackaging::PrepareRuntime(binary, resource, request.RuntimeFiles, request.RuntimeExecutable, request.FinalizeStaged, error)) return std::make_pair(report, error);
        if (!jEditorPackager::Build(request, report, error)) return std::make_pair(report, error);
        return std::make_pair(report, std::string());
    }); Message("Building package on a CPU worker...");
}
void Dialogs(jEditor& editor)
{
    auto& s = State();
    if (s.UnsavedPrompt) { ImGui::OpenPopup("Unsaved scene"); s.UnsavedPrompt = false; }
    if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save the current scene before continuing?");
        if (ImGui::Button("Save"))
        { if (SaveScene(editor, SceneDefault(editor))) { auto action = std::move(s.Pending); if (action) action(); ImGui::CloseCurrentPopup(); } }
        ImGui::SameLine(); if (ImGui::Button("Discard")) { auto action = std::move(s.Pending); if (action) action(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { s.Pending = {}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (s.OpenModal) { ImGui::OpenPopup("Editor file operation"); s.OpenModal = false; }
    if (ImGui::BeginPopupModal("Editor file operation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static const char* titles[] = {"", "Save scene (.jscene)", "Open scene (.jscene)", "Create project folder", "Open project.jproject", "Import source file", "Save prefab (.jprefab)", "Instantiate prefab", "Build package into new folder"};
        ImGui::TextUnformatted(titles[static_cast<int>(s.Modal)]);
        ImGui::SetNextItemWidth(520); ImGui::InputText("Path", s.Path, sizeof(s.Path));
        if (s.Modal == Dialog::NewProject) ImGui::InputText("Project name", s.Name, sizeof(s.Name));
        if (s.Modal == Dialog::Import) ImGui::InputText("Destination folder", s.AssetDestination, sizeof(s.AssetDestination));
        if (ImGui::Button("Apply") && s.Path[0])
        {
            bool ok = true; std::string error; const fs::path path = jEditorAssetPath::FromUtf8(s.Path);
            switch (s.Modal)
            {
            case Dialog::Save: ok = SaveScene(editor, path); break;
            case Dialog::Open: ok = LoadAuthoringScene(editor, path, error); break;
            case Dialog::NewProject:
            {
                jEditorProject project; ok = project.Create(path, s.Name, error);
                if (ok) ok = OpenAuthoringProject(editor, project.GetProjectFile(), error);
                break;
            }
            case Dialog::OpenProject: ok = OpenAuthoringProject(editor, path, error); break;
            case Dialog::Import: ok = s.Registry && s.Registry->ImportFile(path, s.AssetDestination, error); if (ok) s.Registry->ScanAsync(); break;
            case Dialog::SavePrefab:
                ok = editor.GetDocument().SavePrefab(editor.GetSelectedEntityId(), path);
                error = editor.GetDocument().LastError();
                if (ok && s.Registry) s.Registry->ScanAsync();
                break;
            case Dialog::OpenPrefab: ok = editor.GetDocument().InstantiatePrefab(path) != 0; error = editor.GetDocument().LastError(); break;
            case Dialog::Package: StartPackage(editor, path); break;
            default: break;
            }
            if (ok) { s.Modal = Dialog::None; ImGui::CloseCurrentPopup(); }
            else if (!error.empty()) Message(error, true);
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { s.Modal = Dialog::None; ImGui::CloseCurrentPopup(); }
        if (!s.Status.empty()) ImGui::TextWrapped("%s", s.Status.c_str());
        ImGui::EndPopup();
    }
}
}

bool IsAuthoringWorkspaceVisible() { return State().Visible && !State().Runtime; }
bool IsAuthoringViewportOnly() { return State().ViewportOnly; }
void RequestAuthoringViewportOnlyToggle()
{
    auto& s = State();
    if (s.Runtime || s.UnsavedPrompt || s.OpenModal || !ImGui::GetCurrentContext() || ImGui::GetTopMostPopupModal()) return;
    s.ToggleViewportOnly = !s.ToggleViewportOnly;
}
void ApplyAuthoringViewportOnlyToggle(jEditor& editor)
{
    auto& s = State();
    if (!s.ToggleViewportOnly || !ImGui::GetCurrentContext()) return;
    // Let a held gizmo or asset drag finish in the layout where it started.
    if (ImGuizmo::IsUsing() || ImGui::GetDragDropPayload()) return;
    s.ToggleViewportOnly = false;
    if (s.UnsavedPrompt || s.OpenModal || ImGui::GetTopMostPopupModal()) return;
    s.ViewportOnly = !s.ViewportOnly;
    s.ViewHovered = s.ViewFocused = false;
    editor.SetViewportInputState(false, false);
    editor.SetShortcutInputState(false);
    editor.Placement.bPickRequested = false;
    if (!s.ViewportOnly) return;

    FinishWorkspaceEdit(editor);
    ImGui::ClearActiveID();
    if (!ImGui::GetCurrentContext()->OpenPopupStack.empty()) ImGui::ClosePopupToLevel(0, true);

    // Entering from a detached panel moves keyboard input to the main window.
    auto& platform = ImGui::GetPlatformIO();
    auto* main = ImGui::GetMainViewport();
    if (main->PlatformHandle && platform.Platform_SetWindowFocus && platform.Platform_GetWindowFocus)
        for (auto* viewport : platform.Viewports)
            if (viewport != main && viewport->PlatformHandle && platform.Platform_GetWindowFocus(viewport))
            { platform.Platform_SetWindowFocus(main); break; }
}
void SetAuthoringWorkspaceVisible(bool visible)
{
    if (!visible && g_Editor)
    {
        jEditorCapture::Stop();
        FinishWorkspaceEdit(*g_Editor);
        ClearAssetPlacement(*g_Editor);
        g_Editor->Placement.bPickRequested = false;
    }
    auto& s = State();
    s.Visible = visible;
    s.ViewportOnly = s.ToggleViewportOnly = false;
}
void SetAuthoringRuntimeMode(bool runtime)
{
    auto& s = State();
    s.Runtime = runtime;
    s.Visible = !runtime;
    s.ViewportOnly = s.ToggleViewportOnly = false;
}
void SetTestPlatformWindows(bool enabled) { if (enabled) State().TestPlatformWindows = true; else State().TestPlatformWindows.reset(); }
void SetAuthoringIntegrationDrawHook(std::function<void()> draw) { State().IntegrationDraw = std::move(draw); }
void SetAuthoringTestStateDirectory(const fs::path& directory)
{
    auto& s = State();
    const auto layout = fs::absolute(directory) / "workspace.json";
    if (s.LayoutFile == layout) return;
    // The ImGui IO field borrows the string storage until backend shutdown.
    if (ImGui::GetCurrentContext()) ImGui::GetIO().IniFilename = nullptr;
    s.LayoutFile = layout; s.ImGuiFile = jEditorAssetPath::ToUtf8(layout.parent_path() / "imgui.ini");
    s.Layout = Json::object(); s.LoadLayout = true; s.ResetLayout = true;
}
bool RequestAuthoringWindowClose()
{
    auto& s = State();
    if (s.Runtime || s.CloseApproved || !g_Editor) return true;
    FinishWorkspaceEdit(*g_Editor);
    ClearAssetPlacement(*g_Editor);
    if (g_Editor->GetDocument().IsPlaying()) g_Editor->StopPlay();
    if (!g_Editor->GetDocument().IsDirty()) return true;
    s.Visible = true;
    s.ViewportOnly = s.ToggleViewportOnly = false;
    GuardUnsaved(*g_Editor, [] {
        State().CloseApproved = true;
        if (!g_rhi) return;
        if (IsUseVulkan()) glfwSetWindowShouldClose(static_cast<GLFWwindow*>(g_rhi->GetWindow()), GLFW_TRUE);
#ifdef _WIN32
        else PostMessage(static_cast<HWND>(g_rhi->GetWindow()), WM_CLOSE, 0, 0);
#endif
    });
    return false;
}
void RequestAuthoringSceneReplacement(std::function<void()> action)
{
    if (!g_Editor || State().Runtime) { action(); return; }
    auto& s = State();
    s.Visible = true;
    s.ViewportOnly = s.ToggleViewportOnly = false;
    GuardUnsaved(*g_Editor, std::move(action));
}
bool IsAuthoringRuntimeMode() { return State().Runtime; }
std::filesystem::path GetAuthoringProjectFile()
{
    const auto& project = State().Project;
    return project ? project->GetProjectFile() : std::filesystem::path{};
}
void SetAuthoringExecutablePath(const std::filesystem::path& path)
{
    std::error_code error;
    State().ExecutablePath = std::filesystem::weakly_canonical(path, error);
    if (error) State().ExecutablePath = std::filesystem::absolute(path);
}
bool IsAuthoringViewportHovered() { return State().ViewHovered; }
bool IsAuthoringViewportFocused() { return State().ViewFocused; }
float GetAuthoringSceneAspect()
{
    if (IsAuthoringViewportOnly() || !IsAuthoringWorkspaceVisible() || !g_Editor || g_Editor->GetDocument().IsPlaying()) return 0.0f;
    const auto size = State().ViewSize;
    return size.x > 0.0f && size.y > 0.0f ? size.x / size.y : 0.0f;
}
bool GetAuthoringViewportRect(float& x, float& y, float& width, float& height)
{
    const auto& s = State();
    if (s.ViewportOnly || !IsAuthoringWorkspaceVisible() || s.ViewSize.x <= 0 || s.ViewSize.y <= 0) return false;
    x = s.ViewPosition.x; y = s.ViewPosition.y; width = s.ViewSize.x; height = s.ViewSize.y;
    return true;
}
bool GetAuthoringRendererControlRect(float& x, float& y, float& width, float& height)
{
    const auto& s = State();
    if (s.ViewportOnly || !IsAuthoringWorkspaceVisible() || !s.RendererControlVisible) return false;
    x = s.RendererControlPosition.x; y = s.RendererControlPosition.y;
    width = s.RendererControlSize.x; height = s.RendererControlSize.y;
    return width > 0 && height > 0;
}
bool GetAuthoringRendererChoiceRect(int renderer, float& x, float& y, float& width, float& height)
{
    const auto& s = State();
    if (s.ViewportOnly || !IsAuthoringWorkspaceVisible() || renderer < 0 || static_cast<size_t>(renderer) >= s.RendererChoices.size()) return false;
    const auto& choice = s.RendererChoices[static_cast<size_t>(renderer)];
    if (!choice.Visible) return false;
    x = choice.Position.x; y = choice.Position.y; width = choice.Size.x; height = choice.Size.y;
    return width > 0 && height > 0;
}
bool MapAuthoringViewportPoint(double& x, double& y)
{
    auto& s = State();
    if (s.ViewportOnly) return false;
    if (!IsAuthoringWorkspaceVisible()) return true;
    if (!s.ViewHovered || s.ViewSize.x <= 0 || s.ViewSize.y <= 0) return false;
    x = (x - s.ViewPosition.x) * SCR_WIDTH / s.ViewSize.x;
    y = (y - s.ViewPosition.y) * SCR_HEIGHT / s.ViewSize.y;
    return x >= 0 && y >= 0 && x < SCR_WIDTH && y < SCR_HEIGHT;
}
void RevealAuthoringSelection(const jEditor& editor) { State().RevealHierarchy = editor.GetSelectedEntityId(); }
bool OpenAuthoringProject(jEditor& editor, const std::filesystem::path& file, std::string& error)
{
    auto project = std::make_unique<jEditorProject>(); if (!project->Open(file, error)) return false;
    if (!jEditorProjectSettings::Validate(*project, error)) return false;
    Document replacement;
    std::filesystem::path startup;
    std::string serialized;
    if (!project->GetConfig().StartupScene.empty())
    {
        const auto path = jEditorProject::ResolveSafe(project->GetRoot(), project->GetConfig().StartupScene, error, true);
        if (!path || !replacement.Load(*path)) { if (path) error = replacement.LastError(); return false; }
        startup = *path; serialized = replacement.ToJson();
    }
    if (editor.GetDocument().IsPlaying() || editor.GetDocument().HasTransaction()) { error = "Stop Play and finish the active edit before changing projects"; return false; }
    auto registry = std::make_unique<jEditorAssetRegistry>(*project);
    auto& s = State(); jEditorAssetPanel::Shutdown(); if (s.Registry) s.Registry->Shutdown();
    ClearAssetPlacement(editor);
    s.SelectedAsset.clear(); s.FocusAssets = false;
    s.HierarchyActive = s.RangeAnchor = s.RevealHierarchy = 0; s.Collapsed.clear(); s.RowRevision = ~uint64_t(0);
    jEditorExtensions::Shutdown(editor); jEditorTools::Shutdown();
    editor.ResetWorldBindings(true); editor.SetProjectRoot(project->GetRoot());
    if (g_Engine && g_Editor == &editor) g_Engine->Game.ClearImportedSceneForAuthoring();
    editor.SetAssetResolver([root = project->GetRoot()](const std::string&, const std::string& source)
    { std::string error; const auto path = jEditorProject::ResolveSafe(root, source, error, true); return path ? *path : std::filesystem::path(); });
    s.Registry = std::move(registry); s.Project = std::move(project);
    if (!jEditorProjectSettings::Apply(*s.Project, editor, error)) return false;
    if (!s.Runtime)
    {
        s.Registry->ScanAsync();
        if (!s.Project->RememberRecent(s.LayoutFile.parent_path() / "recent-projects.json", error)) Message(error, true);
    }
    s.AssetRevision = ~uint64_t(0);
    if (!serialized.empty() && !editor.GetDocument().LoadJson(serialized, startup)) { error = editor.GetDocument().LastError(); return false; }
    Message("Opened project " + s.Project->GetConfig().Name); return true;
}
bool LoadAuthoringScene(jEditor& editor, const std::filesystem::path& scene, std::string& error)
{
    Document candidate;
    if (!candidate.Load(scene)) { error = candidate.LastError(); return false; }
    if (editor.GetDocument().IsPlaying() || editor.GetDocument().HasTransaction()) { error = "Stop Play and finish the current edit first"; return false; }
    ClearAssetPlacement(editor);
    jEditorExtensions::Shutdown(editor); jEditorTools::Shutdown();
    if (!editor.GetDocument().LoadJson(candidate.ToJson(), scene)) { error = editor.GetDocument().LastError(); return false; }
    auto& s = State();
    s.HierarchyActive = s.RangeAnchor = s.RevealHierarchy = 0; s.Collapsed.clear(); s.RowRevision = ~uint64_t(0);
    editor.SetSelectedEntities({}); Message("Opened scene " + jEditorAssetPath::ToUtf8(scene)); return true;
}
void TickAuthoringWorkspace(jEditor& editor, float dt)
{
    jModelUploadSession::CollectRetired();
    auto& s = State(); if (s.LoadLayout) LoadLayout();
    if (s.ViewportOnly && !editor.GetDocument().IsPlaying())
    {
        AdvancePlacement(editor);
        AdvanceModelReplacement(editor);
    }
    jSceneFeatureTools::Tick(editor, s.Project.get());
    s.FrameTimes[s.FrameOffset++ % s.FrameTimes.size()] = dt * 1000; s.FrameOffset %= s.FrameTimes.size();
    if (s.Registry)
    {
        jEditorAssetPanel::Tick(editor, *s.Project, *s.Registry);
        if (auto result = s.Registry->PollScan())
        {
            if (result->Succeeded) Message("Asset catalog: " + std::to_string(result->AssetCount) + " files");
            else if (!result->Cancelled) Message(result->Error, true);
            for (const auto& diagnostic : result->Diagnostics) jLog::Get().Write(EjLogLevel::Warning, "Assets", diagnostic.Path + ": " + diagnostic.Message);
        }
        s.WatchSeconds += dt;
        if (s.WatchSeconds >= 5 && !s.Registry->GetWorkStats().Active && !s.Packaging.valid()) { s.WatchSeconds = 0; s.Registry->ScanAsync(); }
    }
    if (s.Packaging.valid() && s.Packaging.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try { auto result = s.Packaging.get(); Message(result.second.empty() ? "Package ready: " + jEditorAssetPath::ToUtf8(result.first.Manifest) : result.second, !result.second.empty()); }
        catch (const std::exception& e) { Message(e.what(), true); }
    }
    s.AutoSaveSeconds += dt;
    if (s.AutoSave && s.AutoSaveSeconds >= 60 && editor.GetDocument().IsDirty() && !editor.GetDocument().IsPlaying() && !editor.GetDocument().HasTransaction())
    {
        s.AutoSaveSeconds = 0;
        const auto recovery = editor.GetProjectRoot() / ".jeditor/recovery.jscene";
        try { fs::create_directories(recovery.parent_path()); auto temporary = recovery; temporary += ".tmp"; std::ofstream out(temporary); out << editor.GetDocument().ToJson(); out.close(); if (out) { std::error_code ec; fs::rename(temporary, recovery, ec); if (ec) fs::copy_file(temporary, recovery, fs::copy_options::overwrite_existing); } }
        catch (const std::exception& e) { Message(std::string("Autosave: ") + e.what(), true); }
    }
    jEditorTools::Tick(editor, dt);
    jEditorExtensions::Tick(editor, dt);
}
void UpdateAuthoringViewportOnlyInput(jEditor& editor)
{
    auto& s = State();
    const auto& io = ImGui::GetIO();
#ifdef IMGUI_HAS_DOCK
    if (s.DockSpaceId && (io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
        ImGui::DockSpace(s.DockSpaceId, ImVec2(), ImGuiDockNodeFlags_KeepAliveOnly | ImGuiDockNodeFlags_PassthruCentralNode);
#endif
    auto* main = ImGui::GetMainViewport();
    const auto getFocus = ImGui::GetPlatformIO().Platform_GetWindowFocus;
    s.ViewFocused = !io.AppFocusLost && (!getFocus || (main->PlatformHandle && getFocus(main)));
    const ImVec2 max(main->Pos.x + main->Size.x, main->Pos.y + main->Size.y);
    s.ViewHovered = s.ViewFocused && ImRect(main->Pos, max).Contains(io.MousePos);
    SyncWorkspaceInput();
    editor.SetViewportInputState(s.ViewHovered, s.ViewFocused);
    if (s.ViewHovered && (io.MouseDown[1] || (io.KeyAlt && io.MouseDown[0])) && (io.MouseDelta.x || io.MouseDelta.y))
        editor.HandleCameraMouseMove(int32(io.MouseDelta.x), int32(io.MouseDelta.y), Camera());
}
void RenderAuthoringWorkspace(jEditor& editor)
{
    auto& s = State(); auto& document = editor.GetDocument();
    s.ViewHovered = s.ViewFocused = s.RendererControlVisible = false; s.ViewSize = {};
    for (auto& choice : s.RendererChoices) choice.Visible = false;
    editor.SetViewportInputState(false, false); editor.SetShortcutInputState(false);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) jEditorTools::FinishTerrainStroke(editor);
    if (s.OwnPropertyTransaction && (s.PropertyOwner != editor.GetSelectedEntityId() || !ImGui::IsAnyItemActive())) FinishInspectorEdit(editor);
    editor.SetAuthoringEnabled(true); editor.Placement.EnablePlacementMode = true;
    auto& io = ImGui::GetIO();
    if (io.AppFocusLost) editor.CancelViewportDuplicateDrag();
    if (!ImGui::IsKeyDown(ImGuiKey_Escape)) s.SuppressEditorEscape = false;
    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (escapePressed && editor.CancelViewportDuplicateDrag()) s.SuppressEditorEscape = true;
    if ((s.Placement || s.Replacement) && escapePressed)
    {
        if (const auto* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType("JENGINE_ASSET"))
        {
            ImGui::ClearDragDrop();
            ImGui::ClearActiveID();
        }
        s.SuppressEditorEscape = true;
        Message(s.Replacement ? "Asset replacement cancelled." : "Asset placement cancelled.");
        ClearAssetPlacement(editor);
    }
    if (s.Replacement && document.IsPlaying()) ClearAssetPlacement(editor);
    if (s.Placement && (document.IsPlaying() || (!s.Placement->Drop && !ImGui::GetDragDropPayload()))) ClearAssetPlacement(editor);
    SyncWorkspaceInput();
    if ((io.KeyCtrl || io.KeySuper) && !io.WantTextInput)
    {
        if (!document.IsPlaying() && ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift || document.Path().empty()) ShowDialog(Dialog::Save, SceneDefault(editor)); else SaveScene(editor, document.Path()); }
        if (!document.IsPlaying() && ImGui::IsKeyPressed(ImGuiKey_O, false)) GuardUnsaved(editor, [] { ShowDialog(Dialog::Open); });
        if (!document.IsPlaying() && ImGui::IsKeyPressed(ImGuiKey_N, false)) GuardUnsaved(editor, [&editor, &document] { jEditorExtensions::Shutdown(editor); jEditorTools::Shutdown(); if (g_Engine && g_Editor == &editor) { editor.ResetWorldBindings(false); g_Engine->Game.ClearImportedSceneForAuthoring(); } document.NewScene(); });
        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && editor.GetSelectedEntityId()) CopySelection(editor);
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteSelection(editor);
    }
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::BeginDisabled(document.IsPlaying());
            if (ImGui::MenuItem("New scene", "Ctrl+N")) GuardUnsaved(editor, [&editor, &document] { jEditorExtensions::Shutdown(editor); jEditorTools::Shutdown(); if (g_Engine && g_Editor == &editor) { editor.ResetWorldBindings(false); g_Engine->Game.ClearImportedSceneForAuthoring(); } document.NewScene(); });
            if (ImGui::MenuItem("Open scene", "Ctrl+O")) GuardUnsaved(editor, [] { ShowDialog(Dialog::Open); });
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveScene(editor, SceneDefault(editor));
            if (ImGui::MenuItem("Save As")) ShowDialog(Dialog::Save, SceneDefault(editor));
            if (ImGui::MenuItem("Set current as Startup Scene", nullptr, false, s.Project && !document.Path().empty()))
                SetStartupScene(editor, document.Path());
            ImGui::Separator();
            if (ImGui::MenuItem("New project")) GuardUnsaved(editor, [] { ShowDialog(Dialog::NewProject); });
            if (ImGui::MenuItem("Open project")) GuardUnsaved(editor, [] { ShowDialog(Dialog::OpenProject); });
            if (ImGui::BeginMenu("Recent projects"))
            {
                std::string error; for (const auto& path : jEditorProject::LoadRecent(s.LayoutFile.parent_path() / "recent-projects.json", error))
                    if (ImGui::MenuItem(jEditorAssetPath::ToUtf8(path).c_str())) GuardUnsaved(editor, [&editor, path] { std::string error; if (!OpenAuthoringProject(editor, path, error)) Message(error, true); });
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Instantiate prefab")) ShowDialog(Dialog::OpenPrefab);
            if (ImGui::MenuItem("Recover autosave")) GuardUnsaved(editor, [&editor] { std::string error; if (!LoadAuthoringScene(editor, editor.GetProjectRoot() / ".jeditor/recovery.jscene", error)) Message(error, true); });
            if (ImGui::MenuItem("Build package", nullptr, false, s.Project != nullptr)) ShowDialog(Dialog::Package);
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem(("Undo " + document.UndoLabel()).c_str(), "Ctrl+Z", false, document.CanUndo())) document.Undo();
            if (ImGui::MenuItem(("Redo " + document.RedoLabel()).c_str(), "Ctrl+Y", false, document.CanRedo())) document.Redo();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) editor.DuplicateSelected();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, editor.GetSelectedEntityId() != 0)) CopySelection(editor);
            if (ImGui::MenuItem("Paste", "Ctrl+V")) PasteSelection(editor);
            if (ImGui::MenuItem("Delete", "Delete")) editor.DeleteSelected();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add")) { AddMenu(editor); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("Assets / Console", nullptr, &s.ShowAssets);
            ImGui::MenuItem("Profiler", nullptr, &s.ShowProfiler);
            ImGui::MenuItem("Material / Animation / Effects / Terrain", nullptr, &s.ShowTools);
            if (ImGui::MenuItem("Extensions / hot reload")) jEditorExtensions::SetVisible(editor, true);
            if (ImGui::MenuItem("Project settings")) jEditorProjectSettings::SetVisible(true);
            if (ImGui::MenuItem("Environment / reflections / atmosphere")) jSceneFeatureTools::SetVisible(true);
            ImGui::MenuItem("Layout settings", nullptr, &s.ShowSettings);
            if (ImGui::MenuItem("Reset layout")) { s.ResetLayout = true; s.LeftWidth = 245; s.RightWidth = 325; s.BottomHeight = 205; }
            if (ImGui::MenuItem("Save layout")) SaveLayout();
            if (ImGui::MenuItem("Viewport only", "F11")) RequestAuthoringViewportOnlyToggle();
            if (ImGui::MenuItem("Rendering panels", "F10")) SetAuthoringWorkspaceVisible(false);
            ImGui::EndMenu();
        }
        CaptureMenu(editor);
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard Shortcuts"))
            {
                if (!s.ShowKeyboardShortcuts) s.ShortcutFilter.Clear();
                s.ShowKeyboardShortcuts = true;
                ImGui::SetWindowFocus("Keyboard Shortcuts");
            }
            ImGui::EndMenu();
        }
        if (!document.IsPlaying())
        {
            if (ImGui::Button("Play"))
            {
                FinishWorkspaceEdit(editor);
                ClearAssetPlacement(editor);
                editor.BeginPlay();
            }
        }
        else { if (ImGui::Button("Stop")) editor.StopPlay(); }
        ImGui::Text("%s%s | %.1f ms", document.Name().c_str(), document.IsDirty() ? " *" : "", io.Framerate > 0 ? 1000 / io.Framerate : 0);
        ImGui::EndMainMenuBar();
    }
    if (s.Visible && !document.IsPlaying() && s.Modal == Dialog::None && !s.UnsavedPrompt)
    {
        AdvancePlacement(editor);
        AdvanceModelReplacement(editor);
    }
    const auto* vp = ImGui::GetMainViewport(); const float top = ImGui::GetFrameHeight() + 3;
#ifdef IMGUI_HAS_DOCK
    if (s.Docking && jEditorUiRenderer::SupportsDocking())
    {
        const ImGuiID dock = s.DockSpaceId = ImGui::DockSpaceOverViewport(vp, ImGuiDockNodeFlags_PassthruCentralNode);
        if (!s.DocksInitialized || s.ResetLayout)
        {
            if (s.ResetLayout || !ImGui::DockBuilderGetNode(dock) || !ImGui::DockBuilderGetNode(dock)->IsSplitNode())
            {
                ImGui::DockBuilderRemoveNode(dock); ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
                ImGuiID center = dock, leftDock, rightDock, bottomDock;
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .21f, &leftDock, &center);
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .30f, &rightDock, &center);
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .29f, &bottomDock, &center);
                ImGui::DockBuilderDockWindow("Hierarchy", leftDock); ImGui::DockBuilderDockWindow("Inspector", rightDock);
                ImGui::DockBuilderDockWindow("Project tools", bottomDock); ImGui::DockBuilderDockWindow("Scene", center); ImGui::DockBuilderDockWindow("Game", center);
                ImGui::DockBuilderFinish(dock);
            }
            s.DocksInitialized = true;
        }
    }
#endif
    const float bottom = s.ShowAssets ? std::min(s.BottomHeight, vp->Size.y * .4f) : 0;
    const float left = std::min(s.LeftWidth, vp->Size.x * .26f), right = std::min(s.RightWidth, vp->Size.x * .33f);
    const bool docked = s.Docking && jEditorUiRenderer::SupportsDocking();
    const auto flags = s.FixedLayout && !docked ? ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse : ImGuiWindowFlags_None;
    const auto condition = (s.FixedLayout || s.ResetLayout) ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    const auto panel = [&](const char* name, float x, float y, float w, float h, ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None)
    {
        if (!docked)
        {
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + x, vp->Pos.y + y), condition);
            ImGui::SetNextWindowSize(ImVec2(w, h), condition);
        }
        const bool open = ImGui::Begin(name, nullptr, flags | extraFlags);
        ImGui::SetWindowFontScale(s.FontScale);
        return open;
    };
    if (panel("Hierarchy", 0, top, left, vp->Size.y - top - bottom)) { if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) editor.SetShortcutInputState(true); Hierarchy(editor); } ImGui::End();
    if (s.OwnPropertyTransaction && s.PropertyOwner != editor.GetSelectedEntityId()) FinishInspectorEdit(editor);
    if (panel("Inspector", vp->Size.x - right, top, right, vp->Size.y - top - bottom)) Inspector(editor); ImGui::End();
    const float viewWidth = std::max(64.0f, vp->Size.x - left - right);
    const bool scene = !document.IsPlaying();
    const ImGuiWindowFlags viewFlags = scene ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None;
    if (scene) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool viewOpen = panel(scene ? "Scene" : "Game", left, top, viewWidth, vp->Size.y - top - bottom, viewFlags);
    if (viewOpen && scene) SceneToolbar(editor);
    const bool toolbarBlocksInput = viewOpen && ViewportRendererToolbar();
    if (viewOpen && (toolbarBlocksInput || s.ViewToolbarOwnsInput) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Release an existing drag even when the toolbar blocks viewport input.
        ImGuizmo::Enable(false);
        editor.RenderViewportGizmo(nullptr);
        ImGuizmo::Enable(true);
    }
    auto size = ImGui::GetContentRegionAvail();
    if (viewOpen && size.x >= 1.0f && size.y >= 1.0f)
    {
        auto position = ImGui::GetCursorScreenPos();
        if (!scene)
        {
            const float aspect = static_cast<float>(std::max(1, SCR_WIDTH)) / std::max(1, SCR_HEIGHT);
            if (size.x / size.y > aspect)
            {
                const float width = std::max(1.0f, size.y * aspect);
                position.x += (size.x - width) * .5f;
                size.x = width;
            }
            else
            {
                const float height = std::max(1.0f, size.x / aspect);
                position.y += (size.y - height) * .5f;
                size.y = height;
            }
        }
        ImGui::SetCursorScreenPos(position); s.ViewPosition = position; s.ViewSize = size;
        if (void* texture = jEditorUiRenderer::ScenePreviewTexture()) ImGui::Image(texture, size);
        else ImGui::Dummy(size);
        const bool imageHovered = ImGui::IsItemHovered();
        if (imageHovered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) s.ViewToolbarOwnsInput = false;
        const bool viewportInputBlocked = toolbarBlocksInput || s.ViewToolbarOwnsInput;
        s.ViewHovered = imageHovered && !viewportInputBlocked;
        s.ViewFocused = ImGui::IsWindowFocused() && !viewportInputBlocked;
        editor.SetViewportInputState(s.ViewHovered, s.ViewFocused);
        if (s.ViewFocused) editor.SetShortcutInputState(true);
        auto* camera = Camera();
        const bool dragging = ImGui::GetDragDropPayload() != nullptr;
        if (scene && camera) RenderAssetPlacement(editor, *camera);
        auto* sceneDraw = ImGui::GetWindowDrawList();
        sceneDraw->PushClipRect(position, ImVec2(position.x + size.x, position.y + size.y), true);
        const bool terrainBrush = !dragging && s.ShowTools && s.PaintTerrainInScene && camera &&
            jEditorTools::RenderTerrainBrush(editor, *camera, position, size, s.ViewHovered);
        const bool gizmoInput = !dragging && !terrainBrush && !viewportInputBlocked &&
            editor.RenderViewportGizmo(camera, position.x, position.y, size.x, size.y);
        if (dragging || terrainBrush) editor.CancelViewportDuplicateDrag();
        sceneDraw->PopClipRect();
        if (!dragging && s.ViewHovered && !gizmoInput)
        {
            if (!document.IsPlaying() && !terrainBrush && ImGui::IsMouseClicked(0) && !io.KeyAlt)
            {
                double x = io.MousePos.x, y = io.MousePos.y;
                if (MapAuthoringViewportPoint(x, y))
                {
                    editor.Placement.bPickRequested = true;
                    editor.Placement.PickToggleSelection = io.KeyCtrl || io.KeySuper;
                    editor.Placement.PickMouseX = int32(x);
                    editor.Placement.PickMouseY = int32(y);
                }
            }
            if ((io.MouseDown[1] || (io.KeyAlt && io.MouseDown[0])) && (io.MouseDelta.x || io.MouseDelta.y))
                editor.HandleCameraMouseMove(int32(io.MouseDelta.x), int32(io.MouseDelta.y), camera);
        }
        if (!s.Status.empty() && size.x > 12 && size.y > ImGui::GetTextLineHeight() + 16)
        {
            const ImVec2 status(position.x + 6, position.y + size.y - ImGui::GetTextLineHeight() - 8);
            sceneDraw->PushClipRect(position, ImVec2(position.x + size.x, position.y + size.y), true);
            sceneDraw->AddRectFilled(ImVec2(status.x - 4, status.y - 3),
                ImVec2(std::min(position.x + size.x, status.x + ImGui::CalcTextSize(s.Status.c_str()).x + 4), position.y + size.y - 2), IM_COL32(16,16,16,220));
            sceneDraw->AddText(status, IM_COL32_WHITE, s.Status.c_str());
            sceneDraw->PopClipRect();
        }
    }
    else editor.CancelViewportDuplicateDrag();
    ImGui::End();
    if (scene) ImGui::PopStyleVar();
    if (s.ShowAssets && bottom > 0)
    {
        if (panel("Project tools", 0, vp->Size.y - bottom, vp->Size.x, bottom))
        {
            if (ImGui::BeginTabBar("Project tabs"))
            {
                if (ImGui::BeginTabItem("Assets", nullptr, s.FocusAssets ? ImGuiTabItemFlags_SetSelected : 0))
                { s.FocusAssets = false; Assets(editor); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Console")) { Logs(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Imports"))
                {
                    ImGui::TextWrapped("%s", editor.ImportStatus().c_str());
                    ImGui::Text("Pending %zu", editor.PendingImportCount());
                    if (ImGui::Button("Cancel pending imports")) { ClearAssetPlacement(editor); editor.CancelPendingImports(); }
                    if (s.Registry)
                    {
                        const auto scan = s.Registry->GetWorkStats();
                        ImGui::Text("Catalog scan: %s", scan.Active ? "Working" : "Idle");
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
    if (s.ShowSettings)
    {
        if (ImGui::Begin("Layout settings", &s.ShowSettings))
        {
            ImGui::Checkbox("Fixed split layout", &s.FixedLayout); ImGui::SliderFloat("Hierarchy width", &s.LeftWidth, 180, 700);
            if (jEditorUiRenderer::SupportsDocking()) ImGui::Checkbox("Dock panels", &s.Docking);
            if (jEditorUiRenderer::SupportsPlatformWindows()) ImGui::Checkbox("Allow separate OS windows", &s.PlatformWindows);
            ImGui::SliderFloat("Inspector width", &s.RightWidth, 240, 800); ImGui::SliderFloat("Bottom height", &s.BottomHeight, 100, 600);
            ImGui::SliderFloat("Text scale", &s.FontScale, .8f, 2); ImGui::Checkbox("Autosave every 60 seconds", &s.AutoSave);
            if (ImGui::Button("Save layout")) SaveLayout();
        } ImGui::End();
    }
    if (s.ShowProfiler) { if (ImGui::Begin("Editor diagnostics", &s.ShowProfiler)) Profiler(editor); ImGui::End(); }
    s.PaintTerrainInScene = false;
    if (s.ShowTools)
    {
        if (ImGui::Begin("Authoring tools", &s.ShowTools))
            s.PaintTerrainInScene = jEditorTools::Render(editor, s.Project.get(), s.Registry.get());
        ImGui::End();
    }
    if (!s.PaintTerrainInScene || !s.ShowTools) jEditorTools::FinishTerrainStroke(editor);
    s.ResetLayout = false;
    Dialogs(editor);
    CaptureOptions(editor);
    KeyboardShortcuts();
    jEditorExtensions::Render(editor);
    jEditorProjectSettings::Render(editor, s.Project.get());
    jSceneFeatureTools::Render(editor, s.Project.get());
    if (s.IntegrationDraw) s.IntegrationDraw();
    if (s.ViewHovered && !io.WantTextInput && !ImGui::IsAnyItemActive()) io.WantCaptureMouse = false;
    if (s.ViewFocused && !io.WantTextInput && !ImGui::IsAnyItemActive()) io.WantCaptureKeyboard = false;
    jConsole::Get().Render();
}
void ShutdownAuthoringWorkspace()
{
    jEditorCapture::Shutdown();
    jSceneFeatureTools::Shutdown();
    auto& s = State(); jEditorAssetPanel::Shutdown(); if (s.Registry) s.Registry->Shutdown();
    s.ShowKeyboardShortcuts = false;
    if (g_Editor) { ClearAssetPlacement(*g_Editor); jEditorExtensions::Shutdown(*g_Editor); }
    if (s.Packaging.valid()) s.Packaging.wait();
    jEditorTools::Shutdown(); jEditorUiRenderer::Shutdown();
}
void PrepareAuthoringUiFrame()
{
#ifdef IMGUI_HAS_DOCK
    auto& s = State(); auto& io = ImGui::GetIO();
    if (s.Runtime)
    {
        io.ConfigFlags &= ~(ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable);
        io.IniFilename = nullptr;
        return;
    }
    if (IsAuthoringWorkspaceVisible() && s.Docking && jEditorUiRenderer::SupportsDocking()) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    else io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    // Dear ImGui disallows first enabling viewports on its second frame, while
    // a display can wake between those frames. Defer that one transition.
    const bool enableViewports = IsAuthoringWorkspaceVisible() && s.TestPlatformWindows.value_or(s.PlatformWindows)
        && jEditorUiRenderer::SupportsPlatformWindows()
        && (ImGui::GetFrameCount() != 1 || (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable));
    if (enableViewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    else io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    if (io.IniFilename != s.ImGuiFile.c_str())
    {
        std::error_code error; std::filesystem::create_directories(s.LayoutFile.parent_path(), error);
        io.IniFilename = s.ImGuiFile.c_str();
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }
#endif
}
#endif
