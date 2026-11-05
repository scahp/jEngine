#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "Renderer/Features/jWaterSimulation.h"
#include "Renderer/Features/jAdvancedShadowSettings.h"
#include "Material/jMaterialMapping.h"

// Authoring data has no runtime pointers and is edited only by its creating thread.
// Angles are degrees; transforms use T * Rz * Ry * Rx * S, column vectors.
namespace jEditorAuthoring
{
using EntityId = uint64_t;
constexpr EntityId InvalidEntity = 0;

enum class EntityKind { Empty, Cube, Sphere, Model, PointLight, SpotLight, DirectionalLight, Camera, ParticleEmitter, Terrain, Water };
struct Vec3
{
    float X = 0, Y = 0, Z = 0;
    bool operator==(const Vec3&) const = default;
};
struct Transform
{
    Vec3 Position{}, RotationDegrees{}, Scale{1, 1, 1};
    bool operator==(const Transform&) const = default;
};
struct MaterialGraphOverrides
{
    bool BaseColor = false, BaseColorTexture = false, Metallic = false, Roughness = false, NormalTexture = false;
    bool SubsurfaceWeight = false, SubsurfaceColor = false, SubsurfaceThickness = false, SubsurfaceProfile = false;
    bool ShadingModel = false, SubsurfaceSpace = false;
    bool SpecularWeight = false, DiffuseOcclusion = false, PreScatterWeight = false;
    bool operator==(const MaterialGraphOverrides&) const = default;
};
struct MaterialProperties
{
    bool OverrideEnabled = false;
    Vec3 BaseColor{1, 1, 1}, Emissive{};
    float Metallic = 0, Roughness = 0.5f, Opacity = 1;
    std::string BaseColorTexture, NormalTexture, GraphAsset;
    std::optional<MaterialGraphOverrides> GraphOverrides;
    // Presence of the serialized subsurface block distinguishes an explicit
    // zero weight from an older instance inheriting its imported surface.
    bool SubsurfaceOverrideEnabled = false;
    float SubsurfaceWeight = 0.f;
    Vec3 SubsurfaceColor{1.f, 1.f, 1.f};
    float SubsurfaceThicknessWorld = 1.f;
    uint32_t SubsurfaceProfile = 0;
    // Omitted legacy data inherits the imported/shared material skin settings.
    bool SkinOverrideEnabled = false;
    jMaterialMapping::ShadingModel ShadingModel = jMaterialMapping::ShadingModel::StandardPBR;
    jMaterialMapping::SubsurfaceSpace SubsurfaceSpace = jMaterialMapping::SubsurfaceSpace::Screen;
    float SpecularWeight = 1.f, DiffuseOcclusion = 1.f, PreScatterWeight = 1.f;
    bool operator==(const MaterialProperties&) const = default;
};
struct LightProperties
{
    Vec3 Color{1, 1, 1};
    float Intensity = 1, Range = 1000, InnerConeDegrees = 20, OuterConeDegrees = 35;
    bool CastShadows = true;
    // Absent inherits project/global defaults; present replaces the whole set.
    std::optional<jAdvancedShadows::Settings> ShadowOverride;
    bool operator==(const LightProperties&) const = default;
};
struct CameraProperties
{
    float VerticalFovDegrees = 60, NearPlane = 0.1f, FarPlane = 10000;
    bool Primary = false;
    bool operator==(const CameraProperties&) const = default;
};
struct AnimationKeyframe
{
    float Time = 0;
    Transform Local{};
    bool operator==(const AnimationKeyframe&) const = default;
};
struct AnimationProperties
{
    float Duration = 1;
    bool Enabled = false, Loop = true;
    std::vector<AnimationKeyframe> Keys;
    bool operator==(const AnimationProperties&) const = default;
};
struct ParticleProperties
{
    bool Enabled = true;
    uint32_t MaxParticles = 1024, Seed = 1;
    float SpawnRate = 30, Lifetime = 2, Speed = 100, Size = 5;
    Vec3 Gravity{0, -98, 0}, Color{1, 1, 1};
    bool operator==(const ParticleProperties&) const = default;
};
struct TerrainProperties
{
    uint32_t Resolution = 17;
    float SizeX = 1000, SizeZ = 1000;
    // Empty is a flat grid. Otherwise exactly Resolution * Resolution samples.
    std::vector<float> Heights;
    bool operator==(const TerrainProperties&) const = default;
};
struct WaterProperties
{
    uint32_t Resolution = 33;
    float SizeX = 1000, SizeZ = 1000;
    // A calm four-wave pond preset, in the same world units as this surface.
    // Runtime shaders currently support at most four waves; CPU queries use
    // exactly this data and an explicit caller-owned time in seconds.
    jWaterSimulation::Settings Waves{0.f, {
        {{1.f, .25f}, 8.f, 400.f, 70.f, .25f, 0.f},
        {{-.3f, 1.f}, 5.f, 230.f, 45.f, .25f, 1.2f},
        {{.6f, -.8f}, 3.f, 140.f, 30.f, .2f, 2.4f},
        {{-1.f, -.5f}, 2.f, 90.f, 20.f, .15f, .6f}}};
    bool operator==(const WaterProperties&) const = default;
};
struct NavigationProperties
{
    bool Enabled = false;
    float CellSize = 50, AgentRadius = 25, AgentHeight = 180, MaxSlopeDegrees = 45;
    bool operator==(const NavigationProperties&) const = default;
};
struct Entity
{
    EntityId Id = InvalidEntity, Parent = InvalidEntity;
    EntityKind Kind = EntityKind::Empty;
    std::string Name = "Entity";
    bool Visible = true, Locked = false;
    // Renderable objects only: absent inherits the source/prototype caster flag.
    // This is independent of Light.CastShadows.
    std::optional<bool> ShadowCaster;
    Transform Local{};
    // Project-relative, forward-slash paths. Empty means a procedural/no asset entity.
    std::string SourceAsset, AssetId;
    // Mesh-local TRS overrides survive model save/reload; empty uses source defaults.
    std::vector<Transform> MeshLocalTransforms;
    MaterialProperties Material{};
    LightProperties Light{};
    CameraProperties Camera{};
    AnimationProperties Animation{};
    ParticleProperties Particles{};
    TerrainProperties Terrain{};
    WaterProperties Water{};
    NavigationProperties Navigation{};
    bool operator==(const Entity&) const = default;
};
struct Limits
{
    size_t MaxEntities = 10000;
    size_t MaxFileBytes = 16 * 1024 * 1024;
    size_t MaxHistoryBytes = 32 * 1024 * 1024;
    size_t MaxHistoryEntries = 128;
    size_t MaxHierarchyDepth = 256;
};

class Document
{
public:
    explicit Document(Limits limits = {});
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const std::map<EntityId, Entity>& Entities() const;
    // Returned entity pointers/references expire on the next content mutation. Store IDs.
    const Entity* Find(EntityId id) const;
    const std::string& Name() const;
    const std::string& LastError() const;
    const std::filesystem::path& Path() const;
    // Monotonically increases on content changes, including Undo/Redo/StopPlay.
    uint64_t Revision() const;
    bool IsDirty() const;
    bool IsPlaying() const;
    bool HasTransaction() const;
    bool CanUndo() const;
    bool CanRedo() const;
    std::string UndoLabel() const;
    std::string RedoLabel() const;

    bool NewScene(const std::string& name = "Untitled");
    bool RenameScene(const std::string& name);
    EntityId Create(EntityKind kind, const std::string& name = {}, EntityId parent = InvalidEntity);
    bool Update(const Entity& entity, const std::string& label = "Edit properties");
    bool UpdateMany(std::span<const Entity> entities, const std::string& label);
    bool SetLocalPositions(const std::vector<std::pair<EntityId, Vec3>>& positions, const std::string& label);
    // A locked entity/subtree cannot be edited, moved, duplicated or deleted.
    bool Reparent(EntityId id, EntityId parent, bool preserveWorld = true);
    EntityId Duplicate(EntityId root);
    bool Delete(EntityId root);
    bool GetWorldTransform(EntityId id, Transform& transform) const;
    // Samples typed linear keyframes without mutating dirty/history/revision state.
    bool EvaluateLocal(EntityId id, float seconds, Transform& transform) const;
    // Column-major 4x4 affine matrix; preserves shear when ancestors introduce it.
    bool GetWorldMatrix(EntityId id, float matrix[16], float inverse[16] = nullptr) const;
    std::vector<EntityId> Children(EntityId parent = InvalidEntity) const;
    bool IsEffectivelyVisible(EntityId id) const;
    bool IsEffectivelyLocked(EntityId id) const;

    EntityId SelectedId() const;
    bool Select(EntityId id);
    bool BeginTransaction(const std::string& label);
    bool CommitTransaction();
    bool CancelTransaction();
    bool Undo();
    bool Redo();

    // Strict version 1 JSON. Failed loads leave scene/history/path/dirty state intact.
    std::string ToJson() const;
    bool LoadJson(const std::string& text);
    bool LoadJson(const std::string& text, const std::filesystem::path& sourcePath);
    // Clipboard forest: deduplicates selected descendants, preserves copied root
    // world TRS/key poses, remaps every ID, and pastes atomically as one command.
    // Nonrepresentable shear or omitted animated ancestors fail explicitly.
    std::string SelectionJson(const std::vector<EntityId>& selection) const;
    std::vector<EntityId> PasteSelectionJson(const std::string& text, EntityId parent = InvalidEntity);
    std::string PrefabJson(EntityId root) const;
    EntityId InstantiatePrefabJson(const std::string& text, EntityId parent = InvalidEntity,
        std::optional<Vec3> rootPosition = std::nullopt);
    bool Save(const std::filesystem::path& path, bool backup = true);
    bool Load(const std::filesystem::path& path);
    bool SavePrefab(EntityId root, const std::filesystem::path& path);
    EntityId InstantiatePrefab(const std::filesystem::path& path, EntityId parent = InvalidEntity);
    // Play edits are discarded and the original history/selection/dirty state restored.
    bool BeginPlay();
    bool StopPlay();

private:
    struct Impl;
    std::unique_ptr<Impl> Data;
};
const char* KindName(EntityKind kind);
} // namespace jEditorAuthoring
