#include "pch.h"
#include "jEditorWorkspace.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "jEditorTools.h"
#include "jEditorUiRenderer.h"
#include "jEditorExtensions.h"
#include "jEditorPackaging.h"
#include "jEditorAssetPanel.h"
#include "jEditorProjectSettings.h"
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
#include "RHI/jRHI.h"
#include <fstream>
#include <future>
#include <set>

namespace
{
namespace fs = std::filesystem;
using namespace jEditorAuthoring;
using Json = nlohmann::json;
enum class Dialog { None, Save, Open, NewProject, OpenProject, Import, SavePrefab, OpenPrefab, Package };
struct Row { EntityId Id; int Depth; bool Children; std::string Label; };
struct Workspace
{
    bool Visible = true, Runtime = false, ViewHovered = false, ViewFocused = false;
    bool FixedLayout = true, ShowAssets = true, ShowLogs = true, ShowProfiler = false, ShowTools = false;
    bool Docking = true, PlatformWindows = false, DocksInitialized = false;
    std::optional<bool> TestPlatformWindows;
    std::function<void()> IntegrationDraw;
    bool ShowSettings = false, LoadLayout = true, ResetLayout = false, AutoSave = true;
    float LeftWidth = 245, RightWidth = 325, BottomHeight = 205, FontScale = 1;
    float AutoSaveSeconds = 0, WatchSeconds = 0;
    ImVec2 ViewPosition{}, ViewSize{};
    uint64_t RowRevision = ~uint64_t(0), AssetRevision = ~uint64_t(0);
    uint64_t RowSignature = ~uint64_t(0);
    ImGuiTextFilter EntityFilter, AssetFilter, LogFilter;
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
void GuardUnsaved(jEditor& editor, std::function<void()> action)
{
    FinishInspectorEdit(editor);
    if (editor.GetDocument().IsDirty()) { State().Pending = std::move(action); State().UnsavedPrompt = true; }
    else action();
}
void CopySelection(jEditor& editor)
{
    FinishInspectorEdit(editor);
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
    FinishInspectorEdit(editor);
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
    FinishInspectorEdit(editor);
    try { if (path.has_parent_path()) fs::create_directories(path.parent_path()); }
    catch (const std::exception& error) { Message(error.what(), true); return false; }
    if (!editor.GetDocument().Save(path)) { Message(editor.GetDocument().LastError(), true); return false; }
    Message("Saved " + jEditorAssetPath::ToUtf8(path));
    auto& s = State();
    if (s.Project)
    {
        std::error_code ec; const auto relative = fs::relative(path, s.Project->GetRoot(), ec);
        if (!ec && !relative.empty() && *relative.begin() != "..")
        {
            s.Project->EditConfig().StartupScene = jEditorAssetPath::ToUtf8(relative);
            std::string error;
            if (!s.Project->Save(error)) Message(error, true);
        }
    }
    return true;
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
bool FinishProperty(Document& document, Entity& entity, bool changed, const char* label)
{
    if (ImGui::IsItemActivated() && !document.HasTransaction())
    {
        if (document.BeginTransaction(label)) { State().OwnPropertyTransaction = true; State().PropertyOwner = entity.Id; }
    }
    if (changed && !document.Update(entity, label)) Message(document.LastError(), true);
    if (ImGui::IsItemDeactivated() && State().OwnPropertyTransaction)
    {
        if (document.HasTransaction()) document.CommitTransaction();
        State().OwnPropertyTransaction = false; State().PropertyOwner = 0;
    }
    return changed;
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
    for (int i = 0; i <= static_cast<int>(EntityKind::Terrain); ++i)
    {
        auto kind = static_cast<EntityKind>(i);
        if (kind == EntityKind::Model) continue;
        if (ImGui::MenuItem(KindName(kind))) editor.CreateEntity(kind);
    }
}
void BuildRows(Document& document)
{
    auto& s = State(); const std::string filter(s.EntityFilter.InputBuf);
    if (s.RowRevision == document.Revision() && s.CachedEntityFilter == filter) return;
    s.RowRevision = document.Revision();
    uint64_t signature = std::hash<std::string>{}(filter);
    const auto mix = [&](uint64_t value) { signature ^= value + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2); };
    std::map<EntityId, std::vector<EntityId>> children;
    for (const auto& [id, entity] : document.Entities()) { mix(id); mix(entity.Parent); mix(std::hash<std::string>{}(entity.Name)); }
    for (auto id : s.Collapsed) mix(id);
    if (s.RowSignature == signature && s.CachedEntityFilter == filter) return;
    s.RowSignature = signature; s.Rows.clear(); s.CachedEntityFilter = filter;
    for (const auto& [id, entity] : document.Entities()) children[entity.Parent].push_back(id);
    std::set<EntityId> matches;
    if (!filter.empty()) for (const auto& [id, e] : document.Entities())
        if (s.EntityFilter.PassFilter(e.Name.c_str()))
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
    BuildRows(document);
    ImGui::TextDisabled("%zu objects | %zu selected", document.Entities().size(), editor.GetSelectedEntities().size());
    ImGui::BeginChild("EntityRows", ImVec2(0, 0), false);
    ImGuiListClipper clip; clip.Begin(static_cast<int>(s.Rows.size()));
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
        if (ImGui::Selectable(row.Label.c_str(), active)) editor.SelectEntity(row.Id, ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);
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
            editor.SelectEntity(row.Id);
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
}
void TextureSlot(Document& document, Entity& entity, const char* label, std::string& path)
{
    ImGui::TextWrapped("%s: %s", label, path.empty() ? "None (drop a texture)" : path.c_str());
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_ASSET"))
        {
            auto& s = State();
            const std::string id(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize) - 1);
            const auto* asset = s.Registry ? s.Registry->FindById(id) : nullptr;
            if (asset && asset->Kind == jEditorAssetKind::Texture) { path = asset->SourcePath; entity.Material.OverrideEnabled = true; document.Update(entity, "Assign texture"); }
        }
        ImGui::EndDragDropTarget();
    }
}
void Inspector(jEditor& editor)
{
    auto& document = editor.GetDocument(); const auto* selected = document.Find(editor.GetSelectedEntityId());
    if (!selected) { ImGui::TextWrapped("Select an object in Hierarchy or the scene."); return; }
    Entity e = *selected; ImGui::PushID(static_cast<int>(e.Id));
    char name[256]; std::snprintf(name, sizeof(name), "%s", e.Name.c_str());
    const bool renamed = ImGui::InputText("Name", name, sizeof(name)); if (renamed) e.Name = name;
    FinishProperty(document, e, renamed, "Rename object");
    ImGui::TextDisabled("%s | ID %llu", KindName(e.Kind), static_cast<unsigned long long>(e.Id));
    bool visible = e.Visible; if (ImGui::Checkbox("Visible", &visible)) { e.Visible = visible; document.Update(e, "Set visibility"); }
    ImGui::SameLine(); bool locked = e.Locked; if (ImGui::Checkbox("Locked", &locked)) { e.Locked = locked; document.Update(e, "Set lock"); }
    ImGui::BeginDisabled(document.IsEffectivelyLocked(e.Id));
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Vec3Field(document, e, "Position", e.Local.Position);
        Vec3Field(document, e, "Rotation (deg)", e.Local.RotationDegrees);
        Vec3Field(document, e, "Scale", e.Local.Scale, .01f);
        if (ImGui::Button("Reset transform")) { e.Local = {}; document.Update(e, "Reset transform"); }
        int operation = editor.Placement.GizmoOperation == ImGuizmo::ROTATE ? 1 : editor.Placement.GizmoOperation == ImGuizmo::SCALE ? 2 : 0;
        if (ImGui::Combo("Tool", &operation, "Move\0Rotate\0Scale\0")) editor.Placement.GizmoOperation = operation == 1 ? ImGuizmo::ROTATE : operation == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
        bool local = editor.Placement.GizmoMode == ImGuizmo::LOCAL;
        if (ImGui::Checkbox("Local axes", &local)) editor.Placement.GizmoMode = local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        ImGui::SameLine(); ImGui::Checkbox("Snap", &editor.Placement.UseSnap);
        if (editor.Placement.UseSnap) ImGui::DragFloat3("Move snap", editor.Placement.TranslationSnap, .1f, .001f, 10000);
    }
    if (e.Kind == EntityKind::Cube || e.Kind == EntityKind::Sphere || e.Kind == EntityKind::Model || e.Kind == EntityKind::Terrain)
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (e.Kind == EntityKind::Model && ImGui::Checkbox("Override imported material", &e.Material.OverrideEnabled)) document.Update(e, "Toggle material override");
            FinishMaterialProperty(document, e, ImGui::ColorEdit3("Base color", &e.Material.BaseColor.X), "Material color");
            FinishMaterialProperty(document, e, ImGui::ColorEdit3("Emissive", &e.Material.Emissive.X, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float), "Emissive");
            FinishMaterialProperty(document, e, ImGui::SliderFloat("Metallic", &e.Material.Metallic, 0, 1), "Metallic");
            FinishMaterialProperty(document, e, ImGui::SliderFloat("Roughness", &e.Material.Roughness, .01f, 1), "Roughness");
            FinishMaterialProperty(document, e, ImGui::SliderFloat("Opacity", &e.Material.Opacity, 0, 1), "Opacity");
            TextureSlot(document, e, "Base color texture", e.Material.BaseColorTexture);
            TextureSlot(document, e, "Normal texture", e.Material.NormalTexture);
            if (ImGui::Button("Material graph")) State().ShowTools = true;
        }
    if (e.Kind == EntityKind::PointLight || e.Kind == EntityKind::SpotLight || e.Kind == EntityKind::DirectionalLight)
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
    if (asset.Kind == jEditorAssetKind::Model) editor.AddModel(asset.SourcePath, asset.Id, asset.Revision);
    else if (asset.Kind == jEditorAssetKind::Scene)
    {
        std::string error; const auto file = State().Registry->Resolve(asset.Id, error);
        if (file) GuardUnsaved(editor, [&editor, path = *file] { std::string error; if (!LoadAuthoringScene(editor, path, error)) Message(error, true); });
        else Message(error, true);
    }
}
void Assets(jEditor& editor)
{
    auto& s = State();
    if (!s.Project || !s.Registry) { ImGui::TextWrapped("Create or open a project from File to browse assets."); return; }
    jEditorAssetPanel::Render(editor, *s.Project, *s.Registry, s.SelectedAsset,
        [&editor](const std::string& id) {
            auto& state = State();
            if (const auto* found = state.Registry->FindById(id)) { const auto snapshot = *found; ActivateAsset(editor, snapshot); }
        });
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
    if (!SaveScene(editor, SceneDefault(editor))) return;
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
            case Dialog::SavePrefab: ok = editor.GetDocument().SavePrefab(editor.GetSelectedEntityId(), path); error = editor.GetDocument().LastError(); break;
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
void SetAuthoringWorkspaceVisible(bool visible) { if (!visible && g_Editor) FinishInspectorEdit(*g_Editor); State().Visible = visible; }
void SetAuthoringRuntimeMode(bool runtime) { State().Runtime = runtime; State().Visible = !runtime; }
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
    FinishInspectorEdit(*g_Editor);
    if (g_Editor->GetDocument().IsPlaying()) g_Editor->StopPlay();
    if (!g_Editor->GetDocument().IsDirty()) return true;
    s.Visible = true;
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
    State().Visible = true;
    GuardUnsaved(*g_Editor, std::move(action));
}
bool IsAuthoringRuntimeMode() { return State().Runtime; }
void SetAuthoringExecutablePath(const std::filesystem::path& path)
{
    std::error_code error;
    State().ExecutablePath = std::filesystem::weakly_canonical(path, error);
    if (error) State().ExecutablePath = std::filesystem::absolute(path);
}
bool IsAuthoringViewportHovered() { return State().ViewHovered; }
bool IsAuthoringViewportFocused() { return State().ViewFocused; }
bool MapAuthoringViewportPoint(double& x, double& y)
{
    auto& s = State();
    if (!IsAuthoringWorkspaceVisible()) return true;
    if (!s.ViewHovered || s.ViewSize.x <= 0 || s.ViewSize.y <= 0) return false;
    x = (x - s.ViewPosition.x) * SCR_WIDTH / s.ViewSize.x;
    y = (y - s.ViewPosition.y) * SCR_HEIGHT / s.ViewSize.y;
    return x >= 0 && y >= 0 && x < SCR_WIDTH && y < SCR_HEIGHT;
}
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
    jEditorExtensions::Shutdown(editor); jEditorTools::Shutdown();
    if (!editor.GetDocument().LoadJson(candidate.ToJson(), scene)) { error = editor.GetDocument().LastError(); return false; }
    editor.SetSelectedEntities({}); Message("Opened scene " + jEditorAssetPath::ToUtf8(scene)); return true;
}
void TickAuthoringWorkspace(jEditor& editor, float dt)
{
    jModelUploadSession::CollectRetired();
    auto& s = State(); if (s.LoadLayout) LoadLayout();
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
void RenderAuthoringWorkspace(jEditor& editor)
{
    auto& s = State(); auto& document = editor.GetDocument();
    editor.SetViewportInputState(false, false); editor.SetShortcutInputState(false);
    if (s.OwnPropertyTransaction && (s.PropertyOwner != editor.GetSelectedEntityId() || !ImGui::IsAnyItemActive())) FinishInspectorEdit(editor);
    editor.SetAuthoringEnabled(true); editor.Placement.EnablePlacementMode = true;
    auto& io = ImGui::GetIO();
    // Platform windows send input through ImGui's backend. Normalize that same
    // event stream for engine shortcuts and camera controls in every viewport.
    for (int key = 0; key < 26; ++key) SetKeyDownState(static_cast<EInputKey>(int(EInputKey::A) + key), ImGui::IsKeyDown(static_cast<ImGuiKey>(int(ImGuiKey_A) + key)));
    for (int key = 0; key < 10; ++key) SetKeyDownState(static_cast<EInputKey>(int(EInputKey::NUM0) + key), ImGui::IsKeyDown(static_cast<ImGuiKey>(int(ImGuiKey_0) + key)));
    const std::pair<EInputKey, ImGuiKey> mapped[] = {{EInputKey::DELETE_KEY, ImGuiKey_Delete}, {EInputKey::ESCAPE, ImGuiKey_Escape}, {EInputKey::SPACE, ImGuiKey_Space}, {EInputKey::ENTER, ImGuiKey_Enter}, {EInputKey::TAB, ImGuiKey_Tab}, {EInputKey::LEFT, ImGuiKey_LeftArrow}, {EInputKey::RIGHT, ImGuiKey_RightArrow}, {EInputKey::UP, ImGuiKey_UpArrow}, {EInputKey::DOWN, ImGuiKey_DownArrow}};
    for (const auto& [engineKey, uiKey] : mapped) SetKeyDownState(engineKey, ImGui::IsKeyDown(uiKey));
    SetKeyDownState(EInputKey::CTRL, io.KeyCtrl || io.KeySuper); SetKeyDownState(EInputKey::ALT, io.KeyAlt); SetKeyDownState(EInputKey::SHIFT, io.KeyShift);
    SetKeyDownState(EInputKey::PLUS, ImGui::IsKeyDown(ImGuiKey_Equal) || ImGui::IsKeyDown(ImGuiKey_KeypadAdd)); SetKeyDownState(EInputKey::MINUS, ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_KeypadSubtract));
    g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down = io.MouseDown[0];
    g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down = io.MouseDown[1];
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
            ImGui::MenuItem("Layout settings", nullptr, &s.ShowSettings);
            if (ImGui::MenuItem("Reset layout")) { s.ResetLayout = true; s.LeftWidth = 245; s.RightWidth = 325; s.BottomHeight = 205; }
            if (ImGui::MenuItem("Save layout")) SaveLayout();
            if (ImGui::MenuItem("Rendering panels", "F10")) s.Visible = false;
            ImGui::EndMenu();
        }
        if (!document.IsPlaying()) { if (ImGui::Button("Play")) editor.BeginPlay(); }
        else { if (ImGui::Button("Stop")) editor.StopPlay(); }
        ImGui::Text("%s%s | %.1f ms", document.Name().c_str(), document.IsDirty() ? " *" : "", io.Framerate > 0 ? 1000 / io.Framerate : 0);
        ImGui::EndMainMenuBar();
    }
    const auto* vp = ImGui::GetMainViewport(); const float top = ImGui::GetFrameHeight() + 3;
#ifdef IMGUI_HAS_DOCK
    if (s.Docking && jEditorUiRenderer::SupportsDocking())
    {
        const ImGuiID dock = ImGui::DockSpaceOverViewport(vp, ImGuiDockNodeFlags_PassthruCentralNode);
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
    const auto panel = [&](const char* name, float x, float y, float w, float h)
    { if (!docked) { ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + x, vp->Pos.y + y), condition); ImGui::SetNextWindowSize(ImVec2(w, h), condition); } const bool open = ImGui::Begin(name, nullptr, flags); ImGui::SetWindowFontScale(s.FontScale); return open; };
    if (panel("Hierarchy", 0, top, left, vp->Size.y - top - bottom)) { if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) editor.SetShortcutInputState(true); Hierarchy(editor); } ImGui::End();
    if (s.OwnPropertyTransaction && s.PropertyOwner != editor.GetSelectedEntityId()) FinishInspectorEdit(editor);
    if (panel("Inspector", vp->Size.x - right, top, right, vp->Size.y - top - bottom)) Inspector(editor); ImGui::End();
    const float viewWidth = std::max(64.0f, vp->Size.x - left - right);
    if (panel(document.IsPlaying() ? "Game" : "Scene", left, top, viewWidth, vp->Size.y - top - bottom))
    {
        auto position = ImGui::GetCursorScreenPos(); auto size = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(SCR_WIDTH) / std::max(1, SCR_HEIGHT);
        if (size.x / std::max(1.0f, size.y) > aspect) { const float width = size.y * aspect; position.x += (size.x - width) * .5f; size.x = width; }
        else { const float height = size.x / aspect; position.y += (size.y - height) * .5f; size.y = height; }
        size.x = std::max(1.0f, size.x); size.y = std::max(1.0f, size.y);
        ImGui::SetCursorScreenPos(position); s.ViewPosition = position; s.ViewSize = size;
        if (void* texture = jEditorUiRenderer::ScenePreviewTexture()) ImGui::Image(texture, size);
        else ImGui::InvisibleButton("Scene surface", size);
        s.ViewHovered = ImGui::IsItemHovered(); s.ViewFocused = ImGui::IsWindowFocused();
        editor.SetViewportInputState(s.ViewHovered, s.ViewFocused);
        if (s.ViewFocused) editor.SetShortcutInputState(true);
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_ASSET"))
                if (s.Registry && payload->DataSize > 0)
                    if (const auto* asset = s.Registry->FindById(static_cast<const char*>(payload->Data))) ActivateAsset(editor, *asset);
            ImGui::EndDragDropTarget();
        }
        editor.RenderViewportGizmo(Camera(), position.x, position.y, size.x, size.y);
        if (s.ViewHovered && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
        {
            if (ImGui::IsMouseClicked(0) && !io.KeyAlt)
            {
                double x = io.MousePos.x, y = io.MousePos.y;
                if (MapAuthoringViewportPoint(x, y)) { editor.Placement.bPickRequested = true; editor.Placement.PickMouseX = int32(x); editor.Placement.PickMouseY = int32(y); }
            }
            if ((io.MouseDown[1] || (io.KeyAlt && io.MouseDown[0])) && (io.MouseDelta.x || io.MouseDelta.y)) editor.HandleCameraMouseMove(int32(io.MouseDelta.x), int32(io.MouseDelta.y), Camera());
        }
    }
    ImGui::End();
    if (s.ShowAssets)
    {
        if (panel("Project tools", 0, vp->Size.y - bottom, vp->Size.x, bottom))
        {
            if (ImGui::BeginTabBar("Project tabs"))
            {
                if (ImGui::BeginTabItem("Assets")) { Assets(editor); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Console")) { Logs(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Imports")) { ImGui::TextWrapped("%s", editor.ImportStatus().c_str()); ImGui::Text("Pending %zu", editor.PendingImportCount()); if (ImGui::Button("Cancel pending imports")) editor.CancelPendingImports(); if (s.Registry) { const auto scan = s.Registry->GetWorkStats(); ImGui::Text("Catalog scan: %s", scan.Active ? "Working" : "Idle"); } ImGui::EndTabItem(); }
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
    if (s.ShowTools) { if (ImGui::Begin("Authoring tools", &s.ShowTools)) jEditorTools::Render(editor); ImGui::End(); }
    s.ResetLayout = false;
    if (!s.Status.empty()) { ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + left + 5, vp->Pos.y + vp->Size.y - bottom - 25), ImGuiCond_Always); if (ImGui::Begin("Editor status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs)) ImGui::TextUnformatted(s.Status.c_str()); ImGui::End(); }
    Dialogs(editor);
    jEditorExtensions::Render(editor);
    jEditorProjectSettings::Render(editor, s.Project.get());
    if (s.IntegrationDraw) s.IntegrationDraw();
    if (s.ViewHovered && !io.WantTextInput && !ImGui::IsAnyItemActive()) io.WantCaptureMouse = false;
    if (s.ViewFocused && !io.WantTextInput && !ImGui::IsAnyItemActive()) io.WantCaptureKeyboard = false;
    jConsole::Get().Render();
}
void ShutdownAuthoringWorkspace()
{
    auto& s = State(); jEditorAssetPanel::Shutdown(); if (s.Registry) s.Registry->Shutdown();
    if (g_Editor) jEditorExtensions::Shutdown(*g_Editor);
    if (s.Packaging.valid()) s.Packaging.wait();
    jEditorTools::Shutdown(); jEditorUiRenderer::Shutdown();
}
void PrepareAuthoringUiFrame()
{
#ifdef IMGUI_HAS_DOCK
    auto& s = State(); auto& io = ImGui::GetIO();
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
