#include "pch.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include <filesystem>
#include <limits>

int RunEditorToolsTests()
{
    using namespace jEditorTools;
    using namespace jEditorAuthoring;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) { std::cout << (condition ? "PASS " : "FAIL ") << "editor tools: " << name << '\n'; failures += !condition; };
    MaterialGraph graph;
    graph.Nodes = {{1, 0, 0, NodeKind::Color, {.2f, .4f, .6f}}, {2, 0, 0, NodeKind::Scalar, {}, .5f}, {3, 1, 2, NodeKind::Multiply}};
    graph.BaseColor = 3; graph.Metallic = 2; graph.Roughness = 2;
    MaterialProperties material; std::string error;
    expect(EvaluateMaterial(graph, material, error) && std::abs(material.BaseColor.X - .1f) < .0001f && material.Metallic == .5f && material.OverrideEnabled, "PBR graph compiles connected outputs");
    const auto previous = material; auto cycle = graph; cycle.Nodes[2].InputA = 3;
    expect(!EvaluateMaterial(cycle, material, error) && material == previous, "cyclic graph rejects without changing compiled material");
    auto missing = graph; missing.Nodes[2].InputA = 100;
    expect(!EvaluateMaterial(missing, material, error), "missing graph reference rejects");
    auto texture = graph; texture.Nodes[0].Kind = NodeKind::Texture; texture.Nodes[0].Texture = "Assets/Textures/color.png";
    expect(EvaluateMaterial(texture, material, error) && material.BaseColorTexture == "Assets/Textures/color.png", "texture multiplied by color compiles a real texture slot");
    texture.Nodes[0].Texture = "../private.png";
    expect(!EvaluateMaterial(texture, material, error), "material path traversal rejects");
    const auto fixture = std::filesystem::temp_directory_path() / ("jengine-tools-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixture);
    const auto file = fixture / "test.jmaterial"; MaterialGraph loaded;
    expect(SaveMaterial(graph, file, error) && LoadMaterial(file, loaded, error) && EvaluateMaterial(loaded, material, error) && material.BaseColor == previous.BaseColor, "node graph saves and reloads connected values");
    TerrainProperties terrain; terrain.Resolution = 17;
    auto flat = BuildTerrain(terrain);
    expect(flat.Positions.size() == 289 && flat.Indices.size() == 16 * 16 * 6, "terrain mesh has complete grid topology");
    expect(PaintTerrain(terrain, 0, 0, 100, 25, 0) && terrain.Heights[8 * 17 + 8] == 25, "raise brush modifies center height");
    const auto raised = BuildTerrain(terrain);
    Vector hit;
    expect(FindTerrainHit(raised, Vector(0, 50, -25), Vector(0, -1, 1), hit) &&
        hit.IsNearlyEqual(Vector(0, 25, 0), .001f), "viewport brush ray hits the sculpted mesh instead of the flat grid");
    expect(!FindTerrainHit(raised, Vector(501, 50, 0), Vector(0, -1, 0), hit) &&
        !FindTerrainHit(raised, Vector(0, 50, 0), Vector(0, 1, 0), hit), "viewport brush ray misses outside or behind the terrain");
    expect(PaintTerrain(terrain, 0, 0, 100, 10, 1) && terrain.Heights[8 * 17 + 8] == 15, "lower brush modifies actual heightfield");
    expect(PaintTerrain(terrain, 0, 0, 100, 5, 2) && terrain.Heights[8 * 17 + 8] == 5, "flatten brush reaches requested height");
    const auto unchanged = terrain.Heights;
    expect(!PaintTerrain(terrain, 0, 0, -1, 1, 0) && terrain.Heights == unchanged, "invalid brush leaves terrain unchanged");
    NavigationProperties navigation; navigation.CellSize = 25; navigation.AgentRadius = 10; navigation.AgentHeight = 30;
    NavigationBake bake;
    expect(BakeNavigation(flat, navigation, {-200, 0, -200}, {200, 0, 200}, bake, error) && bake.PolygonCount > 0 && bake.Path.size() >= 2, "Recast bakes walkable polygons and Detour finds reachable path");
    const auto oldCount = bake.PolygonCount; auto invalid = flat; invalid.Indices[0] = UINT32_MAX;
    expect(!BakeNavigation(invalid, navigation, {}, {}, bake, error) && bake.PolygonCount == oldCount, "invalid navigation input retains previous bake");
    std::error_code cleanup; std::filesystem::remove_all(fixture, cleanup);
    return failures;
}
