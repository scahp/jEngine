#include "pch.h"
#include "jEditorProjectSettings.h"
#include "FileLoader/jEditorAssets.h"
#include "jOptions.h"
#include "jEngine.h"
#include "Renderer/Features/jShadowFiltering.h"
#include "RHI/jRHI.h"
#include "RHI/jRaytracingScene.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <set>
#include <thread>

namespace jEditorProjectSettings
{
namespace
{
namespace fs = std::filesystem;
struct Action { const char* Name; const char* Label; EInputKey Default; };
constexpr std::array<Action, 6> Actions{{
    {"camera.forward", "Forward", EInputKey::W}, {"camera.back", "Back", EInputKey::S},
    {"camera.left", "Left", EInputKey::A}, {"camera.right", "Right", EInputKey::D},
    {"camera.up", "Up (right mouse held)", EInputKey::E}, {"camera.down", "Down (right mouse held)", EInputKey::Q}
}};
constexpr std::array<const char*, 10> RenderKeys{{"render.deferred", "render.surfelgi", "render.surfelgi.inline",
    "render.ssgi", "render.bloom", "render.vsync", "render.surfelgi.intensity", "render.surfelgi.inline.rays",
    "render.shadow.filter", "render.shadow.radiusTexels"}};
struct Prepared
{
    bool Deferred, SurfelGI, Inline, SSGI, Bloom, VSync = false;
    float Intensity;
    int Rays;
    jShadowFiltering::Settings ShadowFilter;
    std::map<std::string, EInputKey> Bindings;
    Prepared()
    {
        const jOptions defaults;
        Deferred = defaults.UseDeferredRenderer; SurfelGI = defaults.UseSurfelGI;
        Inline = defaults.SurfelGIInlineRayEnable; SSGI = defaults.UseSSGI; Bloom = defaults.BloomEyeAdaptation;
        Intensity = defaults.SurfelGIIntensity; Rays = defaults.SurfelGIInlineRayCount;
        jShadowFiltering::TryMakeSettings(defaults.ShadowFilterMode, defaults.ShadowFilterRadiusTexels, ShadowFilter);
        for (const auto& action : Actions) Bindings.emplace(action.Name, action.Default);
    }
};
struct PanelState
{
    const std::thread::id Owner = std::this_thread::get_id();
    bool Visible = false, Initialized = false, Changed = false, PresentModeChange = false;
    jEditorProjectConfig Draft, Original;
    std::map<std::string, EInputKey> Bindings;
    std::vector<std::string> SceneChoices;
    std::string Message;
    char Name[257]{}, StartupScene[4097]{};
};
PanelState& State() { static PanelState state; return state; }
bool Owner() { return std::this_thread::get_id() == State().Owner; }
std::string Upper(std::string text)
{
    for (char& c : text) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return text;
}
const Action* FindAction(const char* name)
{
    if (!name) return nullptr;
    for (const auto& action : Actions) if (std::strcmp(name, action.Name) == 0) return &action;
    return nullptr;
}
bool KnownKey(const std::string& key)
{
    if (std::find(RenderKeys.begin(), RenderKeys.end(), key) != RenderKeys.end()) return true;
    for (const auto& action : Actions) if (key == std::string("input.") + action.Name) return true;
    return false;
}
bool Boolean(const std::map<std::string, std::string>& values, const char* key, bool& output, std::string& error)
{
    const auto found = values.find(key);
    if (found == values.end()) return true;
    if (found->second != "true" && found->second != "false") { error = std::string(key) + " must be true or false"; return false; }
    output = found->second == "true";
    return true;
}
bool ValidUtf8(const std::string& text)
{
    for (size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) continue;
        int count = 0; uint32_t value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - i < size_t(count)) return false;
        for (int n = 0; n < count; ++n)
        {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
bool Prepare(const jEditorProjectConfig& config, Prepared& prepared, std::string& error)
{
    error.clear();
    if (config.Name.empty() || config.Name.size() > 256 || config.Name.find('\0') != std::string::npos || !ValidUtf8(config.Name))
    { error = "Project name must contain 1..256 valid UTF-8 bytes without NUL"; return false; }
    for (unsigned char c : config.Name) if (c < 0x20 || c == 0x7f) { error = "Project name cannot contain control characters"; return false; }
    if (config.Settings.size() > 1024) { error = "Project setting count limit exceeded"; return false; }
    for (const auto& [key, value] : config.Settings)
        if (key.empty() || key.size() > 256 || value.size() > 65536 || key.find('\0') != std::string::npos || value.find('\0') != std::string::npos || !ValidUtf8(key) || !ValidUtf8(value))
        { error = "Project setting text exceeds limits, contains NUL or is invalid UTF-8"; return false; }
    if (!Boolean(config.Settings, "render.deferred", prepared.Deferred, error)
        || !Boolean(config.Settings, "render.surfelgi", prepared.SurfelGI, error)
        || !Boolean(config.Settings, "render.surfelgi.inline", prepared.Inline, error)
        || !Boolean(config.Settings, "render.ssgi", prepared.SSGI, error)
        || !Boolean(config.Settings, "render.bloom", prepared.Bloom, error)
        || !Boolean(config.Settings, "render.vsync", prepared.VSync, error)) return false;
    if (auto found = config.Settings.find("render.surfelgi.intensity"); found != config.Settings.end())
    {
        float value = 0;
        const auto parsed = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
        if (parsed.ec != std::errc() || parsed.ptr != found->second.data() + found->second.size() || !std::isfinite(value) || value < 0 || value > 8)
        { error = "render.surfelgi.intensity must be a finite number from 0 to 8"; return false; }
        prepared.Intensity = value;
    }
    if (auto found = config.Settings.find("render.surfelgi.inline.rays"); found != config.Settings.end())
    {
        int value = 0;
        const auto parsed = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
        if (parsed.ec != std::errc() || parsed.ptr != found->second.data() + found->second.size() || value < 1 || value > 16)
        { error = "render.surfelgi.inline.rays must be an integer from 1 to 16"; return false; }
        prepared.Rays = value;
    }
    int shadowMode = static_cast<int>(prepared.ShadowFilter.Method);
    float shadowRadius = prepared.ShadowFilter.RadiusTexels;
    if (auto found = config.Settings.find("render.shadow.filter"); found != config.Settings.end())
    {
        const auto parsed = std::from_chars(found->second.data(), found->second.data() + found->second.size(), shadowMode);
        if (parsed.ec != std::errc() || parsed.ptr != found->second.data() + found->second.size())
        { error = "render.shadow.filter must be 0 (existing) or 1 (Poisson PCF)"; return false; }
    }
    if (auto found = config.Settings.find("render.shadow.radiusTexels"); found != config.Settings.end())
    {
        const auto parsed = std::from_chars(found->second.data(), found->second.data() + found->second.size(), shadowRadius);
        if (parsed.ec != std::errc() || parsed.ptr != found->second.data() + found->second.size())
        { error = "render.shadow.radiusTexels must be a finite number from 0 to 4"; return false; }
    }
    if (!jShadowFiltering::TryMakeSettings(shadowMode, shadowRadius, prepared.ShadowFilter))
    { error = "Shadow filtering requires render.shadow.filter 0 or 1 and finite render.shadow.radiusTexels from 0 to 4"; return false; }
    std::map<EInputKey, std::string> used;
    for (const auto& action : Actions)
    {
        auto key = action.Default;
        if (auto found = config.Settings.find(std::string("input.") + action.Name); found != config.Settings.end())
            if (!ParseKey(found->second, key)) { error = std::string("Invalid portable camera key for ") + action.Name; return false; }
        auto [existing, unique] = used.emplace(key, action.Name);
        if (!unique) { error = std::string(action.Name) + " conflicts with " + existing->second; return false; }
        prepared.Bindings[action.Name] = key;
    }
    return true;
}
bool ValidatePaths(const jEditorProjectConfig& config, const fs::path& root, std::string& error)
{
    try
    {
        const fs::path base = root.empty() ? fs::current_path() : root;
        const auto portable = [](const std::string& value) {
            return ValidUtf8(value) && value.find('\\') == std::string::npos && value.find('\0') == std::string::npos
                && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
        };
        if (config.AssetRoot.empty() || !portable(config.AssetRoot) || !portable(config.StartupScene))
        { error = "Project paths must use portable forward-slash text without controls"; return false; }
        if (!jEditorProject::ResolveSafe(base, jEditorAssetPath::FromUtf8(config.AssetRoot), error)) return false;
        if (config.StartupScene.empty()) return true;
        if (config.StartupScene.size() > 4096) { error = "Startup scene path exceeds 4096 bytes"; return false; }
        const auto relative = jEditorAssetPath::FromUtf8(config.StartupScene);
        if (relative.extension() != ".jscene") { error = "Startup scene must be a project-relative .jscene file"; return false; }
        const auto resolved = jEditorProject::ResolveSafe(base, relative, error, !root.empty());
        if (!resolved) return false;
        if (!root.empty() && !fs::is_regular_file(*resolved)) { error = "Startup scene must be an existing regular scene file"; return false; }
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
void Publish(Prepared&& prepared)
{
    auto& state = State();
    const bool vsync = prepared.VSync && GRHISupportVsync;
    if (GUseVsync != vsync) state.PresentModeChange = true;
    gOptions.UseDeferredRenderer = prepared.Deferred;
    gOptions.ShadowFilterMode = static_cast<int>(prepared.ShadowFilter.Method);
    gOptions.ShadowFilterRadiusTexels = prepared.ShadowFilter.RadiusTexels;
    gOptions.UseSurfelGI = prepared.SurfelGI;
    gOptions.SurfelGIInlineRayEnable = prepared.Inline && GSupportInlineRaytracing;
    gOptions.UseSSGI = prepared.SSGI;
    gOptions.BloomEyeAdaptation = prepared.Bloom;
    gOptions.SurfelGIIntensity = prepared.Intensity;
    gOptions.SurfelGIInlineRayCount = prepared.Rays;
    GUseVsync = vsync;
    state.Bindings = std::move(prepared.Bindings);
}
bool SameConfig(const jEditorProjectConfig& a, const jEditorProjectConfig& b)
{
    return a.Id == b.Id && a.Name == b.Name && a.AssetRoot == b.AssetRoot && a.StartupScene == b.StartupScene && a.Settings == b.Settings;
}
void RefreshScenes(const jEditorProject& project)
{
    auto& state = State(); state.SceneChoices.clear();
    std::error_code ec;
    size_t visited = 0;
    for (fs::recursive_directory_iterator it(project.GetRoot(), fs::directory_options::skip_permission_denied, ec), end;
        !ec && it != end && visited < 8192 && state.SceneChoices.size() < 1024; it.increment(ec))
    {
        ++visited;
        if (it->is_symlink(ec)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
        const auto filename = it->path().filename().native();
        if (it->is_directory(ec))
        {
            if (!filename.empty() && filename.front() == fs::path::value_type('.')) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || it->path().extension() != ".jscene") continue;
        const auto relative = it->path().lexically_relative(project.GetRoot());
        const auto text = relative.generic_u8string();
        state.SceneChoices.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    std::sort(state.SceneChoices.begin(), state.SceneChoices.end());
}
void LoadDraft(const jEditorProject& project)
{
    auto& state = State();
    state.Draft = project.GetConfig(); state.Original = project.GetConfig(); state.Initialized = true; state.Changed = false;
    std::snprintf(state.Name, sizeof(state.Name), "%s", state.Draft.Name.c_str());
    std::snprintf(state.StartupScene, sizeof(state.StartupScene), "%s", state.Draft.StartupScene.c_str());
    state.Message.clear(); RefreshScenes(project);
}
void BoolWidget(const char* label, const char* setting, bool initial)
{
    auto& state = State(); bool value = initial;
    if (auto found = state.Draft.Settings.find(setting); found != state.Draft.Settings.end()) value = found->second == "true";
    if (ImGui::Checkbox(label, &value)) { state.Draft.Settings[setting] = value ? "true" : "false"; state.Changed = true; }
}
std::vector<EInputKey> BindableKeys()
{
    std::vector<EInputKey> result;
    for (int value = int(EInputKey::A); value <= int(EInputKey::NUM9); ++value) result.push_back(EInputKey(value));
    for (auto key : {EInputKey::LEFT, EInputKey::RIGHT, EInputKey::UP, EInputKey::DOWN, EInputKey::SPACE}) result.push_back(key);
    return result;
}
} // namespace

const char* KeyName(EInputKey key)
{
    static constexpr const char* letters[] = {"A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"};
    static constexpr const char* digits[] = {"0","1","2","3","4","5","6","7","8","9"};
    if (key >= EInputKey::A && key <= EInputKey::Z) return letters[int(key) - int(EInputKey::A)];
    if (key >= EInputKey::NUM0 && key <= EInputKey::NUM9) return digits[int(key) - int(EInputKey::NUM0)];
    switch (key)
    {
    case EInputKey::LEFT: return "LEFT";
    case EInputKey::RIGHT: return "RIGHT";
    case EInputKey::UP: return "UP";
    case EInputKey::DOWN: return "DOWN";
    case EInputKey::SPACE: return "SPACE";
    default: return nullptr;
    }
}
bool ParseKey(const std::string& name, EInputKey& key)
{
    const std::string canonical = Upper(name);
    if (canonical.size() == 1 && canonical[0] >= 'A' && canonical[0] <= 'Z') { key = EInputKey(int(EInputKey::A) + canonical[0] - 'A'); return true; }
    if (canonical.size() == 1 && canonical[0] >= '0' && canonical[0] <= '9') { key = EInputKey(int(EInputKey::NUM0) + canonical[0] - '0'); return true; }
    for (auto candidate : {EInputKey::LEFT, EInputKey::RIGHT, EInputKey::UP, EInputKey::DOWN, EInputKey::SPACE})
        if (canonical == KeyName(candidate)) { key = candidate; return true; }
    return false;
}
bool Validate(const jEditorProjectConfig& config, std::string& error, const fs::path& root)
{
    Prepared prepared;
    return Prepare(config, prepared, error) && ValidatePaths(config, root, error);
}
bool Validate(const jEditorProject& project, std::string& error)
{
    if (project.GetRoot().empty()) { error = "No project is open"; return false; }
    return Validate(project.GetConfig(), error, project.GetRoot());
}
bool Apply(const jEditorProject& project, jEditor& editor, std::string& error)
{
    (void)editor;
    if (!Owner()) { error = "Project settings must apply on the editor owner thread"; return false; }
    Prepared prepared;
    if (project.GetRoot().empty()) { error = "No project is open"; return false; }
    if (!Prepare(project.GetConfig(), prepared, error) || !ValidatePaths(project.GetConfig(), project.GetRoot(), error)) return false;
    Publish(std::move(prepared)); error.clear(); return true;
}
bool Save(jEditorProject& project, jEditor& editor, const jEditorProjectConfig& candidate, std::string& error)
{
    (void)editor;
    if (!Owner()) { error = "Project settings must save on the editor owner thread"; return false; }
    if (project.GetRoot().empty()) { error = "No project is open"; return false; }
    if (candidate.Id != project.GetConfig().Id || candidate.AssetRoot != project.GetConfig().AssetRoot)
    { error = "Project identity and asset root cannot be changed in project settings"; return false; }
    jEditorProject proposed = project;
    proposed.EditConfig() = candidate;
    for (const auto& [key, value] : project.GetConfig().Settings)
        if (!KnownKey(key)) proposed.EditConfig().Settings.emplace(key, value);
    Prepared prepared;
    if (!Prepare(proposed.GetConfig(), prepared, error) || !ValidatePaths(proposed.GetConfig(), proposed.GetRoot(), error)) return false;
    try { if (!proposed.Save(error)) return false; }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    project = std::move(proposed); Publish(std::move(prepared)); error.clear(); return true;
}
void SetVisible(bool visible) { if (Owner()) State().Visible = visible; }
bool IsVisible() { return Owner() && State().Visible; }
bool ConsumePresentModeChange()
{
    if (!Owner()) return false;
    const bool changed = State().PresentModeChange; State().PresentModeChange = false; return changed;
}
EInputKey Binding(const char* actionName, EInputKey fallback)
{
    if (!Owner() || !FindAction(actionName)) return fallback;
    auto found = State().Bindings.find(actionName);
    return found == State().Bindings.end() ? fallback : found->second;
}
bool ActionDown(const char* actionName, EInputKey fallback)
{
    if (!Owner() || IsKeyDown(EInputKey::SHIFT) || IsKeyDown(EInputKey::CTRL) || IsKeyDown(EInputKey::ALT)) return false;
    const auto key = Binding(actionName, fallback);
    return key > EInputKey::UNKNOWN && key < EInputKey::MAX && IsKeyDown(key);
}
void Render(jEditor& editor, jEditorProject* project)
{
    if (!Owner() || !State().Visible || !ImGui::GetCurrentContext()) return;
    auto& state = State();
    ImGui::SetNextWindowSize(ImVec2(560, 610), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &state.Visible)) { ImGui::End(); return; }
    if (!project || project->GetRoot().empty()) { ImGui::TextWrapped("Create or open a project to edit renderer defaults, camera keys and the startup scene."); ImGui::End(); return; }
    if (!state.Initialized || state.Draft.Id != project->GetConfig().Id || (!state.Changed && !SameConfig(state.Original, project->GetConfig()))) LoadDraft(*project);
    Prepared requested; std::string draftError;
    const bool validValues = Prepare(state.Draft, requested, draftError);
    const auto projectText = project->GetProjectFile().generic_u8string();
    ImGui::TextWrapped("%s", reinterpret_cast<const char*>(projectText.c_str()));
    ImGui::TextDisabled("Changes take effect only after Save and Apply.");
    if (ImGui::InputText("Project name", state.Name, sizeof(state.Name))) { state.Draft.Name = state.Name; state.Changed = true; }
    ImGui::Text("Asset root: %s", state.Draft.AssetRoot.c_str());
    if (ImGui::CollapsingHeader("Startup scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::InputText("Relative .jscene path", state.StartupScene, sizeof(state.StartupScene)))
        { state.Draft.StartupScene = state.StartupScene; state.Changed = true; }
        if (ImGui::BeginCombo("Scene picker", state.Draft.StartupScene.empty() ? "No startup scene" : state.Draft.StartupScene.c_str()))
        {
            if (ImGui::Selectable("No startup scene", state.Draft.StartupScene.empty()))
            { state.Draft.StartupScene.clear(); state.StartupScene[0] = 0; state.Changed = true; }
            for (const auto& scene : state.SceneChoices)
                if (ImGui::Selectable(scene.c_str(), scene == state.Draft.StartupScene))
                { state.Draft.StartupScene = scene; std::snprintf(state.StartupScene, sizeof(state.StartupScene), "%s", scene.c_str()); state.Changed = true; }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Refresh scene list")) RefreshScenes(*project);
        ImGui::TextDisabled("Project-relative paths only; the selected file must exist.");
    }
    if (ImGui::CollapsingHeader("Renderer defaults", ImGuiTreeNodeFlags_DefaultOpen))
    {
        BoolWidget("Deferred renderer", "render.deferred", requested.Deferred);
        if (ImGui::TreeNodeEx("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            constexpr const char* filters[] = {"Existing", "Poisson PCF"};
            int mode = static_cast<int>(requested.ShadowFilter.Method);
            float radius = requested.ShadowFilter.RadiusTexels;
            ImGui::PushItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x
                - ImGui::CalcTextSize("Radius in texels").x - ImGui::GetStyle().ItemInnerSpacing.x));
            const bool modeChanged = ImGui::Combo("Filter", &mode, filters, 2);
            const bool radiusChanged = ImGui::SliderFloat("Radius in texels", &radius, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopItemWidth();
            jShadowFiltering::Settings validated;
            if ((modeChanged || radiusChanged) && jShadowFiltering::TryMakeSettings(mode, radius, validated))
            {
                state.Draft.Settings["render.shadow.filter"] = std::to_string(static_cast<int>(validated.Method));
                state.Draft.Settings["render.shadow.radiusTexels"] = std::to_string(validated.RadiusTexels);
                state.Changed = true;
            }
            const bool pathTracing = g_Engine && g_Engine->Game.IsUsingPathTracingRenderer();
            const bool hwrt = g_Engine && !pathTracing
                && g_Engine->Game.GetActiveSceneRenderPipeline() == jGame::ESceneRenderPipeline::Deferred
                && gOptions.UseHWRTDirectLighting && gOptions.UseRaytracing && GSupportRaytracing
                && g_rhi && g_rhi->RaytracingScene && g_rhi->RaytracingScene->IsValid();
            if (g_Engine) ImGui::Text("Active renderer: %s", g_Engine->Game.GetActiveSceneRenderPipelineName());
            if (pathTracing) ImGui::TextWrapped("Path Tracing is active; these settings are retained for raster shadow maps.");
            else if (hwrt) ImGui::TextWrapped("HWRT direct lighting is active; ray-traced shadows bypass this filter.");
            else ImGui::TextWrapped("Shadow-map filtering only. Path Tracing and HWRT rays do not use this filter.");
            ImGui::TreePop();
        }
        BoolWidget("SurfelGI", "render.surfelgi", requested.SurfelGI);
        BoolWidget("Inline ray gather (requested)", "render.surfelgi.inline", requested.Inline);
        ImGui::TextColored(GSupportInlineRaytracing ? ImVec4(.4f,1,.4f,1) : ImVec4(1,.75f,.25f,1), "%s",
            GSupportInlineRaytracing ? "Inline ray query is supported by the active device." : "Inline ray query is unsupported; this request is retained, effective state is disabled.");
        BoolWidget("SSGI", "render.ssgi", requested.SSGI);
        BoolWidget("Bloom and eye adaptation", "render.bloom", requested.Bloom);
        BoolWidget("VSync (requested)", "render.vsync", requested.VSync);
        if (!GRHISupportVsync) ImGui::TextWrapped("VSync control is unavailable on the active RHI; the requested value is retained.");
        float intensity = requested.Intensity;
        if (ImGui::SliderFloat("SurfelGI intensity", &intensity, 0, 8)) { state.Draft.Settings["render.surfelgi.intensity"] = std::to_string(intensity); state.Changed = true; }
        int rays = requested.Rays;
        if (ImGui::SliderInt("Inline rays per surfel", &rays, 1, 16)) { state.Draft.Settings["render.surfelgi.inline.rays"] = std::to_string(rays); state.Changed = true; }
        ImGui::TextWrapped("Saved renderer defaults are shared by Scene and Game. Ray-based lighting bypasses shadow-map filtering.");
    }
    if (ImGui::CollapsingHeader("Editor camera keys", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const auto keys = BindableKeys();
        for (const auto& action : Actions)
        {
            EInputKey key = requested.Bindings[action.Name];
            const char* current = KeyName(key);
            if (ImGui::BeginCombo(action.Label, current ? current : "Invalid key"))
            {
                for (auto candidate : keys)
                    if (ImGui::Selectable(KeyName(candidate), candidate == key))
                    { state.Draft.Settings[std::string("input.") + action.Name] = KeyName(candidate); state.Changed = true; }
                ImGui::EndCombo();
            }
        }
        ImGui::TextWrapped("Viewport focus still applies. Ctrl, Alt and Shift chords never move the camera. These bindings control the editor camera, not game input.");
    }
    if (!validValues) ImGui::TextColored(ImVec4(1,.35f,.25f,1), "%s", draftError.c_str());
    if (state.Changed) ImGui::TextUnformatted("Unsaved project settings");
    if (ImGui::Button("Save and Apply"))
    {
        std::string error;
        if (Save(*project, editor, state.Draft, error)) { LoadDraft(*project); state.Message = "Project settings saved and applied."; }
        else state.Message = "Settings were not saved: " + error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert edits")) LoadDraft(*project);
    if (ImGui::Button("Use engine defaults"))
    {
        for (auto it = state.Draft.Settings.begin(); it != state.Draft.Settings.end();)
            if (KnownKey(it->first)) it = state.Draft.Settings.erase(it); else ++it;
        state.Changed = true;
    }
    if (!state.Message.empty()) ImGui::TextWrapped("%s", state.Message.c_str());
    ImGui::End();
}
} // namespace jEditorProjectSettings
