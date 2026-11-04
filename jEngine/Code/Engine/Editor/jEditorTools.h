#pragma once
#include "jEditorDocument.h"
#include <string>
#include <vector>
#include <memory>
class jEditor;
class jCamera;
class jEditorProject;
class jEditorAssetRegistry;
struct ImVec2;
struct Vector;
struct jEditorThumbnail;
namespace jEditorTools
{
struct Mesh
{
    std::vector<jEditorAuthoring::Vec3> Positions, Normals;
    std::vector<uint32_t> Indices;
};
Mesh BuildTerrain(const jEditorAuthoring::TerrainProperties& terrain);
enum class NodeKind { Color, Scalar, Texture, Multiply, Add };
struct MaterialNode
{
    uint32_t Id = 0, InputA = 0, InputB = 0;
    NodeKind Kind = NodeKind::Color;
    jEditorAuthoring::Vec3 Color{1, 1, 1};
    float Scalar = 1, X = 0, Y = 0;
    std::string Texture;
};
struct MaterialGraph
{
    std::vector<MaterialNode> Nodes;
    uint32_t BaseColor = 0, Metallic = 0, Roughness = 0, Normal = 0;
};
bool EvaluateMaterial(const MaterialGraph& graph, jEditorAuthoring::MaterialProperties& material, std::string& error);
bool SaveMaterial(const MaterialGraph& graph, const std::filesystem::path& file, std::string& error);
bool LoadMaterial(const std::filesystem::path& file, MaterialGraph& graph, std::string& error);
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
bool FindTerrainHit(const Mesh& mesh, const Vector& origin, const Vector& direction, Vector& hit);
bool Render(jEditor& editor, const jEditorProject* project, const jEditorAssetRegistry* registry);
bool RenderTerrainBrush(jEditor& editor, const jCamera& camera, const ImVec2& position, const ImVec2& size, bool hovered);
void FinishTerrainStroke(jEditor& editor);
void Tick(jEditor& editor, float deltaTime);
void Shutdown();
}
