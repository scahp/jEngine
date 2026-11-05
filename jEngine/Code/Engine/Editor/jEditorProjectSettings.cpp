#include "pch.h"
#include "jEditorProjectSettings.h"
#include "FileLoader/jEditorAssets.h"
#include "jOptions.h"
#include "jEngine.h"
#include "Renderer/Features/jShadowFiltering.h"
#include "Renderer/jAdvancedShadowPass.h"
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
constexpr const char* AdvancedShadowKeys[] = {
    "render.shadow.projection", "render.shadow.pointProjection", "render.shadow.representation", "render.shadow.resolution",
    "render.shadow.cascade.count", "render.shadow.cascade.lambda", "render.shadow.maxDistance", "render.shadow.cascade.stabilize",
    "render.shadow.casterPadding", "render.shadow.moments.positiveExponent", "render.shadow.moments.negativeExponent",
    "render.shadow.moments.minimumVariance", "render.shadow.moments.bleedReduction", "render.shadow.moments.blurRadius",
    "render.shadow.deep.opacityMultiplier", "render.shadow.deep.tauMax", "render.shadow.deep.maxDistance",
    "render.shadow.volume.maxTriangles", "render.shadow.volume.range"};
constexpr const char* RendererFeatureKeys[] = {
    "render.msaa.samples", "render.sss.enabled", "render.sss.strength", "render.sss.radiusScale",
    "render.sss.depthRejectWorld", "render.sss.normalThreshold", "render.sss.maxRadiusPixels",
    "render.hiz.enabled", "render.hiz.depthBias", "render.hiz.maxCandidates",
    "render.textureSubsurface.tileResolution", "render.textureSubsurface.captureResolution",
    "render.textureSubsurface.gaussianStepScale", "render.textureSubsurface.worldUnitsPerMillimeter",
    "render.textureSubsurface.enableTransmission", "render.textureSubsurface.transmissionExtinctionPerMillimeter",
    "render.textureSubsurface.transmissionThicknessScale", "render.textureSubsurface.captureBiasWorld"};
constexpr const char* SceneKeys[] = {
    "render.environment.enabled", "render.environment.intensity", "render.environment.combineGI", "render.environment.shAsset",
    "render.ppr.enabled", "render.ppr.fillHoles", "render.ppr.plane", "render.ppr.thickness", "render.ppr.normalThreshold",
    "render.ppr.intensity", "render.ppr.roughness", "render.ppr.edgeFade", "render.ppr.fallbackLod", "render.ppr.fallbackRadiance",
    "render.atmosphere.enabled", "render.atmosphere.intensity", "render.atmosphere.metersPerUnit", "render.atmosphere.groundHeight",
    "render.atmosphere.heightMeters", "render.atmosphere.maxDistanceMeters", "render.atmosphere.rayleighHeight",
    "render.atmosphere.mieHeight", "render.atmosphere.rayleigh", "render.atmosphere.mie", "render.atmosphere.anisotropy",
    "render.atmosphere.sun", "render.atmosphere.solar", "render.atmosphere.viewSamples", "render.atmosphere.lightSamples",
    "render.atmosphere.useSceneLight"};
struct Prepared
{
    bool Deferred, SurfelGI, Inline, SSGI, Bloom, VSync = false;
    float Intensity;
    int Rays;
    jShadowFiltering::Settings ShadowFilter;
    jAdvancedShadows::Settings AdvancedShadows;
    int MSAASamples = 1;
    jSubsurface::Settings Subsurface;
    jTextureSubsurface::Settings TextureSubsurface;
    jHiZOcclusion::Settings HiZOcclusion;
    jSceneFeatures::Settings SceneFeatures;
    std::string EnvironmentAsset;
    std::map<std::string, EInputKey> Bindings;
    Prepared()
    {
        const jOptions defaults;
        Deferred = defaults.UseDeferredRenderer; SurfelGI = defaults.UseSurfelGI;
        Inline = defaults.SurfelGIInlineRayEnable; SSGI = defaults.UseSSGI; Bloom = defaults.BloomEyeAdaptation;
        Intensity = defaults.SurfelGIIntensity; Rays = defaults.SurfelGIInlineRayCount;
        jShadowFiltering::TryMakeSettings(defaults.ShadowFilterMode, defaults.ShadowFilterRadiusTexels, ShadowFilter);
        SceneFeatures = defaults.SceneFeatures;
        MSAASamples = defaults.RequestedMSAASamples; Subsurface = defaults.Subsurface; HiZOcclusion = defaults.HiZOcclusion;
        TextureSubsurface = defaults.TextureSubsurface;
        for (const auto& action : Actions) Bindings.emplace(action.Name, action.Default);
    }
};
bool PrepareRendererFeatures(const jEditorProjectConfig& config, Prepared& prepared, std::string& error);
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
    if (std::find(std::begin(AdvancedShadowKeys), std::end(AdvancedShadowKeys), key) != std::end(AdvancedShadowKeys)) return true;
    if (std::find(std::begin(RendererFeatureKeys), std::end(RendererFeatureKeys), key) != std::end(RendererFeatureKeys)) return true;
    if (std::find(std::begin(SceneKeys), std::end(SceneKeys), key) != std::end(SceneKeys)) return true;
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
bool Prepare(const jEditorProjectConfig& config, Prepared& prepared, std::string& error, const fs::path& root = {})
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
    if (!ReadAdvancedShadows(config, prepared.AdvancedShadows, error)) return false;
    if (!PrepareRendererFeatures(config, prepared, error)) return false;
    if (!ReadSceneFeatures(config, root, prepared.SceneFeatures, prepared.EnvironmentAsset, error)) return false;
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
    gOptions.AdvancedShadows = prepared.AdvancedShadows;
    gOptions.RequestedMSAASamples = prepared.MSAASamples;
    gOptions.Subsurface = prepared.Subsurface;
    gOptions.TextureSubsurface = prepared.TextureSubsurface;
    gOptions.HiZOcclusion = prepared.HiZOcclusion;
    gOptions.SceneFeatures = prepared.SceneFeatures;
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

namespace
{
bool FeatureFloat(const std::map<std::string,std::string>& values, const char* key, float& out, std::string& error)
{
    const auto found=values.find(key); if(found==values.end())return true;
    const auto& text=found->second;
    float candidate=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),candidate);
    if(text.empty() || text.size()>256 || parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size() || !std::isfinite(candidate))
    {error=std::string(key)+" must be a finite number without trailing text";return false;}
    out=candidate;return true;
}
bool FeatureUnsigned(const std::map<std::string,std::string>& values,const char* key,std::uint32_t& out,std::string& error)
{
    const auto found=values.find(key);if(found==values.end())return true;
    const auto& text=found->second;std::uint32_t candidate=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),candidate);
    if(text.empty() || text.size()>32 || parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size())
    {error=std::string(key)+" must be an unsigned integer";return false;}
    out=candidate;return true;
}
bool PrepareRendererFeatures(const jEditorProjectConfig& config, Prepared& prepared, std::string& error)
{
    auto sss = prepared.Subsurface;
    auto hiz = prepared.HiZOcclusion;
    std::uint32_t samples = std::uint32_t(prepared.MSAASamples);
    const auto& values = config.Settings;
    if (!FeatureUnsigned(values, "render.msaa.samples", samples, error)) return false;
    if (samples != 1 && samples != 2 && samples != 4)
    { error = "render.msaa.samples must be 1, 2 or 4"; return false; }
    if (!Boolean(values, "render.sss.enabled", sss.Enabled, error)
        || !FeatureFloat(values, "render.sss.strength", sss.Strength, error)
        || !FeatureFloat(values, "render.sss.radiusScale", sss.RadiusScale, error)
        || !FeatureFloat(values, "render.sss.depthRejectWorld", sss.DepthRejectWorld, error)
        || !FeatureFloat(values, "render.sss.normalThreshold", sss.NormalThreshold, error)
        || !FeatureFloat(values, "render.sss.maxRadiusPixels", sss.MaxRadiusPixels, error)
        || !Boolean(values, "render.hiz.enabled", hiz.Enabled, error)
        || !FeatureFloat(values, "render.hiz.depthBias", hiz.DepthBias, error)
        || !FeatureUnsigned(values, "render.hiz.maxCandidates", hiz.MaxCandidates, error)) return false;
    jSubsurface::Settings checkedSSS; jHiZOcclusion::Settings checkedHiZ;
    if (!jSubsurface::ValidateSettings(sss, checkedSSS, error) || !jHiZOcclusion::ValidateSettings(hiz, checkedHiZ, error)) return false;
    if (!ReadTextureSubsurface(config, prepared.TextureSubsurface, error)) return false;
    prepared.MSAASamples = int(samples); prepared.Subsurface = checkedSSS; prepared.HiZOcclusion = checkedHiZ;
    return true;
}
template<std::size_t N> bool FeatureVector(const std::map<std::string,std::string>& values,const char* key,
    std::array<float,N>& out,std::string& error)
{
    const auto found=values.find(key);if(found==values.end())return true;
    const auto& text=found->second;
    if(text.empty() || text.size()>512){error=std::string(key)+" vector exceeds text limits";return false;}
    std::array<float,N> candidate{};std::size_t begin=0;
    for(std::size_t i=0;i<N;++i)
    {
        const auto end=text.find(',',begin);
        if((i+1<N && end==std::string::npos) || (i+1==N && end!=std::string::npos))
        {error=std::string(key)+" requires exactly "+std::to_string(N)+" comma-separated numbers";return false;}
        const auto limit=end==std::string::npos?text.size():end;
        const auto parsed=std::from_chars(text.data()+begin,text.data()+limit,candidate[i]);
        if(begin==limit || parsed.ec!=std::errc() || parsed.ptr!=text.data()+limit || !std::isfinite(candidate[i]))
        {error=std::string(key)+" contains an invalid vector component";return false;}
        begin=limit+1;
    }
    out=candidate;return true;
}
std::string FeatureText(float value)
{
    char text[64]{};
    const auto result=std::to_chars(text,text+sizeof(text),value,std::chars_format::general,std::numeric_limits<float>::max_digits10);
    if(result.ec!=std::errc())throw std::runtime_error("Cannot encode scene feature number");
    return std::string(text,result.ptr);
}
template<std::size_t N> std::string FeatureText(const std::array<float,N>& values)
{
    std::string text;
    for(std::size_t i=0;i<N;++i){if(i)text+=',';text+=FeatureText(values[i]);}
    return text;
}
bool FeatureAssetPath(const std::string& value,const fs::path& root,std::string& error)
{
    if(value.empty())return true;
    if(value.size()>4096 || !ValidUtf8(value) || value.find('\\')!=std::string::npos
        || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<0x20 || c==0x7f;}))
    {error="Environment SH asset must be a portable project-relative .jsh path";return false;}
    const auto relative=jEditorAssetPath::FromUtf8(value);
    if(relative.extension()!=".jsh"){error="Environment SH asset must use .jsh";return false;}
    return bool(jEditorProject::ResolveSafe(root.empty()?fs::current_path():root,relative,error,!root.empty()));
}
}

bool ReadTextureSubsurface(const jEditorProjectConfig& config, jTextureSubsurface::Settings& output, std::string& error)
{
    jTextureSubsurface::Settings candidate;
    const auto& values = config.Settings;
    if (!FeatureUnsigned(values, "render.textureSubsurface.tileResolution", candidate.TileResolution, error)
        || !FeatureUnsigned(values, "render.textureSubsurface.captureResolution", candidate.CaptureResolution, error)
        || !FeatureFloat(values, "render.textureSubsurface.gaussianStepScale", candidate.GaussianStepScale, error)
        || !FeatureFloat(values, "render.textureSubsurface.worldUnitsPerMillimeter", candidate.WorldUnitsPerMillimeter, error)
        || !Boolean(values, "render.textureSubsurface.enableTransmission", candidate.EnableTransmission, error)
        || !FeatureFloat(values, "render.textureSubsurface.transmissionExtinctionPerMillimeter", candidate.TransmissionExtinctionPerMillimeter, error)
        || !FeatureFloat(values, "render.textureSubsurface.transmissionThicknessScale", candidate.TransmissionThicknessScale, error)
        || !FeatureFloat(values, "render.textureSubsurface.captureBiasWorld", candidate.CaptureBiasWorld, error)) return false;
    return jTextureSubsurface::ValidateSettings(candidate, output, error);
}
bool WriteTextureSubsurface(jEditorProjectConfig& config, const jTextureSubsurface::Settings& input, std::string& error)
{
    jTextureSubsurface::Settings checked;
    if (!jTextureSubsurface::ValidateSettings(input, checked, error)) return false;
    auto candidate = config.Settings;
    candidate["render.textureSubsurface.tileResolution"] = std::to_string(checked.TileResolution);
    candidate["render.textureSubsurface.captureResolution"] = std::to_string(checked.CaptureResolution);
    candidate["render.textureSubsurface.gaussianStepScale"] = FeatureText(checked.GaussianStepScale);
    candidate["render.textureSubsurface.worldUnitsPerMillimeter"] = FeatureText(checked.WorldUnitsPerMillimeter);
    candidate["render.textureSubsurface.enableTransmission"] = checked.EnableTransmission ? "true" : "false";
    candidate["render.textureSubsurface.transmissionExtinctionPerMillimeter"] = FeatureText(checked.TransmissionExtinctionPerMillimeter);
    candidate["render.textureSubsurface.transmissionThicknessScale"] = FeatureText(checked.TransmissionThicknessScale);
    candidate["render.textureSubsurface.captureBiasWorld"] = FeatureText(checked.CaptureBiasWorld);
    config.Settings = std::move(candidate); return true;
}
bool ReadAdvancedShadows(const jEditorProjectConfig& config, jAdvancedShadows::Settings& output, std::string& error)
{
    jAdvancedShadows::Settings candidate;
    const auto& values = config.Settings;
    std::uint32_t projection = 0, point = 0, representation = 0;
    if (!FeatureUnsigned(values, "render.shadow.projection", projection, error)
        || !FeatureUnsigned(values, "render.shadow.pointProjection", point, error)
        || !FeatureUnsigned(values, "render.shadow.representation", representation, error)) return false;
    candidate.DirectionalProjection = jAdvancedShadows::Projection(projection);
    candidate.PointProjection = jAdvancedShadows::PointProjection(point);
    candidate.Method = jAdvancedShadows::Representation(representation);
#define JE_SHADOW_READ_INT(key, field) if (!FeatureUnsigned(values, "render.shadow." key, candidate.field, error)) return false
#define JE_SHADOW_READ_FLOAT(key, field) if (!FeatureFloat(values, "render.shadow." key, candidate.field, error)) return false
    JE_SHADOW_READ_INT("resolution", MapResolution);
    JE_SHADOW_READ_INT("cascade.count", CascadeCount);
    JE_SHADOW_READ_FLOAT("cascade.lambda", SplitLambda);
    JE_SHADOW_READ_FLOAT("maxDistance", MaxDistance);
    if (!Boolean(values, "render.shadow.cascade.stabilize", candidate.CascadeStabilize, error)) return false;
    JE_SHADOW_READ_FLOAT("casterPadding", CasterPadding);
    JE_SHADOW_READ_FLOAT("moments.positiveExponent", PositiveExponent);
    JE_SHADOW_READ_FLOAT("moments.negativeExponent", NegativeExponent);
    JE_SHADOW_READ_FLOAT("moments.minimumVariance", MinimumVariance);
    JE_SHADOW_READ_FLOAT("moments.bleedReduction", BleedReduction);
    JE_SHADOW_READ_INT("moments.blurRadius", BlurRadius);
    JE_SHADOW_READ_FLOAT("deep.opacityMultiplier", DeepOpacityMultiplier);
    JE_SHADOW_READ_FLOAT("deep.tauMax", DeepTauMax);
    JE_SHADOW_READ_FLOAT("deep.maxDistance", DeepMaxDistance);
    JE_SHADOW_READ_INT("volume.maxTriangles", VolumeMaxTriangles);
    JE_SHADOW_READ_FLOAT("volume.range", VolumeRange);
#undef JE_SHADOW_READ_INT
#undef JE_SHADOW_READ_FLOAT
    return jAdvancedShadows::ValidateSettings(candidate, output, error);
}

bool WriteAdvancedShadows(jEditorProjectConfig& config, const jAdvancedShadows::Settings& input, std::string& error)
{
    jAdvancedShadows::Settings checked;
    if (!jAdvancedShadows::ValidateSettings(input, checked, error)) return false;
    auto values = config.Settings;
    values["render.shadow.projection"] = std::to_string(std::uint32_t(checked.DirectionalProjection));
    values["render.shadow.pointProjection"] = std::to_string(std::uint32_t(checked.PointProjection));
    values["render.shadow.representation"] = std::to_string(std::uint32_t(checked.Method));
#define JE_SHADOW_WRITE_INT(key, field) values["render.shadow." key] = std::to_string(checked.field)
#define JE_SHADOW_WRITE_FLOAT(key, field) values["render.shadow." key] = FeatureText(checked.field)
    JE_SHADOW_WRITE_INT("resolution", MapResolution);
    JE_SHADOW_WRITE_INT("cascade.count", CascadeCount);
    JE_SHADOW_WRITE_FLOAT("cascade.lambda", SplitLambda);
    JE_SHADOW_WRITE_FLOAT("maxDistance", MaxDistance);
    values["render.shadow.cascade.stabilize"] = checked.CascadeStabilize ? "true" : "false";
    JE_SHADOW_WRITE_FLOAT("casterPadding", CasterPadding);
    JE_SHADOW_WRITE_FLOAT("moments.positiveExponent", PositiveExponent);
    JE_SHADOW_WRITE_FLOAT("moments.negativeExponent", NegativeExponent);
    JE_SHADOW_WRITE_FLOAT("moments.minimumVariance", MinimumVariance);
    JE_SHADOW_WRITE_FLOAT("moments.bleedReduction", BleedReduction);
    JE_SHADOW_WRITE_INT("moments.blurRadius", BlurRadius);
    JE_SHADOW_WRITE_FLOAT("deep.opacityMultiplier", DeepOpacityMultiplier);
    JE_SHADOW_WRITE_FLOAT("deep.tauMax", DeepTauMax);
    JE_SHADOW_WRITE_FLOAT("deep.maxDistance", DeepMaxDistance);
    JE_SHADOW_WRITE_INT("volume.maxTriangles", VolumeMaxTriangles);
    JE_SHADOW_WRITE_FLOAT("volume.range", VolumeRange);
#undef JE_SHADOW_WRITE_INT
#undef JE_SHADOW_WRITE_FLOAT
    config.Settings = std::move(values); error.clear(); return true;
}

bool RenderAdvancedShadowControls(jAdvancedShadows::Settings& settings, std::string& error,
    std::optional<jAdvancedShadows::LightKind> kind)
{
    using namespace jAdvancedShadows;
    Settings candidate;
    if (!ValidateSettings(settings, candidate, error))
        ImGui::TextColored(ImVec4(1,.6f,.4f,1), "%s", error.c_str());
    ImGui::PushItemWidth(std::max(48.f, ImGui::GetContentRegionAvail().x
        - ImGui::CalcTextSize("Maximum shadow distance").x - ImGui::GetStyle().ItemInnerSpacing.x));
    bool changed = false;
    int method = int(candidate.Method);
    if (ImGui::Combo("Representation", &method, "Depth\0Variance (VSM)\0Exponential (ESM)\0Exponential Variance (EVSM)\0Deep (8 bins)\0Shadow Volume\0"))
    { candidate.Method = Representation(method); changed = true; }
    if (!kind || *kind == LightKind::Directional)
    {
        int projection = int(candidate.DirectionalProjection);
        if (ImGui::Combo("Directional projection", &projection, "Single\0Cascaded (CSM)\0Perspective (PSM)\0"))
        { candidate.DirectionalProjection = Projection(projection); changed = true; }
    }
    if (!kind || *kind == LightKind::Point)
    {
        int projection = int(candidate.PointProjection);
        if (ImGui::Combo("Point projection", &projection, "Cube\0Dual Paraboloid\0"))
        { candidate.PointProjection = PointProjection(projection); changed = true; }
    }
    if (candidate.Method != Representation::ShadowVolume)
    {
        int resolution = candidate.MapResolution == 64 ? 0 : candidate.MapResolution == 128 ? 1
            : candidate.MapResolution == 256 ? 2 : candidate.MapResolution == 512 ? 3 : 4;
        if (ImGui::Combo("Map tile resolution", &resolution, "64\0" "128\0" "256\0" "512\0" "1024\0"))
        { candidate.MapResolution = 64u << resolution; changed = true; }
        changed |= ImGui::DragFloat("Maximum shadow distance", &candidate.MaxDistance, 10, .01f, 1e6f, "%.4g", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Caster padding", &candidate.CasterPadding, 10, 0, 1e5f, "%.4g", ImGuiSliderFlags_AlwaysClamp);
    }
    if ((!kind || *kind == LightKind::Directional) && candidate.DirectionalProjection == Projection::Cascaded
        && candidate.Method != Representation::ShadowVolume)
    {
        int count = int(candidate.CascadeCount);
        if (ImGui::SliderInt("Cascade count", &count, 1, 4)) { candidate.CascadeCount = std::uint32_t(count); changed = true; }
        changed |= ImGui::SliderFloat("Split lambda", &candidate.SplitLambda, 0, 1);
        changed |= ImGui::Checkbox("Stabilize cascades", &candidate.CascadeStabilize);
    }
    if (candidate.Method == Representation::Variance || candidate.Method == Representation::Exponential
        || candidate.Method == Representation::ExponentialVariance)
    {
        if (candidate.Method != Representation::Variance)
            changed |= ImGui::SliderFloat("Positive exponent", &candidate.PositiveExponent, .1f, 40);
        if (candidate.Method == Representation::ExponentialVariance)
            changed |= ImGui::SliderFloat("Negative exponent", &candidate.NegativeExponent, .1f, 40);
        if (candidate.Method != Representation::Exponential)
        {
            changed |= ImGui::DragFloat("Minimum variance", &candidate.MinimumVariance, 1e-6f, 1e-8f, 1, "%.8g", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Light bleed reduction", &candidate.BleedReduction, 0, .999f);
        }
        int radius = int(candidate.BlurRadius);
        if (ImGui::SliderInt("Moment blur radius", &radius, 0, 8)) { candidate.BlurRadius = std::uint32_t(radius); changed = true; }
    }
    if (candidate.Method == Representation::Deep)
    {
        ImGui::TextDisabled("8 optical-depth bins; opacity includes material alpha.");
        changed |= ImGui::SliderFloat("Opacity multiplier", &candidate.DeepOpacityMultiplier, 0, 16);
        changed |= ImGui::SliderFloat("Maximum optical depth", &candidate.DeepTauMax, .01f, 32);
        changed |= ImGui::DragFloat("Deep maximum distance", &candidate.DeepMaxDistance, 10, .01f, 1e6f, "%.4g", ImGuiSliderFlags_AlwaysClamp);
    }
    if (candidate.Method == Representation::ShadowVolume
        || ((!kind || *kind == LightKind::Point) && candidate.PointProjection == PointProjection::DualParaboloid))
    {
        int triangles = int(candidate.VolumeMaxTriangles);
        if (ImGui::SliderInt("Geometry triangle budget", &triangles, 1, int(MaximumVolumeTriangles)))
        { candidate.VolumeMaxTriangles = std::uint32_t(triangles); changed = true; }
    }
    if (candidate.Method == Representation::ShadowVolume)
    {
        changed |= ImGui::DragFloat("Volume range", &candidate.VolumeRange, 10, 32, 1e6f, "%.4g", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextWrapped("Volumes require Single directional / Cube point projection. Source triangle and GPU budgets are bounded.");
    }
    Settings validated;
    const bool valid = ValidateSettings(candidate, validated, error)
        && (!kind || ValidateForLight(candidate, *kind, error));
    if (!valid) ImGui::TextColored(ImVec4(1,.6f,.4f,1), "%s", error.c_str());
    if (!kind && valid)
        for (const auto light : {LightKind::Directional, LightKind::Point})
        {
            std::string reason;
            if (!ValidateForLight(candidate, light, reason)) ImGui::TextColored(ImVec4(1,.75f,.4f,1), "%s", reason.c_str());
        }
    if (!kind && ImGui::TreeNode("Last-frame shadow diagnostics"))
    {
        const auto& frame = jAdvancedShadows::GetLastFrameStats();
        ImGui::Text("Additional targets: %.2f MiB", double(frame.AdditionalTargetBytes) / (1024 * 1024));
        for (std::size_t i = 0; i < std::min<std::size_t>(frame.Lights.size(), 16); ++i)
        {
            const auto& light = frame.Lights[i];
            ImGui::Text("Light %u: advanced %s, %d raster / %d compute passes", unsigned(i + 1),
                light.Active ? "active" : "inactive", light.RasterPasses, light.ComputePasses);
            if (!light.Diagnostic.empty()) ImGui::TextWrapped("%s", light.Diagnostic.c_str());
        }
        if (frame.Lights.size() > 16) ImGui::TextDisabled("%u more lights", unsigned(frame.Lights.size() - 16));
        ImGui::TreePop();
    }
    ImGui::PopItemWidth();
    if (changed && valid) { settings = validated; return true; }
    return false;
}

bool ReadSceneFeatures(const jEditorProjectConfig& config,const fs::path& root,
    jSceneFeatures::Settings& output,std::string& shAsset,std::string& error)
{
    error.clear();
    try
    {
        jSceneFeatures::Settings candidate;
        std::string asset;
        const auto& values=config.Settings;
        auto& e=candidate.Environment;auto& p=candidate.Reflection;auto& a=candidate.Atmosphere;
        if(!Boolean(values,"render.environment.enabled",e.Enabled,error)
            || !Boolean(values,"render.environment.combineGI",e.CombineWithDiffuseGI,error)
            || !Boolean(values,"render.ppr.enabled",p.Enabled,error) || !Boolean(values,"render.ppr.fillHoles",p.FillHoles,error)
            || !Boolean(values,"render.atmosphere.enabled",a.Enabled,error)
            || !Boolean(values,"render.atmosphere.useSceneLight",a.UseSceneDirectionalLight,error)
            || !FeatureFloat(values,"render.environment.intensity",e.Intensity,error)
            || !FeatureFloat(values,"render.ppr.thickness",p.PlaneThickness,error)
            || !FeatureFloat(values,"render.ppr.normalThreshold",p.NormalThreshold,error)
            || !FeatureFloat(values,"render.ppr.intensity",p.Intensity,error)
            || !FeatureFloat(values,"render.ppr.roughness",p.RoughnessThreshold,error)
            || !FeatureFloat(values,"render.ppr.edgeFade",p.EdgeFade,error)
            || !FeatureFloat(values,"render.ppr.fallbackLod",p.FallbackRoughnessLod,error)
            || !FeatureVector(values,"render.ppr.plane",p.Plane,error)
            || !FeatureVector(values,"render.ppr.fallbackRadiance",p.FallbackRadiance,error)
            || !FeatureFloat(values,"render.atmosphere.intensity",a.Intensity,error)
            || !FeatureFloat(values,"render.atmosphere.metersPerUnit",a.MetersPerWorldUnit,error)
            || !FeatureFloat(values,"render.atmosphere.groundHeight",a.GroundHeight,error)
            || !FeatureFloat(values,"render.atmosphere.heightMeters",a.AtmosphereHeightMeters,error)
            || !FeatureFloat(values,"render.atmosphere.maxDistanceMeters",a.MaxDistanceMeters,error)
            || !FeatureFloat(values,"render.atmosphere.rayleighHeight",a.RayleighScaleHeightMeters,error)
            || !FeatureFloat(values,"render.atmosphere.mieHeight",a.MieScaleHeightMeters,error)
            || !FeatureFloat(values,"render.atmosphere.anisotropy",a.MieAnisotropy,error)
            || !FeatureVector(values,"render.atmosphere.rayleigh",a.RayleighScattering,error)
            || !FeatureVector(values,"render.atmosphere.mie",a.MieScattering,error)
            || !FeatureVector(values,"render.atmosphere.sun",a.SunDirection,error)
            || !FeatureVector(values,"render.atmosphere.solar",a.SolarIrradiance,error)
            || !FeatureUnsigned(values,"render.atmosphere.viewSamples",a.ViewSamples,error)
            || !FeatureUnsigned(values,"render.atmosphere.lightSamples",a.LightSamples,error))return false;
        if(const auto found=values.find("render.environment.shAsset");found!=values.end())asset=found->second;
        if(!FeatureAssetPath(asset,root,error))return false;
        if(!asset.empty() && !root.empty())
        {
            const auto path=jEditorProject::ResolveSafe(root,asset,error,true);
            if(!path || !jEnvironmentLighting::LoadAsset(*path,e.Radiance,error))return false;
        }
        jSceneFeatures::Settings validated;
        if(!jSceneFeatures::ValidateSettings(candidate,validated,error))return false;
        output=validated;shAsset=std::move(asset);return true;
    }
    catch(const std::exception& exception){error=exception.what();return false;}
}

bool WriteSceneFeatures(jEditorProjectConfig& config,const jSceneFeatures::Settings& input,
    const std::string& shAsset,std::string& error)
{
    try
    {
        jSceneFeatures::Settings checked;
        if(!jSceneFeatures::ValidateSettings(input,checked,error) || !FeatureAssetPath(shAsset,{},error))return false;
        auto candidate=config.Settings;
        const auto boolean=[&](const char* key,bool value){candidate[key]=value?"true":"false";};
        const auto scalar=[&](const char* key,float value){candidate[key]=FeatureText(value);};
        const auto vector=[&](const char* key,const auto& value){candidate[key]=FeatureText(value);};
        const auto& e=checked.Environment;const auto& p=checked.Reflection;const auto& a=checked.Atmosphere;
        boolean("render.environment.enabled",e.Enabled);boolean("render.environment.combineGI",e.CombineWithDiffuseGI);
        scalar("render.environment.intensity",e.Intensity);candidate["render.environment.shAsset"]=shAsset;
        boolean("render.ppr.enabled",p.Enabled);boolean("render.ppr.fillHoles",p.FillHoles);vector("render.ppr.plane",p.Plane);
        scalar("render.ppr.thickness",p.PlaneThickness);scalar("render.ppr.normalThreshold",p.NormalThreshold);
        scalar("render.ppr.intensity",p.Intensity);scalar("render.ppr.roughness",p.RoughnessThreshold);
        scalar("render.ppr.edgeFade",p.EdgeFade);scalar("render.ppr.fallbackLod",p.FallbackRoughnessLod);
        vector("render.ppr.fallbackRadiance",p.FallbackRadiance);
        boolean("render.atmosphere.enabled",a.Enabled);boolean("render.atmosphere.useSceneLight",a.UseSceneDirectionalLight);
        scalar("render.atmosphere.intensity",a.Intensity);scalar("render.atmosphere.metersPerUnit",a.MetersPerWorldUnit);
        scalar("render.atmosphere.groundHeight",a.GroundHeight);scalar("render.atmosphere.heightMeters",a.AtmosphereHeightMeters);
        scalar("render.atmosphere.maxDistanceMeters",a.MaxDistanceMeters);scalar("render.atmosphere.rayleighHeight",a.RayleighScaleHeightMeters);
        scalar("render.atmosphere.mieHeight",a.MieScaleHeightMeters);scalar("render.atmosphere.anisotropy",a.MieAnisotropy);
        vector("render.atmosphere.rayleigh",a.RayleighScattering);vector("render.atmosphere.mie",a.MieScattering);
        vector("render.atmosphere.sun",a.SunDirection);vector("render.atmosphere.solar",a.SolarIrradiance);
        candidate["render.atmosphere.viewSamples"]=std::to_string(a.ViewSamples);
        candidate["render.atmosphere.lightSamples"]=std::to_string(a.LightSamples);
        config.Settings=std::move(candidate);error.clear();return true;
    }
    catch(const std::exception& exception){error=exception.what();return false;}
}

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
    return Prepare(config, prepared, error, root) && ValidatePaths(config, root, error);
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
    if (!Prepare(project.GetConfig(), prepared, error, project.GetRoot()) || !ValidatePaths(project.GetConfig(), project.GetRoot(), error)) return false;
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
    if (!Prepare(proposed.GetConfig(), prepared, error, proposed.GetRoot()) || !ValidatePaths(proposed.GetConfig(), proposed.GetRoot(), error)) return false;
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
    const bool validValues = Prepare(state.Draft, requested, draftError, project->GetRoot());
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
        const auto& diagnostics = GetRendererFeatureDiagnostics();
        int samples = requested.MSAASamples == 4 ? 2 : requested.MSAASamples == 2 ? 1 : 0;
        if (ImGui::Combo("Deferred MSAA samples", &samples, "1x\0" "2x\0" "4x\0"))
        { state.Draft.Settings["render.msaa.samples"] = std::to_string(samples == 2 ? 4 : samples == 1 ? 2 : 1); state.Changed = true; }
        ImGui::Text("Requested %dx / applied %dx", requested.MSAASamples, diagnostics.AppliedMSAASamples);
        if (!diagnostics.MSAASupportReason.empty()) ImGui::TextWrapped("%s", diagnostics.MSAASupportReason.c_str());
        if (ImGui::TreeNode("Surface diffusion (SSS)"))
        {
            BoolWidget("Enable Deferred SSS", "render.sss.enabled", requested.Subsurface.Enabled);
            const auto quality = [&](const char* label, const char* key, float initial, float low, float high, const char* format = "%.3g")
            {
                if (ImGui::SliderFloat(label, &initial, low, high, format))
                { state.Draft.Settings[key] = FeatureText(initial); state.Changed = true; }
            };
            quality("Global SSS strength", "render.sss.strength", requested.Subsurface.Strength, 0, 1);
            quality("Diffusion radius scale", "render.sss.radiusScale", requested.Subsurface.RadiusScale, 0, 100);
            quality("Depth rejection (world)", "render.sss.depthRejectWorld", requested.Subsurface.DepthRejectWorld, .00001f, 10000);
            quality("Normal threshold", "render.sss.normalThreshold", requested.Subsurface.NormalThreshold, -1, 1);
            quality("Maximum radius (pixels)", "render.sss.maxRadiusPixels", requested.Subsurface.MaxRadiusPixels, 0, jSubsurface::MaximumRadiusPixels);
            if (ImGui::TreeNode("Texture-space transport"))
            {
                const auto& texture = requested.TextureSubsurface;
                const auto resolution = [&](const char* label, const char* key, uint32_t value, bool capture)
                {
                    int index = value == 128 ? 0 : value == 256 ? 1 : value == 512 ? 2 : value == 1024 ? 3 : 4;
                    const char* choices[] = {"128", "256", "512", "1024", "2048"};
                    if (ImGui::Combo(label, &index, choices, capture ? 5 : 4))
                    { state.Draft.Settings[key] = std::to_string(128u << index); state.Changed = true; }
                };
                resolution("UV tile resolution", "render.textureSubsurface.tileResolution", texture.TileResolution, false);
                resolution("Light capture resolution", "render.textureSubsurface.captureResolution", texture.CaptureResolution, true);
                quality("Gaussian step scale", "render.textureSubsurface.gaussianStepScale", texture.GaussianStepScale, 0, 10);
                float units = texture.WorldUnitsPerMillimeter;
                if (ImGui::DragFloat("World units per millimeter", &units, .001f, .00001f, 1000, "%.6g", ImGuiSliderFlags_AlwaysClamp))
                { state.Draft.Settings["render.textureSubsurface.worldUnitsPerMillimeter"] = FeatureText(units); state.Changed = true; }
                BoolWidget("Enable thin-surface transmission", "render.textureSubsurface.enableTransmission", texture.EnableTransmission);
                quality("Transmission extinction / mm", "render.textureSubsurface.transmissionExtinctionPerMillimeter", texture.TransmissionExtinctionPerMillimeter, .001f, 100);
                quality("Transmission thickness scale", "render.textureSubsurface.transmissionThicknessScale", texture.TransmissionThicknessScale, 0, 10);
                quality("Capture bias (world)", "render.textureSubsurface.captureBiasWorld", texture.CaptureBiasWorld, 0, 100);
                ImGui::TextWrapped("Used by materials selecting Texture (UV) diffusion. World-unit conversion is project data; source-specific scale is not assumed for other projects. These requests apply only after Save and Apply.");
                ImGui::TreePop();
            }
            ImGui::TextWrapped("Set material subsurface weight, color, world thickness and profile in Inspector or the material graph. Diffusion uses a bounded 17-sample kernel per axis.");
            if (!diagnostics.SubsurfaceSupportReason.empty()) ImGui::TextWrapped("%s", diagnostics.SubsurfaceSupportReason.c_str());
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Hi-Z occlusion"))
        {
            BoolWidget("Enable Hi-Z", "render.hiz.enabled", requested.HiZOcclusion.Enabled);
            float bias = requested.HiZOcclusion.DepthBias;
            if (ImGui::SliderFloat("Conservative depth bias", &bias, 0, .01f, "%.6g"))
            { state.Draft.Settings["render.hiz.depthBias"] = FeatureText(bias); state.Changed = true; }
            int candidates = int(requested.HiZOcclusion.MaxCandidates);
            if (ImGui::SliderInt("Maximum candidates", &candidates, 1, int(jHiZOcclusion::MaximumCandidates)))
            { state.Draft.Settings["render.hiz.maxCandidates"] = std::to_string(candidates); state.Changed = true; }
            ImGui::Text("Admitted candidates: %u | indirect draws: %u", diagnostics.HiZCandidateCount, diagnostics.HiZIndirectDrawCount);
            if (!diagnostics.HiZSupportReason.empty()) ImGui::TextWrapped("%s", diagnostics.HiZSupportReason.c_str());
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto advanced = requested.AdvancedShadows;
            std::string advancedError;
            if (RenderAdvancedShadowControls(advanced, advancedError)
                && WriteAdvancedShadows(state.Draft, advanced, advancedError)) state.Changed = true;
            ImGui::Separator();
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
            else ImGui::TextWrapped("Raster shadows only. The comparison filter applies to Depth; Path Tracing and HWRT rays bypass these settings.");
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
