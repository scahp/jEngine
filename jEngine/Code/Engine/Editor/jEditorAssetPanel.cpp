#include "pch.h"
#include "jEditorAssetPanel.h"
#include "Code/Engine/jEditor.h"
#include "jEditorTools.h"
#include "jEditorUiRenderer.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jImageFileLoader.h"
#include "Core/jLog.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <deque>
#include <future>
#include <set>

#ifdef ENABLE_EDITOR_FEATURES
namespace jEditorAssetPanel
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using jEditorAssetPath::FromUtf8;
using jEditorAssetPath::ToUtf8;
constexpr size_t MaxThumbnails = 64, MaxThumbnailRequests = 8;
enum class Operation { None, NewFolder, Import, Rename, Move, Trash, Settings, Reimport, Activate, Refresh, CancelScan };
struct Request { Operation Type = Operation::None; std::string Id, Source; bool Folder = false; };
struct ThumbnailRequest { std::string Key, Id; fs::path File, ProjectRoot; bool Material = false; };
struct ThumbnailResult { ThumbnailRequest Request; jEditorThumbnail Pixels; std::string Error; };
struct ThumbnailEntry { void* Handle = nullptr; uint32_t Width = 0, Height = 0; std::string Error; uint64_t Used = 0; };
struct FolderRow { std::string Path, Label; int Depth = 0; bool Children = false; };
struct Panel
{
    std::string ProjectToken, ProjectRoot, Folder, Selected, Status, SettingsWarning;
    uint64_t CatalogRevision = UINT64_MAX, Frame = 0;
    bool StatusError = false, FilterDirty = true, FoldersDirty = true, OpenModal = false;
    int KindFilter = 0;
    ImGuiTextFilter Search;
    std::vector<jEditorAssetRecord> Assets;
    std::vector<size_t> Filtered;
    std::set<std::string> Folders, Expanded, Discovered, VisibleKeys;
    std::vector<FolderRow> FolderRows;
    std::map<std::string, ThumbnailEntry> Thumbnails;
    std::vector<void*> Retired;
    std::deque<ThumbnailRequest> Requests;
    std::future<ThumbnailResult> Worker;
    std::optional<ThumbnailRequest> Active;
    std::shared_ptr<std::atomic<bool>> Cancel;
    double LastUpload = 0;
    size_t VisiblePreviews = 0;
    Request Modal;
    std::vector<Request> Deferred;
    char Name[512]{}, SourceFile[2048]{}, Destination[2048]{};
    float Scale = 1;
    bool FlipUVs = true, GenerateNormals = true;
};
Panel& State() { static Panel panel; return panel; }
void Message(const std::string& text, bool error = false)
{
    auto& s = State(); s.Status = text; s.StatusError = error;
    jLog::Get().Write(error ? EjLogLevel::Warning : EjLogLevel::Info, "Assets", text);
}
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return text;
}
std::string Key(const jEditorAssetRecord& asset) { return State().ProjectToken + "|" + asset.Id + "|" + std::to_string(asset.Revision); }
const jEditorAssetRecord* Find(const std::string& id)
{
    const auto& assets = State().Assets;
    const auto it = std::find_if(assets.begin(), assets.end(), [&](const auto& asset) { return asset.Id == id; });
    return it == assets.end() ? nullptr : &*it;
}
void Copy(char* destination, size_t size, const std::string& value) { std::snprintf(destination, size, "%s", value.c_str()); }
void ReleaseRetired()
{
    for (auto handle : State().Retired) jEditorUiRenderer::ReleaseThumbnail(handle);
    State().Retired.clear();
}
void QueueThumbnail(const jEditorAssetRecord& asset, const jEditorProject& project, bool material, bool priority)
{
    auto& s = State();
    const auto key = Key(asset); s.VisibleKeys.insert(key);
    if (auto found = s.Thumbnails.find(key); found != s.Thumbnails.end()) { found->second.Used = s.Frame; return; }
    if (s.Active && s.Active->Key == key) return;
    const auto duplicate = std::find_if(s.Requests.begin(), s.Requests.end(), [&](const auto& request) { return request.Key == key; });
    if (duplicate != s.Requests.end())
    {
        if (priority) { auto copy = *duplicate; s.Requests.erase(duplicate); s.Requests.push_front(std::move(copy)); }
        return;
    }
    if (s.Requests.size() >= MaxThumbnailRequests) { if (!priority) return; s.Requests.pop_back(); }
    std::string error;
    const auto file = jEditorProject::ResolveSafe(project.GetRoot(), asset.SourcePath, error, true);
    if (!file) return;
    ThumbnailRequest request{key, asset.Id, *file, project.GetRoot(), material};
    if (priority) s.Requests.push_front(std::move(request)); else s.Requests.push_back(std::move(request));
}
struct PreviewImage
{
    void* Handle = nullptr;
    uint32_t Width = 0, Height = 0;
    const char* Status = "Preview unavailable";
};
PreviewImage Thumbnail(const jEditorAssetRecord& asset, const jEditorProject& project, bool priority = false)
{
    auto& s = State();
    if (asset.Missing) return {nullptr, 0, 0, "Asset file missing"};
    const auto file = FromUtf8(asset.SourcePath);
    const bool material = asset.Kind == jEditorAssetKind::Material && Lower(ToUtf8(file.extension())) == ".jmaterial";
    if (!material && (asset.Kind != jEditorAssetKind::Texture || !SupportsEditorThumbnail(file)))
        return {nullptr, 0, 0, "Preview format unsupported"};
    if (priority || s.VisiblePreviews++ < MaxThumbnails) QueueThumbnail(asset, project, material, priority);
    const auto found = s.Thumbnails.find(Key(asset));
    if (found == s.Thumbnails.end()) return {nullptr, 0, 0, "Preparing preview"};
    auto& entry = found->second;
    entry.Used = s.Frame;
    return {entry.Handle, entry.Width, entry.Height, entry.Error.c_str()};
}
ImVec2 FitPreview(const PreviewImage& preview, const ImVec2& bounds)
{
    const float width = float(preview.Width), height = float(preview.Height);
    const float scale = std::min(bounds.x / width, bounds.y / height);
    return ImVec2(width * scale, height * scale);
}
void DrawFittedPreview(ImDrawList& draw, const PreviewImage& preview, const ImVec2& min, const ImVec2& max)
{
    const auto size = FitPreview(preview, ImVec2(max.x - min.x, max.y - min.y));
    const ImVec2 corner((min.x + max.x - size.x) * .5f, (min.y + max.y - size.y) * .5f);
    draw.AddImage(preview.Handle, corner, ImVec2(corner.x + size.x, corner.y + size.y));
}
void AddFolders(const std::string& source)
{
    auto path = FromUtf8(source);
    while (!path.empty()) { State().Folders.insert(ToUtf8(path)); path = path.parent_path(); }
}
void Discover(const fs::path& root, const std::string& folder)
{
    auto& s = State();
    if (!s.Discovered.insert(folder).second) return;
    std::string error;
    const auto absolute = jEditorProject::ResolveSafe(root, folder, error, true);
    if (!absolute) return;
    std::error_code ec; fs::directory_iterator it(*absolute, ec), end;
    size_t count = 0;
    while (!ec && it != end && count++ < 50000)
    {
        const auto status = it->symlink_status(ec);
        const auto name = ToUtf8(it->path().filename());
        if (!ec && fs::is_directory(status) && !fs::is_symlink(status) && !name.empty() && name[0] != '.'
            && Lower(name) != "build" && Lower(name) != "cache" && Lower(name) != "logs")
            AddFolders(ToUtf8((FromUtf8(folder) / FromUtf8(name))));
        it.increment(ec);
    }
}
void BuildFolders(jEditorProject& project)
{
    auto& s = State();
    if (!s.FoldersDirty) return;
    for (const auto& folder : s.Expanded) Discover(project.GetRoot(), folder);
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& folder : s.Folders) children[ToUtf8(FromUtf8(folder).parent_path())].push_back(folder);
    s.FolderRows.clear();
    const auto append = [&](auto&& self, const std::string& parent, int depth) -> void {
        if (depth > 32) return;
        for (const auto& folder : children[parent])
        {
            s.FolderRows.push_back({folder, ToUtf8(FromUtf8(folder).filename()), depth,
                !children[folder].empty() || !s.Discovered.count(folder)});
            if (s.Expanded.count(folder)) self(self, folder, depth + 1);
        }
    };
    append(append, "", 0); s.FoldersDirty = false;
}
void Open(Request request, const std::string& destination = {})
{
    auto& s = State(); s.Modal = std::move(request); s.OpenModal = true;
    s.Status.clear(); s.SettingsWarning.clear();
    Copy(s.Name, sizeof(s.Name), ToUtf8(FromUtf8(s.Modal.Source).filename()));
    if (s.Modal.Type == Operation::NewFolder) Copy(s.Name, sizeof(s.Name), "New Folder");
    s.SourceFile[0] = 0;
    Copy(s.Destination, sizeof(s.Destination), destination.empty() ? ToUtf8(FromUtf8(s.Modal.Source).parent_path()) : destination);
    s.Scale = 1; s.FlipUVs = true; s.GenerateNormals = true;
    if (s.Modal.Type == Operation::Settings)
        if (const auto* asset = Find(s.Modal.Id))
            try
            {
                const auto json = Json::parse(asset->ImportSettingsJson);
                s.Scale = json.value("scale", 1.0f); s.FlipUVs = json.value("flipUVs", true); s.GenerateNormals = json.value("generateNormals", true);
                for (auto it = json.begin(); it != json.end(); ++it)
                    if (it.key() != "scale" && it.key() != "flipUVs" && it.key() != "generateNormals")
                        s.SettingsWarning = "Apply replaces unsupported model options with these settings.";
            }
            catch (...) { s.SettingsWarning = "Current options are invalid. Apply valid settings to repair them."; }
}
void Invalidate(jEditor& editor, jEditorAssetRegistry& registry,
    const std::map<std::string, std::pair<uint64_t, bool>>& before)
{
    // All values used by operations are copied; no caller keeps catalog references
    // while a rescan replaces its storage.
    const auto assets = registry.GetAssets();
    for (const auto& asset : assets)
    {
        const auto old = before.find(asset.Id);
        if (old == before.end() || (old->second.first == asset.Revision && old->second.second == asset.Missing)) continue;
        if (asset.Kind == jEditorAssetKind::Texture)
        {
            std::string error; const auto file = registry.Resolve(asset.Id, error);
            if (file) jImageFileLoader::GetInstance().InvalidateImageData(jName(ToUtf8(*file)));
        }
        editor.ReimportAsset(asset.SourcePath, asset.Revision);
    }
}
std::map<std::string, std::pair<uint64_t, bool>> Revisions(const std::vector<jEditorAssetRecord>& assets)
{
    std::map<std::string, std::pair<uint64_t, bool>> result;
    for (const auto& asset : assets) result.emplace(asset.Id, std::make_pair(asset.Revision, asset.Missing));
    return result;
}
bool Execute(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry,
    const Request& request, std::string& selected)
{
    auto& s = State(); std::string error; bool ok = false;
    const auto before = Revisions(registry.GetAssets());
    try
    {
    switch (request.Type)
    {
    case Operation::NewFolder:
        ok = registry.CreateFolder(ToUtf8((FromUtf8(s.Destination) / FromUtf8(s.Name))), error); break;
    case Operation::Import:
        if (!FromUtf8(s.SourceFile).is_absolute()) { error = "Choose an absolute source file path."; break; }
        ok = registry.ImportFile(FromUtf8(s.SourceFile), ToUtf8((FromUtf8(s.Destination) / FromUtf8(s.SourceFile).filename())), error); break;
    case Operation::Rename:
    case Operation::Move:
    {
        if (!request.Folder && request.Type == Operation::Rename
            && Lower(ToUtf8(FromUtf8(s.Name).extension())) != Lower(ToUtf8(FromUtf8(request.Source).extension())))
        { error = "Keep the asset file extension when renaming."; break; }
        const auto destination = request.Type == Operation::Rename ? FromUtf8(request.Source).parent_path() / FromUtf8(s.Name)
            : FromUtf8(s.Destination) / FromUtf8(request.Source).filename();
        ok = registry.MovePath(request.Source, ToUtf8(destination), error); break;
    }
    case Operation::Trash:
    {
        fs::path recoverable;
        ok = registry.TrashPath(request.Source, recoverable, error);
        if (ok && selected == request.Id) selected.clear();
        if (ok) s.Status = "Moved to project trash: " + ToUtf8(recoverable.lexically_relative(project.GetRoot()));
        break;
    }
    case Operation::Settings:
        if (!std::isfinite(s.Scale) || s.Scale <= 0) { error = "Scale must be a finite positive value."; break; }
        ok = registry.SetImportSettings(request.Id, Json{{"scale", s.Scale}, {"flipUVs", s.FlipUVs},
            {"generateNormals", s.GenerateNormals}}.dump(), error); break;
    case Operation::Reimport:
    { std::vector<std::string> invalidated; ok = registry.Reimport(request.Id, invalidated, error); break; }
    default: break;
    }
    }
    catch (const std::exception& exception) { error = exception.what(); }
    if (!ok) { Message(error.empty() ? "Asset operation failed." : error, true); return false; }
    Invalidate(editor, registry, before);
    if (request.Type != Operation::Trash) Message("Asset operation completed.");
    else Message(s.Status);
    s.CatalogRevision = UINT64_MAX; s.FilterDirty = true; s.FoldersDirty = true; s.Discovered.clear();
    if (request.Type == Operation::NewFolder)
    {
        s.Folder = ToUtf8((FromUtf8(s.Destination) / FromUtf8(s.Name))); AddFolders(s.Folder);
        s.Expanded.insert(ToUtf8(FromUtf8(s.Folder).parent_path()));
    }
    return true;
}
void FolderTree(jEditorProject& project)
{
    auto& s = State(); BuildFolders(project);
    if (ImGui::Selectable("All assets", s.Folder.empty())) { s.Folder.clear(); s.FilterDirty = true; }
    ImGuiListClipper clip; clip.Begin(static_cast<int>(s.FolderRows.size()));
    while (clip.Step()) for (int index = clip.DisplayStart; index < clip.DisplayEnd; ++index)
    {
        const auto row = s.FolderRows[static_cast<size_t>(index)];
        ImGui::PushID(row.Path.c_str()); ImGui::Indent(float(row.Depth) * 12);
        auto flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (row.Path == s.Folder) flags |= ImGuiTreeNodeFlags_Selected;
        if (!row.Children) flags |= ImGuiTreeNodeFlags_Leaf;
        ImGui::SetNextItemOpen(s.Expanded.count(row.Path) != 0, ImGuiCond_Always);
        const bool expanded = ImGui::TreeNodeEx(row.Label.c_str(), flags);
        if (expanded != (s.Expanded.count(row.Path) != 0))
        { if (expanded) s.Expanded.insert(row.Path); else s.Expanded.erase(row.Path); s.FoldersDirty = true; }
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { s.Folder = row.Path; s.FilterDirty = true; }
        if (ImGui::BeginPopupContextItem("folder_menu"))
        {
            if (ImGui::MenuItem("New folder")) Open({Operation::NewFolder, {}, row.Path, true}, row.Path);
            if (ImGui::MenuItem("Import file")) Open({Operation::Import, {}, row.Path, true}, row.Path);
            if (ImGui::MenuItem("Rename")) Open({Operation::Rename, {}, row.Path, true});
            if (ImGui::MenuItem("Move")) Open({Operation::Move, {}, row.Path, true}, ToUtf8(FromUtf8(row.Path).parent_path()));
            if (ImGui::MenuItem("Move to trash")) Open({Operation::Trash, {}, row.Path, true});
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_ASSET"))
                if (payload->DataSize == 37 && static_cast<const char*>(payload->Data)[36] == 0)
                    if (const auto* asset = Find(static_cast<const char*>(payload->Data)))
                        Open({Operation::Move, asset->Id, asset->SourcePath, false}, row.Path);
            ImGui::EndDragDropTarget();
        }
        ImGui::Unindent(float(row.Depth) * 12); ImGui::PopID();
    }
}
void AssetMenu(const jEditorAssetRecord& asset)
{
    if (!ImGui::BeginPopupContextItem("asset_menu")) return;
    if ((asset.Kind == jEditorAssetKind::Model || asset.Kind == jEditorAssetKind::Scene)
        && ImGui::MenuItem(asset.Kind == jEditorAssetKind::Model ? "Add model instance" : "Open scene", nullptr, false, !asset.Missing))
        State().Deferred.push_back({Operation::Activate, asset.Id, asset.SourcePath});
    if (ImGui::MenuItem("Reimport", nullptr, false, !asset.Missing)) State().Deferred.push_back({Operation::Reimport, asset.Id, asset.SourcePath});
    if (asset.Kind == jEditorAssetKind::Model && ImGui::MenuItem("Import settings")) Open({Operation::Settings, asset.Id, asset.SourcePath});
    if (ImGui::MenuItem("Rename")) Open({Operation::Rename, asset.Id, asset.SourcePath});
    if (ImGui::MenuItem("Move")) Open({Operation::Move, asset.Id, asset.SourcePath});
    if (ImGui::MenuItem("Move to trash")) Open({Operation::Trash, asset.Id, asset.SourcePath});
    ImGui::EndPopup();
}
void Grid(jEditorProject& project, std::string& selected)
{
    auto& s = State();
    if (s.FilterDirty)
    {
        s.Filtered.clear();
        for (size_t i = 0; i < s.Assets.size(); ++i)
        {
            const auto& asset = s.Assets[i];
            if (!s.Folder.empty() && ToUtf8(FromUtf8(asset.SourcePath).parent_path()) != s.Folder) continue;
            if (s.KindFilter && static_cast<int>(asset.Kind) + 1 != s.KindFilter) continue;
            if (!s.Search.PassFilter(asset.SourcePath.c_str())) continue;
            s.Filtered.push_back(i);
        }
        s.FilterDirty = false;
    }
    if (s.Filtered.empty()) { ImGui::TextWrapped("No assets in this view."); return; }
    constexpr float previewBottom = 82, tileHeight = 128, rowHeight = tileHeight + 8;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / 108));
    if (!ImGui::BeginTable("asset_tiles", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) return;
    ImGuiListClipper clip; clip.Begin(static_cast<int>((s.Filtered.size() + columns - 1) / columns), rowHeight);
    while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
    {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        for (int column = 0; column < columns; ++column)
        {
            const size_t index = size_t(row) * columns + column; if (index >= s.Filtered.size()) break;
            ImGui::TableSetColumnIndex(column);
            const auto& asset = s.Assets[s.Filtered[index]];
            ImGui::PushID(asset.Id.c_str());
            const float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
            ImGui::InvisibleButton("asset", ImVec2(width, tileHeight));
            const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) { selected = asset.Id; s.Selected = selected; }
            if (hovered && ImGui::IsMouseDoubleClicked(0)) s.Deferred.push_back({Operation::Activate, asset.Id, asset.SourcePath});
            if (ImGui::BeginDragDropSource())
            { ImGui::SetDragDropPayload("JENGINE_ASSET", asset.Id.c_str(), asset.Id.size() + 1); ImGui::TextUnformatted(asset.SourcePath.c_str()); ImGui::EndDragDropSource(); }
            AssetMenu(asset);
            auto* draw = ImGui::GetWindowDrawList();
            const auto background = selected == asset.Id ? ImGui::GetColorU32(ImGuiCol_Header)
                : hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg);
            draw->AddRectFilled(minimum, maximum, background, 4);
            const auto preview = Thumbnail(asset, project);
            if (preview.Handle && width > 12.f)
            {
                const ImVec2 previewMin(minimum.x + 6, minimum.y + 6), previewMax(maximum.x - 6, minimum.y + previewBottom);
                draw->AddRectFilled(previewMin, previewMax, IM_COL32(20, 23, 29, 255), 3);
                DrawFittedPreview(*draw, preview, previewMin, previewMax);
            }
            else draw->AddText(ImVec2(minimum.x + 6, minimum.y + 24), ImGui::GetColorU32(ImGuiCol_TextDisabled), jEditorAssetKindName(asset.Kind));
            const auto label = ToUtf8(FromUtf8(asset.SourcePath).filename());
            const ImVec4 clipRect(minimum.x + 4, minimum.y + previewBottom + 4, maximum.x - 4, maximum.y - 2);
            if (width > 8.f)
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(clipRect.x, clipRect.y),
                    asset.Missing ? IM_COL32(255, 120, 100, 255) : ImGui::GetColorU32(ImGuiCol_Text), label.c_str(), nullptr, width - 8, &clipRect);
            if (hovered)
            {
                ImGui::BeginTooltip(); ImGui::TextUnformatted(asset.SourcePath.c_str());
                ImGui::Text("%s | %llu bytes | revision %llu", jEditorAssetKindName(asset.Kind),
                    static_cast<unsigned long long>(asset.Fingerprint.Size), static_cast<unsigned long long>(asset.Revision));
                if (preview.Handle) ImGui::Image(preview.Handle, FitPreview(preview, ImVec2(160, 160)));
                for (const auto& missing : asset.MissingDependencies) ImGui::TextColored(ImVec4(1,.4f,.3f,1), "Missing: %s", missing.c_str());
                const auto thumb = s.Thumbnails.find(Key(asset));
                if (thumb != s.Thumbnails.end() && !thumb->second.Error.empty()) ImGui::TextWrapped("Preview: %s", thumb->second.Error.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}
void Details(jEditorProject& project, const std::string& selected)
{
    const auto* asset = Find(selected); if (!asset) return;
    ImGui::TextWrapped("%s", ToUtf8(FromUtf8(asset->SourcePath).filename()).c_str());
    ImGui::TextDisabled("%s", jEditorAssetKindName(asset->Kind));
    const auto preview = Thumbnail(*asset, project, true);
    const auto available = ImGui::GetContentRegionAvail();
    const float edge = std::min({160.f, available.x, std::max(48.f, available.y - ImGui::GetStyle().ItemSpacing.y)});
    if (preview.Handle && edge > 8.f)
    {
        ImGui::Dummy(ImVec2(edge, edge));
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, IM_COL32(20, 23, 29, 255), 3);
        DrawFittedPreview(*draw, preview, ImVec2(min.x + 4, min.y + 4), ImVec2(max.x - 4, max.y - 4));
    }
    const auto status = State().Thumbnails.find(Key(*asset));
    if (status != State().Thumbnails.end() && !status->second.Error.empty())
        ImGui::TextWrapped("%s", status->second.Error.c_str());
    ImGui::TextWrapped("%s", asset->SourcePath.c_str());
    if (asset->Kind == jEditorAssetKind::Model && ImGui::Button("Import settings")) Open({Operation::Settings, asset->Id, asset->SourcePath});
    if (ImGui::Button("Reimport")) State().Deferred.push_back({Operation::Reimport, asset->Id, asset->SourcePath});
    if (!asset->MissingDependencies.empty())
    { ImGui::SeparatorText("Missing dependencies"); for (const auto& missing : asset->MissingDependencies) ImGui::TextWrapped("%s", missing.c_str()); }
}
void Modal(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry, std::string& selected)
{
    auto& s = State();
    if (s.OpenModal) { ImGui::OpenPopup("Asset operation"); s.OpenModal = false; }
    if (!ImGui::BeginPopupModal("Asset operation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto request = s.Modal;
    if (!request.Source.empty()) ImGui::TextWrapped("%s", request.Source.c_str());
    switch (request.Type)
    {
    case Operation::NewFolder: ImGui::InputText("Folder name", s.Name, sizeof(s.Name)); break;
    case Operation::Import:
        ImGui::InputText("Source file (absolute)", s.SourceFile, sizeof(s.SourceFile));
        ImGui::InputText("Project folder", s.Destination, sizeof(s.Destination)); break;
    case Operation::Rename: ImGui::InputText("New name", s.Name, sizeof(s.Name)); break;
    case Operation::Move: ImGui::InputText("Destination folder", s.Destination, sizeof(s.Destination)); break;
    case Operation::Trash: ImGui::TextWrapped("Move this item to recoverable project trash? Referenced files are kept safe."); break;
    case Operation::Settings:
        ImGui::InputFloat("Geometry scale", &s.Scale, .01f, .1f, "%.6g");
        ImGui::Checkbox("Flip texture V", &s.FlipUVs);
        ImGui::Checkbox("Generate missing normals", &s.GenerateNormals);
        if (ImGui::Button("Defaults")) { s.Scale = 1; s.FlipUVs = true; s.GenerateNormals = true; }
        if (!s.SettingsWarning.empty()) ImGui::TextWrapped("%s", s.SettingsWarning.c_str());
        break;
    default: break;
    }
    if (s.StatusError && !s.Status.empty()) ImGui::TextColored(ImVec4(1,.4f,.3f,1), "%s", s.Status.c_str());
    if (ImGui::Button(request.Type == Operation::Trash ? "Move to trash" : "Apply"))
        if (Execute(editor, project, registry, request, selected)) { s.Modal = {}; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { s.Modal = {}; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
}

void Tick(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry)
{
    auto& s = State(); ++s.Frame; ReleaseRetired();
    const auto token = project.GetConfig().Id + "|" + ToUtf8(project.GetRoot());
    if (s.ProjectToken != token)
    {
        if (s.Cancel) s.Cancel->store(true);
        for (const auto& item : s.Thumbnails) if (item.second.Handle) s.Retired.push_back(item.second.Handle);
        s.Thumbnails.clear(); ReleaseRetired(); s.Requests.clear(); s.VisibleKeys.clear();
        s.ProjectToken = token; s.ProjectRoot = ToUtf8(project.GetRoot());
        s.Assets.clear(); s.CatalogRevision = UINT64_MAX; s.Folder.clear(); s.Folders.clear(); s.Expanded.clear(); s.Discovered.clear();
        s.Expanded.insert(project.GetConfig().AssetRoot); s.Expanded.insert("Scenes");
    }
    if (s.CatalogRevision != registry.GetRevision())
    {
        const auto before = Revisions(s.Assets);
        s.Assets = registry.GetAssets();
        if (!before.empty()) Invalidate(editor, registry, before);
        s.CatalogRevision = registry.GetRevision(); s.FilterDirty = true; s.FoldersDirty = true; s.Discovered.clear();
        s.Folders.clear(); AddFolders(project.GetConfig().AssetRoot); AddFolders("Scenes");
        for (const auto& asset : s.Assets) AddFolders(ToUtf8(FromUtf8(asset.SourcePath).parent_path()));
    }
    if (s.Worker.valid() && s.Worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready
        && editor.PendingImportCount() == 0 && ImGui::GetCurrentContext() && ImGui::GetTime() - s.LastUpload >= .1)
    {
        auto result = s.Worker.get(); s.Active.reset();
        const auto* current = registry.FindById(result.Request.Id);
        if (current && result.Request.Key == Key(*current) && s.VisibleKeys.count(result.Request.Key))
        {
            if (s.Thumbnails.size() >= MaxThumbnails)
            {
                auto oldest = std::min_element(s.Thumbnails.begin(), s.Thumbnails.end(), [](const auto& a, const auto& b) { return a.second.Used < b.second.Used; });
                if (oldest->second.Handle) s.Retired.push_back(oldest->second.Handle);
                s.Thumbnails.erase(oldest); ReleaseRetired();
            }
            ThumbnailEntry entry; entry.Error = result.Error; entry.Used = s.Frame;
            entry.Width = result.Pixels.Width; entry.Height = result.Pixels.Height;
            if (!result.Pixels.Rgba.empty())
            { entry.Handle = jEditorUiRenderer::CreateThumbnail(result.Pixels); if (!entry.Handle) entry.Error = "Preview could not be uploaded."; }
            s.Thumbnails.emplace(result.Request.Key, std::move(entry)); s.LastUpload = ImGui::GetTime();
        }
    }
    while (!s.Worker.valid() && !s.Requests.empty() && editor.PendingImportCount() == 0)
    {
        auto request = std::move(s.Requests.front()); s.Requests.pop_front();
        const auto* asset = registry.FindById(request.Id);
        if (!asset || request.Key != Key(*asset) || !s.VisibleKeys.count(request.Key)) continue;
        s.Cancel = std::make_shared<std::atomic<bool>>(false); const auto cancel = s.Cancel;
        s.Active = request;
        try
        {
            s.Worker = std::async(std::launch::async, [request, cancel] {
                ThumbnailResult result; result.Request = request;
                try
                {
                    if (!cancel->load())
                    {
                        if (request.Material) jEditorTools::PrepareMaterialThumbnail(request.ProjectRoot, request.File, result.Pixels, result.Error);
                        else PrepareEditorThumbnail(request.File, result.Pixels, result.Error, 128);
                    }
                }
                catch (const std::exception& exception) { result.Pixels = {}; result.Error = exception.what(); }
                catch (...) { result.Pixels = {}; result.Error = "Preview preparation failed."; }
                if (cancel->load()) { result.Pixels = {}; result.Error = "Cancelled"; }
                return result;
            });
        }
        catch (const std::exception& exception) { s.Active.reset(); Message(exception.what(), true); }
    }
    // Draw calls from either panel mark requests for the next owner tick.
    s.VisibleKeys.clear(); s.VisiblePreviews = 0;
}

void RenderTexturePreview(const jEditorProject* project, const jEditorAssetRegistry* registry,
    const std::string& sourcePath, const ImVec2& size)
{
    ImGui::Dummy(size);
    if (!ImGui::IsItemVisible()) return;
    PreviewImage preview{nullptr, 0, 0, "Open a project to preview textures"};
    if (sourcePath.empty()) preview.Status = "No texture assigned";
    else if (project && registry)
    {
        if (State().ProjectToken != project->GetConfig().Id + "|" + ToUtf8(project->GetRoot()))
            preview.Status = "Preparing asset catalog";
        else if (const auto* asset = registry->FindBySourcePath(sourcePath); asset && asset->Kind == jEditorAssetKind::Texture)
            preview = Thumbnail(*asset, *project, true);
        else preview.Status = "Texture not in asset catalog";
    }
    auto* draw = ImGui::GetWindowDrawList();
    const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    draw->AddRectFilled(min, max, IM_COL32(20, 23, 29, 255), 3);
    if (preview.Handle)
        DrawFittedPreview(*draw, preview, ImVec2(min.x + 2, min.y + 2), ImVec2(max.x - 2, max.y - 2));
    else
    {
        const auto textSize = ImGui::CalcTextSize("--");
        draw->AddText(ImVec2((min.x + max.x - textSize.x) * .5f, (min.y + max.y - textSize.y) * .5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "--");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        if (preview.Handle) ImGui::Image(preview.Handle, FitPreview(preview, ImVec2(180, 180)));
        else ImGui::TextUnformatted(preview.Status);
        if (!sourcePath.empty()) ImGui::TextUnformatted(sourcePath.c_str());
        ImGui::EndTooltip();
    }
}

void Render(jEditor& editor, jEditorProject& project, jEditorAssetRegistry& registry,
    std::string& selected, const ActivateAsset& activate)
{
    auto& s = State();
    if (s.ProjectToken != project.GetConfig().Id + "|" + ToUtf8(project.GetRoot()))
    { ImGui::TextUnformatted("Preparing asset browser..."); return; }
    s.Deferred.clear();
    const auto folder = s.Folder.empty() ? project.GetConfig().AssetRoot : s.Folder;
    if (ImGui::Button("New folder")) Open({Operation::NewFolder, {}, folder, true}, folder);
    ImGui::SameLine(); if (ImGui::Button("Import file")) Open({Operation::Import, {}, folder, true}, folder);
    ImGui::SameLine(); if (ImGui::Button("Refresh")) s.Deferred.push_back({Operation::Refresh});
    const auto work = registry.GetWorkStats();
    if (work.Active) { ImGui::SameLine(); if (ImGui::Button("Cancel scan")) s.Deferred.push_back({Operation::CancelScan}); }
    if (s.Search.Draw("Search", 220)) s.FilterDirty = true;
    ImGui::SameLine(); ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("Kind", &s.KindFilter, "All\0Models\0Textures\0Materials\0Scenes\0Shaders\0Data\0")) s.FilterDirty = true;
    ImGui::BeginChild("asset_body", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
    ImGui::BeginChild("asset_folders", ImVec2(170, 0), true); FolderTree(project); ImGui::EndChild();
    ImGui::SameLine();
    const float available = ImGui::GetContentRegionAvail().x;
    const bool details = Find(selected) && available >= 390.f;
    const float gridWidth = details ? available - std::clamp(available * .33f, 150.f, 230.f) - ImGui::GetStyle().ItemSpacing.x : 0;
    ImGui::BeginChild("asset_grid", ImVec2(gridWidth, 0), true); Grid(project, selected); ImGui::EndChild();
    if (details) { ImGui::SameLine(); ImGui::BeginChild("asset_details", ImVec2(0, 0), true); Details(project, selected); ImGui::EndChild(); }
    ImGui::EndChild(); s.Selected = selected;
    if (!s.Status.empty()) ImGui::TextColored(s.StatusError ? ImVec4(1,.4f,.3f,1) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", s.Status.c_str());
    else ImGui::TextDisabled("%zu assets%s | %zu previews", s.Filtered.size(), work.Active ? " | scanning" : "", s.Thumbnails.size());
    for (const auto request : s.Deferred)
    {
        if (request.Type == Operation::Refresh) { registry.ScanAsync(); s.Discovered.clear(); s.FoldersDirty = true; }
        else if (request.Type == Operation::CancelScan) registry.CancelScan();
        else if (request.Type == Operation::Activate)
        {
            const auto* asset = registry.FindById(request.Id);
            if (asset && !asset->Missing && (asset->Kind == jEditorAssetKind::Model || asset->Kind == jEditorAssetKind::Scene))
            {
                if (activate) activate(request.Id);
                else if (asset->Kind == jEditorAssetKind::Model) editor.AddModel(asset->SourcePath, asset->Id, asset->Revision);
            }
        }
        else if (request.Type == Operation::Reimport) Execute(editor, project, registry, request, selected);
    }
    Modal(editor, project, registry, selected);
}

void Shutdown()
{
    auto& s = State(); if (s.Cancel) s.Cancel->store(true);
    if (s.Worker.valid()) { s.Worker.wait(); try { s.Worker.get(); } catch (...) {} }
    s.Active.reset(); s.Requests.clear();
    // A project swap can happen after this frame's asset draw list was built.
    // Keep those IDs alive; the next owner Tick retires them before any new draw.
    for (const auto& entry : s.Thumbnails) if (entry.second.Handle) s.Retired.push_back(entry.second.Handle);
    s.Thumbnails.clear(); s.Assets.clear(); s.ProjectToken.clear(); s.Modal = {}; s.OpenModal = false;
}
}
#else
namespace jEditorAssetPanel
{
void Tick(jEditor&, jEditorProject&, jEditorAssetRegistry&) {}
void Render(jEditor&, jEditorProject&, jEditorAssetRegistry&, std::string&, const ActivateAsset&) {}
void RenderTexturePreview(const jEditorProject*, const jEditorAssetRegistry*, const std::string&, const ImVec2&) {}
void Shutdown() {}
}
#endif
