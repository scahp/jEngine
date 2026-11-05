#pragma once
#include "jEditorDocument.h"
#include "Material/jMaterial.h"
#include <string>
#include <vector>
#include <memory>
class jEditor;
class jCamera;
class jEditorProject;
class jEditorAssetRegistry;
class jObject;
struct ImVec2;
struct Vector;
struct jEditorThumbnail;
struct jImageData;
namespace jEditorTools
{
struct Mesh
{
    std::vector<jEditorAuthoring::Vec3> Positions, Normals;
    std::vector<uint32_t> Indices;
};
Mesh BuildTerrain(const jEditorAuthoring::TerrainProperties& terrain);
enum class NodeKind
{
    Color, Scalar, Texture, Multiply, Add,
    UV, TextureObject, TextureSample, TextureSampleNormal, Channel,
    Parallax, POM, Relief, Interior, Horizon, DiffusionProfile
};
struct MaterialNode
{
    uint32_t Id = 0, InputA = 0, InputB = 0;
    NodeKind Kind = NodeKind::Color;
    jEditorAuthoring::Vec3 Color{1, 1, 1};
    float Scalar = 1, X = 0, Y = 0;
    std::string Texture;
    uint32_t InputC = 0, InputD = 0;
    std::array<float, 4> Parameters{1, 1, 0, 0};
    jMaterialMapping::TextureSemantic Semantic = jMaterialMapping::TextureSemantic::Linear;
    jMaterialMapping::NormalSpace NormalSpace = jMaterialMapping::NormalSpace::Tangent;
    std::array<float, 3> InputBasisSignXYZ{1.f, 1.f, 1.f};
    // Absent: legacy Parameters.x flip for Tangent, no flip for Object/World.
    std::optional<bool> FlipNormalY;
};
struct MaterialGraph
{
    std::vector<MaterialNode> Nodes;
    uint32_t BaseColor = 0, Metallic = 0, Roughness = 0, Normal = 0;
    struct SurfaceProperties
    {
        jSurfaceShading Shading;
        jEditorAuthoring::Vec3 Emission{};
        float Opacity = 1.f;
        std::string MetallicRoughnessTexture, EmissionTexture;
    };
    std::optional<SurfaceProperties> Surface;
    uint32_t Horizon = 0;
    uint32_t Version = 1;
    uint32_t SubsurfaceWeight = 0, SubsurfaceColor = 0, SubsurfaceThickness = 0, SubsurfaceProfile = 0;
    uint32_t SpecularWeight = 0, DiffuseOcclusion = 0, PreScatterWeight = 0;
};
bool EvaluateMaterial(const MaterialGraph& graph, jEditorAuthoring::MaterialProperties& material, std::string& error);
bool SaveMaterial(const MaterialGraph& graph, const std::filesystem::path& file, std::string& error);
bool LoadMaterial(const std::filesystem::path& file, MaterialGraph& graph, std::string& error);
struct PreparedMaterialMapping
{
    std::shared_ptr<const jMaterialMapping::Program> Program;
    std::array<std::shared_ptr<jTexture>, jMaterialMapping::MaximumTextures> Textures{};
};
// CPU compilation plus owner-thread managed texture preparation. Failure preserves output.
bool PrepareMaterialMapping(const std::filesystem::path& root, const MaterialGraph& graph,
    PreparedMaterialMapping& output, std::string& error, bool* pending = nullptr);
// Polling is owner-thread only; the monotonic completion epoch supports retry
// even when another caller already harvested a completed worker.
uint64_t PollMaterialMappingTexturePreparation();
size_t PendingMaterialMappingTextureCount();
void InvalidateMaterialMappingTextureCache(const std::filesystem::path& root, const std::string& source);
void CancelMaterialMappingTexturePreparation();
bool DecodeMaterialDataTexture(const std::filesystem::path& file, std::shared_ptr<jImageData>& output, std::string& error);
void ApplyMaterialMapping(jMaterial& material, const PreparedMaterialMapping& prepared, uint32_t overrideMask);
uint32_t MaterialMappingOverrides(const jEditorAuthoring::MaterialProperties& material);
bool ApplySceneMaterialLibrary(const std::filesystem::path& source, const std::vector<jObject*>& objects,
    size_t& appliedSlots, std::string& error, bool* pending = nullptr, bool allowRasterOnly = true);
bool PrepareMaterialThumbnail(const std::filesystem::path& root, const std::filesystem::path& file,
    jEditorThumbnail& thumbnail, std::string& diagnostic);
void InheritMaterialGraph(jEditorAuthoring::MaterialProperties& instance, const jEditorAuthoring::MaterialProperties& defaults);
bool AssignSharedMaterial(jEditor& editor, jEditorAuthoring::EntityId id, const std::string& path, std::string& error);
struct NavigationBake
{
    Mesh DebugMesh;
    std::vector<jEditorAuthoring::Vec3> Path;
    uint32_t PolygonCount = 0;
};
bool BakeNavigation(const Mesh& mesh, const jEditorAuthoring::NavigationProperties& settings,
    const jEditorAuthoring::Vec3& start, const jEditorAuthoring::Vec3& end, NavigationBake& bake, std::string& error);
bool PaintTerrain(jEditorAuthoring::TerrainProperties& terrain, float x, float z,
    float radius, float strength, int mode); // raise, lower, flatten, smooth
// Mesh must come from BuildTerrain. Origin, direction and hit are terrain-local.
bool FindTerrainHit(const Mesh& mesh, const Vector& origin, const Vector& direction, Vector& hit, Vector* normal = nullptr);
bool Render(jEditor& editor, const jEditorProject* project, const jEditorAssetRegistry* registry);
bool RenderTerrainBrush(jEditor& editor, const jCamera& camera, const ImVec2& position, const ImVec2& size, bool hovered);
void FinishTerrainStroke(jEditor& editor);
void Tick(jEditor& editor, float deltaTime);
void Shutdown();
}
