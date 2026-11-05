#include "pch.h"
#include "jEditorDocument.h"
#include "jEditorWater.h"
#include "External/PathTracingDataLoader/json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace jEditorAuthoring
{
namespace
{
using Json = nlohmann::json;
constexpr double Pi = 3.14159265358979323846;
constexpr size_t MaxNameBytes = 256, MaxPathBytes = 4096, MaxAnimationKeys = 4096;
struct SceneState
{
    std::string Name = "Untitled";
    std::map<EntityId, Entity> Entities;
    EntityId Selected = InvalidEntity;
};
struct Matrix4
{
    // Internal row/column indexing. Public export is column-major.
    double M[4][4]{};
    static Matrix4 Identity()
    {
        Matrix4 result;
        for (int i = 0; i < 4; ++i) result.M[i][i] = 1;
        return result;
    }
};
Matrix4 Multiply(const Matrix4& a, const Matrix4& b)
{
    Matrix4 result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) result.M[r][c] += a.M[r][k] * b.M[k][c];
    return result;
}
Matrix4 Compose(const Transform& transform)
{
    const double x = transform.RotationDegrees.X * Pi / 180;
    const double y = transform.RotationDegrees.Y * Pi / 180;
    const double z = transform.RotationDegrees.Z * Pi / 180;
    const double sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y);
    const double sz = std::sin(z), cz = std::cos(z);
    Matrix4 result = Matrix4::Identity();
    result.M[0][0] = cz * cy * transform.Scale.X;
    result.M[1][0] = sz * cy * transform.Scale.X;
    result.M[2][0] = -sy * transform.Scale.X;
    result.M[0][1] = (cz * sy * sx - sz * cx) * transform.Scale.Y;
    result.M[1][1] = (sz * sy * sx + cz * cx) * transform.Scale.Y;
    result.M[2][1] = cy * sx * transform.Scale.Y;
    result.M[0][2] = (cz * sy * cx + sz * sx) * transform.Scale.Z;
    result.M[1][2] = (sz * sy * cx - cz * sx) * transform.Scale.Z;
    result.M[2][2] = cy * cx * transform.Scale.Z;
    result.M[0][3] = transform.Position.X;
    result.M[1][3] = transform.Position.Y;
    result.M[2][3] = transform.Position.Z;
    return result;
}
bool InverseAffine(const Matrix4& m, Matrix4& inverse)
{
    const double a = m.M[0][0], b = m.M[0][1], c = m.M[0][2];
    const double d = m.M[1][0], e = m.M[1][1], f = m.M[1][2];
    const double g = m.M[2][0], h = m.M[2][1], i = m.M[2][2];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < 1e-30) return false;
    inverse = Matrix4::Identity();
    inverse.M[0][0] = (e * i - f * h) / det;
    inverse.M[0][1] = (c * h - b * i) / det;
    inverse.M[0][2] = (b * f - c * e) / det;
    inverse.M[1][0] = (f * g - d * i) / det;
    inverse.M[1][1] = (a * i - c * g) / det;
    inverse.M[1][2] = (c * d - a * f) / det;
    inverse.M[2][0] = (d * h - e * g) / det;
    inverse.M[2][1] = (b * g - a * h) / det;
    inverse.M[2][2] = (a * e - b * d) / det;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) inverse.M[r][3] -= inverse.M[r][k] * m.M[k][3];
    return true;
}
bool Decompose(const Matrix4& m, Transform& transform)
{
    double scale[3]{}, rotation[3][3]{};
    for (int c = 0; c < 3; ++c)
    {
        for (int r = 0; r < 3; ++r) scale[c] += m.M[r][c] * m.M[r][c];
        scale[c] = std::sqrt(scale[c]);
        if (!std::isfinite(scale[c]) || scale[c] < 1e-8) return false;
        for (int r = 0; r < 3; ++r) rotation[r][c] = m.M[r][c] / scale[c];
    }
    for (int c = 0; c < 3; ++c)
        for (int d = c + 1; d < 3; ++d)
        {
            double dot = 0;
            for (int r = 0; r < 3; ++r) dot += rotation[r][c] * rotation[r][d];
            if (std::abs(dot) > 2e-5) return false; // TRS cannot represent shear.
        }
    const double det = rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1])
        - rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0])
        + rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0]);
    if (det < 0)
    {
        scale[0] = -scale[0];
        for (int r = 0; r < 3; ++r) rotation[r][0] = -rotation[r][0];
    }
    const double y = std::asin(std::clamp(-rotation[2][0], -1.0, 1.0));
    double x = 0, z = 0;
    if (std::abs(std::cos(y)) > 1e-6)
    {
        x = std::atan2(rotation[2][1], rotation[2][2]);
        z = std::atan2(rotation[1][0], rotation[0][0]);
    }
    else x = std::atan2(-rotation[1][2], rotation[1][1]);
    transform.Position = {float(m.M[0][3]), float(m.M[1][3]), float(m.M[2][3])};
    transform.RotationDegrees = {float(x * 180 / Pi), float(y * 180 / Pi), float(z * 180 / Pi)};
    transform.Scale = {float(scale[0]), float(scale[1]), float(scale[2])};
    const Matrix4 rebuilt = Compose(transform);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::abs(rebuilt.M[r][c] - m.M[r][c]) > 2e-4 * std::max(1.0, std::abs(m.M[r][c]))) return false;
    return true;
}
bool WorldMatrix(const SceneState& state, EntityId id, size_t maxDepth, Matrix4& output)
{
    std::vector<const Entity*> ancestry;
    while (id != InvalidEntity)
    {
        auto it = state.Entities.find(id);
        if (it == state.Entities.end() || ancestry.size() >= maxDepth) return false;
        ancestry.push_back(&it->second);
        id = it->second.Parent;
    }
    output = Matrix4::Identity();
    for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it) output = Multiply(output, Compose((*it)->Local));
    return true;
}
bool ValidText(const std::string& text, size_t maximum, bool allowEmpty = false)
{
    if ((!allowEmpty && text.empty()) || text.size() > maximum) return false;
    // Validate UTF-8 before passing text to UI, filesystem or JSON serializers.
    for (size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x20 || c == 0x7f) return false;
        if (c < 0x80) continue;
        int following = 0;
        uint32_t code = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { following = 1; code = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { following = 2; code = c & 0x0f; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { following = 3; code = c & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - i < static_cast<size_t>(following)) return false;
        for (int n = 0; n < following; ++n)
        {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
bool ValidAssetPath(const std::string& path)
{
    if (!ValidText(path, MaxPathBytes, true)) return false;
    if (path.empty()) return true;
    if (path.front() == '/' || path.back() == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size())
    {
        const size_t end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
bool Number(float value, float minimum, float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
bool VectorNumber(const Vec3& value, float minimum, float maximum)
{
    return Number(value.X, minimum, maximum) && Number(value.Y, minimum, maximum) && Number(value.Z, minimum, maximum);
}
bool ValidTransform(const Transform& value)
{
    return VectorNumber(value.Position, -1e8f, 1e8f) && VectorNumber(value.RotationDegrees, -1e6f, 1e6f)
        && VectorNumber(value.Scale, -1e4f, 1e4f) && std::abs(value.Scale.X) >= 1e-5f
        && std::abs(value.Scale.Y) >= 1e-5f && std::abs(value.Scale.Z) >= 1e-5f;
}
bool ValidateMaterial(const MaterialProperties& material, std::string& error)
{
    const auto reject = [&](const char* message) { error = message; return false; };
    if (!ValidAssetPath(material.BaseColorTexture) || !ValidAssetPath(material.NormalTexture) || !ValidAssetPath(material.GraphAsset))
        return reject("Material references must be normalized project-relative paths");
    if (material.GraphOverrides && material.GraphAsset.empty()) return reject("Shared material overrides require a graph asset");
    if (!VectorNumber(material.BaseColor, 0, 1) || !VectorNumber(material.Emissive, 0, 1e6f)
        || !Number(material.Metallic, 0, 1) || !Number(material.Roughness, 0, 1) || !Number(material.Opacity, 0, 1))
        return reject("Invalid material properties");
    if (!Number(material.SubsurfaceWeight, 0, 1) || !VectorNumber(material.SubsurfaceColor, 0, 1)
        || !Number(material.SubsurfaceThicknessWorld, 0, 10000) || material.SubsurfaceProfile > 7)
        return reject("Subsurface requires weight/color 0..1, thickness 0..10000 world units and profile 0..7");
    if (material.GraphOverrides && !material.SubsurfaceOverrideEnabled)
    {
        const auto& flags = *material.GraphOverrides;
        if (flags.SubsurfaceWeight || flags.SubsurfaceColor || flags.SubsurfaceThickness || flags.SubsurfaceProfile)
            return reject("Subsurface graph overrides require explicit authored subsurface data");
    }
    if (uint32_t(material.ShadingModel) > 1 || uint32_t(material.SubsurfaceSpace) > 1
        || !Number(material.SpecularWeight, 0, 8) || !Number(material.DiffuseOcclusion, 0, 1)
        || !Number(material.PreScatterWeight, 0, 1))
        return reject("Skin material requires valid model/space, specular weight 0..8 and occlusion/pre-scatter 0..1");
    if (material.GraphOverrides && !material.SkinOverrideEnabled)
    {
        const auto& flags = *material.GraphOverrides;
        if (flags.ShadingModel || flags.SubsurfaceSpace || flags.SpecularWeight || flags.DiffuseOcclusion || flags.PreScatterWeight)
            return reject("Skin graph overrides require explicit authored skin data");
    }
    return true;
}
bool ValidateEntity(const Entity& entity, std::string& error)
{
    const auto reject = [&](const char* message) { error = message; return false; };
    if (!entity.Id || entity.Id == std::numeric_limits<EntityId>::max()) return reject("Invalid or exhausted entity ID");
    if (!KindName(entity.Kind)) return reject("Unknown entity kind");
    if (!ValidText(entity.Name, MaxNameBytes)) return reject("Entity name must be valid UTF-8, 1..256 bytes without controls");
    if (!ValidAssetPath(entity.SourceAsset))
        return reject("Asset references must be normalized project-relative paths");
    if (!ValidText(entity.AssetId, 128, true)) return reject("Invalid asset ID");
    if (!ValidTransform(entity.Local)) return reject("Transform must be finite, bounded and have nonzero scale");
    if (entity.MeshLocalTransforms.size() > 65536) return reject("Mesh transform limit exceeded");
    for (const auto& mesh : entity.MeshLocalTransforms)
        if (!ValidTransform(mesh)) return reject("Invalid mesh-local transform");
    if (!ValidateMaterial(entity.Material, error)) return false;
    if (!entity.MeshMaterials.empty() && entity.Kind != EntityKind::Model) return reject("Submesh materials require a model entity");
    if (entity.MeshMaterials.size() > 65536) return reject("Submesh material limit exceeded");
    for (const auto& [index, material] : entity.MeshMaterials)
    {
        if (index >= 65536) return reject("Submesh material index exceeds the supported limit");
        if (!ValidateMaterial(material, error)) return false;
    }
    if (entity.ShadowCaster && entity.Kind != EntityKind::Cube && entity.Kind != EntityKind::Sphere
        && entity.Kind != EntityKind::Model && entity.Kind != EntityKind::ParticleEmitter
        && entity.Kind != EntityKind::Terrain && entity.Kind != EntityKind::Water)
        return reject("Shadow casting overrides require a renderable object; lights use Light.CastShadows");
    if (!VectorNumber(entity.Light.Color, 0, 1e6f) || !Number(entity.Light.Intensity, 0, 1e9f)
        || !Number(entity.Light.Range, 1e-4f, 1e8f) || !Number(entity.Light.InnerConeDegrees, 0, 89.9f)
        || !Number(entity.Light.OuterConeDegrees, entity.Light.InnerConeDegrees, 89.9f)) return reject("Invalid light properties");
    if (entity.Light.ShadowOverride)
    {
        using K = jAdvancedShadows::LightKind;
        const auto kind = entity.Kind == EntityKind::DirectionalLight ? K::Directional
            : entity.Kind == EntityKind::PointLight ? K::Point : K::Spot;
        if (entity.Kind != EntityKind::DirectionalLight && entity.Kind != EntityKind::PointLight && entity.Kind != EntityKind::SpotLight)
            return reject("Shadow overrides require a directional, point or spot light");
        if (!jAdvancedShadows::ValidateForLight(*entity.Light.ShadowOverride, kind, error)) return false;
    }
    if (!Number(entity.Camera.VerticalFovDegrees, 1, 179) || !Number(entity.Camera.NearPlane, 1e-4f, 1e8f)
        || !Number(entity.Camera.FarPlane, 1e-4f, 1e9f) || entity.Camera.FarPlane <= entity.Camera.NearPlane) return reject("Invalid camera properties");
    if (!Number(entity.Animation.Duration, 1e-4f, 86400) || entity.Animation.Keys.size() > MaxAnimationKeys)
        return reject("Animation exceeds duration/key limits");
    float previousTime = -1;
    const AnimationKeyframe* previousKey = nullptr;
    for (const auto& key : entity.Animation.Keys)
    {
        if (!Number(key.Time, 0, entity.Animation.Duration) || key.Time <= previousTime || !ValidTransform(key.Local))
            return reject("Animation key times must increase within duration and transforms must be valid");
        if (previousKey && (key.Local.Scale.X * previousKey->Local.Scale.X < 0
            || key.Local.Scale.Y * previousKey->Local.Scale.Y < 0 || key.Local.Scale.Z * previousKey->Local.Scale.Z < 0))
            return reject("Animation scale interpolation cannot cross zero");
        previousTime = key.Time;
        previousKey = &key;
    }
    if (entity.Particles.MaxParticles == 0 || entity.Particles.MaxParticles > 1000000
        || !Number(entity.Particles.SpawnRate, 0, 1e6f) || !Number(entity.Particles.Lifetime, 1e-4f, 86400)
        || !Number(entity.Particles.Speed, 0, 1e6f) || !Number(entity.Particles.Size, 1e-4f, 1e6f)
        || !VectorNumber(entity.Particles.Gravity, -1e6f, 1e6f) || !VectorNumber(entity.Particles.Color, 0, 1))
        return reject("Invalid particle properties");
    const size_t resolution = entity.Terrain.Resolution;
    if (resolution < 2 || resolution > 129 || !Number(entity.Terrain.SizeX, 1e-4f, 1e8f) || !Number(entity.Terrain.SizeZ, 1e-4f, 1e8f)
        || (!entity.Terrain.Heights.empty() && entity.Terrain.Heights.size() != resolution * resolution)) return reject("Invalid terrain grid");
    for (float height : entity.Terrain.Heights)
        if (!Number(height, -1e6f, 1e6f)) return reject("Invalid terrain height");
    if (!jEditorWater::Validate(entity.Water, error)) return false;
    if (!Number(entity.Navigation.CellSize, 1e-3f, 1e6f) || !Number(entity.Navigation.AgentRadius, 0, 1e6f)
        || !Number(entity.Navigation.AgentHeight, 1e-3f, 1e6f) || !Number(entity.Navigation.MaxSlopeDegrees, 0, 89.9f))
        return reject("Invalid navigation properties");
    return true;
}
bool Validate(const SceneState& state, const Limits& limits, std::string& error)
{
    if (!ValidText(state.Name, MaxNameBytes)) { error = "Invalid scene name"; return false; }
    if (state.Entities.size() > limits.MaxEntities) { error = "Scene entity limit exceeded"; return false; }
    for (const auto& [id, entity] : state.Entities)
    {
        if (id != entity.Id || !ValidateEntity(entity, error)) return false;
        std::set<EntityId> ancestry;
        const Entity* current = &entity;
        while (current)
        {
            if (!ancestry.insert(current->Id).second) { error = "Hierarchy cycle rejected"; return false; }
            if (ancestry.size() > limits.MaxHierarchyDepth) { error = "Hierarchy depth limit exceeded"; return false; }
            if (!current->Parent) break;
            auto parent = state.Entities.find(current->Parent);
            if (parent == state.Entities.end()) { error = "Missing hierarchy parent"; return false; }
            current = &parent->second;
        }
        Matrix4 world;
        if (!WorldMatrix(state, id, limits.MaxHierarchyDepth, world)) { error = "Invalid hierarchy"; return false; }
        for (const auto& row : world.M)
            for (double value : row)
                if (!std::isfinite(value) || std::abs(value) > 1e12) { error = "World transform exceeds supported range"; return false; }
    }
    return true;
}
std::vector<EntityId> Subtree(const SceneState& state, EntityId root)
{
    if (!state.Entities.count(root)) return {};
    std::map<EntityId, std::vector<EntityId>> children;
    for (const auto& [id, entity] : state.Entities) children[entity.Parent].push_back(id);
    std::vector<EntityId> result{root};
    for (size_t i = 0; i < result.size(); ++i)
    {
        auto it = children.find(result[i]);
        if (it != children.end()) result.insert(result.end(), it->second.begin(), it->second.end());
    }
    return result;
}
bool EffectiveFlag(const SceneState& state, EntityId id, bool locked)
{
    size_t remaining = state.Entities.size();
    while (id && remaining--)
    {
        auto it = state.Entities.find(id);
        if (it == state.Entities.end()) return locked;
        if (locked ? it->second.Locked : !it->second.Visible) return locked;
        id = it->second.Parent;
    }
    return !locked;
}
size_t StateBytes(const SceneState& state)
{
    size_t bytes = sizeof(SceneState) + state.Name.capacity();
    for (const auto& [id, entity] : state.Entities)
    {
        (void)id;
        bytes += sizeof(Entity) + 128 + entity.Name.capacity() + entity.SourceAsset.capacity() + entity.AssetId.capacity()
            + entity.Material.BaseColorTexture.capacity() + entity.Material.NormalTexture.capacity() + entity.Material.GraphAsset.capacity()
            + entity.Animation.Keys.capacity() * sizeof(AnimationKeyframe) + entity.Terrain.Heights.capacity() * sizeof(float)
            + entity.Water.Waves.Waves.capacity() * sizeof(jWaterSimulation::Wave)
            + entity.MeshLocalTransforms.capacity() * sizeof(Transform);
        for (const auto& [index, material] : entity.MeshMaterials)
            bytes += sizeof(MaterialProperties) + 48 + material.BaseColorTexture.capacity()
                + material.NormalTexture.capacity() + material.GraphAsset.capacity();
    }
    return bytes;
}
Json Encode(const Vec3& value) { return Json::array({value.X, value.Y, value.Z}); }
Json Encode(const Transform& value)
{
    return {{"position", Encode(value.Position)}, {"rotationDegrees", Encode(value.RotationDegrees)}, {"scale", Encode(value.Scale)}};
}
Json EncodeShadow(const jAdvancedShadows::Settings& s)
{
    return {{"projection", std::uint32_t(s.DirectionalProjection)}, {"pointProjection", std::uint32_t(s.PointProjection)},
        {"representation", std::uint32_t(s.Method)}, {"resolution", s.MapResolution}, {"cascadeCount", s.CascadeCount},
        {"splitLambda", s.SplitLambda}, {"maxDistance", s.MaxDistance}, {"cascadeStabilize", s.CascadeStabilize},
        {"casterPadding", s.CasterPadding}, {"positiveExponent", s.PositiveExponent}, {"negativeExponent", s.NegativeExponent},
        {"minimumVariance", s.MinimumVariance}, {"bleedReduction", s.BleedReduction}, {"blurRadius", s.BlurRadius},
        {"deepOpacityMultiplier", s.DeepOpacityMultiplier}, {"deepTauMax", s.DeepTauMax}, {"deepMaxDistance", s.DeepMaxDistance},
        {"volumeMaxTriangles", s.VolumeMaxTriangles}, {"volumeRange", s.VolumeRange}};
}
Json EncodeMaterial(const MaterialProperties& value)
{
    Json material{{"overrideEnabled", value.OverrideEnabled}, {"baseColor", Encode(value.BaseColor)}, {"emissive", Encode(value.Emissive)},
        {"metallic", value.Metallic}, {"roughness", value.Roughness}, {"opacity", value.Opacity},
        {"baseColorTexture", value.BaseColorTexture}, {"normalTexture", value.NormalTexture}, {"graphAsset", value.GraphAsset}};
    if (const auto& overrides = value.GraphOverrides)
    {
        material["graphOverrides"] = {{"baseColor", overrides->BaseColor}, {"baseColorTexture", overrides->BaseColorTexture},
            {"metallic", overrides->Metallic}, {"roughness", overrides->Roughness}, {"normalTexture", overrides->NormalTexture}};
        if (overrides->SubsurfaceWeight || overrides->SubsurfaceColor || overrides->SubsurfaceThickness || overrides->SubsurfaceProfile)
            material["graphOverrides"]["subsurface"] = {{"weight", overrides->SubsurfaceWeight}, {"color", overrides->SubsurfaceColor},
                {"thickness", overrides->SubsurfaceThickness}, {"profile", overrides->SubsurfaceProfile}};
        if (overrides->ShadingModel || overrides->SubsurfaceSpace || overrides->SpecularWeight || overrides->DiffuseOcclusion || overrides->PreScatterWeight)
            material["graphOverrides"]["skin"] = {{"shadingModel", overrides->ShadingModel}, {"subsurfaceSpace", overrides->SubsurfaceSpace},
                {"specularWeight", overrides->SpecularWeight}, {"diffuseOcclusion", overrides->DiffuseOcclusion}, {"preScatterWeight", overrides->PreScatterWeight}};
    }
    if (value.SubsurfaceOverrideEnabled)
        material["subsurface"] = {{"weight", value.SubsurfaceWeight}, {"color", Encode(value.SubsurfaceColor)},
            {"thicknessWorld", value.SubsurfaceThicknessWorld}, {"profile", value.SubsurfaceProfile}};
    if (value.SkinOverrideEnabled)
        material["skin"] = {{"shadingModel", uint32_t(value.ShadingModel)}, {"subsurfaceSpace", uint32_t(value.SubsurfaceSpace)},
            {"specularWeight", value.SpecularWeight}, {"diffuseOcclusion", value.DiffuseOcclusion}, {"preScatterWeight", value.PreScatterWeight}};
    return material;
}
Json Encode(const Entity& e)
{
    Json keys = Json::array();
    Json meshes = Json::array();
    for (const auto& mesh : e.MeshLocalTransforms) meshes.push_back(Encode(mesh));
    for (const auto& key : e.Animation.Keys) keys.push_back({{"time", key.Time}, {"transform", Encode(key.Local)}});
    Json result{{"id", e.Id}, {"parent", e.Parent}, {"kind", KindName(e.Kind)}, {"name", e.Name}, {"visible", e.Visible},
        {"locked", e.Locked}, {"transform", Encode(e.Local)}, {"sourceAsset", e.SourceAsset}, {"assetId", e.AssetId}, {"meshLocalTransforms", std::move(meshes)},
        {"material", EncodeMaterial(e.Material)},
        {"light", {{"color", Encode(e.Light.Color)}, {"intensity", e.Light.Intensity}, {"range", e.Light.Range},
            {"innerConeDegrees", e.Light.InnerConeDegrees}, {"outerConeDegrees", e.Light.OuterConeDegrees}, {"castShadows", e.Light.CastShadows}}},
        {"camera", {{"verticalFovDegrees", e.Camera.VerticalFovDegrees}, {"nearPlane", e.Camera.NearPlane},
            {"farPlane", e.Camera.FarPlane}, {"primary", e.Camera.Primary}}},
        {"animation", {{"enabled", e.Animation.Enabled}, {"loop", e.Animation.Loop}, {"duration", e.Animation.Duration}, {"keys", std::move(keys)}}},
        {"particles", {{"enabled", e.Particles.Enabled}, {"maxParticles", e.Particles.MaxParticles}, {"seed", e.Particles.Seed},
            {"spawnRate", e.Particles.SpawnRate}, {"lifetime", e.Particles.Lifetime}, {"speed", e.Particles.Speed},
            {"size", e.Particles.Size}, {"gravity", Encode(e.Particles.Gravity)}, {"color", Encode(e.Particles.Color)}}},
        {"terrain", {{"resolution", e.Terrain.Resolution}, {"sizeX", e.Terrain.SizeX}, {"sizeZ", e.Terrain.SizeZ}, {"heights", e.Terrain.Heights}}},
        {"navigation", {{"enabled", e.Navigation.Enabled}, {"cellSize", e.Navigation.CellSize}, {"agentRadius", e.Navigation.AgentRadius},
            {"agentHeight", e.Navigation.AgentHeight}, {"maxSlopeDegrees", e.Navigation.MaxSlopeDegrees}}}};
    if (!e.MeshMaterials.empty())
    {
        auto& materials = result["meshMaterials"] = Json::array();
        for (const auto& [index, material] : e.MeshMaterials)
            materials.push_back({{"index", index}, {"material", EncodeMaterial(material)}});
    }
    if (e.Light.ShadowOverride) result["light"]["shadow"] = EncodeShadow(*e.Light.ShadowOverride);
    if (e.ShadowCaster) result["shadowCaster"] = *e.ShadowCaster;
    // Keep unchanged v1 documents byte-shape compatible for non-water defaults.
    if (e.Kind == EntityKind::Water || e.Water != WaterProperties{})
    {
        Json waves = Json::array();
        for (const auto& wave : e.Water.Waves.Waves)
            waves.push_back({{"direction", Json::array({wave.Direction[0], wave.Direction[1]})},
                {"amplitude", wave.Amplitude}, {"wavelength", wave.Wavelength}, {"speed", wave.Speed},
                {"steepness", wave.Steepness}, {"phaseRadians", wave.PhaseRadians}});
        result["water"] = {{"resolution", e.Water.Resolution}, {"sizeX", e.Water.SizeX},
            {"sizeZ", e.Water.SizeZ}, {"baseHeight", e.Water.Waves.BaseHeight}, {"waves", std::move(waves)}};
    }
    return result;
}
std::string Serialize(const SceneState& state, const char* format = "jEngineScene", EntityId root = InvalidEntity,
    const std::vector<EntityId>* selectionRoots = nullptr)
{
    Json entities = Json::array();
    for (const auto& [id, entity] : state.Entities) { (void)id; entities.push_back(Encode(entity)); }
    Json result{{"format", format}, {"version", 1}, {"name", state.Name}, {"entities", std::move(entities)}};
    if (root) result["root"] = root;
    if (selectionRoots) result["roots"] = *selectionRoots;
    return result.dump(2) + "\n";
}
void ObjectKeys(const Json& json, std::initializer_list<const char*> keys, const char* optionalKey = nullptr,
    const char* secondOptionalKey = nullptr, const char* thirdOptionalKey = nullptr)
{
    if (!json.is_object() || json.size() != keys.size() + (optionalKey && json.contains(optionalKey))
        + (secondOptionalKey && json.contains(secondOptionalKey)) + (thirdOptionalKey && json.contains(thirdOptionalKey)))
        throw std::runtime_error("Unexpected or missing JSON fields");
    for (const char* key : keys)
        if (!json.contains(key)) throw std::runtime_error(std::string("Missing JSON field: ") + key);
}
std::string String(const Json& value)
{
    if (!value.is_string()) throw std::runtime_error("Expected JSON string");
    return value.get<std::string>();
}
float Float(const Json& value)
{
    if (!value.is_number()) throw std::runtime_error("Expected JSON number");
    const double number = value.get<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) throw std::runtime_error("Non-finite/out-of-range JSON number");
    return float(number);
}
uint64_t Unsigned(const Json& value)
{
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<int64_t>() >= 0))
        throw std::runtime_error("Expected nonnegative integer");
    return value.get<uint64_t>();
}
uint32_t Unsigned32(const Json& value)
{
    const uint64_t number = Unsigned(value);
    if (number > UINT32_MAX) throw std::runtime_error("Integer exceeds uint32 range");
    return uint32_t(number);
}
bool Boolean(const Json& value)
{
    if (!value.is_boolean()) throw std::runtime_error("Expected JSON boolean");
    return value.get<bool>();
}
jAdvancedShadows::Settings DecodeShadow(const Json& j)
{
    ObjectKeys(j, {"projection", "pointProjection", "representation", "resolution", "cascadeCount", "splitLambda", "maxDistance",
        "cascadeStabilize", "casterPadding", "positiveExponent", "negativeExponent", "minimumVariance", "bleedReduction",
        "blurRadius", "deepOpacityMultiplier", "deepTauMax", "deepMaxDistance", "volumeMaxTriangles", "volumeRange"});
    jAdvancedShadows::Settings s;
    s.DirectionalProjection = jAdvancedShadows::Projection(Unsigned32(j.at("projection")));
    s.PointProjection = jAdvancedShadows::PointProjection(Unsigned32(j.at("pointProjection")));
    s.Method = jAdvancedShadows::Representation(Unsigned32(j.at("representation")));
    s.MapResolution = Unsigned32(j.at("resolution")); s.CascadeCount = Unsigned32(j.at("cascadeCount"));
    s.SplitLambda = Float(j.at("splitLambda")); s.MaxDistance = Float(j.at("maxDistance"));
    s.CascadeStabilize = Boolean(j.at("cascadeStabilize")); s.CasterPadding = Float(j.at("casterPadding"));
    s.PositiveExponent = Float(j.at("positiveExponent")); s.NegativeExponent = Float(j.at("negativeExponent"));
    s.MinimumVariance = Float(j.at("minimumVariance")); s.BleedReduction = Float(j.at("bleedReduction"));
    s.BlurRadius = Unsigned32(j.at("blurRadius")); s.DeepOpacityMultiplier = Float(j.at("deepOpacityMultiplier"));
    s.DeepTauMax = Float(j.at("deepTauMax")); s.DeepMaxDistance = Float(j.at("deepMaxDistance"));
    s.VolumeMaxTriangles = Unsigned32(j.at("volumeMaxTriangles")); s.VolumeRange = Float(j.at("volumeRange"));
    return s;
}
Vec3 DecodeVec(const Json& value)
{
    if (!value.is_array() || value.size() != 3) throw std::runtime_error("Expected three vector components");
    return {Float(value[0]), Float(value[1]), Float(value[2])};
}
Transform DecodeTransform(const Json& value)
{
    ObjectKeys(value, {"position", "rotationDegrees", "scale"});
    return {DecodeVec(value.at("position")), DecodeVec(value.at("rotationDegrees")), DecodeVec(value.at("scale"))};
}
MaterialProperties DecodeMaterial(const Json& m)
{
    MaterialProperties material;
    ObjectKeys(m, {"overrideEnabled", "baseColor", "emissive", "metallic", "roughness", "opacity", "baseColorTexture", "normalTexture", "graphAsset"}, "graphOverrides", "subsurface", "skin");
    material.OverrideEnabled = Boolean(m.at("overrideEnabled"));
    material.BaseColor = DecodeVec(m.at("baseColor")); material.Emissive = DecodeVec(m.at("emissive"));
    material.Metallic = Float(m.at("metallic")); material.Roughness = Float(m.at("roughness")); material.Opacity = Float(m.at("opacity"));
    material.BaseColorTexture = String(m.at("baseColorTexture")); material.NormalTexture = String(m.at("normalTexture")); material.GraphAsset = String(m.at("graphAsset"));
    if (const auto it = m.find("graphOverrides"); it != m.end())
    {
        ObjectKeys(*it, {"baseColor", "baseColorTexture", "metallic", "roughness"}, "normalTexture", "subsurface", "skin");
        // Older scenes stored any explicit normal path as a local slot.
        const auto normal = it->find("normalTexture");
        material.GraphOverrides = MaterialGraphOverrides{Boolean(it->at("baseColor")), Boolean(it->at("baseColorTexture")),
            Boolean(it->at("metallic")), Boolean(it->at("roughness")),
            normal == it->end() ? !material.NormalTexture.empty() : Boolean(*normal)};
        if (const auto sss = it->find("subsurface"); sss != it->end())
        {
            ObjectKeys(*sss, {"weight", "color", "thickness", "profile"});
            auto& flags = *material.GraphOverrides;
            flags.SubsurfaceWeight = Boolean(sss->at("weight")); flags.SubsurfaceColor = Boolean(sss->at("color"));
            flags.SubsurfaceThickness = Boolean(sss->at("thickness")); flags.SubsurfaceProfile = Boolean(sss->at("profile"));
        }
        if (const auto skin = it->find("skin"); skin != it->end())
        {
            ObjectKeys(*skin, {"shadingModel", "subsurfaceSpace", "specularWeight", "diffuseOcclusion", "preScatterWeight"});
            auto& flags = *material.GraphOverrides;
            flags.ShadingModel = Boolean(skin->at("shadingModel")); flags.SubsurfaceSpace = Boolean(skin->at("subsurfaceSpace"));
            flags.SpecularWeight = Boolean(skin->at("specularWeight")); flags.DiffuseOcclusion = Boolean(skin->at("diffuseOcclusion"));
            flags.PreScatterWeight = Boolean(skin->at("preScatterWeight"));
        }
    }
    if (const auto sss = m.find("subsurface"); sss != m.end())
    {
        ObjectKeys(*sss, {"weight", "color", "thicknessWorld", "profile"});
        material.SubsurfaceOverrideEnabled = true;
        material.SubsurfaceWeight = Float(sss->at("weight")); material.SubsurfaceColor = DecodeVec(sss->at("color"));
        material.SubsurfaceThicknessWorld = Float(sss->at("thicknessWorld")); material.SubsurfaceProfile = Unsigned32(sss->at("profile"));
    }
    if (const auto skin = m.find("skin"); skin != m.end())
    {
        ObjectKeys(*skin, {"shadingModel", "subsurfaceSpace", "specularWeight", "diffuseOcclusion", "preScatterWeight"});
        material.SkinOverrideEnabled = true;
        material.ShadingModel = jMaterialMapping::ShadingModel(Unsigned32(skin->at("shadingModel")));
        material.SubsurfaceSpace = jMaterialMapping::SubsurfaceSpace(Unsigned32(skin->at("subsurfaceSpace")));
        material.SpecularWeight = Float(skin->at("specularWeight")); material.DiffuseOcclusion = Float(skin->at("diffuseOcclusion"));
        material.PreScatterWeight = Float(skin->at("preScatterWeight"));
    }
    return material;
}
Entity DecodeEntity(const Json& j)
{
    ObjectKeys(j, {"id", "parent", "kind", "name", "visible", "locked", "transform", "sourceAsset", "assetId", "meshLocalTransforms",
        "material", "light", "camera", "animation", "particles", "terrain", "navigation"}, "water", "shadowCaster", "meshMaterials");
    Entity e;
    e.Id = Unsigned(j.at("id")); e.Parent = Unsigned(j.at("parent"));
    const std::string kind = String(j.at("kind"));
    bool known = false;
    for (int i = 0; i <= int(EntityKind::Water); ++i)
        if (kind == KindName(EntityKind(i))) { e.Kind = EntityKind(i); known = true; break; }
    if (!known) throw std::runtime_error("Unknown entity kind");
    e.Name = String(j.at("name")); e.Visible = Boolean(j.at("visible")); e.Locked = Boolean(j.at("locked"));
    if (const auto shadow = j.find("shadowCaster"); shadow != j.end()) e.ShadowCaster = Boolean(*shadow);
    e.Local = DecodeTransform(j.at("transform")); e.SourceAsset = String(j.at("sourceAsset")); e.AssetId = String(j.at("assetId"));
    const auto& meshes = j.at("meshLocalTransforms");
    if (!meshes.is_array() || meshes.size() > 65536) throw std::runtime_error("Mesh transform limit exceeded");
    for (const auto& mesh : meshes) e.MeshLocalTransforms.push_back(DecodeTransform(mesh));
    e.Material = DecodeMaterial(j.at("material"));
    if (const auto materials = j.find("meshMaterials"); materials != j.end())
    {
        if (!materials->is_array() || materials->size() > 65536) throw std::runtime_error("Submesh material limit exceeded");
        for (const auto& slot : *materials)
        {
            ObjectKeys(slot, {"index", "material"});
            const auto index = Unsigned32(slot.at("index"));
            if (index >= 65536 || !e.MeshMaterials.emplace(index, DecodeMaterial(slot.at("material"))).second)
                throw std::runtime_error("Invalid or duplicate submesh material index");
        }
    }
    const auto& l = j.at("light");
    ObjectKeys(l, {"color", "intensity", "range", "innerConeDegrees", "outerConeDegrees", "castShadows"}, "shadow");
    e.Light.Color = DecodeVec(l.at("color")); e.Light.Intensity = Float(l.at("intensity")); e.Light.Range = Float(l.at("range"));
    e.Light.InnerConeDegrees = Float(l.at("innerConeDegrees")); e.Light.OuterConeDegrees = Float(l.at("outerConeDegrees")); e.Light.CastShadows = Boolean(l.at("castShadows"));
    if (const auto shadow = l.find("shadow"); shadow != l.end()) e.Light.ShadowOverride = DecodeShadow(*shadow);
    const auto& c = j.at("camera");
    ObjectKeys(c, {"verticalFovDegrees", "nearPlane", "farPlane", "primary"});
    e.Camera.VerticalFovDegrees = Float(c.at("verticalFovDegrees")); e.Camera.NearPlane = Float(c.at("nearPlane"));
    e.Camera.FarPlane = Float(c.at("farPlane")); e.Camera.Primary = Boolean(c.at("primary"));
    const auto& a = j.at("animation");
    ObjectKeys(a, {"enabled", "loop", "duration", "keys"});
    e.Animation.Enabled = Boolean(a.at("enabled")); e.Animation.Loop = Boolean(a.at("loop")); e.Animation.Duration = Float(a.at("duration"));
    if (!a.at("keys").is_array() || a.at("keys").size() > MaxAnimationKeys) throw std::runtime_error("Animation key limit exceeded");
    for (const auto& key : a.at("keys"))
    {
        ObjectKeys(key, {"time", "transform"});
        e.Animation.Keys.push_back({Float(key.at("time")), DecodeTransform(key.at("transform"))});
    }
    const auto& p = j.at("particles");
    ObjectKeys(p, {"enabled", "maxParticles", "seed", "spawnRate", "lifetime", "speed", "size", "gravity", "color"});
    e.Particles.Enabled = Boolean(p.at("enabled")); e.Particles.MaxParticles = Unsigned32(p.at("maxParticles")); e.Particles.Seed = Unsigned32(p.at("seed"));
    e.Particles.SpawnRate = Float(p.at("spawnRate")); e.Particles.Lifetime = Float(p.at("lifetime")); e.Particles.Speed = Float(p.at("speed"));
    e.Particles.Size = Float(p.at("size")); e.Particles.Gravity = DecodeVec(p.at("gravity")); e.Particles.Color = DecodeVec(p.at("color"));
    const auto& t = j.at("terrain");
    ObjectKeys(t, {"resolution", "sizeX", "sizeZ", "heights"});
    e.Terrain.Resolution = Unsigned32(t.at("resolution")); e.Terrain.SizeX = Float(t.at("sizeX")); e.Terrain.SizeZ = Float(t.at("sizeZ"));
    if (!t.at("heights").is_array() || t.at("heights").size() > 129 * 129) throw std::runtime_error("Terrain height limit exceeded");
    for (const auto& height : t.at("heights")) e.Terrain.Heights.push_back(Float(height));
    if (const auto it = j.find("water"); it != j.end())
    {
        ObjectKeys(*it, {"resolution", "sizeX", "sizeZ", "baseHeight", "waves"});
        e.Water.Resolution = Unsigned32(it->at("resolution"));
        e.Water.SizeX = Float(it->at("sizeX")); e.Water.SizeZ = Float(it->at("sizeZ"));
        e.Water.Waves.BaseHeight = Float(it->at("baseHeight"));
        const auto& waves = it->at("waves");
        if (!waves.is_array() || waves.size() > jEditorWater::MaximumGpuWaves)
            throw std::runtime_error("Water wave limit exceeded");
        e.Water.Waves.Waves.clear();
        for (const auto& value : waves)
        {
            ObjectKeys(value, {"direction", "amplitude", "wavelength", "speed", "steepness", "phaseRadians"});
            const auto& direction = value.at("direction");
            if (!direction.is_array() || direction.size() != 2) throw std::runtime_error("Water direction requires two components");
            jWaterSimulation::Wave wave;
            wave.Direction = {Float(direction[0]), Float(direction[1])};
            wave.Amplitude = Float(value.at("amplitude")); wave.Wavelength = Float(value.at("wavelength"));
            wave.Speed = Float(value.at("speed")); wave.Steepness = Float(value.at("steepness"));
            wave.PhaseRadians = Float(value.at("phaseRadians"));
            e.Water.Waves.Waves.push_back(wave);
        }
    }
    const auto& n = j.at("navigation");
    ObjectKeys(n, {"enabled", "cellSize", "agentRadius", "agentHeight", "maxSlopeDegrees"});
    e.Navigation.Enabled = Boolean(n.at("enabled")); e.Navigation.CellSize = Float(n.at("cellSize"));
    e.Navigation.AgentRadius = Float(n.at("agentRadius")); e.Navigation.AgentHeight = Float(n.at("agentHeight")); e.Navigation.MaxSlopeDegrees = Float(n.at("maxSlopeDegrees"));
    return e;
}
bool Parse(const std::string& text, const Limits& limits, SceneState& state, std::string& error, bool prefab = false,
    EntityId* prefabRoot = nullptr, std::vector<EntityId>* selectionRoots = nullptr)
{
    try
    {
        if (text.empty() || text.size() > limits.MaxFileBytes) throw std::runtime_error("Scene file byte limit exceeded or file is empty");
        std::vector<std::set<std::string>> objectKeys;
        const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 32) throw std::runtime_error("JSON nesting limit exceeded");
            if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
            else if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
            else if (event == Json::parse_event_t::key && !objectKeys.back().insert(parsed.get<std::string>()).second)
                throw std::runtime_error("Duplicate JSON field rejected");
            return true;
        };
        const auto json = Json::parse(text, callback);
        if (selectionRoots) ObjectKeys(json, {"format", "version", "name", "entities", "roots"});
        else if (prefab) ObjectKeys(json, {"format", "version", "name", "entities", "root"});
        else ObjectKeys(json, {"format", "version", "name", "entities"});
        if (String(json.at("format")) != (selectionRoots ? "jEngineSelection" : prefab ? "jEnginePrefab" : "jEngineScene") || Unsigned(json.at("version")) != 1)
            throw std::runtime_error("Unsupported scene format/version");
        state.Name = String(json.at("name"));
        if (!json.at("entities").is_array() || json.at("entities").size() > limits.MaxEntities) throw std::runtime_error("Scene entity limit exceeded");
        for (const auto& value : json.at("entities"))
        {
            Entity entity = DecodeEntity(value);
            if (!state.Entities.emplace(entity.Id, std::move(entity)).second) throw std::runtime_error("Duplicate entity ID rejected");
        }
        if (!Validate(state, limits, error)) return false;
        if (selectionRoots)
        {
            const auto& roots = json.at("roots");
            if (!roots.is_array() || roots.empty() || roots.size() > limits.MaxEntities)
                throw std::runtime_error("Clipboard requires a bounded nonempty root selection");
            std::set<EntityId> declared, actual;
            for (const auto& [id, entity] : state.Entities) if (!entity.Parent) actual.insert(id);
            for (const auto& value : roots)
            {
                const EntityId id = Unsigned(value);
                if (!id || !declared.insert(id).second) throw std::runtime_error("Duplicate or invalid clipboard root");
                selectionRoots->push_back(id);
            }
            if (declared != actual) throw std::runtime_error("Clipboard roots must exactly cover its complete entity forest");
        }
        if (prefab)
        {
            const EntityId root = Unsigned(json.at("root"));
            if (!root || !state.Entities.count(root) || state.Entities.at(root).Parent || Subtree(state, root).size() != state.Entities.size())
                throw std::runtime_error("Prefab must contain exactly one complete root subtree");
            if (prefabRoot) *prefabRoot = root;
        }
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool ReadFile(const std::filesystem::path& path, size_t limit, std::string& contents, std::string& error)
{
    try
    {
        if (!std::filesystem::is_regular_file(path)) { error = "Scene path is not a regular file"; return false; }
        const auto size = std::filesystem::file_size(path);
        if (!size || size > limit) { error = "File is empty or exceeds scene byte limit"; return false; }
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Cannot open scene file"; return false; }
        contents.resize(static_cast<size_t>(size));
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (input.gcount() != static_cast<std::streamsize>(contents.size()) || input.peek() != std::char_traits<char>::eof() || input.bad())
        { error = "Scene file changed or could not be read completely"; return false; }
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool ReplaceFile(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error)
{
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { error = std::system_category().message(GetLastError()); return false; }
#else
    if (::rename(from.c_str(), to.c_str()) != 0) { error = std::generic_category().message(errno); return false; }
#endif
    return true;
}
bool AtomicWrite(const std::filesystem::path& path, const std::string& contents, bool backup, size_t limit, std::string& error)
{
    static std::atomic<uint64_t> counter{1};
    std::filesystem::path temporary;
    int descriptor = -1;
    const auto cleanup = [&] {
        if (descriptor >= 0)
        {
#ifdef _WIN32
            _close(descriptor);
#else
            ::close(descriptor);
#endif
            descriptor = -1;
        }
        std::error_code ignored;
        if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
    };
    try
    {
        if (contents.size() > limit) { error = "Serialized scene exceeds byte limit"; return false; }
        if (path.empty() || !path.has_filename()) { error = "A scene filename is required"; return false; }
        const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        if (!std::filesystem::is_directory(parent)) { error = "Scene parent directory does not exist"; return false; }
        std::error_code statusError;
        const auto status = std::filesystem::symlink_status(path, statusError);
        if (!statusError && std::filesystem::exists(status) && (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)))
        { error = "Refusing to replace a symlink or nonregular scene file"; return false; }
#ifdef _WIN32
        const auto processId = GetCurrentProcessId();
#else
        const auto processId = ::getpid();
#endif
        for (int attempt = 0; attempt < 16 && descriptor < 0; ++attempt)
        {
            temporary = path;
            temporary += ".tmp-" + std::to_string(processId) + "-" + std::to_string(counter.fetch_add(1));
#ifdef _WIN32
            const errno_t opened = _wsopen_s(&descriptor, temporary.c_str(),
                _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                _SH_DENYRW, _S_IREAD | _S_IWRITE);
            if (opened != 0) { descriptor = -1; errno = opened; }
#else
            descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
            if (descriptor < 0 && errno != EEXIST) break;
        }
        if (descriptor < 0) { error = "Cannot create exclusive scene temporary file"; temporary.clear(); return false; }
        size_t offset = 0;
        while (offset < contents.size())
        {
#ifdef _WIN32
            const auto count = _write(descriptor, contents.data() + offset, static_cast<unsigned int>(std::min<size_t>(contents.size() - offset, INT_MAX)));
#else
            const auto count = ::write(descriptor, contents.data() + offset, contents.size() - offset);
#endif
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { error = "Failed to write scene temporary file"; cleanup(); return false; }
            offset += static_cast<size_t>(count);
        }
#ifdef _WIN32
        const int syncResult = _commit(descriptor);
        const int closeResult = _close(descriptor);
#else
        const int syncResult = ::fsync(descriptor);
        const int closeResult = ::close(descriptor);
#endif
        descriptor = -1;
        if (syncResult != 0 || closeResult != 0) { error = "Failed to flush scene temporary file"; cleanup(); return false; }
        if (backup && std::filesystem::is_regular_file(status))
        {
            auto backupPath = path; backupPath += ".bak";
            const auto backupStatus = std::filesystem::symlink_status(backupPath, statusError);
            if (!statusError && std::filesystem::exists(backupStatus) && !std::filesystem::is_regular_file(backupStatus))
            { error = "Refusing to replace a nonregular scene backup"; cleanup(); return false; }
            // Bounded read and the same exclusive-temp protocol protect the old
            // destination/backup, including collisions and interrupted writes.
            std::string previous;
            const auto oldSize = std::filesystem::file_size(path);
            if (oldSize > limit || (oldSize && !ReadFile(path, limit, previous, error)))
            {
                if (oldSize > limit) error = "Previous scene exceeds backup byte limit";
                cleanup(); return false;
            }
            if (!AtomicWrite(backupPath, previous, false, limit, error)) { cleanup(); return false; }
        }
        if (!ReplaceFile(temporary, path, error)) { cleanup(); return false; }
        temporary.clear();
#ifndef _WIN32
        // Persist the directory entry too where the filesystem supports it.
        const int directory = ::open(parent.c_str(), O_RDONLY);
        if (directory >= 0) { (void)::fsync(directory); ::close(directory); }
#endif
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); cleanup(); return false; }
}
} // namespace

struct Document::Impl
{
    struct HistoryEntry
    {
        SceneState Before, After;
        uint64_t BeforeToken = 0, AfterToken = 0;
        size_t Bytes = 0;
        std::string Label;
    };
    struct Transaction
    {
        SceneState Before;
        uint64_t Token = 0;
        std::string Label;
    };
    struct PlayState
    {
        SceneState Before;
        uint64_t Token = 0, Saved = 0;
        std::vector<HistoryEntry> Undo, Redo;
        size_t HistoryBytes = 0;
    };
    Limits Bounds;
    const std::thread::id Owner = std::this_thread::get_id();
    SceneState State;
    EntityId NextId = 1;
    uint64_t Revision = 1, NextToken = 2, Token = 1, SavedToken = 1;
    std::filesystem::path Path;
    std::string Error;
    std::vector<HistoryEntry> Undo, Redo;
    size_t HistoryBytes = 0;
    std::optional<Transaction> CurrentTransaction;
    std::optional<PlayState> Play;

    explicit Impl(Limits limits) : Bounds(limits) {}
    bool StartEdit()
    {
        // A misrouted worker must not even mutate the owner's error string.
        if (std::this_thread::get_id() != Owner) return false;
        Error.clear();
        return true;
    }
    bool NoSession()
    {
        if (CurrentTransaction || Play) { Error = "Finish the edit transaction or stop Play first"; return false; }
        return true;
    }
    void Advance() { ++Revision; Token = NextToken++; }
    void Prune()
    {
        while (!Undo.empty() && (Undo.size() + Redo.size() > Bounds.MaxHistoryEntries || HistoryBytes > Bounds.MaxHistoryBytes))
        {
            HistoryBytes -= Undo.front().Bytes;
            Undo.erase(Undo.begin());
        }
    }
    bool Apply(SceneState next, const std::string& label)
    {
        if (!Validate(next, Bounds, Error)) return false;
        const size_t cost = StateBytes(CurrentTransaction ? CurrentTransaction->Before : State) + StateBytes(next) + sizeof(HistoryEntry) + label.size();
        if (cost > Bounds.MaxHistoryBytes) { Error = "Edit exceeds the bounded undo history budget"; return false; }
        if (CurrentTransaction)
        {
            State = std::move(next);
            Advance();
            return true;
        }
        HistoryEntry entry;
        entry.Before = State; entry.After = next; entry.BeforeToken = Token;
        entry.AfterToken = NextToken; entry.Bytes = cost; entry.Label = label.substr(0, MaxNameBytes);
        // Allocate history before publishing the new scene.
        Undo.push_back(std::move(entry));
        for (const auto& old : Redo) HistoryBytes -= old.Bytes;
        Redo.clear();
        HistoryBytes += cost;
        State = std::move(next);
        Advance();
        Prune();
        return true;
    }
    bool EditableSubtree(EntityId root)
    {
        if (!State.Entities.count(root)) { Error = "Entity does not exist"; return false; }
        for (EntityId id : Subtree(State, root))
            if (EffectiveFlag(State, id, true)) { Error = "Locked entity/subtree cannot be edited"; return false; }
        return true;
    }
};

const char* KindName(EntityKind kind)
{
    switch (kind)
    {
    case EntityKind::Empty: return "Empty";
    case EntityKind::Cube: return "Cube";
    case EntityKind::Sphere: return "Sphere";
    case EntityKind::Model: return "Model";
    case EntityKind::PointLight: return "PointLight";
    case EntityKind::SpotLight: return "SpotLight";
    case EntityKind::DirectionalLight: return "DirectionalLight";
    case EntityKind::Camera: return "Camera";
    case EntityKind::ParticleEmitter: return "ParticleEmitter";
    case EntityKind::Terrain: return "Terrain";
    case EntityKind::Water: return "Water";
    default: return nullptr;
    }
}
Document::Document(Limits limits) : Data(std::make_unique<Impl>(limits))
{
    if (!limits.MaxEntities || limits.MaxEntities > 100000 || !limits.MaxHierarchyDepth || limits.MaxHierarchyDepth > 1024
        || limits.MaxFileBytes < 1024 || limits.MaxFileBytes > 256 * 1024 * 1024 || !limits.MaxHistoryEntries || limits.MaxHistoryEntries > 4096
        || limits.MaxHistoryBytes < 4096 || limits.MaxHistoryBytes > size_t(1024) * 1024 * 1024)
        throw std::invalid_argument("Invalid editor document limits");
}
Document::~Document() = default;
const std::map<EntityId, Entity>& Document::Entities() const { return Data->State.Entities; }
const Entity* Document::Find(EntityId id) const
{
    auto it = Data->State.Entities.find(id);
    return it == Data->State.Entities.end() ? nullptr : &it->second;
}
const std::string& Document::Name() const { return Data->State.Name; }
const std::string& Document::LastError() const { return Data->Error; }
const std::filesystem::path& Document::Path() const { return Data->Path; }
uint64_t Document::Revision() const { return Data->Revision; }
bool Document::IsDirty() const { return Data->Token != Data->SavedToken; }
bool Document::IsPlaying() const { return Data->Play.has_value(); }
bool Document::HasTransaction() const { return Data->CurrentTransaction.has_value(); }
bool Document::CanUndo() const { return !Data->CurrentTransaction && !Data->Undo.empty(); }
bool Document::CanRedo() const { return !Data->CurrentTransaction && !Data->Redo.empty(); }
std::string Document::UndoLabel() const { return CanUndo() ? Data->Undo.back().Label : std::string(); }
std::string Document::RedoLabel() const { return CanRedo() ? Data->Redo.back().Label : std::string(); }
bool Document::NewScene(const std::string& name)
{
    if (!Data->StartEdit() || !Data->NoSession()) return false;
    SceneState next; next.Name = name;
    if (!Validate(next, Data->Bounds, Data->Error)) return false;
    Data->State = std::move(next); Data->Undo.clear(); Data->Redo.clear(); Data->HistoryBytes = 0; Data->Path.clear();
    Data->Advance(); Data->SavedToken = Data->Token;
    return true;
}
bool Document::RenameScene(const std::string& name)
{
    if (!Data->StartEdit()) return false;
    if (name == Data->State.Name) return true;
    SceneState next = Data->State; next.Name = name;
    return Data->Apply(std::move(next), "Rename scene");
}
EntityId Document::Create(EntityKind kind, const std::string& name, EntityId parent)
{
    if (!Data->StartEdit()) return InvalidEntity;
    if (parent && (!Find(parent) || EffectiveFlag(Data->State, parent, true))) { Data->Error = "Parent is missing or locked"; return InvalidEntity; }
    if (!KindName(kind) || Data->NextId >= std::numeric_limits<EntityId>::max()) { Data->Error = "Unknown entity kind or exhausted IDs"; return InvalidEntity; }
    Entity entity; entity.Id = Data->NextId; entity.Parent = parent; entity.Kind = kind; entity.Name = name.empty() ? KindName(kind) : name;
    SceneState next = Data->State; next.Entities.emplace(entity.Id, entity); next.Selected = entity.Id;
    if (!Data->Apply(std::move(next), "Create entity")) return InvalidEntity;
    ++Data->NextId;
    return entity.Id;
}
bool Document::Update(const Entity& entity, const std::string& label)
{
    return UpdateMany(std::span(&entity, 1), label);
}
bool Document::UpdateMany(std::span<const Entity> entities, const std::string& label)
{
    if (!Data->StartEdit()) return false;
    bool changed = false;
    std::set<EntityId> ids;
    for (const auto& entity : entities)
    {
        if (entities.size() > 1 && !ids.insert(entity.Id).second) { Data->Error = "Duplicate entity in property edit"; return false; }
        const auto* old = Find(entity.Id);
        if (!old) { Data->Error = "Entity does not exist"; return false; }
        if (entity.Parent != old->Parent) { Data->Error = "Use Reparent to edit hierarchy"; return false; }
        if (*old == entity) continue;
        if (EffectiveFlag(Data->State, entity.Id, true))
        {
            Entity unlocked = *old; unlocked.Locked = false;
            if (!old->Locked || entity != unlocked || (entity.Parent && EffectiveFlag(Data->State, entity.Parent, true)))
            { Data->Error = "Locked entity cannot be edited"; return false; }
        }
        changed = true;
    }
    if (!changed) return true;
    SceneState next = Data->State;
    for (const auto& entity : entities) next.Entities.at(entity.Id) = entity;
    return Data->Apply(std::move(next), label);
}
bool Document::SetLocalPositions(const std::vector<std::pair<EntityId, Vec3>>& positions, const std::string& label)
{
    if (!Data->StartEdit()) return false;
    SceneState next = Data->State;
    bool changed = false;
    for (const auto& [id, position] : positions)
    {
        auto entity = next.Entities.find(id);
        if (entity == next.Entities.end() || IsEffectivelyLocked(id))
        {
            Data->Error = "Cannot move a missing or locked entity";
            return false;
        }
        changed |= entity->second.Local.Position != position;
        entity->second.Local.Position = position;
    }
    return !changed || Data->Apply(std::move(next), label);
}
bool Document::Reparent(EntityId id, EntityId parent, bool preserveWorld)
{
    if (!Data->StartEdit() || !Data->EditableSubtree(id)) return false;
    if (parent && (!Find(parent) || EffectiveFlag(Data->State, parent, true))) { Data->Error = "Parent is missing or locked"; return false; }
    if (Find(id)->Parent == parent) return true;
    for (EntityId descendant : Subtree(Data->State, id))
        if (descendant == parent) { Data->Error = "Hierarchy cycle rejected"; return false; }
    SceneState next = Data->State;
    auto& entity = next.Entities.at(id);
    if (preserveWorld)
    {
        Matrix4 oldWorld, parentWorld = Matrix4::Identity(), inverse;
        if (!WorldMatrix(Data->State, id, Data->Bounds.MaxHierarchyDepth, oldWorld)
            || (parent && !WorldMatrix(Data->State, parent, Data->Bounds.MaxHierarchyDepth, parentWorld))
            || !InverseAffine(parentWorld, inverse) || !Decompose(Multiply(inverse, oldWorld), entity.Local))
        { Data->Error = "Reparent would introduce shear or a singular transform; world transform preserved by rejecting the edit"; return false; }
    }
    entity.Parent = parent;
    return Data->Apply(std::move(next), "Reparent entity");
}
EntityId Document::Duplicate(EntityId root)
{
    if (!Data->StartEdit() || !Data->EditableSubtree(root)) return InvalidEntity;
    const auto subtree = Subtree(Data->State, root);
    if (subtree.size() > Data->Bounds.MaxEntities - Data->State.Entities.size()
        || subtree.size() >= std::numeric_limits<EntityId>::max() - Data->NextId)
    { Data->Error = "Duplicate exceeds entity/ID limit"; return InvalidEntity; }
    SceneState next = Data->State;
    std::map<EntityId, EntityId> remap;
    EntityId cursor = Data->NextId;
    for (EntityId id : subtree) remap[id] = cursor++;
    for (EntityId id : subtree)
    {
        Entity copy = Data->State.Entities.at(id); copy.Id = remap.at(id);
        if (id != root) copy.Parent = remap.at(copy.Parent);
        else if (copy.Name.size() + 5 <= MaxNameBytes) copy.Name += " Copy";
        next.Entities.emplace(copy.Id, std::move(copy));
    }
    next.Selected = remap.at(root);
    if (!Data->Apply(std::move(next), "Duplicate subtree")) return InvalidEntity;
    Data->NextId = cursor;
    return remap.at(root);
}
bool Document::Delete(EntityId root)
{
    if (!Data->StartEdit() || !Data->EditableSubtree(root)) return false;
    SceneState next = Data->State;
    for (EntityId id : Subtree(Data->State, root)) next.Entities.erase(id);
    if (!next.Entities.count(next.Selected)) next.Selected = InvalidEntity;
    return Data->Apply(std::move(next), "Delete subtree");
}
bool Document::GetWorldTransform(EntityId id, Transform& transform) const
{
    Matrix4 world;
    return Find(id) && WorldMatrix(Data->State, id, Data->Bounds.MaxHierarchyDepth, world) && Decompose(world, transform);
}
bool Document::GetWorldMatrix(EntityId id, float matrix[16], float inverse[16]) const
{
    if (!matrix || !Find(id)) return false;
    Matrix4 world;
    if (!WorldMatrix(Data->State, id, Data->Bounds.MaxHierarchyDepth, world)) return false;
    Matrix4 inverseWorld;
    if (inverse && !InverseAffine(world, inverseWorld)) return false;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
        {
            matrix[c * 4 + r] = float(world.M[r][c]);
            if (inverse) inverse[c * 4 + r] = float(inverseWorld.M[r][c]);
        }
    return true;
}
bool Document::EvaluateLocal(EntityId id, float seconds, Transform& transform) const
{
    const auto* entity = Find(id);
    if (!entity || !std::isfinite(seconds)) return false;
    transform = entity->Local;
    const auto& animation = entity->Animation;
    if (!animation.Enabled || animation.Keys.empty()) return true;
    float time = std::max(seconds, 0.0f);
    if (animation.Loop) time = std::fmod(time, animation.Duration);
    else time = std::min(time, animation.Duration);
    const auto upper = std::upper_bound(animation.Keys.begin(), animation.Keys.end(), time,
        [](float value, const AnimationKeyframe& key) { return value < key.Time; });
    if (upper == animation.Keys.begin()) { transform = upper->Local; return true; }
    if (upper == animation.Keys.end()) { transform = animation.Keys.back().Local; return true; }
    const auto& before = *(upper - 1);
    const float alpha = (time - before.Time) / (upper->Time - before.Time);
    const auto lerp = [alpha](const Vec3& a, const Vec3& b) {
        return Vec3{a.X + (b.X - a.X) * alpha, a.Y + (b.Y - a.Y) * alpha, a.Z + (b.Z - a.Z) * alpha};
    };
    const auto angle = [alpha](float a, float b) { return a + std::remainder(b - a, 360.0f) * alpha; };
    transform.Position = lerp(before.Local.Position, upper->Local.Position);
    transform.Scale = lerp(before.Local.Scale, upper->Local.Scale);
    transform.RotationDegrees = {angle(before.Local.RotationDegrees.X, upper->Local.RotationDegrees.X),
        angle(before.Local.RotationDegrees.Y, upper->Local.RotationDegrees.Y), angle(before.Local.RotationDegrees.Z, upper->Local.RotationDegrees.Z)};
    return true;
}
std::vector<EntityId> Document::Children(EntityId parent) const
{
    std::vector<EntityId> result;
    for (const auto& [id, entity] : Data->State.Entities) if (entity.Parent == parent) result.push_back(id);
    return result;
}
bool Document::IsEffectivelyVisible(EntityId id) const { return Find(id) && EffectiveFlag(Data->State, id, false); }
bool Document::IsEffectivelyLocked(EntityId id) const { return !Find(id) || EffectiveFlag(Data->State, id, true); }
EntityId Document::SelectedId() const { return Data->State.Selected; }
bool Document::Select(EntityId id)
{
    if (!Data->StartEdit()) return false;
    if (id && !Find(id)) { Data->Error = "Selection entity does not exist"; return false; }
    Data->State.Selected = id;
    return true;
}
bool Document::BeginTransaction(const std::string& label)
{
    if (!Data->StartEdit()) return false;
    if (Data->CurrentTransaction) { Data->Error = "Nested edit transactions are not supported"; return false; }
    if (!ValidText(label, MaxNameBytes)) { Data->Error = "Invalid transaction label"; return false; }
    Data->CurrentTransaction.emplace(Impl::Transaction{Data->State, Data->Token, label});
    return true;
}
bool Document::CommitTransaction()
{
    if (!Data->StartEdit()) return false;
    if (!Data->CurrentTransaction) { Data->Error = "No active edit transaction"; return false; }
    const auto& transaction = *Data->CurrentTransaction;
    if (transaction.Before.Name == Data->State.Name && transaction.Before.Entities == Data->State.Entities)
    {
        Data->Token = transaction.Token;
        Data->CurrentTransaction.reset();
        return true;
    }
    Impl::HistoryEntry entry;
    entry.Before = transaction.Before; entry.After = Data->State; entry.BeforeToken = transaction.Token;
    entry.AfterToken = Data->Token; entry.Label = transaction.Label;
    entry.Bytes = StateBytes(entry.Before) + StateBytes(entry.After) + sizeof(entry) + entry.Label.size();
    if (entry.Bytes > Data->Bounds.MaxHistoryBytes) { Data->Error = "Edit exceeds undo history budget; cancel the transaction"; return false; }
    Data->Undo.push_back(std::move(entry));
    for (const auto& old : Data->Redo) Data->HistoryBytes -= old.Bytes;
    Data->Redo.clear(); Data->HistoryBytes += Data->Undo.back().Bytes;
    Data->CurrentTransaction.reset(); Data->Prune();
    return true;
}
bool Document::CancelTransaction()
{
    if (!Data->StartEdit()) return false;
    if (!Data->CurrentTransaction) { Data->Error = "No active edit transaction"; return false; }
    Data->State = std::move(Data->CurrentTransaction->Before); Data->Token = Data->CurrentTransaction->Token;
    Data->CurrentTransaction.reset(); ++Data->Revision;
    return true;
}
bool Document::Undo()
{
    if (!Data->StartEdit()) return false;
    if (!CanUndo()) { Data->Error = "No undo available or a transaction is active"; return false; }
    SceneState next = Data->Undo.back().Before;
    Data->Redo.push_back(std::move(Data->Undo.back())); Data->Undo.pop_back();
    Data->State = std::move(next); Data->Token = Data->Redo.back().BeforeToken; ++Data->Revision;
    return true;
}
bool Document::Redo()
{
    if (!Data->StartEdit()) return false;
    if (!CanRedo()) { Data->Error = "No redo available or a transaction is active"; return false; }
    SceneState next = Data->Redo.back().After;
    Data->Undo.push_back(std::move(Data->Redo.back())); Data->Redo.pop_back();
    Data->State = std::move(next); Data->Token = Data->Undo.back().AfterToken; ++Data->Revision;
    return true;
}
std::string Document::ToJson() const
{
    try { return Serialize(Data->State); }
    catch (const std::exception& exception) { Data->Error = exception.what(); return {}; }
}
bool Document::LoadJson(const std::string& text)
{
    if (!Data->StartEdit() || !Data->NoSession()) return false;
    SceneState next;
    if (!Parse(text, Data->Bounds, next, Data->Error)) return false;
    EntityId nextId = Data->NextId;
    if (!next.Entities.empty()) nextId = std::max(nextId, next.Entities.rbegin()->first + 1);
    Data->State = std::move(next); Data->NextId = nextId;
    Data->Undo.clear(); Data->Redo.clear(); Data->HistoryBytes = 0; Data->Path.clear();
    Data->Advance(); Data->SavedToken = Data->Token;
    return true;
}
bool Document::Save(const std::filesystem::path& path, bool backup)
{
    if (!Data->StartEdit() || !Data->NoSession()) return false;
    const auto text = ToJson();
    if (text.empty() || !AtomicWrite(path, text, backup, Data->Bounds.MaxFileBytes, Data->Error)) return false;
    Data->Path = path; Data->SavedToken = Data->Token;
    return true;
}
bool Document::Load(const std::filesystem::path& path)
{
    if (!Data->StartEdit() || !Data->NoSession()) return false;
    std::string text;
    if (!ReadFile(path, Data->Bounds.MaxFileBytes, text, Data->Error) || !LoadJson(text)) return false;
    Data->Path = path;
    return true;
}
bool Document::SavePrefab(EntityId root, const std::filesystem::path& path)
{
    if (!Data->StartEdit()) return false;
    if (!Find(root)) { Data->Error = "Prefab root does not exist"; return false; }
    SceneState prefab; prefab.Name = Find(root)->Name;
    for (EntityId id : Subtree(Data->State, root)) prefab.Entities.emplace(id, *Find(id));
    prefab.Entities.at(root).Parent = InvalidEntity;
    try { return AtomicWrite(path, Serialize(prefab, "jEnginePrefab", root), true, Data->Bounds.MaxFileBytes, Data->Error); }
    catch (const std::exception& exception) { Data->Error = exception.what(); return false; }
}
bool Document::LoadJson(const std::string& text, const std::filesystem::path& sourcePath)
{
    if (!LoadJson(text)) return false;
    Data->Path = sourcePath;
    return true;
}
std::string Document::SelectionJson(const std::vector<EntityId>& selection) const
{
    if (std::this_thread::get_id() != Data->Owner) return {};
    Data->Error.clear();
    try
    {
        if (selection.empty() || selection.size() > Data->Bounds.MaxEntities)
        { Data->Error = "Copy requires a bounded nonempty selection"; return {}; }
        std::set<EntityId> selected;
        for (EntityId id : selection)
            if (!Find(id)) { Data->Error = "Copy selection contains a missing entity ID"; return {}; }
            else selected.insert(id);
        std::vector<EntityId> roots;
        std::set<EntityId> emitted;
        for (EntityId id : selection)
        {
            if (!emitted.insert(id).second) continue;
            bool covered = false;
            for (EntityId parent = Find(id)->Parent; parent; parent = Find(parent)->Parent)
                if (selected.count(parent)) { covered = true; break; }
            if (!covered) roots.push_back(id);
        }
        SceneState forest; forest.Name = "Copied selection";
        for (EntityId root : roots)
        {
            Matrix4 parentWorld = Matrix4::Identity();
            const EntityId sourceParent = Find(root)->Parent;
            if (sourceParent && !WorldMatrix(Data->State, sourceParent, Data->Bounds.MaxHierarchyDepth, parentWorld))
            { Data->Error = "Invalid copied parent transform"; return {}; }
            for (EntityId parent = sourceParent; parent; parent = Find(parent)->Parent)
                if (Find(parent)->Animation.Enabled && !Find(parent)->Animation.Keys.empty())
                { Data->Error = "Copy the animated ancestor together with its descendants to preserve animation"; return {}; }
            for (EntityId id : Subtree(Data->State, root))
            {
                if (EffectiveFlag(Data->State, id, true)) { Data->Error = "Locked selection/subtree cannot be copied"; return {}; }
                forest.Entities.emplace(id, *Find(id));
            }
            auto& copy = forest.Entities.at(root);
            if (!Decompose(Multiply(parentWorld, Compose(copy.Local)), copy.Local))
            { Data->Error = "Copied root world transform contains shear that clipboard TRS cannot represent"; return {}; }
            for (auto& key : copy.Animation.Keys)
                if (!Decompose(Multiply(parentWorld, Compose(key.Local)), key.Local))
                { Data->Error = "Copied animation key world pose contains nonrepresentable shear"; return {}; }
            copy.Parent = InvalidEntity;
        }
        if (!Validate(forest, Data->Bounds, Data->Error)) return {};
        const auto text = Serialize(forest, "jEngineSelection", InvalidEntity, &roots);
        if (text.size() > Data->Bounds.MaxFileBytes) { Data->Error = "Copied selection exceeds clipboard byte limit"; return {}; }
        return text;
    }
    catch (const std::exception& exception) { Data->Error = exception.what(); return {}; }
}
std::vector<EntityId> Document::PasteSelectionJson(const std::string& text, EntityId parent)
{
    if (!Data->StartEdit()) return {};
    if (parent && (!Find(parent) || IsEffectivelyLocked(parent))) { Data->Error = "Paste parent is missing or locked"; return {}; }
    SceneState forest;
    std::vector<EntityId> sourceRoots;
    if (!Parse(text, Data->Bounds, forest, Data->Error, false, nullptr, &sourceRoots)) return {};
    if (forest.Entities.size() > Data->Bounds.MaxEntities - Data->State.Entities.size()
        || forest.Entities.size() >= std::numeric_limits<EntityId>::max() - Data->NextId)
    { Data->Error = "Pasted selection exceeds scene entity/ID limit"; return {}; }
    Matrix4 parentWorld = Matrix4::Identity(), inverse;
    if ((parent && !WorldMatrix(Data->State, parent, Data->Bounds.MaxHierarchyDepth, parentWorld)) || !InverseAffine(parentWorld, inverse))
    { Data->Error = "Paste parent has an invalid world transform"; return {}; }
    for (EntityId ancestor = parent; ancestor; ancestor = Find(ancestor)->Parent)
        if (Find(ancestor)->Animation.Enabled && !Find(ancestor)->Animation.Keys.empty())
        { Data->Error = "Paste under an unanimated parent to preserve copied world key poses"; return {}; }
    for (EntityId root : sourceRoots)
    {
        auto& entity = forest.Entities.at(root);
        if (!Decompose(Multiply(inverse, Compose(entity.Local)), entity.Local))
        { Data->Error = "Paste would introduce nonrepresentable root shear"; return {}; }
        for (auto& key : entity.Animation.Keys)
            if (!Decompose(Multiply(inverse, Compose(key.Local)), key.Local))
            { Data->Error = "Paste would introduce nonrepresentable animation key shear"; return {}; }
    }
    SceneState next = Data->State;
    std::map<EntityId, EntityId> remap;
    EntityId cursor = Data->NextId;
    for (const auto& [id, entity] : forest.Entities) { (void)entity; remap[id] = cursor++; }
    for (const auto& [id, entity] : forest.Entities)
    {
        Entity copy = entity; copy.Id = remap.at(id);
        copy.Parent = entity.Parent ? remap.at(entity.Parent) : parent;
        next.Entities.emplace(copy.Id, std::move(copy));
    }
    std::vector<EntityId> pasted;
    pasted.reserve(sourceRoots.size());
    for (EntityId id : sourceRoots) pasted.push_back(remap.at(id));
    next.Selected = pasted.back();
    if (!Data->Apply(std::move(next), "Paste selection")) return {};
    Data->NextId = cursor;
    return pasted;
}
std::string Document::PrefabJson(EntityId root) const
{
    if (std::this_thread::get_id() != Data->Owner || !Find(root)) return {};
    SceneState prefab; prefab.Name = Find(root)->Name;
    for (EntityId id : Subtree(Data->State, root)) prefab.Entities.emplace(id, *Find(id));
    prefab.Entities.at(root).Parent = InvalidEntity;
    return Serialize(prefab, "jEnginePrefab", root);
}
EntityId Document::InstantiatePrefab(const std::filesystem::path& path, EntityId parent)
{
    if (!Data->StartEdit()) return InvalidEntity;
    if (parent && (!Find(parent) || IsEffectivelyLocked(parent))) { Data->Error = "Prefab parent is missing or locked"; return InvalidEntity; }
    std::string text;
    if (!ReadFile(path, Data->Bounds.MaxFileBytes, text, Data->Error)) return InvalidEntity;
    return InstantiatePrefabJson(text, parent);
}
EntityId Document::InstantiatePrefabJson(const std::string& text, EntityId parent, std::optional<Vec3> rootPosition)
{
    if (!Data->StartEdit()) return InvalidEntity;
    if (parent && (!Find(parent) || IsEffectivelyLocked(parent))) { Data->Error = "Prefab parent is missing or locked"; return InvalidEntity; }
    SceneState prefab;
    EntityId root = InvalidEntity;
    if (!Parse(text, Data->Bounds, prefab, Data->Error, true, &root)) return InvalidEntity;
    if (prefab.Entities.size() > Data->Bounds.MaxEntities - Data->State.Entities.size()
        || prefab.Entities.size() >= std::numeric_limits<EntityId>::max() - Data->NextId)
    { Data->Error = "Prefab exceeds scene entity/ID limit"; return InvalidEntity; }
    SceneState next = Data->State;
    std::map<EntityId, EntityId> remap;
    EntityId cursor = Data->NextId;
    for (const auto& [id, entity] : prefab.Entities) { (void)entity; remap[id] = cursor++; }
    for (const auto& [id, entity] : prefab.Entities)
    {
        Entity copy = entity; copy.Id = remap.at(id); copy.Parent = id == root ? parent : remap.at(entity.Parent);
        if (id == root && rootPosition) copy.Local.Position = *rootPosition;
        next.Entities.emplace(copy.Id, std::move(copy));
    }
    next.Selected = remap.at(root);
    if (!Data->Apply(std::move(next), "Instantiate prefab")) return InvalidEntity;
    Data->NextId = cursor;
    return remap.at(root);
}
bool Document::BeginPlay()
{
    if (!Data->StartEdit() || !Data->NoSession()) return false;
    Impl::PlayState play;
    play.Before = Data->State; play.Token = Data->Token; play.Saved = Data->SavedToken;
    play.Undo = std::move(Data->Undo); play.Redo = std::move(Data->Redo); play.HistoryBytes = Data->HistoryBytes;
    Data->HistoryBytes = 0; Data->Play.emplace(std::move(play));
    ++Data->Revision;
    return true;
}
bool Document::StopPlay()
{
    if (!Data->StartEdit()) return false;
    if (!Data->Play) { Data->Error = "Play is not active"; return false; }
    auto& play = *Data->Play;
    Data->CurrentTransaction.reset(); Data->State = std::move(play.Before); Data->Token = play.Token; Data->SavedToken = play.Saved;
    Data->Undo = std::move(play.Undo); Data->Redo = std::move(play.Redo); Data->HistoryBytes = play.HistoryBytes;
    Data->Play.reset(); ++Data->Revision;
    return true;
}
} // namespace jEditorAuthoring
