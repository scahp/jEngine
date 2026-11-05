#include "pch.h"
#include "jEditorTools.h"
#include "jEditorAssetPanel.h"
#include "jEditorProcedural.h"
#include "Code/Engine/jEditor.h"
#include "Core/jLog.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jEditorAssetPath.h"
#include "FileLoader/jImageFileLoader.h"
#include "RHI/jRHI.h"
#include "RHI/jRaytracingScene.h"
#include "Scene/jCamera.h"
#include "Scene/jMeshObject.h"
#include "jEngine.h"
#include "Renderer/Features/jSubsurfaceSettings.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include <fstream>
#include <span>
#include "stb_image.h"

namespace jEditorTools
{
using namespace jEditorAuthoring;
using Json = nlohmann::json;
namespace
{
constexpr size_t MaxMaterialNodes = 256;
bool CompiledMaterialGraph(const MaterialGraph& graph)
{
    return graph.Horizon || graph.SubsurfaceWeight || graph.SubsurfaceColor || graph.SubsurfaceThickness || graph.SubsurfaceProfile
        || graph.SpecularWeight || graph.DiffuseOcclusion || graph.PreScatterWeight
        || std::any_of(graph.Nodes.begin(), graph.Nodes.end(), [](const auto& node) { return node.Kind > NodeKind::Add; });
}
void SubsurfaceDefaults(const MaterialGraph& graph, MaterialProperties& material)
{
    const auto shading = graph.Surface ? graph.Surface->Shading : jSurfaceShading{};
    material.SubsurfaceOverrideEnabled = graph.Surface.has_value()
        || graph.SubsurfaceWeight || graph.SubsurfaceColor || graph.SubsurfaceThickness || graph.SubsurfaceProfile;
    material.SubsurfaceWeight = shading.Subsurface;
    material.SubsurfaceColor = {shading.SubsurfaceColor[0], shading.SubsurfaceColor[1], shading.SubsurfaceColor[2]};
    material.SubsurfaceThicknessWorld = shading.SubsurfaceThicknessWorld;
    material.SubsurfaceProfile = shading.SubsurfaceProfile;
    material.SkinOverrideEnabled = graph.Surface.has_value() || graph.SpecularWeight || graph.DiffuseOcclusion || graph.PreScatterWeight;
    material.ShadingModel = shading.ShadingModel; material.SubsurfaceSpace = shading.SubsurfaceSpace;
    material.SpecularWeight = shading.SpecularWeight; material.DiffuseOcclusion = shading.DiffuseOcclusion;
    material.PreScatterWeight = shading.PreScatterWeight;
}
bool Finite(float value) { return std::isfinite(value) && std::abs(value) <= 1e8f; }
bool SafeTexture(const std::string& path)
{
    if (path.empty()) return true;
    if (path.size() > 4096 || path.find('\\') != std::string::npos || path.find(':') != std::string::npos || path.find('\0') != std::string::npos) return false;
    const auto p = jEditorAssetPath::FromUtf8(path);
    if (p.is_absolute()) return false;
    for (const auto& part : p) if (part == ".." || part == ".") return false;
    return true;
}
bool ValidSurface(const MaterialGraph::SurfaceProperties& surface)
{
    const auto& s = surface.Shading;
    const auto unit = [](float value) { return value >= 0.f && value <= 1.f; };
    const auto radiance = [](float value) { return value >= 0.f && value <= 1e6f; };
    return unit(s.Transmission) && s.Ior >= 1.f && s.Ior <= 4.f && unit(s.ClearCoat) && unit(s.ClearCoatGloss) &&
        unit(s.Subsurface) && unit(s.SubsurfaceColor[0]) && unit(s.SubsurfaceColor[1]) && unit(s.SubsurfaceColor[2])
        && std::isfinite(s.SubsurfaceThicknessWorld) && s.SubsurfaceThicknessWorld >= 0 && s.SubsurfaceThicknessWorld <= 10000
        && s.SubsurfaceProfile < 8 && s.Anisotropy >= -1.f && s.Anisotropy <= 1.f && unit(s.SpecularTint) &&
        uint32_t(s.ShadingModel) <= 1 && uint32_t(s.SubsurfaceSpace) <= 1 && std::isfinite(s.SpecularWeight)
        && s.SpecularWeight >= 0 && s.SpecularWeight <= 8 && unit(s.DiffuseOcclusion) && unit(s.PreScatterWeight) &&
        unit(s.Sheen) && unit(s.SheenTint) && unit(s.AlphaCutoff) && unit(surface.Opacity) &&
        uint32_t(s.AlphaMode) <= uint32_t(jSurfaceAlphaMode::Blend) &&
        radiance(surface.Emission.X) && radiance(surface.Emission.Y) && radiance(surface.Emission.Z) &&
        SafeTexture(surface.MetallicRoughnessTexture) && SafeTexture(surface.EmissionTexture);
}
MaterialGraph::SurfaceProperties ReadSurface(const Json& json)
{
    if (!json.is_object()) throw std::runtime_error("Invalid material surface");
    MaterialGraph::SurfaceProperties result;
    auto& s = result.Shading;
    s.Transmission = json.value("transmission", 0.f); s.Ior = json.value("ior", 1.5f);
    s.ClearCoat = json.value("clearCoat", 0.f); s.ClearCoatGloss = json.value("clearCoatGloss", 0.f);
    s.Subsurface = json.value("subsurface", 0.f); s.Anisotropy = json.value("anisotropy", 0.f);
    if (const auto color = json.find("subsurfaceColor"); color != json.end())
    {
        if (!color->is_array() || color->size() != 3) throw std::runtime_error("Subsurface color requires three linear RGB numbers");
        for (std::size_t i = 0; i < 3; ++i) s.SubsurfaceColor[i] = color->at(i).get<float>();
    }
    s.SubsurfaceThicknessWorld = json.value("subsurfaceThicknessWorld", 1.f);
    if (const auto profile = json.find("subsurfaceProfile"); profile != json.end())
    {
        if (!profile->is_number_integer() || profile->get<int64_t>() < 0 || profile->get<uint64_t>() > 7)
            throw std::runtime_error("Subsurface profile must be an integer from 0 to 7");
        s.SubsurfaceProfile = profile->get<uint32_t>();
    }
    if (const auto skin = json.find("skin"); skin != json.end())
    {
        if (!skin->is_object() || skin->size() != 5) throw std::runtime_error("Skin settings require a complete five-field block");
        const auto enumValue = [](const Json& value)
        {
            if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<uint64_t>() > 1)
                throw std::runtime_error("Skin model/space must be an integer from 0 to 1");
            return value.get<uint32_t>();
        };
        s.ShadingModel = jSurfaceShadingModel(enumValue(skin->at("shadingModel")));
        s.SubsurfaceSpace = jSubsurfaceSpace(enumValue(skin->at("subsurfaceSpace")));
        s.SpecularWeight = skin->at("specularWeight").get<float>(); s.DiffuseOcclusion = skin->at("diffuseOcclusion").get<float>();
        s.PreScatterWeight = skin->at("preScatterWeight").get<float>();
    }
    s.SpecularTint = json.value("specularTint", 0.f); s.Sheen = json.value("sheen", 0.f); s.SheenTint = json.value("sheenTint", 0.f);
    const auto alpha = json.value("alphaMode", std::string("opaque"));
    if (alpha == "mask") s.AlphaMode = jSurfaceAlphaMode::Mask;
    else if (alpha == "blend") s.AlphaMode = jSurfaceAlphaMode::Blend;
    else if (alpha != "opaque") throw std::runtime_error("Invalid material alpha mode");
    s.AlphaCutoff = json.value("alphaCutoff", .5f);
    s.DoubleSided = json.value("doubleSided", false); s.LegacyPackedMap = json.value("legacyPackedMap", false);
    result.Opacity = json.value("opacity", 1.f);
    if (const auto emission = json.find("emission"); emission != json.end())
    {
        if (!emission->is_array() || emission->size() != 3) throw std::runtime_error("Invalid material emission");
        result.Emission = {emission->at(0).get<float>(), emission->at(1).get<float>(), emission->at(2).get<float>()};
    }
    result.MetallicRoughnessTexture = json.value("metallicRoughnessTexture", std::string{});
    result.EmissionTexture = json.value("emissionTexture", std::string{});
    return result;
}
Json WriteSurface(const MaterialGraph::SurfaceProperties& surface)
{
    const auto& s = surface.Shading;
    const char* alpha = s.AlphaMode == jSurfaceAlphaMode::Mask ? "mask" : s.AlphaMode == jSurfaceAlphaMode::Blend ? "blend" : "opaque";
    Json result{{"transmission", s.Transmission}, {"ior", s.Ior}, {"clearCoat", s.ClearCoat}, {"clearCoatGloss", s.ClearCoatGloss},
        {"subsurface", s.Subsurface}, {"anisotropy", s.Anisotropy}, {"specularTint", s.SpecularTint}, {"sheen", s.Sheen}, {"sheenTint", s.SheenTint},
        {"alphaMode", alpha}, {"alphaCutoff", s.AlphaCutoff}, {"doubleSided", s.DoubleSided}, {"legacyPackedMap", s.LegacyPackedMap},
        {"opacity", surface.Opacity}, {"emission", {surface.Emission.X, surface.Emission.Y, surface.Emission.Z}},
        {"metallicRoughnessTexture", surface.MetallicRoughnessTexture}, {"emissionTexture", surface.EmissionTexture}};
    if (s.SubsurfaceColor != std::array<float, 3>{1, 1, 1} || s.SubsurfaceThicknessWorld != 1 || s.SubsurfaceProfile != 0)
    {
        result["subsurfaceColor"] = s.SubsurfaceColor;
        result["subsurfaceThicknessWorld"] = s.SubsurfaceThicknessWorld;
        result["subsurfaceProfile"] = s.SubsurfaceProfile;
    }
    if (s.ShadingModel != jSurfaceShadingModel::StandardPBR || s.SubsurfaceSpace != jSubsurfaceSpace::Screen
        || s.SpecularWeight != 1 || s.DiffuseOcclusion != 1 || s.PreScatterWeight != 1)
        result["skin"] = {{"shadingModel", uint32_t(s.ShadingModel)}, {"subsurfaceSpace", uint32_t(s.SubsurfaceSpace)},
            {"specularWeight", s.SpecularWeight}, {"diffuseOcclusion", s.DiffuseOcclusion}, {"preScatterWeight", s.PreScatterWeight}};
    return result;
}
Json ReadSceneMaterialBindings(const std::filesystem::path& source)
{
    auto sidecar = source;
    sidecar += ".materials.json";
    if (!std::filesystem::exists(sidecar)) return Json::object();
    const auto root = std::filesystem::absolute(source).parent_path();
    std::string error;
    const auto manifest = jEditorProject::ResolveSafe(root, sidecar.filename(), error, true);
    if (!manifest) throw std::runtime_error(error);
    if (std::filesystem::file_size(*manifest) > 1024 * 1024) throw std::runtime_error("Material library exceeds 1 MiB");
    std::ifstream stream(*manifest);
    Json data;
    stream >> data;
    const auto& bindings = data.at("materials");
    if (data.at("format") != "jEngineSceneMaterials" || data.at("version") != 1 ||
        !bindings.is_object() || bindings.size() > 4096)
        throw std::runtime_error("Invalid scene material library");
    for (const auto& [name, value] : bindings.items())
        if (name.empty() || !value.is_string() || !SafeTexture(value.get<std::string>()))
            throw std::runtime_error("Invalid scene material binding");
    return bindings;
}
bool Write(const std::filesystem::path& file, const std::string& text, std::string& error)
{
    std::filesystem::path temporary;
    try
    {
        const auto parent = file.has_parent_path() ? file.parent_path() : std::filesystem::path(".");
        std::filesystem::create_directories(parent);
        if (std::filesystem::is_symlink(file)) throw std::runtime_error("Refusing material symlink target");
        for (int i = 0; i < 32; ++i)
        {
            const auto candidate = parent / (".material-tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { temporary = candidate; break; }
        }
        if (temporary.empty()) throw std::runtime_error("Cannot reserve material temporary directory");
        const auto staged = temporary / "material";
        std::ofstream out(staged, std::ios::binary); out << text; out.close();
        if (!out) throw std::runtime_error("Cannot write material graph");
#ifdef _WIN32
        if (!MoveFileExW(staged.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace material graph");
#else
        std::filesystem::rename(staged, file);
#endif
        std::filesystem::remove(temporary); return true;
    }
    catch (const std::exception& e) { error = e.what(); if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove_all(temporary, ignored); } return false; }
}
struct MaterialValue
{
    Vec3 Color{1, 1, 1};
    std::string Texture;
    bool IsScalar = false;
};
struct MaterialNodeResult
{
    MaterialValue Value;
    const char* Error = "Material graph cycle rejected";
};

class MaterialEvaluator
{
public:
    explicit MaterialEvaluator(const MaterialGraph& graph)
    {
        for (const auto& node : graph.Nodes) Nodes.emplace(node.Id, &node);
    }
    const MaterialNode* Find(uint32_t id) const
    {
        const auto node = Nodes.find(id);
        return node == Nodes.end() ? nullptr : node->second;
    }
    const MaterialNodeResult& Read(uint32_t id)
    {
        // A recursive read of an unfinished entry observes its cycle error.
        const auto [entry, inserted] = Results.try_emplace(id);
        if (inserted) entry->second = Calculate(id);
        return entry->second;
    }

private:
    MaterialNodeResult Calculate(uint32_t id)
    {
        const auto* node = Find(id);
        if (!node) return {{}, "Material input/output references a missing node"};
        MaterialValue value;
        switch (node->Kind)
        {
        case NodeKind::Color:
            value.Color = node->Color;
            break;
        case NodeKind::Scalar:
            value.Color = {node->Scalar, node->Scalar, node->Scalar};
            value.IsScalar = true;
            break;
        case NodeKind::Texture:
            if (node->Texture.empty()) return {{}, "Texture node requires a project-relative texture"};
            value.Texture = node->Texture;
            break;
        case NodeKind::Multiply:
        case NodeKind::Add:
        {
            const auto& a = Read(node->InputA);
            if (a.Error) return a;
            const auto& b = Read(node->InputB);
            if (b.Error) return b;
            const bool multiply = node->Kind == NodeKind::Multiply;
            if ((!a.Value.Texture.empty() && !b.Value.Texture.empty()) ||
                (!multiply && (!a.Value.Texture.empty() || !b.Value.Texture.empty())))
                return {{}, "This PBR graph supports one texture multiplied by a constant color; texture mixing needs a shader node extension"};
            value.Texture = a.Value.Texture.empty() ? b.Value.Texture : a.Value.Texture;
            value.IsScalar = a.Value.IsScalar && b.Value.IsScalar;
            const auto& x = a.Value.Color;
            const auto& y = b.Value.Color;
            value.Color = multiply ? Vec3{x.X * y.X, x.Y * y.Y, x.Z * y.Z} : Vec3{x.X + y.X, x.Y + y.Y, x.Z + y.Z};
            break;
        }
        default:
            return {{}, "Invalid material node kind"};
        }
        if (!Finite(value.Color.X) || !Finite(value.Color.Y) || !Finite(value.Color.Z))
            return {{}, "Material graph overflow"};
        return {std::move(value), nullptr};
    }

    std::map<uint32_t, const MaterialNode*> Nodes;
    std::map<uint32_t, MaterialNodeResult> Results;
};
}
bool EvaluateMaterial(const MaterialGraph& graph, MaterialProperties& material, std::string& error)
{
    error.clear();
    if (graph.Nodes.empty() || graph.Nodes.size() > MaxMaterialNodes) { error = "A material needs 1..256 nodes"; return false; }
    if (graph.Surface && !ValidSurface(*graph.Surface)) { error = "Invalid material surface value or texture path"; return false; }
    if (CompiledMaterialGraph(graph))
    {
        jMaterialMapping::Program program;
        if (!jMaterialMapping::Compile(graph, program, error)) return false;
        auto result = material;
        result.BaseColor = {1, 1, 1}; result.BaseColorTexture.clear();
        if (graph.Normal)
        {
            const auto node = std::find_if(graph.Nodes.begin(), graph.Nodes.end(), [&](const auto& value) { return value.Id == graph.Normal; });
            result.NormalTexture = node != graph.Nodes.end() && node->Kind == NodeKind::Texture ? node->Texture : std::string{};
        }
        else result.NormalTexture.clear();
        const auto constant = [&](uint32_t id, float& value)
        {
            const auto node = std::find_if(graph.Nodes.begin(), graph.Nodes.end(), [&](const auto& current) { return current.Id == id; });
            if (node != graph.Nodes.end() && node->Kind == NodeKind::Scalar) value = std::clamp(node->Scalar, 0.0f, 1.0f);
        };
        constant(graph.Metallic, result.Metallic); constant(graph.Roughness, result.Roughness);
        if (graph.Surface) { result.Emissive = graph.Surface->Emission; result.Opacity = graph.Surface->Opacity; }
        SubsurfaceDefaults(graph, result);
        result.OverrideEnabled = true; material = std::move(result); return true;
    }
    MaterialEvaluator evaluator(graph);
    for (const auto& node : graph.Nodes)
    {
        if (!node.Id || evaluator.Find(node.Id) != &node || !Finite(node.Scalar) || !Finite(node.Color.X) || !Finite(node.Color.Y) || !Finite(node.Color.Z) || !Finite(node.X) || !Finite(node.Y) || !SafeTexture(node.Texture) || int(node.Kind) < 0 || int(node.Kind) > int(NodeKind::Add))
        { error = "Invalid material node ID, number, kind or texture path"; return false; }
    }
    // Validate disconnected nodes too: unused cyclic subgraphs must not persist.
    for (const auto& node : graph.Nodes)
        if (const auto* problem = evaluator.Read(node.Id).Error) { error = problem; return false; }
    auto result = material;
    const auto& base = evaluator.Read(graph.BaseColor);
    if (base.Error) { error = base.Error; return false; }
    const auto& color = base.Value;
    result.BaseColor = {std::clamp(color.Color.X, 0.0f, 1.0f), std::clamp(color.Color.Y, 0.0f, 1.0f), std::clamp(color.Color.Z, 0.0f, 1.0f)};
    result.BaseColorTexture = color.Texture;
    if (graph.Normal)
    {
        const auto* normal = evaluator.Find(graph.Normal);
        if (!normal || normal->Kind != NodeKind::Texture)
        { error = "Normal output requires a Texture node"; return false; }
        result.NormalTexture = normal->Texture;
    }
    const auto scalarOutput = [&](uint32_t id, float& value, const char* message)
    {
        if (!id) return true;
        const auto& output = evaluator.Read(id);
        if (output.Error || !output.Value.Texture.empty()) { error = message; return false; }
        value = std::clamp(output.Value.Color.X, 0.0f, 1.0f);
        return true;
    };
    if (!scalarOutput(graph.Metallic, result.Metallic, "Metallic output must be scalar") ||
        !scalarOutput(graph.Roughness, result.Roughness, "Roughness output must be scalar")) return false;
    if (graph.Surface)
    {
        result.Emissive = graph.Surface->Emission;
        result.Opacity = graph.Surface->Opacity;
    }
    SubsurfaceDefaults(graph, result);
    result.OverrideEnabled = true; material = std::move(result); return true;
}
bool SaveMaterial(const MaterialGraph& graph, const std::filesystem::path& file, std::string& error)
{
    MaterialProperties checked; if (!EvaluateMaterial(graph, checked, error)) return false;
    Json nodes = Json::array();
    const bool dynamic = CompiledMaterialGraph(graph);
    for (const auto& n : graph.Nodes)
    {
        Json node{{"id", n.Id}, {"kind", int(n.Kind)}, {"a", n.InputA}, {"b", n.InputB}, {"color", {n.Color.X, n.Color.Y, n.Color.Z}}, {"value", n.Scalar}, {"x", n.X}, {"y", n.Y}, {"texture", n.Texture}};
        if (dynamic)
        {
            node["c"] = n.InputC; node["d"] = n.InputD; node["parameters"] = n.Parameters; node["semantic"] = uint32_t(n.Semantic);
            if (n.Kind == NodeKind::TextureSampleNormal)
            {
                node["normalSpace"] = uint32_t(n.NormalSpace); node["inputBasisSignXYZ"] = n.InputBasisSignXYZ;
                if (n.FlipNormalY) node["flipNormalY"] = *n.FlipNormalY;
            }
        }
        nodes.push_back(std::move(node));
    }
    Json json{{"format", "jEngineMaterial"}, {"version", dynamic ? 2 : 1}, {"nodes", nodes}, {"baseColor", graph.BaseColor},
        {"metallic", graph.Metallic}, {"roughness", graph.Roughness}, {"normal", graph.Normal}};
    if (graph.Surface) json["surface"] = WriteSurface(*graph.Surface);
    if (dynamic)
    {
        json["horizon"] = graph.Horizon;
        if (graph.SubsurfaceWeight || graph.SubsurfaceColor || graph.SubsurfaceThickness || graph.SubsurfaceProfile)
            json["subsurfaceOutputs"] = {{"weight", graph.SubsurfaceWeight}, {"color", graph.SubsurfaceColor},
                {"thickness", graph.SubsurfaceThickness}, {"profile", graph.SubsurfaceProfile}};
        if (graph.SpecularWeight || graph.DiffuseOcclusion || graph.PreScatterWeight)
            json["skinOutputs"] = {{"specularWeight", graph.SpecularWeight}, {"diffuseOcclusion", graph.DiffuseOcclusion},
                {"preScatterWeight", graph.PreScatterWeight}};
    }
    return Write(file, json.dump(2), error);
}
bool LoadMaterial(const std::filesystem::path& file, MaterialGraph& graph, std::string& error)
{
    try
    {
        if (std::filesystem::file_size(file) > 1024 * 1024) throw std::runtime_error("Material graph exceeds 1 MiB");
        std::ifstream in(file); Json json; in >> json;
        const auto version = json.at("version");
        if (json.at("format") != "jEngineMaterial" || !version.is_number_integer() || (version != 1 && version != 2)
            || !json.at("nodes").is_array() || json.at("nodes").size() > MaxMaterialNodes) throw std::runtime_error("Unsupported material graph");
        const auto id = [](const Json& value)
        {
            if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<uint64_t>() >= UINT32_MAX)
                throw std::runtime_error("Material IDs must be bounded unsigned integers");
            return value.get<uint32_t>();
        };
        MaterialGraph result; result.Version = version.get<uint32_t>();
        result.BaseColor = id(json.at("baseColor")); result.Metallic = id(json.at("metallic")); result.Roughness = id(json.at("roughness"));
        result.Normal = json.contains("normal") ? id(json.at("normal")) : 0;
        result.Horizon = json.contains("horizon") ? id(json.at("horizon")) : 0;
        if (const auto sss = json.find("subsurfaceOutputs"); sss != json.end())
        {
            if (result.Version != 2 || !sss->is_object() || sss->size() != 4) throw std::runtime_error("Subsurface outputs require a complete version-two output block");
            result.SubsurfaceWeight = id(sss->at("weight")); result.SubsurfaceColor = id(sss->at("color"));
            result.SubsurfaceThickness = id(sss->at("thickness")); result.SubsurfaceProfile = id(sss->at("profile"));
        }
        if (const auto skin = json.find("skinOutputs"); skin != json.end())
        {
            if (result.Version != 2 || !skin->is_object() || skin->size() != 3) throw std::runtime_error("Skin outputs require a complete version-two output block");
            result.SpecularWeight = id(skin->at("specularWeight")); result.DiffuseOcclusion = id(skin->at("diffuseOcclusion"));
            result.PreScatterWeight = id(skin->at("preScatterWeight"));
        }
        if (const auto surface = json.find("surface"); surface != json.end()) result.Surface = ReadSurface(*surface);
        for (const auto& j : json.at("nodes"))
        {
            const auto& kind = j.at("kind");
            if (!kind.is_number_integer() || kind.get<int64_t>() < 0 || kind.get<int64_t>() > int(NodeKind::DiffusionProfile))
                throw std::runtime_error("Material node kind must be a supported integer enum");
            MaterialNode node; node.Id = id(j.at("id")); node.Kind = NodeKind(kind.get<int>());
            if (result.Version == 1 && node.Kind > NodeKind::Add) throw std::runtime_error("New material nodes require graph version 2");
            node.InputA = id(j.at("a")); node.InputB = id(j.at("b"));
            if (!j.at("color").is_array() || j.at("color").size() != 3) throw std::runtime_error("Material color requires exactly three numbers");
            node.Color = {j.at("color").at(0).get<float>(), j.at("color").at(1).get<float>(), j.at("color").at(2).get<float>()};
            node.Scalar = j.at("value").get<float>(); node.X = j.at("x").get<float>(); node.Y = j.at("y").get<float>(); node.Texture = j.at("texture").get<std::string>();
            if (result.Version >= 2)
            {
                node.InputC = j.contains("c") ? id(j.at("c")) : 0; node.InputD = j.contains("d") ? id(j.at("d")) : 0;
                if (j.contains("parameters"))
                {
                    if (!j["parameters"].is_array() || j["parameters"].size() != 4) throw std::runtime_error("Material parameters require exactly four numbers");
                    node.Parameters = j["parameters"].get<std::array<float, 4>>();
                }
                if (j.contains("semantic") && (!j["semantic"].is_number_integer() || j["semantic"].get<int64_t>() < 0 || j["semantic"].get<int64_t>() > 2))
                    throw std::runtime_error("Material texture semantic must be a supported integer enum");
                node.Semantic = jMaterialMapping::TextureSemantic(j.value("semantic", 0u));
                if (j.contains("normalSpace")) node.NormalSpace = jMaterialMapping::NormalSpace(id(j.at("normalSpace")));
                if (const auto basis = j.find("inputBasisSignXYZ"); basis != j.end())
                {
                    if (!basis->is_array() || basis->size() != 3) throw std::runtime_error("Normal input basis requires three signs");
                    node.InputBasisSignXYZ = basis->get<std::array<float, 3>>();
                }
                if (const auto flip = j.find("flipNormalY"); flip != j.end())
                {
                    if (!flip->is_boolean()) throw std::runtime_error("Explicit normal Y flip must be boolean");
                    node.FlipNormalY = flip->get<bool>();
                }
            }
            result.Nodes.push_back(node);
        }
        MaterialProperties checked; if (!EvaluateMaterial(result, checked, error)) return false;
        graph = std::move(result); return true;
    }
    catch (const std::exception& e) { error = e.what(); return false; }
}

bool DecodeMaterialDataTexture(const std::filesystem::path& file, std::shared_ptr<jImageData>& output, std::string& error)
{
    try
    {
        constexpr uintmax_t maxFileBytes = 32 * 1024 * 1024;
        const auto bytes = std::filesystem::file_size(file);
        if (!bytes || bytes > maxFileBytes) { error = "Mapping data file must contain 1..32 MiB"; return false; }
        std::ifstream input(file, std::ios::binary); std::vector<stbi_uc> encoded(static_cast<size_t>(bytes));
        if (!input.read(reinterpret_cast<char*>(encoded.data()), std::streamsize(encoded.size())))
        { error = "Cannot read complete mapping data file"; return false; }
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels)
            || width <= 0 || height <= 0 || width > 8192 || height > 8192 || uint64_t(width) * height > 36000000)
        { error = "Mapping data image exceeds 36,000,000 texels / 8192 per axis or is invalid"; return false; }
        std::unique_ptr<stbi_uc, void(*)(void*)> pixels(stbi_load_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels, 4), stbi_image_free);
        if (!pixels) { error = "Mapping data image decode failed"; return false; }
        auto image = std::make_shared<jImageData>();
        image->Filename = jName(jEditorAssetPath::ToUtf8(file)); image->Width = width; image->Height = height;
        image->sRGB = false; image->Format = ETextureFormat::RGBA8; image->FormatType = EFormatType::UNSIGNED_BYTE;
        image->MipLevel = 1; image->LayerCount = 1; image->CreateMipmapIfPossible = false;
        const size_t size = size_t(width) * size_t(height) * 4;
        image->ImageBulkData.ImageData.assign(pixels.get(), pixels.get() + size);
        image->ImageBulkData.SubresourceFootprints.resize(1);
        auto& footprint = image->ImageBulkData.SubresourceFootprints[0];
        footprint.Width = width; footprint.Height = height; footprint.RowPitch = width * 4;
        footprint.Depth = 0; footprint.MipLevel = 0; footprint.Offset = 0;
        output = std::move(image); error.clear(); return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
namespace
{
constexpr size_t MappingCacheBytes = 256 * 1024 * 1024, MappingCacheEntries = 64, MappingWorkerCount = 2;
struct MappingImageResult { std::shared_ptr<jImageData> Image; std::string Error; uint64_t Generation = 0; };
struct MappingImageEntry
{
    std::filesystem::path Path;
    std::filesystem::file_time_type Modified{};
    uintmax_t FileBytes = 0;
    uint64_t Generation = 1, Touch = 0;
    bool Waiting = false, Failed = false;
    std::future<MappingImageResult> Future;
    std::shared_ptr<jImageData> Image;
    std::string Error;
};
struct MappingImageGroup { std::vector<std::string> Keys; std::chrono::steady_clock::time_point Touch; };
struct MappingImageCache
{
    std::map<std::string, MappingImageEntry> Entries;
    std::map<std::string, MappingImageGroup> Groups;
    std::vector<std::future<MappingImageResult>> Retired;
    uint64_t Epoch = 1, Clock = 0;
};
MappingImageCache& MappingImages() { static MappingImageCache cache; return cache; }
bool MappingKeyPinned(const std::string& key)
{
    for (const auto& [name, group] : MappingImages().Groups)
        if (std::find(group.Keys.begin(), group.Keys.end(), key) != group.Keys.end()) return true;
    return false;
}
size_t MappingDecodedBytes()
{
    size_t bytes = 0;
    for (const auto& [key, entry] : MappingImages().Entries) if (entry.Image) bytes += entry.Image->ImageBulkData.ImageData.size();
    return bytes;
}
bool MappingMakeRoom(size_t bytes, bool newEntry, const std::string& protectedKey = {})
{
    auto& cache = MappingImages();
    while (MappingDecodedBytes() + bytes > MappingCacheBytes || cache.Entries.size() + size_t(newEntry) > MappingCacheEntries)
    {
        auto victim = cache.Entries.end();
        for (auto entry = cache.Entries.begin(); entry != cache.Entries.end(); ++entry)
            if (entry->first != protectedKey && !entry->second.Future.valid() && !entry->second.Waiting && !MappingKeyPinned(entry->first)
                && (victim == cache.Entries.end() || entry->second.Touch < victim->second.Touch)) victim = entry;
        if (victim == cache.Entries.end()) return false;
        cache.Entries.erase(victim);
    }
    return true;
}
void MappingRemoveGroups(const std::string& key)
{
    std::erase_if(MappingImages().Groups, [&](const auto& pair)
    { return std::find(pair.second.Keys.begin(), pair.second.Keys.end(), key) != pair.second.Keys.end(); });
}
void MappingStartJobs()
{
    auto& cache = MappingImages(); size_t active = cache.Retired.size();
    for (const auto& [key, entry] : cache.Entries) active += entry.Future.valid();
    for (auto& [key, entry] : cache.Entries)
    {
        if (active >= MappingWorkerCount) break;
        if (!entry.Waiting || entry.Future.valid()) continue;
        const auto path = entry.Path; const auto generation = entry.Generation;
        entry.Waiting = false;
        try
        {
            entry.Future = std::async(std::launch::async, [path, generation]
            {
                MappingImageResult result; result.Generation = generation;
                DecodeMaterialDataTexture(path, result.Image, result.Error); return result;
            });
        }
        catch (const std::exception& exception)
        { entry.Failed = true; entry.Error = exception.what(); MappingRemoveGroups(key); ++cache.Epoch; continue; }
        ++active;
    }
}
bool MappingHasActiveWork()
{
    const auto& cache = MappingImages();
    if (!cache.Retired.empty()) return true;
    for (const auto& [key, entry] : cache.Entries) if (entry.Waiting || entry.Future.valid()) return true;
    return false;
}
bool IsRawMappingTexture(const jMaterialMapping::TextureBinding& texture, const std::filesystem::path& path)
{
    if (texture.Semantic == jMaterialMapping::TextureSemantic::Color) return false;
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return extension == ".png" || extension == ".tga" || extension == ".bmp" || extension == ".jpg" || extension == ".jpeg";
}
}
uint64_t PollMaterialMappingTexturePreparation()
{
    auto& cache = MappingImages();
    for (auto it = cache.Retired.begin(); it != cache.Retired.end();)
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) { try { it->get(); } catch (...) {} it = cache.Retired.erase(it); ++cache.Epoch; }
        else ++it;
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(cache.Groups, [&](const auto& group) { return now - group.second.Touch > std::chrono::seconds(30); });
    for (auto& [key, entry] : cache.Entries)
    {
        if (!entry.Future.valid() || entry.Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        MappingImageResult result;
        try { result = entry.Future.get(); }
        catch (const std::exception& exception) { result.Error = exception.what(); result.Generation = entry.Generation; }
        ++cache.Epoch;
        std::error_code ec; const auto bytes = std::filesystem::file_size(entry.Path, ec);
        const auto modified = ec ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(entry.Path, ec);
        if (result.Generation != entry.Generation || (!ec && (bytes != entry.FileBytes || modified != entry.Modified)))
        {
            ++entry.Generation; entry.FileBytes = bytes; entry.Modified = modified; entry.Waiting = true; continue;
        }
        if (!result.Image) { entry.Failed = true; entry.Error = result.Error; MappingRemoveGroups(key); continue; }
        // Current groups are pinned until all their source images are ready.
        // Reject an oversized working set rather than endlessly evict/redecode it.
        const size_t size = result.Image->ImageBulkData.ImageData.size();
        if (!MappingMakeRoom(size, false, key))
        { entry.Failed = true; entry.Error = "Mapping data preparation exceeds its 256 MiB / 64-entry budget; reduce data-map resolution"; MappingRemoveGroups(key); continue; }
        entry.Image = std::move(result.Image); entry.Failed = false; entry.Error.clear();
    }
    MappingStartJobs(); return cache.Epoch;
}
size_t PendingMaterialMappingTextureCount()
{
    std::set<std::string> pending;
    const auto& cache = MappingImages();
    for (const auto& [key, entry] : cache.Entries) if (entry.Waiting || entry.Future.valid()) pending.insert(key);
    for (const auto& [name, group] : cache.Groups)
        for (const auto& key : group.Keys) if (!cache.Entries.contains(key)) pending.insert(key);
    return pending.size();
}
void InvalidateMaterialMappingTextureCache(const std::filesystem::path& root, const std::string& source)
{
    auto& cache = MappingImages();
    try
    {
        const auto sourcePath = jEditorAssetPath::FromUtf8(source);
        std::error_code ec; const auto path = std::filesystem::weakly_canonical(sourcePath.is_absolute() ? sourcePath : root / sourcePath, ec);
        if (ec) return;
        const auto key = jEditorAssetPath::ToUtf8(path);
        const auto found = cache.Entries.find(key);
        if (found != cache.Entries.end())
        {
            if (found->second.Future.valid()) cache.Retired.push_back(std::move(found->second.Future));
            cache.Entries.erase(found); MappingRemoveGroups(key); ++cache.Epoch;
        }
    }
    catch (...) {}
}
void CancelMaterialMappingTexturePreparation()
{
    auto& cache = MappingImages();
    for (auto& [key, entry] : cache.Entries) if (entry.Future.valid()) cache.Retired.push_back(std::move(entry.Future));
    cache.Entries.clear(); cache.Groups.clear(); ++cache.Epoch;
}

bool PrepareMaterialMapping(const std::filesystem::path& root, const MaterialGraph& graph,
    PreparedMaterialMapping& output, std::string& error, bool* pending)
{
    if (pending) *pending = false;
    if (GMainThreadID != std::thread::id() && GMainThreadID != std::this_thread::get_id())
    { error = "Mapping texture resources must prepare on the engine owner thread"; return false; }
    try
    {
    jMaterialMapping::Program program;
    if (!jMaterialMapping::Compile(graph, program, error)) return false;
    PreparedMaterialMapping prepared;
    if (program.Active)
    {
        std::array<std::filesystem::path, jMaterialMapping::MaximumTextures> paths;
        std::array<std::shared_ptr<jImageData>, jMaterialMapping::MaximumTextures> images;
        std::vector<std::string> keys; std::string groupKey;
        for (size_t i = 0; i < program.Textures.size(); ++i)
        {
            const auto path = jEditorProject::ResolveSafe(root, jEditorAssetPath::FromUtf8(program.Textures[i].Path), error, true);
            if (!path) return false;
            paths[i] = *path;
            if (IsRawMappingTexture(program.Textures[i], *path))
            { keys.push_back(jEditorAssetPath::ToUtf8(*path)); groupKey += keys.back() + '\n'; }
        }
        auto& cache = MappingImages();
        if (!keys.empty())
        {
            if (cache.Groups.size() >= MappingCacheEntries && !cache.Groups.contains(groupKey))
            {
                if (MappingHasActiveWork()) { if (pending) *pending = true; error = "Mapping data preparation queue is busy"; }
                else error = "Mapping data preparation exceeds 64 pending groups; cancel stale preparation and retry";
                return false;
            }
            cache.Groups[groupKey] = {keys, std::chrono::steady_clock::now()};
        }
        PollMaterialMappingTexturePreparation();
        bool waiting = false;
        for (size_t i = 0; i < program.Textures.size(); ++i)
        {
            if (!IsRawMappingTexture(program.Textures[i], paths[i])) continue;
            const auto key = jEditorAssetPath::ToUtf8(paths[i]);
            const auto bytes = std::filesystem::file_size(paths[i]); const auto modified = std::filesystem::last_write_time(paths[i]);
            auto found = cache.Entries.find(key);
            if (found != cache.Entries.end() && (found->second.FileBytes != bytes || found->second.Modified != modified))
            {
                if (found->second.Future.valid()) cache.Retired.push_back(std::move(found->second.Future));
                cache.Entries.erase(found); found = cache.Entries.end(); ++cache.Epoch;
            }
            if (found == cache.Entries.end())
            {
                if (!MappingMakeRoom(0, true))
                {
                    if (!MappingHasActiveWork())
                    { cache.Groups.erase(groupKey); error = "Mapping data preparation exceeds its pinned 64-entry budget; reduce the working set or retry"; return false; }
                    waiting = true; continue;
                }
                MappingImageEntry entry; entry.Path = paths[i]; entry.FileBytes = bytes; entry.Modified = modified; entry.Waiting = true;
                found = cache.Entries.emplace(key, std::move(entry)).first;
            }
            auto& entry = found->second; entry.Touch = ++cache.Clock;
            if (entry.Failed) { error = entry.Error + ": " + program.Textures[i].Path; cache.Groups.erase(groupKey); return false; }
            if (!entry.Image) { waiting = true; continue; }
            images[i] = entry.Image;
        }
        MappingStartJobs();
        if (waiting) { if (pending) *pending = true; error = "Preparing linear mapping data textures on CPU"; return false; }
        for (size_t i = 0; i < program.Textures.size(); ++i)
        {
            if (!g_rhi) { cache.Groups.erase(groupKey); error = "Material mapping textures require an initialized rendering device"; return false; }
            auto& loader = jImageFileLoader::GetInstance();
            auto texture = images[i] ? loader.LoadPreparedTextureOwned(images[i]) : loader.LoadTextureOwned(jName(jEditorAssetPath::ToUtf8(paths[i])));
            if (!texture) { cache.Groups.erase(groupKey); error = "Could not load mapping texture " + program.Textures[i].Path; return false; }
            prepared.Textures[i] = std::move(texture);
        }
        cache.Groups.erase(groupKey);
        prepared.Program = std::make_shared<jMaterialMapping::Program>(std::move(program));
    }
    output = std::move(prepared); error.clear(); return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
void ApplyMaterialMapping(jMaterial& material, const PreparedMaterialMapping& prepared, uint32_t overrideMask)
{
    material.MappingProgram = prepared.Program; material.MappingTextures = prepared.Textures;
    for (size_t i = 0; i < material.MappingValues.size(); ++i)
    {
        const auto value = prepared.Program ? prepared.Program->Values[i] : std::array<float, 4>{};
        material.MappingValues[i] = Vector4(value[0], value[1], value[2], value[3]);
    }
    material.MappingOutputOverridesMask = prepared.Program ? overrideMask : 0;
    material.NeedToUpdateShaderBindingInstance = true; ++material.Revision;
}
uint32_t MaterialMappingOverrides(const MaterialProperties& material)
{
    using namespace jMaterialMapping;
    const auto& flags = material.GraphOverrides;
    if (!flags) return 0;
    return (flags->BaseColor ? OverrideBaseColor : 0u) | (flags->NormalTexture ? OverrideNormal : 0u)
        | (flags->Metallic ? OverrideMetallic : 0u) | (flags->Roughness ? OverrideRoughness : 0u)
        | (flags->BaseColorTexture ? OverrideBaseColorTexture : 0u)
        | (flags->SubsurfaceWeight ? OverrideSubsurfaceWeight : 0u) | (flags->SubsurfaceColor ? OverrideSubsurfaceColor : 0u)
        | (flags->SubsurfaceThickness ? OverrideSubsurfaceThickness : 0u) | (flags->SubsurfaceProfile ? OverrideSubsurfaceProfile : 0u)
        | (flags->SpecularWeight ? OverrideSpecularWeight : 0u) | (flags->DiffuseOcclusion ? OverrideDiffuseOcclusion : 0u)
        | (flags->PreScatterWeight ? OverridePreScatterWeight : 0u) | (flags->ShadingModel ? OverrideShadingModel : 0u)
        | (flags->SubsurfaceSpace ? OverrideSubsurfaceSpace : 0u);
}

bool ApplySceneMaterialLibrary(const std::filesystem::path& source, const std::vector<jObject*>& objects,
    size_t& appliedSlots, std::string& error, bool* pending, bool allowRasterOnly)
{
    if (pending) *pending = false;
    appliedSlots = 0;
    error.clear();
    try
    {
        const auto bindings = ReadSceneMaterialBindings(source);
        if (bindings.empty()) return true;
        const auto root = std::filesystem::absolute(source).parent_path();

        const auto loadTexture = [&](jMaterial& target, jMaterial::EMaterialTextureType slot, const std::string& relative)
        {
            auto& destination = target.TexData[int(slot)];
            destination.Texture = nullptr;
            if (relative.empty()) return true;
            const auto path = jEditorProject::ResolveSafe(root, jEditorAssetPath::FromUtf8(relative), error, true);
            if (!path) return false;
            destination.FilePath = jName(jEditorAssetPath::ToUtf8(*path));
            auto texture = jImageFileLoader::GetInstance().LoadTextureOwned(destination.FilePath);
            if (!texture) { error = "Could not load material texture " + relative; return false; }
            destination.Texture = texture.get();
            target.OwnedTextures.push_back(std::move(texture));
            return true;
        };

        std::map<std::string, std::shared_ptr<jMaterial>> resolved;
        std::vector<std::pair<jRenderObject*, std::shared_ptr<jMaterial>>> staged;
        for (auto* object : objects)
            for (auto* render : object->RenderObjects)
            {
                const auto& original = render->MaterialPtr;
                if (!original || original->SourceMaterialName.empty()) continue;
                const auto match = bindings.find(original->SourceMaterialName);
                if (match == bindings.end()) continue;
                auto& target = resolved[original->SourceMaterialName];
                if (!target)
                {
                    const auto path = jEditorProject::ResolveSafe(root, jEditorAssetPath::FromUtf8(match->get<std::string>()), error, true);
                    MaterialGraph graph;
                    MaterialProperties properties;
                    if (!path || !LoadMaterial(*path, graph, error) || !EvaluateMaterial(graph, properties, error)) return false;
                    if (!allowRasterOnly)
                    {
                        jMaterialMapping::Program required;
                        if (!jMaterialMapping::Compile(graph, required, error)) return false;
                        if (required.Active) { error = "Mapping materials require Deferred or Forward; original scene materials were retained."; return false; }
                    }
                    PreparedMaterialMapping mapping;
                    if (!PrepareMaterialMapping(root, graph, mapping, error, pending)) return false;
                    if (const auto mesh = std::dynamic_pointer_cast<jMeshMaterial>(original)) target = std::make_shared<jMeshMaterial>(*mesh);
                    else target = std::make_shared<jMaterial>(*original);
                    target->ShaderBindingInstance.reset();
                    target->NeedToUpdateShaderBindingInstance = true;
                    target->UseMaterialProperties = true;
                    target->BaseColorOpacity = Vector4(properties.BaseColor.X, properties.BaseColor.Y, properties.BaseColor.Z, properties.Opacity);
                    target->Emissive = Vector(properties.Emissive.X, properties.Emissive.Y, properties.Emissive.Z);
                    target->MetallicFactor = properties.Metallic;
                    target->RoughnessFactor = properties.Roughness;
                    ApplyMaterialMapping(*target, mapping, 0);
                    const auto surface = graph.Surface.value_or(MaterialGraph::SurfaceProperties{});
                    target->Surface = surface.Shading;
                    target->SubsurfaceOnlySurface = false;
                    target->bRaytracingAlphaTest = surface.Shading.AlphaMode == jSurfaceAlphaMode::Mask;
                    target->RaytracingAlphaCutoff = surface.Shading.AlphaCutoff;
                    using Slot = jMaterial::EMaterialTextureType;
                    const auto* environment = target->TexData[int(Slot::Env)].Texture;
                    std::erase_if(target->OwnedTextures, [environment](const auto& texture) { return texture.get() != environment; });
                    if (!loadTexture(*target, Slot::Albedo, properties.BaseColorTexture) ||
                        !loadTexture(*target, Slot::Normal, properties.NormalTexture) ||
                        !loadTexture(*target, Slot::Metallic, surface.MetallicRoughnessTexture) ||
                        !loadTexture(*target, Slot::Emission, surface.EmissionTexture)) return false;
                    if (!target->HasAlbedoTexture()) target->TexData[int(Slot::Albedo)].Texture = GWhiteTexture.get();
                    ++target->Revision;
                }
                staged.emplace_back(render, target);
            }
        if (staged.empty() && !bindings.empty()) { error = "No source material slots matched the library"; return false; }
        if (!staged.empty() && g_rhi) g_rhi->Flush();
        for (auto& [render, material] : staged)
        {
            if (g_rhi && g_rhi->RaytracingScene && render->IsSupportRaytracing() &&
                material->IsRaytracingAlphaTestEnabled() != render->MaterialPtr->IsRaytracingAlphaTestEnabled())
                g_rhi->RaytracingScene->MarkBLASDirty(render);
            render->MaterialPtr = std::move(material);
        }
        appliedSlots = resolved.size();
        return true;
    }
    catch (const std::exception& e) { error = e.what(); return false; }
}

namespace
{
Vector4 SamplePreviewMap(const jEditorThumbnail& image, float u, float v)
{
    const auto x = std::min(uint32_t(u * float(image.Width)), image.Width - 1);
    const auto y = std::min(uint32_t(v * float(image.Height)), image.Height - 1);
    const auto* rgba = image.Rgba.data() + (size_t(y) * image.Width + x) * 4;
    return Vector4(rgba[0], rgba[1], rgba[2], rgba[3]) * (1.f / 255);
}
}

bool PrepareMaterialThumbnail(const std::filesystem::path& root, const std::filesystem::path& file,
    jEditorThumbnail& thumbnail, std::string& diagnostic)
{
    thumbnail = {};
    MaterialGraph graph;
    MaterialProperties material;
    if (!LoadMaterial(file, graph, diagnostic) || !EvaluateMaterial(graph, material, diagnostic)) return false;
    if (graph.Horizon || std::any_of(graph.Nodes.begin(), graph.Nodes.end(), [](const auto& node) { return node.Kind > NodeKind::Add; }))
        diagnostic = "Basic CPU preview; mapped graph shading is evaluated in the Scene viewport.";

    const auto loadMap = [&](const std::string& source, const char* label, jEditorThumbnail& image)
    {
        if (source.empty()) return;
        std::string error;
        const auto path = jEditorProject::ResolveSafe(root, source, error, true);
        if (path && PrepareEditorThumbnail(*path, image, error, 256)) return;
        if (!diagnostic.empty()) diagnostic += '\n';
        diagnostic += std::string(label) + " preview unavailable. " + error;
    };
    jEditorThumbnail albedo, normalMap;
    loadMap(material.BaseColorTexture, "Base Color map", albedo);
    loadMap(material.NormalTexture, "Normal map", normalMap);

    constexpr uint32_t edge = 128;
    constexpr float center = edge * .5f, radius = edge * .44f;
    thumbnail = {edge, edge, std::vector<uint8_t>(edge * edge * 4)};
    const Vector tint(material.BaseColor.X, material.BaseColor.Y, material.BaseColor.Z);
    const Vector key = Vector(-.6f, .7f, 1.f).GetNormalize();
    const Vector half = (key + Vector(0, 0, 1)).GetNormalize();
    const float gloss = std::lerp(96.f, 5.f, material.Roughness);
    const auto encode = [](float linear)
    {
        return uint8_t(std::lround(std::pow(std::clamp(linear, 0.f, 1.f), 1.f / 2.2f) * 255.f));
    };

    // Orthographic sphere under fixed studio lighting, independent of the scene renderer.
    for (uint32_t y = 0; y < edge; ++y)
        for (uint32_t x = 0; x < edge; ++x)
        {
            const float sx = (float(x) + .5f - center) / radius;
            const float sy = (center - float(y) - .5f) / radius;
            const float r2 = sx * sx + sy * sy;
            if (r2 >= 1.f) continue;
            Vector normal(sx, sy, std::sqrt(1.f - r2));
            const float u = .5f + std::atan2(normal.x, normal.z) / (2.f * PI);
            const float v = std::acos(normal.y) / PI;
            const auto texel = albedo.Rgba.empty() ? Vector4(1.f) : SamplePreviewMap(albedo, u, v);
            if (texel.w < .5f) continue;
            const Vector base = tint * Vector(std::pow(texel.x, 2.2f), std::pow(texel.y, 2.2f), std::pow(texel.z, 2.2f));
            if (!normalMap.Rgba.empty())
            {
                const auto sample = SamplePreviewMap(normalMap, u, v);
                const auto tangent = Vector(normal.z, 0, -normal.x).GetNormalize();
                const auto bitangent = normal.CrossProduct(tangent);
                normal = (tangent * (2.f * sample.x - 1.f) + bitangent * (2.f * sample.y - 1.f) +
                    normal * (2.f * sample.z - 1.f)).GetNormalize();
            }
            const float diffuse = (.20f + .72f * std::max(0.f, normal.DotProduct(key))) * (1.f - .75f * material.Metallic);
            const float highlight = .12f + 1.35f * std::pow(std::max(0.f, normal.DotProduct(half)), gloss);
            const Vector reflectance = Vector(.04f * (1.f - material.Metallic)) + base * material.Metallic;
            const Vector color = base * diffuse + reflectance * highlight;
            auto* pixel = thumbnail.Rgba.data() + (size_t(y) * edge + x) * 4;
            pixel[0] = encode(color.x); pixel[1] = encode(color.y); pixel[2] = encode(color.z);
            pixel[3] = uint8_t(std::lround(std::min(1.f, (1.f - std::sqrt(r2)) * radius) * 255.f));
        }
    return true;
}

void InheritMaterialGraph(MaterialProperties& instance, const MaterialProperties& defaults)
{
    if (!instance.GraphOverrides) return;
    const auto& overrides = *instance.GraphOverrides;
    if (!overrides.BaseColor) instance.BaseColor = defaults.BaseColor;
    if (!overrides.BaseColorTexture) instance.BaseColorTexture = defaults.BaseColorTexture;
    if (!overrides.Metallic) instance.Metallic = defaults.Metallic;
    if (!overrides.Roughness) instance.Roughness = defaults.Roughness;
    if (!overrides.NormalTexture) instance.NormalTexture = defaults.NormalTexture;
    if (!overrides.SubsurfaceWeight) instance.SubsurfaceWeight = defaults.SubsurfaceWeight;
    if (!overrides.SubsurfaceColor) instance.SubsurfaceColor = defaults.SubsurfaceColor;
    if (!overrides.SubsurfaceThickness) instance.SubsurfaceThicknessWorld = defaults.SubsurfaceThicknessWorld;
    if (!overrides.SubsurfaceProfile) instance.SubsurfaceProfile = defaults.SubsurfaceProfile;
    instance.SubsurfaceOverrideEnabled = defaults.SubsurfaceOverrideEnabled
        || overrides.SubsurfaceWeight || overrides.SubsurfaceColor || overrides.SubsurfaceThickness || overrides.SubsurfaceProfile;
    if (!overrides.ShadingModel) instance.ShadingModel = defaults.ShadingModel;
    if (!overrides.SubsurfaceSpace) instance.SubsurfaceSpace = defaults.SubsurfaceSpace;
    if (!overrides.SpecularWeight) instance.SpecularWeight = defaults.SpecularWeight;
    if (!overrides.DiffuseOcclusion) instance.DiffuseOcclusion = defaults.DiffuseOcclusion;
    if (!overrides.PreScatterWeight) instance.PreScatterWeight = defaults.PreScatterWeight;
    instance.SkinOverrideEnabled = defaults.SkinOverrideEnabled
        || overrides.ShadingModel || overrides.SubsurfaceSpace || overrides.SpecularWeight || overrides.DiffuseOcclusion || overrides.PreScatterWeight;
}

bool AssignSharedMaterial(jEditor& editor, EntityId id, const std::string& path, std::string& error)
{
    auto& document = editor.GetDocument();
    const auto* selected = document.Find(id);
    if (!selected || document.IsPlaying() || document.IsEffectivelyLocked(id))
    { error = "Select an editable object to assign a shared material"; return false; }
    const auto file = jEditorProject::ResolveSafe(editor.GetProjectRoot(), jEditorAssetPath::FromUtf8(path), error, true);
    MaterialGraph graph;
    MaterialProperties defaults;
    if (!file || !LoadMaterial(*file, graph, error) || !EvaluateMaterial(graph, defaults, error)) return false;
    auto entity = *selected;
    auto& material = entity.Material;
    if (!material.GraphOverrides || material.GraphAsset != path)
    {
        const bool localNormal = !material.NormalTexture.empty() && (!material.GraphOverrides || material.GraphOverrides->NormalTexture);
        material.GraphOverrides.emplace().NormalTexture = localNormal;
    }
    material.GraphAsset = path;
    material.OverrideEnabled = true;
    InheritMaterialGraph(material, defaults);
    if (!document.Update(entity, "Assign shared material")) { error = document.LastError(); return false; }
    editor.ReimportAsset(path);
    return true;
}

Mesh BuildTerrain(const TerrainProperties& terrain)
{
    jEditorProcedural::Mesh source; Mesh result; std::string error;
    if (!jEditorProcedural::BuildTerrainMesh(terrain, source, error)) return result;
    for (const auto& v : source.Vertices) { result.Positions.push_back(v.Position); result.Normals.push_back(v.Normal); }
    result.Indices = std::move(source.Indices); return result;
}
bool FindTerrainHit(const Mesh& mesh, const Vector& origin, const Vector& direction, Vector& hit)
{
    float closest = std::numeric_limits<float>::infinity();
    const auto vertex = [&](uint32_t index)
    {
        const auto& p = mesh.Positions[index];
        return Vector(p.X, p.Y, p.Z);
    };
    for (size_t i = 0; i < mesh.Indices.size(); i += 3)
    {
        const Vector a = vertex(mesh.Indices[i]);
        const Vector ab = vertex(mesh.Indices[i + 1]) - a;
        const Vector ac = vertex(mesh.Indices[i + 2]) - a;
        const Vector p = direction.CrossProduct(ac);
        const float det = ab.DotProduct(p);
        if (det == 0) continue;
        const Vector offset = origin - a;
        const float u = offset.DotProduct(p) / det;
        if (u < 0 || u > 1) continue;
        const Vector q = offset.CrossProduct(ab);
        const float v = direction.DotProduct(q) / det;
        if (v < 0 || u + v > 1) continue;
        const float distance = ac.DotProduct(q) / det;
        if (distance >= 0 && distance < closest) closest = distance;
    }
    if (!std::isfinite(closest)) return false;
    hit = origin + direction * closest;
    return true;
}
bool PaintTerrain(TerrainProperties& terrain, float x, float z, float radius, float strength, int mode)
{
    if (terrain.Resolution < 2 || terrain.Resolution > 129 || !Finite(x) || !Finite(z) || !Finite(radius) || radius <= 0 || !Finite(strength) || !Finite(terrain.SizeX) || !Finite(terrain.SizeZ) || terrain.SizeX <= 0 || terrain.SizeZ <= 0 || mode < 0 || mode > 3) return false;
    const size_t n = terrain.Resolution;
    if (terrain.Heights.empty()) terrain.Heights.assign(n * n, 0);
    if (terrain.Heights.size() != n * n) return false;
    auto result = terrain.Heights;
    for (size_t row = 0; row < n; ++row) for (size_t col = 0; col < n; ++col)
    {
        const float px = (float(col) / float(n - 1) - .5f) * terrain.SizeX, pz = (float(row) / float(n - 1) - .5f) * terrain.SizeZ;
        const float distance = std::sqrt((px - x) * (px - x) + (pz - z) * (pz - z)); if (distance > radius) continue;
        const float weight = 1 - distance / radius; float value = terrain.Heights[row * n + col];
        if (mode == 0) value += std::abs(strength) * weight;
        if (mode == 1) value -= std::abs(strength) * weight;
        if (mode == 2) value += (strength - value) * weight;
        if (mode == 3)
        {
            float sum = 0; int count = 0;
            for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx)
            {
                const int cx = int(col) + dx, rz = int(row) + dz;
                if (cx >= 0 && rz >= 0 && cx < int(n) && rz < int(n)) { sum += terrain.Heights[size_t(rz) * n + size_t(cx)]; ++count; }
            }
            value += (sum / count - value) * weight;
        }
        if (!Finite(value) || std::abs(value) > 1e6f) return false; result[row * n + col] = value;
    }
    terrain.Heights = std::move(result); return true;
}
bool BakeNavigation(const Mesh& mesh, const NavigationProperties& settings, const Vec3& start, const Vec3& end, NavigationBake& output, std::string& error)
{
    try
    {
        if (mesh.Positions.empty() || mesh.Positions.size() > 1000000 || mesh.Indices.empty() || mesh.Indices.size() % 3 || mesh.Indices.size() > 6000000) throw std::runtime_error("Invalid navigation source mesh");
        if (!Finite(settings.CellSize) || settings.CellSize < .01f || !Finite(settings.AgentHeight) || settings.AgentHeight <= 0 || !Finite(settings.AgentRadius) || settings.AgentRadius < 0 || !Finite(settings.MaxSlopeDegrees) || settings.MaxSlopeDegrees < 0 || settings.MaxSlopeDegrees >= 90) throw std::runtime_error("Invalid navigation agent settings");
        std::vector<float> vertices; vertices.reserve(mesh.Positions.size() * 3); std::vector<int> indices; indices.reserve(mesh.Indices.size());
        for (const auto& v : mesh.Positions) { if (!Finite(v.X) || !Finite(v.Y) || !Finite(v.Z)) throw std::runtime_error("Invalid navigation vertex"); vertices.insert(vertices.end(), {v.X, v.Y, v.Z}); }
        for (auto index : mesh.Indices) { if (index >= mesh.Positions.size()) throw std::runtime_error("Invalid navigation triangle"); indices.push_back(int(index)); }
        rcContext context; rcConfig cfg{}; cfg.cs = settings.CellSize; cfg.ch = std::max(.05f, cfg.cs / 4);
        cfg.walkableSlopeAngle = settings.MaxSlopeDegrees; cfg.walkableHeight = int(std::ceil(settings.AgentHeight / cfg.ch));
        cfg.walkableClimb = int(std::floor(cfg.cs / cfg.ch)); cfg.walkableRadius = int(std::ceil(settings.AgentRadius / cfg.cs));
        cfg.maxEdgeLen = 12; cfg.maxSimplificationError = 1.3f; cfg.minRegionArea = 4; cfg.mergeRegionArea = 16; cfg.maxVertsPerPoly = 6;
        cfg.detailSampleDist = cfg.cs * 6; cfg.detailSampleMaxError = cfg.ch;
        rcCalcBounds(vertices.data(), int(mesh.Positions.size()), cfg.bmin, cfg.bmax); cfg.bmin[1] -= cfg.ch * 2; cfg.bmax[1] += settings.AgentHeight * 2;
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
        if (cfg.width <= 0 || cfg.height <= 0 || cfg.width > 512 || cfg.height > 512 || cfg.walkableHeight > 65535 || cfg.walkableRadius > 255) throw std::runtime_error("Navigation voxel budget exceeded; increase cell size");
        auto height = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>(rcAllocHeightfield(), rcFreeHeightField);
        auto compact = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        auto contours = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>(rcAllocContourSet(), rcFreeContourSet);
        auto poly = std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>(rcAllocPolyMesh(), rcFreePolyMesh);
        auto detail = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
        if (!height || !compact || !contours || !poly || !detail || !rcCreateHeightfield(&context, *height, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) throw std::runtime_error("Cannot allocate navigation heightfield");
        std::vector<unsigned char> areas(indices.size() / 3);
        rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, vertices.data(), int(mesh.Positions.size()), indices.data(), int(areas.size()), areas.data());
        if (!rcRasterizeTriangles(&context, vertices.data(), int(mesh.Positions.size()), indices.data(), areas.data(), int(areas.size()), *height, cfg.walkableClimb)) throw std::runtime_error("Navigation rasterization failed");
        rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *height); rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *height); rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *height);
        if (!rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *height, *compact) || !rcErodeWalkableArea(&context, cfg.walkableRadius, *compact) || !rcBuildDistanceField(&context, *compact) || !rcBuildRegions(&context, *compact, 0, cfg.minRegionArea, cfg.mergeRegionArea) || !rcBuildContours(&context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours) || !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *poly) || !rcBuildPolyMeshDetail(&context, *poly, *compact, cfg.detailSampleDist, cfg.detailSampleMaxError, *detail)) throw std::runtime_error("Navigation mesh build failed");
        if (!poly->npolys) throw std::runtime_error("No walkable polygons; check winding, slope and agent size");
        for (int i = 0; i < poly->npolys; ++i) { poly->flags[i] = 1; poly->areas[i] = 0; }
        dtNavMeshCreateParams params{}; params.verts = poly->verts; params.vertCount = poly->nverts; params.polys = poly->polys; params.polyAreas = poly->areas; params.polyFlags = poly->flags; params.polyCount = poly->npolys; params.nvp = poly->nvp;
        params.detailMeshes = detail->meshes; params.detailVerts = detail->verts; params.detailVertsCount = detail->nverts; params.detailTris = detail->tris; params.detailTriCount = detail->ntris;
        params.walkableHeight = settings.AgentHeight; params.walkableRadius = settings.AgentRadius; params.walkableClimb = cfg.walkableClimb * cfg.ch;
        std::copy(poly->bmin, poly->bmin + 3, params.bmin); std::copy(poly->bmax, poly->bmax + 3, params.bmax); params.cs = cfg.cs; params.ch = cfg.ch; params.buildBvTree = true;
        unsigned char* data = nullptr; int bytes = 0;
        if (!dtCreateNavMeshData(&params, &data, &bytes)) throw std::runtime_error("Detour data creation failed");
        auto nav = std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)>(dtAllocNavMesh(), dtFreeNavMesh);
        if (!nav || dtStatusFailed(nav->init(data, bytes, DT_TILE_FREE_DATA))) { dtFree(data); throw std::runtime_error("Detour initialization failed"); }
        auto query = std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)>(dtAllocNavMeshQuery(), dtFreeNavMeshQuery);
        if (!query || dtStatusFailed(query->init(nav.get(), 2048))) throw std::runtime_error("Detour query allocation failed");
        dtQueryFilter filter; const float extents[3] = {cfg.cs * 3, settings.AgentHeight * 2, cfg.cs * 3};
        const float from[3] = {start.X, start.Y, start.Z}, to[3] = {end.X, end.Y, end.Z}; float nearestFrom[3], nearestTo[3]; dtPolyRef a = 0, b = 0;
        query->findNearestPoly(from, extents, &filter, &a, nearestFrom); query->findNearestPoly(to, extents, &filter, &b, nearestTo);
        NavigationBake result; result.PolygonCount = uint32_t(poly->npolys);
        for (int i = 0; i < poly->nverts; ++i) result.DebugMesh.Positions.push_back({poly->bmin[0] + poly->verts[i * 3] * cfg.cs, poly->bmin[1] + poly->verts[i * 3 + 1] * cfg.ch, poly->bmin[2] + poly->verts[i * 3 + 2] * cfg.cs});
        for (int p = 0; p < poly->npolys; ++p)
        {
            const auto* ids = &poly->polys[p * poly->nvp * 2];
            for (int i = 2; i < poly->nvp && ids[i] != RC_MESH_NULL_IDX; ++i) result.DebugMesh.Indices.insert(result.DebugMesh.Indices.end(), {ids[0], ids[i - 1], ids[i]});
        }
        if (a && b)
        {
            dtPolyRef path[512]; int count = 0;
            if (dtStatusSucceed(query->findPath(a, b, nearestFrom, nearestTo, &filter, path, &count, 512)) && count && path[count - 1] == b)
            {
                float straight[512 * 3]; int points = 0;
                if (dtStatusSucceed(query->findStraightPath(nearestFrom, nearestTo, path, count, straight, nullptr, nullptr, &points, 512)))
                    for (int i = 0; i < points; ++i) result.Path.push_back({straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]});
            }
        }
        output = std::move(result); return true;
    }
    catch (const std::exception& e) { error = e.what(); return false; }
}

namespace
{
constexpr const char* BrushModes[] = {"Raise", "Lower", "Flatten", "Smooth"};
struct ToolState
{
    EntityId Selected = 0; MaterialGraph Graph; uint32_t SelectedNode = 0, NextNode = 4;
    std::vector<MaterialGraph> Undo, Redo;
    std::string Error, AssignedGraph; char GraphPath[512] = "Assets/Materials/Material.jmaterial";
    bool AssignedGraphHasNormal = false;
    std::filesystem::path SceneMaterialRoot;
    std::vector<std::pair<std::string, std::string>> SceneMaterials;
    size_t SceneMaterialIndex = 0;
    float Time = 0, BrushX = 0, BrushZ = 0, BrushRadius = 100, BrushStrength = 20;
    int BrushMode = 0; bool Preview = false;
    bool PaintInScene = false;
    EntityId StrokeEntity = 0;
    std::optional<Vector> LastDab;
    TerrainProperties BrushTerrain;
    Mesh BrushMesh;
    bool PreviewStarted = false;
    Vec3 NavFrom{-200, 0, -200}, NavTo{200, 0, 200}; NavigationBake Nav;
};
ToolState& Tools() { static ToolState state; return state; }

float TerrainHeight(const TerrainProperties& terrain, float x, float z)
{
    if (terrain.Heights.empty()) return 0;
    const auto n = terrain.Resolution;
    const float gridX = (x / terrain.SizeX + .5f) * float(n - 1);
    const float gridZ = (z / terrain.SizeZ + .5f) * float(n - 1);
    const auto col = std::min(uint32_t(gridX), n - 2);
    const auto row = std::min(uint32_t(gridZ), n - 2);
    const float u = gridX - float(col), v = gridZ - float(row);
    const auto i = size_t(row) * n + col;
    const float a = terrain.Heights[i], b = terrain.Heights[i + 1];
    const float c = terrain.Heights[i + n], d = terrain.Heights[i + n + 1];
    // Match the diagonal used by BuildTerrainMesh.
    if (u + v <= 1) return a + (b - a) * u + (c - a) * v;
    return d + (c - d) * (1 - u) + (b - d) * (1 - v);
}

void DrawTerrainBrush(const TerrainProperties& terrain, const Vector& hit, const Matrix& localToClip,
    const ImVec2& position, const ImVec2& size)
{
    const auto& s = Tools();
    const auto project = [&](const Vector& point, ImVec2& pixel)
    {
        const auto clip = localToClip.Transform(Vector4(point, 1));
        if (!(clip.w > 0 && clip.z >= 0 && clip.z <= clip.w)) return false;
        pixel = ImVec2(position.x + (.5f + .5f * clip.x / clip.w) * size.x,
            position.y + (.5f - .5f * clip.y / clip.w) * size.y);
        return std::isfinite(pixel.x) && std::isfinite(pixel.y);
    };
    auto* draw = ImGui::GetWindowDrawList();
    constexpr ImU32 outline = IM_COL32(89, 227, 255, 255);
    constexpr int segments = 64;
    ImVec2 previous{};
    bool previousVisible = false;
    for (int i = 0; i <= segments; ++i)
    {
        const float angle = 2 * PI * float(i) / segments;
        const float x = std::clamp(hit.x + std::cos(angle) * s.BrushRadius, -terrain.SizeX / 2, terrain.SizeX / 2);
        const float z = std::clamp(hit.z + std::sin(angle) * s.BrushRadius, -terrain.SizeZ / 2, terrain.SizeZ / 2);
        ImVec2 pixel{};
        const bool visible = project(Vector(x, TerrainHeight(terrain, x, z), z), pixel);
        if (visible && previousVisible)
        {
            draw->AddLine(previous, pixel, IM_COL32(0, 0, 0, 220), 4);
            draw->AddLine(previous, pixel, outline, 2);
        }
        previous = pixel;
        previousVisible = visible;
    }
    ImVec2 center;
    if (!project(hit, center)) return;
    draw->AddCircleFilled(center, 4, IM_COL32(0, 0, 0, 220));
    draw->AddCircleFilled(center, 2, outline);

    char label[128];
    if (s.BrushMode == 3)
        std::snprintf(label, sizeof(label), "Smooth  |  Radius %.1f  |  Strength 100%%", s.BrushRadius);
    else
        std::snprintf(label, sizeof(label), "%s  |  Radius %.1f  |  %s %.1f", BrushModes[s.BrushMode], s.BrushRadius,
            s.BrushMode == 2 ? "Target height" : "Strength", s.BrushMode == 2 ? s.BrushStrength : std::abs(s.BrushStrength));
    const auto textSize = ImGui::CalcTextSize(label);
    const ImVec2 textPosition(
        std::clamp(center.x + 12, position.x + 6, position.x + std::max(6.0f, size.x - textSize.x - 6)),
        std::clamp(center.y + 14, position.y + 6, position.y + std::max(6.0f, size.y - textSize.y - 6)));
    draw->AddRectFilled(ImVec2(textPosition.x - 4, textPosition.y - 3),
        ImVec2(textPosition.x + textSize.x + 4, textPosition.y + textSize.y + 3), IM_COL32(15, 22, 28, 230), 4);
    draw->AddText(textPosition, IM_COL32_WHITE, label);
}

void ApplyTerrainDabs(Document& doc, const Entity& terrain, const Vector& hit)
{
    auto& s = Tools();
    if (!s.StrokeEntity)
    {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || doc.HasTransaction() ||
            !doc.BeginTransaction("Paint terrain stroke")) return;
        s.StrokeEntity = terrain.Id;
    }
    const auto delta = ImGui::GetIO().MouseDelta;
    if (s.LastDab && delta.x == 0 && delta.y == 0) return;

    const Vector from = s.LastDab.value_or(hit);
    const float distance = std::hypot(hit.x - from.x, hit.z - from.z);
    const float spacing = std::max(s.BrushRadius * .25f, .00001f);
    if (s.LastDab && distance < spacing) return;
    const int count = s.LastDab ? int(std::min(32.0f, std::floor(distance / spacing))) : 1;
    const float step = std::max(spacing, distance / count);
    auto edited = terrain;
    Vector lastDab = hit;
    for (int i = 1; i <= count; ++i)
    {
        const Vector dab = distance > 0 ? from + (hit - from) * std::min(1.0f, step * i / distance) : hit;
        if (!PaintTerrain(edited.Terrain, dab.x, dab.z, s.BrushRadius, s.BrushStrength, s.BrushMode))
        {
            s.Error = "Terrain brush exceeds the supported height range.";
            return;
        }
        lastDab = dab;
    }
    if (!doc.Update(edited, "Paint terrain stroke")) s.Error = doc.LastError();
    else s.LastDab = lastDab;
}

void GraphHistory()
{
    auto& s = Tools(); s.Undo.push_back(s.Graph); if (s.Undo.size() > 64) s.Undo.erase(s.Undo.begin()); s.Redo.clear();
}
constexpr float MaterialNodeWidth = 165;
const char* MaterialNodeName(NodeKind kind)
{
    static const char* names[] = {"Color", "Scalar", "Texture", "Multiply", "Add", "UV", "Texture Object", "Texture Sample",
        "Sample Normal", "Channel", "Parallax", "POM", "Relief", "Interior", "Horizon", "Diffusion Profile"};
    const auto index = int(kind);
    return index >= 0 && index < int(std::size(names)) ? names[index] : "Invalid";
}
struct MaterialInputPin { const char* Label; uint32_t MaterialNode::*Source; };
std::span<const MaterialInputPin> MaterialInputs(NodeKind kind)
{
    static const MaterialInputPin arithmetic[] = {{"A", &MaterialNode::InputA}, {"B", &MaterialNode::InputB}};
    static const MaterialInputPin sample[] = {{"Texture", &MaterialNode::InputA}, {"UV", &MaterialNode::InputB}};
    static const MaterialInputPin channel[] = {{"Value", &MaterialNode::InputA}};
    static const MaterialInputPin mapping[] = {{"UV", &MaterialNode::InputA}, {"Height", &MaterialNode::InputB}};
    static const MaterialInputPin interior[] = {{"UV", &MaterialNode::InputA}, {"Color", &MaterialNode::InputB},
        {"Depth?", &MaterialNode::InputC}, {"Backdrop?", &MaterialNode::InputD}};
    static const MaterialInputPin horizon[] = {{"UV", &MaterialNode::InputA}, {"Layer 0", &MaterialNode::InputB},
        {"Layer 1", &MaterialNode::InputC}, {"AO", &MaterialNode::InputD}};
    switch (kind)
    {
    case NodeKind::Add: case NodeKind::Multiply: return arithmetic;
    case NodeKind::TextureSample: case NodeKind::TextureSampleNormal: return sample;
    case NodeKind::Channel: return channel;
    case NodeKind::Parallax: case NodeKind::POM: case NodeKind::Relief: return mapping;
    case NodeKind::Interior: return interior;
    case NodeKind::Horizon: return horizon;
    default: return {};
    }
}
float MaterialNodeHeight(NodeKind kind)
{
    if (kind == NodeKind::Texture || kind == NodeKind::TextureObject) return 108.f;
    if (kind <= NodeKind::Add) return 82.f;
    return std::max(82.f, 64.f + float(MaterialInputs(kind).size()) * 25.f);
}
bool MaterialUVKind(NodeKind kind)
{ return kind == NodeKind::UV || kind == NodeKind::Parallax || kind == NodeKind::POM || kind == NodeKind::Relief; }
bool AcceptMaterialInput(NodeKind destination, uint32_t MaterialNode::*input, NodeKind source)
{
    if (destination == NodeKind::TextureSample || destination == NodeKind::TextureSampleNormal)
        return input == &MaterialNode::InputA ? source == NodeKind::TextureObject : MaterialUVKind(source) || source == NodeKind::Add || source == NodeKind::Multiply;
    if (destination == NodeKind::Parallax || destination == NodeKind::POM || destination == NodeKind::Relief
        || destination == NodeKind::Interior || destination == NodeKind::Horizon)
        return input == &MaterialNode::InputA ? MaterialUVKind(source) || source == NodeKind::Add || source == NodeKind::Multiply : source == NodeKind::TextureObject;
    return source != NodeKind::TextureObject && source != NodeKind::Horizon && source != NodeKind::DiffusionProfile;
}
constexpr float MaterialPinY(int index) { return 41.f + float(index) * 25.f; }
constexpr struct MaterialOutputPin
{
    const char* Label;
    uint32_t MaterialGraph::*Source;
} MaterialOutputPins[] = {
    {"Base Color", &MaterialGraph::BaseColor}, {"Normal", &MaterialGraph::Normal},
    {"Metallic", &MaterialGraph::Metallic}, {"Roughness", &MaterialGraph::Roughness}, {"Horizon", &MaterialGraph::Horizon},
    {"SSS Weight", &MaterialGraph::SubsurfaceWeight}, {"SSS Color", &MaterialGraph::SubsurfaceColor},
    {"SSS Thickness", &MaterialGraph::SubsurfaceThickness}, {"SSS Profile", &MaterialGraph::SubsurfaceProfile},
    {"Specular Weight", &MaterialGraph::SpecularWeight}, {"Diffuse Occlusion", &MaterialGraph::DiffuseOcclusion},
    {"Pre-scatter Weight", &MaterialGraph::PreScatterWeight}
};
bool AcceptMaterialOutput(const MaterialOutputPin& pin, NodeKind kind)
{
    if (pin.Source == &MaterialGraph::Horizon) return kind == NodeKind::Horizon;
    if (pin.Source == &MaterialGraph::SubsurfaceProfile) return kind == NodeKind::DiffusionProfile;
    if (pin.Source == &MaterialGraph::SubsurfaceWeight || pin.Source == &MaterialGraph::SubsurfaceThickness
        || pin.Source == &MaterialGraph::SpecularWeight || pin.Source == &MaterialGraph::DiffuseOcclusion
        || pin.Source == &MaterialGraph::PreScatterWeight)
        return kind == NodeKind::Scalar || kind == NodeKind::Channel || kind == NodeKind::Multiply || kind == NodeKind::Add;
    if (pin.Source == &MaterialGraph::Normal)
        return kind == NodeKind::Texture || kind == NodeKind::TextureSampleNormal || kind == NodeKind::Color || kind == NodeKind::Multiply || kind == NodeKind::Add;
    return kind != NodeKind::Horizon && kind != NodeKind::TextureObject && kind != NodeKind::DiffusionProfile && !MaterialUVKind(kind);
}

uint32_t DroppedMaterialNode()
{
    uint32_t node = 0;
    if (!ImGui::BeginDragDropTarget()) return node;
    if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_MATERIAL_NODE"); payload && payload->DataSize == sizeof(node))
        node = *static_cast<const uint32_t*>(payload->Data);
    ImGui::EndDragDropTarget();
    return node;
}

void DrawMaterialLink(ImDrawList& draw, const ImVec2& origin, const ImVec2& input, uint32_t id)
{
    if (!id) return;
    const auto& nodes = Tools().Graph.Nodes;
    const auto source = std::find_if(nodes.begin(), nodes.end(), [id](const auto& node) { return node.Id == id; });
    if (source == nodes.end()) return;
    const ImVec2 output(origin.x + source->X + MaterialNodeWidth, origin.y + source->Y + MaterialPinY(0));
    const float bend = std::clamp(std::abs(input.x - output.x) * .5f, 32.f, 100.f);
    draw.AddBezierCubic(output, ImVec2(output.x + bend, output.y), ImVec2(input.x - bend, input.y), input,
        IM_COL32(90, 180, 255, 255), 2);
}

void DrawMaterialOutput(ImDrawList& draw, const ImVec2& position)
{
    auto& s = Tools();
    const ImVec2 end(position.x + MaterialNodeWidth, position.y + MaterialPinY(int(std::size(MaterialOutputPins)) - 1) + 16);
    draw.AddRectFilled(position, end, IM_COL32(42, 47, 55, 255), 5);
    draw.AddRect(position, end, IM_COL32(110, 130, 160, 255), 5);
    draw.AddRectFilled(position, ImVec2(end.x, position.y + 23), IM_COL32(46, 89, 135, 255), 5, ImDrawFlags_RoundCornersTop);
    draw.AddText(ImVec2(position.x + 8, position.y + 4), IM_COL32_WHITE, "Material Output");
    ImGui::PushID("MaterialOutput");
    for (int i = 0; i < int(std::size(MaterialOutputPins)); ++i)
    {
        const auto& pin = MaterialOutputPins[i];
        auto& output = s.Graph.*pin.Source;
        ImGui::PushID(pin.Label);
        ImGui::SetCursorScreenPos(ImVec2(position.x + 3, position.y + MaterialPinY(i) - 10));
        if (ImGui::Button(pin.Label, ImVec2(91, 20))) ImGui::OpenPopup("Connect");
        if (const auto dropped = DroppedMaterialNode(); dropped && dropped != output)
        {
            const auto source = std::find_if(s.Graph.Nodes.begin(), s.Graph.Nodes.end(), [dropped](const auto& node) { return node.Id == dropped; });
            if (source != s.Graph.Nodes.end() && AcceptMaterialOutput(pin, source->Kind))
            { GraphHistory(); output = dropped; s.Error.clear(); }
            else s.Error = "This node type is incompatible with the selected material output";
        }
        ImGui::SameLine();
        const auto source = output ? "#" + std::to_string(output) : std::string("None");
        ImGui::TextDisabled("%s", source.c_str());
        if (ImGui::BeginPopup("Connect"))
        {
            if (ImGui::Selectable("Disconnect", output == 0) && output) { GraphHistory(); output = 0; }
            for (const auto& node : s.Graph.Nodes)
                if (AcceptMaterialOutput(pin, node.Kind) &&
                    ImGui::Selectable(("Node " + std::to_string(node.Id)).c_str(), output == node.Id) && output != node.Id)
                { GraphHistory(); output = node.Id; s.Error.clear(); }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

void FormatMaterialValue(char* text, size_t size, const MaterialValue& value, int precision)
{
    const auto& color = value.Color;
    if (value.IsScalar)
        std::snprintf(text, size, "%.*g", precision, color.X);
    else if (!value.Texture.empty() && color.X == color.Y && color.X == color.Z)
        std::snprintf(text, size, "Tint %.*g", precision, color.X);
    else
        std::snprintf(text, size, "%.*g %.*g %.*g", precision, color.X, precision, color.Y, precision, color.Z);
}

void DrawMaterialValue(ImDrawList& draw, const ImVec2& position, const MaterialNode& node, MaterialEvaluator& evaluator)
{
    if (node.Kind > NodeKind::Add)
    {
        const char* type = node.Kind == NodeKind::TextureObject ? "Texture2D asset" : node.Kind == NodeKind::Horizon ? "GPU visibility + AO"
            : node.Kind == NodeKind::DiffusionProfile ? "Static diffusion profile"
            : MaterialUVKind(node.Kind) ? "GPU float2 UV" : node.Kind == NodeKind::Channel ? "GPU scalar" : "GPU sampled color";
        draw.AddText(ImVec2(position.x + 7, position.y + MaterialNodeHeight(node.Kind) - 19), IM_COL32(170, 190, 210, 255), type);
        return;
    }
    const bool arithmetic = node.Kind == NodeKind::Multiply || node.Kind == NodeKind::Add;
    const ImVec2 min(position.x + (arithmetic ? 34.f : 8.f), position.y + 56.f);
    const ImVec2 max(position.x + MaterialNodeWidth - 8.f, position.y + 76.f);
    if (!ImGui::IsRectVisible(min, max)) return;
    const auto& result = evaluator.Read(node.Id);
    const auto& value = result.Value;
    draw.AddRectFilled(min, max, IM_COL32(23, 27, 33, 255), 3);
    float textRight = max.x - 4.f;
    if (!result.Error && !value.IsScalar)
    {
        const ImVec2 swatchMin(max.x - 17.f, min.y + 4.f), swatchMax(max.x - 4.f, max.y - 4.f);
        draw.AddRectFilled(swatchMin, swatchMax, ImGui::ColorConvertFloat4ToU32(ImVec4(value.Color.X, value.Color.Y, value.Color.Z, 1)), 2);
        draw.AddRect(swatchMin, swatchMax, IM_COL32(122, 132, 150, 255), 2);
        textRight = swatchMin.x - 4.f;
    }
    char preview[96];
    const char* label = preview;
    if (result.Error) label = arithmetic && (!node.InputA || !node.InputB) ? "Unconnected" : "Unavailable";
    else FormatMaterialValue(preview, sizeof(preview), value, 3);
    const ImVec4 clip(min.x, min.y, textRight, max.y);
    draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + 5.f, min.y + 3.f),
        ImGui::GetColorU32(result.Error ? ImGuiCol_TextDisabled : ImGuiCol_Text), label, nullptr, 0, &clip);
    if (!ImGui::IsWindowHovered() || !ImGui::IsMouseHoveringRect(min, max)) return;

    ImGui::BeginTooltip();
    if (result.Error) ImGui::TextUnformatted(result.Error);
    else
    {
        FormatMaterialValue(preview, sizeof(preview), value, 9);
        ImGui::Text("%s  %s", value.IsScalar ? "Value" : value.Texture.empty() ? "RGB" : "Texture factor", preview);
        if (!value.Texture.empty()) ImGui::TextUnformatted(value.Texture.c_str());
        if (arithmetic)
        {
            const auto& a = evaluator.Read(node.InputA).Value;
            const auto& b = evaluator.Read(node.InputB).Value;
            FormatMaterialValue(preview, sizeof(preview), a, 9);
            ImGui::Text("A  %s%s", a.Texture.empty() ? "" : "Texture x ", preview);
            FormatMaterialValue(preview, sizeof(preview), b, 9);
            ImGui::Text("B  %s%s", b.Texture.empty() ? "" : "Texture x ", preview);
        }
    }
    ImGui::EndTooltip();
}

void SurfaceSettings(MaterialGraph::SurfaceProperties& surface)
{
    if (!ImGui::CollapsingHeader("Surface settings")) return;
    auto edited = surface;
    auto& shading = edited.Shading;
    bool changed = ImGui::SliderFloat("Transmission", &shading.Transmission, 0.f, 1.f);
    changed |= ImGui::SliderFloat("Subsurface weight", &shading.Subsurface, 0, 1);
    changed |= ImGui::ColorEdit3("Subsurface color (linear)", shading.SubsurfaceColor.data());
    changed |= ImGui::DragFloat("Subsurface thickness (world)", &shading.SubsurfaceThicknessWorld, .01f, 0, 10000, "%.3g", ImGuiSliderFlags_AlwaysClamp);
    static const auto profiles = [] { std::array<const char*, 8> names{}; for (size_t i = 0; i < names.size(); ++i) names[i] = jSubsurface::Profiles[i].Name; return names; }();
    int profile = int(shading.SubsurfaceProfile);
    if (ImGui::Combo("Subsurface profile", &profile, profiles.data(), int(profiles.size())))
    { shading.SubsurfaceProfile = uint32(profile); changed = true; }
    int model = int(shading.ShadingModel), space = int(shading.SubsurfaceSpace);
    if (ImGui::Combo("Shading model", &model, "Standard PBR\0Skin Kelemen\0"))
    { shading.ShadingModel = jSurfaceShadingModel(model); changed = true; }
    if (ImGui::Combo("Subsurface space", &space, "Screen\0Texture (UV)\0"))
    { shading.SubsurfaceSpace = jSubsurfaceSpace(space); changed = true; }
    changed |= ImGui::SliderFloat("Specular weight", &shading.SpecularWeight, 0, 8);
    changed |= ImGui::SliderFloat("Diffuse occlusion", &shading.DiffuseOcclusion, 0, 1);
    changed |= ImGui::SliderFloat("Pre-scatter weight", &shading.PreScatterWeight, 0, 1);
    if (shading.ShadingModel == jSurfaceShadingModel::SkinKelemen)
        ImGui::TextWrapped("Skin Kelemen uses the Roughness output as Beckmann m. Diffuse occlusion affects diffuse/ambient, not specular.");
    changed |= ImGui::SliderFloat("IOR", &shading.Ior, 1.f, 4.f);
    changed |= ImGui::SliderFloat("Clear Coat", &shading.ClearCoat, 0.f, 1.f);
    changed |= ImGui::SliderFloat("Clear Coat Gloss", &shading.ClearCoatGloss, 0.f, 1.f);
    changed |= ImGui::SliderFloat("Opacity", &edited.Opacity, 0.f, 1.f);
    changed |= ImGui::ColorEdit3("Emission", &edited.Emission.X, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    int alphaMode = int(shading.AlphaMode);
    if (ImGui::Combo("Alpha mode", &alphaMode, "Opaque\0Mask\0Blend\0"))
    { shading.AlphaMode = jSurfaceAlphaMode(alphaMode); changed = true; }
    if (shading.AlphaMode == jSurfaceAlphaMode::Mask)
        changed |= ImGui::SliderFloat("Alpha cutoff", &shading.AlphaCutoff, 0.f, 1.f);
    changed |= ImGui::Checkbox("Double sided", &shading.DoubleSided);
    const auto textureField = [&](const char* label, std::string& texture)
    {
        char path[512]; std::snprintf(path, sizeof(path), "%s", texture.c_str());
        if (ImGui::InputText(label, path, sizeof(path))) { texture = path; changed = true; }
    };
    textureField("Metallic Roughness map", edited.MetallicRoughnessTexture);
    textureField("Emission map", edited.EmissionTexture);
    if (changed) { GraphHistory(); surface = std::move(edited); }
}

void GraphUI(jEditor& editor, Entity e, const jEditorProject* project, const jEditorAssetRegistry* registry)
{
    auto& s = Tools();
    const bool library = !s.SceneMaterialRoot.empty();
    ImGui::TextWrapped("Drag Out to a typed input or Material Output pin. Click an input to choose or disconnect its source.");
    static int addKind = 0; ImGui::Combo("New node", &addKind,
        "Color\0Scalar\0Texture\0Multiply\0Add\0UV\0Texture Object\0Texture Sample\0Sample Normal\0Channel\0Parallax\0POM\0Relief\0Interior\0Horizon\0Diffusion Profile\0");
    ImGui::BeginDisabled(s.Graph.Nodes.size() >= MaxMaterialNodes);
    if (ImGui::Button("Add node"))
    {
        GraphHistory(); MaterialNode node; node.Id = s.NextNode++; node.Kind = NodeKind(addKind);
        node.X = 20 + float(s.Graph.Nodes.size() % 3) * 185; node.Y = 20 + float(s.Graph.Nodes.size() / 3) * 180;
        if (node.Kind == NodeKind::Channel) node.Parameters = {0, 0, 0, 0};
        if (node.Kind == NodeKind::Parallax) node.Parameters = {.05f, 4, 0, 0};
        if (node.Kind == NodeKind::POM || node.Kind == NodeKind::Relief) node.Parameters = {.05f, 32, 6, 0};
        if (node.Kind == NodeKind::Interior) node.Parameters = {1, 1, 1, 1};
        if (node.Kind == NodeKind::Horizon) node.Parameters = {.05f, 1, 1, 0};
        if (node.Kind == NodeKind::DiffusionProfile) node.Parameters = {0, 0, 0, 0};
        s.Graph.Nodes.push_back(node);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Undo graph") && !s.Undo.empty()) { s.Redo.push_back(s.Graph); s.Graph = s.Undo.back(); s.Undo.pop_back(); }
    ImGui::SameLine(); if (ImGui::Button("Redo graph") && !s.Redo.empty()) { s.Undo.push_back(s.Graph); s.Graph = s.Redo.back(); s.Redo.pop_back(); }
    ImGui::SameLine();
    const bool showOutput = ImGui::Button("Show output");
    ImGui::BeginChild("MaterialCanvas", ImVec2(0, 390), true, ImGuiWindowFlags_HorizontalScrollbar);
    const auto origin = ImGui::GetCursorScreenPos(); auto* draw = ImGui::GetWindowDrawList();
    ImVec2 output(origin.x + 230, origin.y + 20);
    float canvasBottom = output.y + MaterialPinY(int(std::size(MaterialOutputPins)) - 1) + 28;
    for (const auto& node : s.Graph.Nodes)
    {
        output.x = std::max(output.x, origin.x + node.X + MaterialNodeWidth + 64);
        canvasBottom = std::max(canvasBottom, origin.y + node.Y + MaterialNodeHeight(node.Kind) + 12);
        for (size_t input = 0; input < MaterialInputs(node.Kind).size(); ++input)
        {
            const float x = origin.x + node.X, y = origin.y + node.Y;
            DrawMaterialLink(*draw, origin, ImVec2(x, y + MaterialPinY(int(input))), node.*MaterialInputs(node.Kind)[input].Source);
        }
    }
    for (int i = 0; i < int(std::size(MaterialOutputPins)); ++i)
        DrawMaterialLink(*draw, origin, ImVec2(output.x, output.y + MaterialPinY(i)), s.Graph.*MaterialOutputPins[i].Source);
    MaterialEvaluator evaluator(s.Graph);
    for (auto& node : s.Graph.Nodes)
    {
        ImGui::PushID(int(node.Id)); ImVec2 p(origin.x + node.X, origin.y + node.Y);
        const ImVec2 end(p.x + MaterialNodeWidth, p.y + MaterialNodeHeight(node.Kind));
        draw->AddRectFilled(p, end, IM_COL32(42, 47, 55, 255), 5);
        draw->AddRect(p, end, IM_COL32(110, 130, 160, 255), 5);
        ImGui::SetCursorScreenPos(p); if (ImGui::Button((std::string(MaterialNodeName(node.Kind)) + " #" + std::to_string(node.Id)).c_str(), ImVec2(MaterialNodeWidth, 23))) s.SelectedNode = node.Id;
        if (ImGui::IsItemActivated()) GraphHistory();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
        {
            node.X = std::max(0.f, node.X + ImGui::GetIO().MouseDelta.x);
            node.Y = std::max(0.f, node.Y + ImGui::GetIO().MouseDelta.y);
        }
        for (size_t input = 0; input < MaterialInputs(node.Kind).size(); ++input)
        {
            const auto& pin = MaterialInputs(node.Kind)[input];
            auto& source = node.*pin.Source;
            ImGui::PushID(int(input));
            ImGui::SetCursorScreenPos(ImVec2(p.x + 3, p.y + MaterialPinY(int(input)) - 10));
            if (ImGui::Button(pin.Label, ImVec2(node.Kind <= NodeKind::Add ? 24.f : 74.f, 20))) ImGui::OpenPopup("Input source");
            if (const auto dropped = DroppedMaterialNode(); dropped && dropped != source)
            {
                const auto candidate = std::find_if(s.Graph.Nodes.begin(), s.Graph.Nodes.end(), [dropped](const auto& value) { return value.Id == dropped; });
                if (candidate != s.Graph.Nodes.end() && dropped != node.Id && AcceptMaterialInput(node.Kind, pin.Source, candidate->Kind))
                { GraphHistory(); source = dropped; s.Error.clear(); }
                else s.Error = "Incompatible material pin types";
            }
            if (ImGui::BeginPopup("Input source"))
            {
                if (ImGui::Selectable("Disconnect", source == 0) && source) { GraphHistory(); source = 0; }
                for (const auto& candidate : s.Graph.Nodes)
                    if (candidate.Id != node.Id && AcceptMaterialInput(node.Kind, pin.Source, candidate.Kind)
                        && ImGui::Selectable((std::string(MaterialNodeName(candidate.Kind)) + " #" + std::to_string(candidate.Id)).c_str(), source == candidate.Id)
                        && source != candidate.Id) { GraphHistory(); source = candidate.Id; s.Error.clear(); }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + MaterialNodeWidth - 33, p.y + MaterialPinY(0) - 10)); ImGui::Button("Out", ImVec2(30, 20));
        if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("JENGINE_MATERIAL_NODE", &node.Id, sizeof(node.Id)); ImGui::Text("Output %u", node.Id); ImGui::EndDragDropSource(); }
        if (node.Kind <= NodeKind::Add) draw->AddText(ImVec2(p.x + 35, p.y + (node.Kind == NodeKind::Texture ? 65.f : 34.f)), IM_COL32_WHITE, MaterialNodeName(node.Kind));
        if (node.Kind == NodeKind::Texture || node.Kind == NodeKind::TextureObject)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x + 103, p.y + 55));
            jEditorAssetPanel::RenderTexturePreview(project, registry, node.Texture, ImVec2(53, 46));
        }
        else DrawMaterialValue(*draw, p, node, evaluator);
        ImGui::PopID();
    }
    DrawMaterialOutput(*draw, output);
    ImGui::SetCursorScreenPos(ImVec2(output.x + MaterialNodeWidth + 8, canvasBottom));
    ImGui::Dummy(ImVec2(1, 1));
    if (showOutput) { ImGui::SetScrollHereX(1); ImGui::SetScrollY(0); }
    ImGui::EndChild();
    auto node = std::find_if(s.Graph.Nodes.begin(), s.Graph.Nodes.end(), [&](const auto& n) { return n.Id == s.SelectedNode; });
    if (node != s.Graph.Nodes.end())
    {
        ImGui::Text("Selected node %u", node->Id);
        const auto before = s.Graph;
        bool activated = false;
        const auto track = [&] { activated |= ImGui::IsItemActivated(); };
        if (node->Kind == NodeKind::Color) ImGui::ColorEdit3("Node color", &node->Color.X);
        if (node->Kind == NodeKind::Scalar) ImGui::DragFloat("Node value", &node->Scalar, .01f);
        if (node->Kind == NodeKind::Color || node->Kind == NodeKind::Scalar) track();
        if (node->Kind == NodeKind::Texture || node->Kind == NodeKind::TextureObject)
        {
            char path[512]; std::snprintf(path, sizeof(path), "%s", node->Texture.c_str()); if (ImGui::InputText("Texture path", path, sizeof(path))) node->Texture = path; track();
            if (node->Kind == NodeKind::TextureObject)
            {
                int semantic = int(node->Semantic);
                if (ImGui::Combo("Texture data", &semantic, "Linear (height / data)\0Color (sRGB)\0Normal (linear)\0")) node->Semantic = jMaterialMapping::TextureSemantic(semantic);
                track();
                ImGui::TextWrapped("Color samples use this encoding. Height, depth, Horizon and normal inputs always read linear data.");
            }
        }
        auto& parameters = node->Parameters;
        if (node->Kind == NodeKind::UV)
        {
            ImGui::DragFloat2("UV scale", parameters.data(), .01f, -10000, 10000); track();
            ImGui::DragFloat2("UV offset", parameters.data() + 2, .01f, -10000, 10000); track();
        }
        if (node->Kind == NodeKind::Channel)
        { int channel = int(parameters[0]); if (ImGui::SliderInt("Channel (RGBA)", &channel, 0, 3)) parameters[0] = float(channel); track(); }
        if (node->Kind == NodeKind::TextureSampleNormal)
        {
            int space = int(node->NormalSpace);
            if (ImGui::Combo("Normal space", &space, "Tangent\0Object\0World\0")) node->NormalSpace = jMaterialMapping::NormalSpace(space);
            track();
            bool flip = node->FlipNormalY.value_or(node->NormalSpace == jMaterialMapping::NormalSpace::Tangent && parameters[0] > .5f);
            if (ImGui::Checkbox("Flip normal Y", &flip)) node->FlipNormalY = flip;
            track();
            for (int axis = 0; axis < 3; ++axis)
            {
                const char* labels[] = {"Negate source X", "Negate source Y", "Negate source Z"};
                bool negate = node->InputBasisSignXYZ[axis] < 0;
                if (ImGui::Checkbox(labels[axis], &negate)) node->InputBasisSignXYZ[axis] = negate ? -1.f : 1.f;
                track();
            }
            ImGui::TextWrapped("Normal maps are linear data. Object follows the mesh transform; World stays in engine-world axes. Source axis signs are separate from UV V flip. Object/World default to no Y flip; existing Tangent graphs keep their legacy flip.");
        }
        if (node->Kind == NodeKind::Parallax || node->Kind == NodeKind::POM || node->Kind == NodeKind::Relief)
        {
            ImGui::SliderFloat("Height / depth scale", &parameters[0], 0, 1); track();
            int steps = int(parameters[1]); if (ImGui::SliderInt("Bounded steps", &steps, 1, 128)) parameters[1] = float(steps); track();
            if (node->Kind != NodeKind::Parallax)
            { int refinements = int(parameters[2]); if (ImGui::SliderInt("Refinements", &refinements, 0, 8)) parameters[2] = float(refinements); track(); }
            bool mode = parameters[3] > .5f;
            if (ImGui::Checkbox(node->Kind == NodeKind::Relief ? "Dual depth (R front / G back)" : "Flip height V", &mode)) parameters[3] = mode ? 1.f : 0.f;
            track();
        }
        if (node->Kind == NodeKind::Interior)
        {
            ImGui::DragFloat2("Room tiling", parameters.data(), .1f, .01f, 256); track();
            ImGui::SliderFloat("Room depth", &parameters[2], .001f, 64); track();
            ImGui::SliderFloat("Room ambient", &parameters[3], 0, 4); track();
            ImGui::TextWrapped("Connect Depth for RG dual-depth relief; Backdrop is optional. This is a raster material illusion, not room geometry.");
        }
        if (node->Kind == NodeKind::Horizon)
        {
            ImGui::SliderFloat("Softness (radians)", &parameters[0], .001f, PI * .5f); track();
            ImGui::SliderFloat("Ambient occlusion strength", &parameters[1], 0, 1); track();
            ImGui::SliderFloat("Horizon angle scale", &parameters[2], 0, 4); track();
            ImGui::TextWrapped("Connect baked Layer 0, Layer 1 and AO textures. Horizon visibility is a separate output; it is never multiplied into albedo.");
        }
        if (node->Kind == NodeKind::DiffusionProfile)
        {
            static const auto profiles = [] { std::array<const char*, 8> names{}; for (size_t i = 0; i < names.size(); ++i) names[i] = jSubsurface::Profiles[i].Name; return names; }();
            int profile = int(parameters[0]);
            if (ImGui::Combo("Diffusion profile", &profile, profiles.data(), int(profiles.size()))) parameters[0] = float(profile);
            track();
            ImGui::TextWrapped("Approximate world-unit diffusion presets. Connect to SSS Profile; material thickness bounds the effective radius.");
        }
        if (activated) { s.Undo.push_back(before); if (s.Undo.size() > 64) s.Undo.erase(s.Undo.begin()); s.Redo.clear(); }
        if (ImGui::Button("Remove node"))
        {
            GraphHistory();
            for (const auto& pin : MaterialOutputPins)
                if (s.Graph.*pin.Source == node->Id) s.Graph.*pin.Source = 0;
            for (auto& current : s.Graph.Nodes)
                for (const auto& pin : MaterialInputs(current.Kind))
                    if (current.*pin.Source == node->Id) current.*pin.Source = 0;
            s.Graph.Nodes.erase(node);
            s.SelectedNode = 0;
        }
    }
    if (!s.Graph.Surface && ImGui::Button("Add surface settings")) { GraphHistory(); s.Graph.Surface.emplace(); }
    if (s.Graph.Surface) SurfaceSettings(*s.Graph.Surface);
    ImGui::TextWrapped("Surface diffusion requires Deferred and Project Settings > Enable Deferred SSS. Weight 0 or world thickness 0 disables diffusion. The existing PT subsurface scalar remains a separate shading approximation.");
    if (std::any_of(s.Graph.Nodes.begin(), s.Graph.Nodes.end(), [](const auto& value) { return value.Kind > NodeKind::Add; }))
    {
        ImGui::TextWrapped("Mapping nodes render in Deferred / Forward. Path Tracing and HWRT material evaluation do not support these nodes.");
        if (g_Engine && g_Engine->Game.IsUsingPathTracingRenderer()) ImGui::TextColored(ImVec4(1,.75f,.25f,1), "Switch to Deferred / Forward to preview this graph.");
    }
    if (library)
    {
        ImGui::TextWrapped("%s", s.GraphPath);
        if (ImGui::Button("Save and apply to scene"))
        {
            auto path = jEditorProject::ResolveSafe(s.SceneMaterialRoot, s.GraphPath, s.Error, true);
            if (path && SaveMaterial(s.Graph, *path, s.Error)) g_Engine->Game.RequestSceneMaterialRefresh();
        }
        return;
    }
    ImGui::InputText("Graph asset", s.GraphPath, sizeof(s.GraphPath));
    if (ImGui::Button("Load graph"))
    {
        auto path = jEditorProject::ResolveSafe(editor.GetProjectRoot(), s.GraphPath, s.Error, true);
        if (path && LoadMaterial(*path, s.Graph, s.Error)) { s.NextNode = 1; for (const auto& n : s.Graph.Nodes) s.NextNode = std::max(s.NextNode, n.Id + 1); }
    }
    ImGui::BeginDisabled(editor.GetDocument().IsPlaying() || editor.GetDocument().IsEffectivelyLocked(e.Id));
    ImGui::SameLine(); if (ImGui::Button("Save and Apply"))
    {
        auto path = jEditorProject::ResolveSafe(editor.GetProjectRoot(), s.GraphPath, s.Error);
        auto defaults = e.Material.GraphOverrides ? MaterialProperties{} : e.Material;
        if (s.AssignedGraphHasNormal) defaults.NormalTexture.clear();
        if (path && EvaluateMaterial(s.Graph, defaults, s.Error) && SaveMaterial(s.Graph, *path, s.Error))
        {
            const bool dynamic = CompiledMaterialGraph(s.Graph);
            if (dynamic && !e.Material.GraphOverrides) e.Material.GraphOverrides.emplace();
            if (e.Material.GraphOverrides) InheritMaterialGraph(e.Material, defaults);
            else e.Material = std::move(defaults);
            e.Material.OverrideEnabled = true;
            e.Material.GraphAsset = s.GraphPath;
            if (!editor.GetDocument().Update(e, "Apply material graph")) s.Error = editor.GetDocument().LastError();
            else { s.AssignedGraph = e.Material.GraphAsset; s.AssignedGraphHasNormal = s.Graph.Normal != 0; }
            editor.ReimportAsset(s.GraphPath);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use as shared material") && AssignSharedMaterial(editor, e.Id, s.GraphPath, s.Error))
        s.Selected = 0;
    ImGui::EndDisabled();
}

void SelectSceneMaterial(size_t index)
{
    auto& s = Tools();
    const auto& path = s.SceneMaterials[index].second;
    auto file = jEditorProject::ResolveSafe(s.SceneMaterialRoot, path, s.Error, true);
    MaterialGraph graph;
    if (!file || !LoadMaterial(*file, graph, s.Error)) return;
    s.SceneMaterialIndex = index;
    s.Graph = std::move(graph);
    s.SelectedNode = 0;
    s.NextNode = 1;
    for (const auto& node : s.Graph.Nodes) s.NextNode = std::max(s.NextNode, node.Id + 1);
    s.Undo.clear(); s.Redo.clear();
    std::snprintf(s.GraphPath, sizeof(s.GraphPath), "%s", path.c_str());
}

bool RenderSceneMaterialLibrary(jEditor& editor, const jEditorProject* project, const jEditorAssetRegistry* registry)
{
    if (!g_Engine) return false;
    auto& s = Tools();
    const auto& game = g_Engine->Game;
    const auto active = game.GetActivePathTracingSceneIndex();
    if (active < 0) return false;
    if (s.SceneMaterialRoot.empty())
    {
        if (!ImGui::SmallButton("Scene material library")) return false;
        try
        {
            const std::filesystem::path source(game.GetLoadablePathTracingScenes().at(active).FilePath);
            const auto bindings = ReadSceneMaterialBindings(source);
            if (bindings.empty()) throw std::runtime_error("The active scene has no material library");
            s.SceneMaterials.clear();
            for (const auto& [name, path] : bindings.items())
                s.SceneMaterials.emplace_back(name, path.get<std::string>());
            s.SceneMaterialRoot = std::filesystem::absolute(source).parent_path();
            s.Graph = {};
            s.SceneMaterialIndex = 0;
            SelectSceneMaterial(0);
        }
        catch (const std::exception& error) { s.Error = error.what(); ImGui::TextWrapped("%s", s.Error.c_str()); return false; }
    }
    else if (ImGui::SmallButton("Object material graph"))
    {
        s.SceneMaterialRoot.clear();
        s.Selected = 0;
        s.AssignedGraph.clear();
        return false;
    }
    const auto& selected = s.SceneMaterials[s.SceneMaterialIndex];
    if (ImGui::BeginCombo("Source material", selected.first.c_str()))
    {
        for (size_t i = 0; i < s.SceneMaterials.size(); ++i)
            if (ImGui::Selectable(s.SceneMaterials[i].first.c_str(), i == s.SceneMaterialIndex)) SelectSceneMaterial(i);
        ImGui::EndCombo();
    }
    if (!s.Graph.Nodes.empty())
    {
        const bool matchingProject = project && project->GetRoot() == s.SceneMaterialRoot;
        GraphUI(editor, {}, matchingProject ? project : nullptr, matchingProject ? registry : nullptr);
    }
    if (!s.Error.empty()) ImGui::TextWrapped("%s", s.Error.c_str());
    return true;
}
}
void FinishTerrainStroke(jEditor& editor)
{
    auto& s = Tools();
    if (!s.StrokeEntity) return;
    s.StrokeEntity = 0;
    s.LastDab.reset();
    auto& doc = editor.GetDocument();
    if (doc.HasTransaction() && !doc.CommitTransaction())
    {
        s.Error = doc.LastError();
        doc.CancelTransaction();
    }
}

bool RenderTerrainBrush(jEditor& editor, const jCamera& camera, const ImVec2& position, const ImVec2& size, bool hovered)
{
    auto& s = Tools();
    auto& doc = editor.GetDocument();
    const auto* entity = doc.Find(editor.GetSelectedEntityId());
    if (!entity || entity->Kind != EntityKind::Terrain || doc.IsPlaying() ||
        doc.IsEffectivelyLocked(entity->Id) || !doc.IsEffectivelyVisible(entity->Id))
    {
        FinishTerrainStroke(editor);
        return false;
    }

    const auto& io = ImGui::GetIO();
    const bool navigating = io.KeyAlt || io.MouseDown[ImGuiMouseButton_Right];
    if (s.StrokeEntity && (s.StrokeEntity != entity->Id || navigating)) FinishTerrainStroke(editor);
    if (!hovered || navigating || !Finite(s.BrushRadius) || s.BrushRadius <= 0 || !Finite(s.BrushStrength))
    {
        s.LastDab.reset();
        return true;
    }

    Matrix world, inverseWorld;
    if (!doc.GetWorldMatrix(entity->Id, world.mm, inverseWorld.mm)) return true;
    if (s.BrushMesh.Indices.empty() || s.BrushTerrain != entity->Terrain)
    {
        s.BrushTerrain = entity->Terrain;
        s.BrushMesh = BuildTerrain(s.BrushTerrain);
    }
    const float x = 2 * (io.MousePos.x - position.x) / size.x - 1;
    const float y = 1 - 2 * (io.MousePos.y - position.y) / size.y;
    const Matrix clipToWorld = camera.GetInverseViewProjectionMatrix();
    const Vector nearPoint = clipToWorld.TransformPoint(Vector(x, y, 0));
    const Vector rayPoint = clipToWorld.TransformPoint(Vector(x, y, .9f));
    const Vector origin = inverseWorld.TransformPoint(nearPoint);
    const Vector direction = inverseWorld.TransformDirection(rayPoint - nearPoint);
    Vector hit;
    const Matrix localToClip = camera.GetViewProjectionMatrix() * world;
    if (!FindTerrainHit(s.BrushMesh, origin, direction, hit) ||
        !(localToClip.TransformPoint(hit).z <= 1))
    {
        s.LastDab.reset();
        return true;
    }

    DrawTerrainBrush(entity->Terrain, hit, localToClip, position, size);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) ApplyTerrainDabs(doc, *entity, hit);
    return true;
}

bool Render(jEditor& editor, const jEditorProject* project, const jEditorAssetRegistry* registry)
{
    if (RenderSceneMaterialLibrary(editor, project, registry)) return false;
    auto& s = Tools(); auto& doc = editor.GetDocument(); const auto* selected = doc.Find(editor.GetSelectedEntityId());
    if (!selected) { ImGui::TextUnformatted("Select an object to edit its material, animation or procedural components."); return false; }
    Entity e = *selected;
    bool paintInScene = false;
    if (s.Selected != e.Id || s.AssignedGraph != e.Material.GraphAsset)
    {
        s.Selected = e.Id; s.AssignedGraph = e.Material.GraphAsset; s.Graph = {}; s.Undo.clear(); s.Redo.clear();
        s.Graph.Nodes = {{1, 0, 0, NodeKind::Color, e.Material.BaseColor, 1, 20, 20, ""}, {2, 0, 0, NodeKind::Scalar, {}, e.Material.Metallic, 20, 120, ""}, {3, 0, 0, NodeKind::Scalar, {}, e.Material.Roughness, 20, 220, ""}};
        s.Graph.BaseColor = 1; s.Graph.Metallic = 2; s.Graph.Roughness = 3; s.NextNode = 4; s.Error.clear();
        if (!e.Material.GraphAsset.empty()) { std::snprintf(s.GraphPath, sizeof(s.GraphPath), "%s", e.Material.GraphAsset.c_str()); const auto file = editor.GetProjectRoot() / e.Material.GraphAsset; LoadMaterial(file, s.Graph, s.Error); for (const auto& n : s.Graph.Nodes) s.NextNode = std::max(s.NextNode, n.Id + 1); }
        s.AssignedGraphHasNormal = s.Graph.Normal != 0;
    }
    if (ImGui::BeginTabBar("AdvancedTools"))
    {
        if (ImGui::BeginTabItem("Material graph")) { GraphUI(editor, e, project, registry); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Animation"))
        {
            bool changed = ImGui::Checkbox("Enabled", &e.Animation.Enabled); ImGui::SameLine(); changed |= ImGui::Checkbox("Loop", &e.Animation.Loop);
            changed |= ImGui::DragFloat("Duration", &e.Animation.Duration, .05f, .01f, 86400);
            if (ImGui::SliderFloat("Time", &s.Time, 0, e.Animation.Duration) && doc.IsPlaying()) editor.SetPlayTime(s.Time);
            ImGui::Checkbox("Preview in Play", &s.Preview);
            if (ImGui::Button("Key current transform"))
            {
                auto at = std::lower_bound(e.Animation.Keys.begin(), e.Animation.Keys.end(), s.Time, [](const auto& key, float t) { return key.Time < t; });
                if (at != e.Animation.Keys.end() && std::abs(at->Time - s.Time) < .0001f) at->Local = e.Local;
                else e.Animation.Keys.insert(at, {s.Time, e.Local}); changed = true;
            }
            ImGui::TextDisabled("Keys store local position, rotation in degrees and scale. Play evaluates without changing the saved scene.");
            for (size_t i = 0; i < e.Animation.Keys.size(); ++i)
            {
                ImGui::PushID(int(i)); auto& key = e.Animation.Keys[i]; ImGui::Text("%.3f s", key.Time); ImGui::SameLine();
                if (ImGui::SmallButton("Go")) { s.Time = key.Time; e.Local = key.Local; changed = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("Delete key")) { e.Animation.Keys.erase(e.Animation.Keys.begin() + i); changed = true; ImGui::PopID(); break; } ImGui::PopID();
            }
            if (changed && !doc.Update(e, "Edit animation")) s.Error = doc.LastError();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Particles"))
        {
            if (e.Kind != EntityKind::ParticleEmitter) ImGui::TextUnformatted("Add a ParticleEmitter from the Add menu and select it.");
            else
            {
                bool changed = ImGui::Checkbox("Emission", &e.Particles.Enabled); int maximum = int(e.Particles.MaxParticles), seed = int(e.Particles.Seed);
                changed |= ImGui::SliderInt("Maximum particles", &maximum, 1, 4096); e.Particles.MaxParticles = uint32_t(maximum);
                changed |= ImGui::InputInt("Seed", &seed); e.Particles.Seed = uint32_t(std::max(0, seed));
                changed |= ImGui::DragFloat("Spawn / second", &e.Particles.SpawnRate, 1, 0, 10000);
                changed |= ImGui::DragFloat("Lifetime", &e.Particles.Lifetime, .05f, .01f, 1000);
                changed |= ImGui::DragFloat("Speed", &e.Particles.Speed, 1, 0, 100000);
                changed |= ImGui::DragFloat("Size", &e.Particles.Size, .1f, .01f, 10000);
                changed |= ImGui::DragFloat3("Gravity", &e.Particles.Gravity.X, 1); changed |= ImGui::ColorEdit3("Particle color", &e.Particles.Color.X);
                if (changed && !doc.Update(e, "Edit particle emitter")) s.Error = doc.LastError();
                ImGui::TextUnformatted("Play previews deterministic CPU billboard simulation.");
            } ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Terrain / Navigation"))
        {
            if (e.Kind != EntityKind::Terrain) ImGui::TextUnformatted("Add a Terrain from the Add menu and select it.");
            else
            {
                int resolution = int(e.Terrain.Resolution); bool changed = false;
                if (ImGui::SliderInt("Grid resolution", &resolution, 3, 129)) { e.Terrain.Resolution = uint32_t(resolution); e.Terrain.Heights.clear(); changed = true; }
                changed |= ImGui::DragFloat("Width", &e.Terrain.SizeX, 1, 1, 100000);
                changed |= ImGui::DragFloat("Depth", &e.Terrain.SizeZ, 1, 1, 100000);
                ImGui::Combo("Brush", &s.BrushMode, BrushModes, IM_ARRAYSIZE(BrushModes));
                ImGui::DragFloat("Radius", &s.BrushRadius, 1, 1, 100000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                if (s.BrushMode == 3) ImGui::TextUnformatted("Strength: 100% with falloff");
                else ImGui::DragFloat(s.BrushMode == 2 ? "Target height" : "Strength", &s.BrushStrength, 1);
                ImGui::BeginDisabled(doc.IsPlaying() || doc.IsEffectivelyLocked(e.Id) || !doc.IsEffectivelyVisible(e.Id));
                ImGui::Checkbox("Paint in Scene", &s.PaintInScene);
                ImGui::EndDisabled();
                paintInScene = s.PaintInScene;
                if (paintInScene) ImGui::TextWrapped("Left-click or drag on the selected terrain. Right-drag or Alt+drag to move the camera.");
                ImGui::SliderFloat("Brush X", &s.BrushX, -e.Terrain.SizeX / 2, e.Terrain.SizeX / 2); ImGui::SliderFloat("Brush Z", &s.BrushZ, -e.Terrain.SizeZ / 2, e.Terrain.SizeZ / 2);
                if (ImGui::Button("Apply brush")) changed |= PaintTerrain(e.Terrain, s.BrushX, s.BrushZ, s.BrushRadius, s.BrushStrength, s.BrushMode);
                changed |= ImGui::Checkbox("Navigation enabled", &e.Navigation.Enabled);
                changed |= ImGui::DragFloat("Cell size", &e.Navigation.CellSize, 1, 1, 1000);
                changed |= ImGui::DragFloat("Agent radius", &e.Navigation.AgentRadius, 1, 0, 1000);
                changed |= ImGui::DragFloat("Agent height", &e.Navigation.AgentHeight, 1, 1, 1000);
                changed |= ImGui::SliderFloat("Maximum slope", &e.Navigation.MaxSlopeDegrees, 0, 89);
                ImGui::DragFloat3("Path start (local)", &s.NavFrom.X, 1); ImGui::DragFloat3("Path end (local)", &s.NavTo.X, 1);
                if (ImGui::Button("Bake and find path"))
                {
                    if (BakeNavigation(BuildTerrain(e.Terrain), e.Navigation, s.NavFrom, s.NavTo, s.Nav, s.Error))
                    {
                        const auto file = jEditorProject::ResolveSafe(editor.GetProjectRoot(), "Navigation/" + std::to_string(e.Id) + ".jnav", s.Error);
                        Json vertices = Json::array(), path = Json::array(); for (const auto& v : s.Nav.DebugMesh.Positions) vertices.push_back({v.X, v.Y, v.Z}); for (const auto& v : s.Nav.Path) path.push_back({v.X, v.Y, v.Z});
                        if (file) Write(*file, Json{{"format", "jEngineNavigation"}, {"version", 1}, {"entity", e.Id}, {"vertices", vertices}, {"indices", s.Nav.DebugMesh.Indices}, {"path", path}}.dump(), s.Error);
                    }
                }
                ImGui::Text("Walkable polygons %u | Path points %zu", s.Nav.PolygonCount, s.Nav.Path.size());
                if (!s.Nav.Path.empty()) { ImGui::BeginChild("Path preview", ImVec2(0, 100), true); for (const auto& point : s.Nav.Path) ImGui::Text("%.1f, %.1f, %.1f", point.X, point.Y, point.Z); ImGui::EndChild(); }
                if (changed && !doc.Update(e, "Edit terrain / navigation")) s.Error = doc.LastError();
            } ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!s.Error.empty()) ImGui::TextColored(ImVec4(1, .4f, .25f, 1), "%s", s.Error.c_str());
    return paintInScene;
}
void Tick(jEditor& editor, float dt)
{
    auto& s = Tools();
    if (!s.Preview) { if (s.PreviewStarted && editor.GetDocument().IsPlaying()) editor.StopPlay(); s.PreviewStarted = false; return; }
    if (s.PreviewStarted && !editor.GetDocument().IsPlaying()) { s.Preview = false; s.PreviewStarted = false; return; }
    if (!editor.GetDocument().IsPlaying())
    {
        if (!editor.BeginPlay()) { s.Error = editor.GetDocument().LastError(); s.Preview = false; return; }
    }
    s.PreviewStarted = true;
    if (!std::isfinite(dt) || dt < 0) return;
    s.Time = editor.PlayTime();
}
void Shutdown()
{
    CancelMaterialMappingTexturePreparation();
    auto& retired = MappingImages().Retired;
    for (auto& future : retired) if (future.valid()) { try { future.get(); } catch (...) {} }
    retired.clear();
    Tools() = ToolState{};
}
}
