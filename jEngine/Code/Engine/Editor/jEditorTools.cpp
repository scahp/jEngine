#include "pch.h"
#include "jEditorTools.h"
#include "jEditorProcedural.h"
#include "Code/Engine/jEditor.h"
#include "Core/jLog.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jEditorAssetPath.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include <fstream>
#include <set>

namespace jEditorTools
{
using namespace jEditorAuthoring;
using Json = nlohmann::json;
namespace
{
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
struct Value { Vec3 Color{1, 1, 1}; std::string Texture; };
}
bool EvaluateMaterial(const MaterialGraph& graph, MaterialProperties& material, std::string& error)
{
    error.clear();
    if (graph.Nodes.empty() || graph.Nodes.size() > 256) { error = "A material needs 1..256 nodes"; return false; }
    std::map<uint32_t, const MaterialNode*> nodes;
    for (const auto& node : graph.Nodes)
    {
        if (!node.Id || !nodes.emplace(node.Id, &node).second || !Finite(node.Scalar) || !Finite(node.Color.X) || !Finite(node.Color.Y) || !Finite(node.Color.Z) || !Finite(node.X) || !Finite(node.Y) || !SafeTexture(node.Texture) || int(node.Kind) < 0 || int(node.Kind) > int(NodeKind::Add))
        { error = "Invalid material node ID, number, kind or texture path"; return false; }
    }
    std::map<uint32_t, Value> cache; std::set<uint32_t> active;
    std::function<bool(uint32_t, Value&)> evaluate = [&](uint32_t id, Value& out)
    {
        if (const auto it = cache.find(id); it != cache.end()) { out = it->second; return true; }
        const auto it = nodes.find(id);
        if (it == nodes.end()) { error = "Material input/output references a missing node"; return false; }
        if (!active.insert(id).second) { error = "Material graph cycle rejected"; return false; }
        const auto& node = *it->second; Value a, b;
        switch (node.Kind)
        {
        case NodeKind::Color: out.Color = node.Color; break;
        case NodeKind::Scalar: out.Color = {node.Scalar, node.Scalar, node.Scalar}; break;
        case NodeKind::Texture: if (node.Texture.empty()) { error = "Texture node requires a project-relative texture"; return false; } out.Texture = node.Texture; break;
        case NodeKind::Multiply:
        case NodeKind::Add:
            if (!evaluate(node.InputA, a) || !evaluate(node.InputB, b)) return false;
            if ((!a.Texture.empty() && !b.Texture.empty()) || (node.Kind == NodeKind::Add && (!a.Texture.empty() || !b.Texture.empty())))
            { error = "This PBR graph supports one texture multiplied by a constant color; texture mixing needs a shader node extension"; return false; }
            out.Texture = a.Texture.empty() ? b.Texture : a.Texture;
            if (node.Kind == NodeKind::Multiply) out.Color = {a.Color.X * b.Color.X, a.Color.Y * b.Color.Y, a.Color.Z * b.Color.Z};
            else out.Color = {a.Color.X + b.Color.X, a.Color.Y + b.Color.Y, a.Color.Z + b.Color.Z};
            break;
        }
        active.erase(id); cache[id] = out;
        if (!Finite(out.Color.X) || !Finite(out.Color.Y) || !Finite(out.Color.Z)) { error = "Material graph overflow"; return false; }
        return true;
    };
    // Validate disconnected nodes too: unused cyclic subgraphs must not persist.
    for (const auto& [id, ignored] : nodes) { (void)ignored; Value value; if (!evaluate(id, value)) return false; }
    auto result = material; Value color, metallic, roughness;
    if (!evaluate(graph.BaseColor, color)) return false;
    result.BaseColor = {std::clamp(color.Color.X, 0.0f, 1.0f), std::clamp(color.Color.Y, 0.0f, 1.0f), std::clamp(color.Color.Z, 0.0f, 1.0f)};
    result.BaseColorTexture = color.Texture;
    if (graph.Metallic) { if (!evaluate(graph.Metallic, metallic) || !metallic.Texture.empty()) { error = "Metallic output must be scalar"; return false; } result.Metallic = std::clamp(metallic.Color.X, 0.0f, 1.0f); }
    if (graph.Roughness) { if (!evaluate(graph.Roughness, roughness) || !roughness.Texture.empty()) { error = "Roughness output must be scalar"; return false; } result.Roughness = std::clamp(roughness.Color.X, 0.0f, 1.0f); }
    result.OverrideEnabled = true; material = std::move(result); return true;
}
bool SaveMaterial(const MaterialGraph& graph, const std::filesystem::path& file, std::string& error)
{
    MaterialProperties checked; if (!EvaluateMaterial(graph, checked, error)) return false;
    Json nodes = Json::array();
    for (const auto& n : graph.Nodes) nodes.push_back({{"id", n.Id}, {"kind", int(n.Kind)}, {"a", n.InputA}, {"b", n.InputB}, {"color", {n.Color.X, n.Color.Y, n.Color.Z}}, {"value", n.Scalar}, {"x", n.X}, {"y", n.Y}, {"texture", n.Texture}});
    return Write(file, Json{{"format", "jEngineMaterial"}, {"version", 1}, {"nodes", nodes}, {"baseColor", graph.BaseColor}, {"metallic", graph.Metallic}, {"roughness", graph.Roughness}}.dump(2), error);
}
bool LoadMaterial(const std::filesystem::path& file, MaterialGraph& graph, std::string& error)
{
    try
    {
        if (std::filesystem::file_size(file) > 1024 * 1024) throw std::runtime_error("Material graph exceeds 1 MiB");
        std::ifstream in(file); Json json; in >> json;
        if (json.at("format") != "jEngineMaterial" || json.at("version") != 1 || !json.at("nodes").is_array() || json.at("nodes").size() > 256) throw std::runtime_error("Unsupported material graph");
        MaterialGraph result; result.BaseColor = json.at("baseColor").get<uint32_t>(); result.Metallic = json.at("metallic").get<uint32_t>(); result.Roughness = json.at("roughness").get<uint32_t>();
        for (const auto& j : json.at("nodes"))
        {
            MaterialNode node; node.Id = j.at("id").get<uint32_t>(); node.Kind = NodeKind(j.at("kind").get<int>());
            node.InputA = j.at("a").get<uint32_t>(); node.InputB = j.at("b").get<uint32_t>();
            node.Color = {j.at("color").at(0).get<float>(), j.at("color").at(1).get<float>(), j.at("color").at(2).get<float>()};
            node.Scalar = j.at("value").get<float>(); node.X = j.at("x").get<float>(); node.Y = j.at("y").get<float>(); node.Texture = j.at("texture").get<std::string>(); result.Nodes.push_back(node);
        }
        MaterialProperties checked; if (!EvaluateMaterial(result, checked, error)) return false;
        graph = std::move(result); return true;
    }
    catch (const std::exception& e) { error = e.what(); return false; }
}
Mesh BuildTerrain(const TerrainProperties& terrain)
{
    jEditorProcedural::Mesh source; Mesh result; std::string error;
    if (!jEditorProcedural::BuildTerrainMesh(terrain, source, error)) return result;
    for (const auto& v : source.Vertices) { result.Positions.push_back(v.Position); result.Normals.push_back(v.Normal); }
    result.Indices = std::move(source.Indices); return result;
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
struct ToolState
{
    EntityId Selected = 0; MaterialGraph Graph; uint32_t SelectedNode = 0, NextNode = 4;
    std::vector<MaterialGraph> Undo, Redo;
    std::string Error; char GraphPath[512] = "Assets/Materials/Material.jmaterial";
    float Time = 0, BrushX = 0, BrushZ = 0, BrushRadius = 100, BrushStrength = 20;
    int BrushMode = 0; bool Preview = false;
    bool PreviewStarted = false;
    Vec3 NavFrom{-200, 0, -200}, NavTo{200, 0, 200}; NavigationBake Nav;
};
ToolState& Tools() { static ToolState state; return state; }
void GraphHistory()
{
    auto& s = Tools(); s.Undo.push_back(s.Graph); if (s.Undo.size() > 64) s.Undo.erase(s.Undo.begin()); s.Redo.clear();
}
void OutputCombo(const char* label, uint32_t& output)
{
    auto& s = Tools(); const auto text = output ? std::to_string(output) : std::string("None");
    if (ImGui::BeginCombo(label, text.c_str()))
    {
        if (ImGui::Selectable("None", output == 0)) { GraphHistory(); output = 0; }
        for (const auto& node : s.Graph.Nodes) if (ImGui::Selectable(std::to_string(node.Id).c_str(), output == node.Id)) { GraphHistory(); output = node.Id; }
        ImGui::EndCombo();
    }
}
void GraphUI(jEditor& editor, Entity e)
{
    auto& s = Tools();
    ImGui::TextWrapped("Drag node headers to arrange. Drag an output pin onto input A/B to connect. Save and Apply stores the graph and its compiled PBR properties.");
    int type = 0; static int addKind = 0; ImGui::Combo("New node", &addKind, "Color\0Scalar\0Texture\0Multiply\0Add\0");
    if (ImGui::Button("Add node")) { GraphHistory(); MaterialNode node; node.Id = s.NextNode++; node.Kind = NodeKind(addKind); node.X = 20 + float(s.Graph.Nodes.size() % 3) * 185; node.Y = 20 + float(s.Graph.Nodes.size() / 3) * 130; s.Graph.Nodes.push_back(node); }
    ImGui::SameLine(); if (ImGui::Button("Undo graph") && !s.Undo.empty()) { s.Redo.push_back(s.Graph); s.Graph = s.Undo.back(); s.Undo.pop_back(); }
    ImGui::SameLine(); if (ImGui::Button("Redo graph") && !s.Redo.empty()) { s.Undo.push_back(s.Graph); s.Graph = s.Redo.back(); s.Redo.pop_back(); }
    ImGui::BeginChild("MaterialCanvas", ImVec2(0, 310), true, ImGuiWindowFlags_HorizontalScrollbar);
    const auto origin = ImGui::GetCursorScreenPos(); auto* draw = ImGui::GetWindowDrawList();
    for (const auto& node : s.Graph.Nodes)
        for (int input = 0; input < 2; ++input)
        {
            const auto id = input ? node.InputB : node.InputA; const auto source = std::find_if(s.Graph.Nodes.begin(), s.Graph.Nodes.end(), [id](const auto& n) { return n.Id == id; });
            if (source != s.Graph.Nodes.end())
            {
                ImVec2 a(origin.x + source->X + 160, origin.y + source->Y + 45), b(origin.x + node.X, origin.y + node.Y + 45 + input * 25);
                draw->AddBezierCubic(a, ImVec2(a.x + 70, a.y), ImVec2(b.x - 70, b.y), b, IM_COL32(90, 180, 255, 255), 2);
            }
        }
    for (auto& node : s.Graph.Nodes)
    {
        ImGui::PushID(int(node.Id)); ImVec2 p(origin.x + node.X, origin.y + node.Y);
        draw->AddRectFilled(p, ImVec2(p.x + 165, p.y + 108), IM_COL32(42, 47, 55, 255), 5);
        draw->AddRect(p, ImVec2(p.x + 165, p.y + 108), IM_COL32(110, 130, 160, 255), 5);
        ImGui::SetCursorScreenPos(p); if (ImGui::Button(("Node " + std::to_string(node.Id)).c_str(), ImVec2(165, 23))) s.SelectedNode = node.Id;
        if (ImGui::IsItemActivated()) GraphHistory();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) { node.X += ImGui::GetIO().MouseDelta.x; node.Y += ImGui::GetIO().MouseDelta.y; }
        for (int input = 0; input < 2; ++input)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x + 3, p.y + 31 + input * 25)); ImGui::Button(input ? "B" : "A", ImVec2(24, 20));
            if (ImGui::BeginDragDropTarget())
            {
                if (const auto* payload = ImGui::AcceptDragDropPayload("JENGINE_MATERIAL_NODE"))
                    if (payload->DataSize == sizeof(uint32_t)) { GraphHistory(); (input ? node.InputB : node.InputA) = *static_cast<const uint32_t*>(payload->Data); }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + 132, p.y + 31)); ImGui::Button("Out", ImVec2(30, 20));
        if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("JENGINE_MATERIAL_NODE", &node.Id, sizeof(node.Id)); ImGui::Text("Output %u", node.Id); ImGui::EndDragDropSource(); }
        static const char* kinds[] = {"Color", "Scalar", "Texture", "Multiply", "Add"};
        draw->AddText(ImVec2(p.x + 35, p.y + 65), IM_COL32_WHITE, kinds[int(node.Kind)]); ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 800, origin.y + 700)); ImGui::Dummy(ImVec2(1, 1)); ImGui::EndChild();
    auto node = std::find_if(s.Graph.Nodes.begin(), s.Graph.Nodes.end(), [&](const auto& n) { return n.Id == s.SelectedNode; });
    if (node != s.Graph.Nodes.end())
    {
        ImGui::Text("Selected node %u", node->Id);
        const auto before = s.Graph;
        if (node->Kind == NodeKind::Color) ImGui::ColorEdit3("Node color", &node->Color.X);
        if (node->Kind == NodeKind::Scalar) ImGui::DragFloat("Node value", &node->Scalar, .01f);
        if (node->Kind == NodeKind::Texture) { char path[512]; std::snprintf(path, sizeof(path), "%s", node->Texture.c_str()); if (ImGui::InputText("Texture path", path, sizeof(path))) node->Texture = path; }
        if (node->Kind != NodeKind::Multiply && node->Kind != NodeKind::Add && ImGui::IsItemActivated()) { s.Undo.push_back(before); if (s.Undo.size() > 64) s.Undo.erase(s.Undo.begin()); s.Redo.clear(); }
        if (ImGui::Button("Remove node")) { GraphHistory(); s.Graph.Nodes.erase(node); s.SelectedNode = 0; }
    }
    OutputCombo("Base color output", s.Graph.BaseColor); OutputCombo("Metallic output", s.Graph.Metallic); OutputCombo("Roughness output", s.Graph.Roughness);
    ImGui::InputText("Graph asset", s.GraphPath, sizeof(s.GraphPath));
    if (ImGui::Button("Load graph"))
    {
        auto path = jEditorProject::ResolveSafe(editor.GetProjectRoot(), s.GraphPath, s.Error, true);
        if (path && LoadMaterial(*path, s.Graph, s.Error)) { s.NextNode = 1; for (const auto& n : s.Graph.Nodes) s.NextNode = std::max(s.NextNode, n.Id + 1); }
    }
    ImGui::SameLine(); if (ImGui::Button("Save and Apply"))
    {
        auto path = jEditorProject::ResolveSafe(editor.GetProjectRoot(), s.GraphPath, s.Error);
        if (path && EvaluateMaterial(s.Graph, e.Material, s.Error) && SaveMaterial(s.Graph, *path, s.Error))
        { e.Material.GraphAsset = s.GraphPath; if (!editor.GetDocument().Update(e, "Apply material graph")) s.Error = editor.GetDocument().LastError(); }
    }
}
}
void Render(jEditor& editor)
{
    auto& s = Tools(); auto& doc = editor.GetDocument(); const auto* selected = doc.Find(editor.GetSelectedEntityId());
    if (!selected) { ImGui::TextUnformatted("Select an object to edit its material, animation or procedural components."); return; }
    Entity e = *selected;
    if (s.Selected != e.Id)
    {
        s.Selected = e.Id; s.Graph = {}; s.Undo.clear(); s.Redo.clear();
        s.Graph.Nodes = {{1, 0, 0, NodeKind::Color, e.Material.BaseColor, 1, 20, 20, ""}, {2, 0, 0, NodeKind::Scalar, {}, e.Material.Metallic, 220, 20, ""}, {3, 0, 0, NodeKind::Scalar, {}, e.Material.Roughness, 420, 20, ""}};
        s.Graph.BaseColor = 1; s.Graph.Metallic = 2; s.Graph.Roughness = 3; s.NextNode = 4; s.Error.clear();
        if (!e.Material.GraphAsset.empty()) { std::snprintf(s.GraphPath, sizeof(s.GraphPath), "%s", e.Material.GraphAsset.c_str()); const auto file = editor.GetProjectRoot() / e.Material.GraphAsset; LoadMaterial(file, s.Graph, s.Error); for (const auto& n : s.Graph.Nodes) s.NextNode = std::max(s.NextNode, n.Id + 1); }
    }
    if (ImGui::BeginTabBar("AdvancedTools"))
    {
        if (ImGui::BeginTabItem("Material graph")) { GraphUI(editor, e); ImGui::EndTabItem(); }
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
                ImGui::Combo("Brush", &s.BrushMode, "Raise\0Lower\0Flatten\0Smooth\0");
                ImGui::SliderFloat("Brush X", &s.BrushX, -e.Terrain.SizeX / 2, e.Terrain.SizeX / 2); ImGui::SliderFloat("Brush Z", &s.BrushZ, -e.Terrain.SizeZ / 2, e.Terrain.SizeZ / 2);
                ImGui::DragFloat("Radius", &s.BrushRadius, 1, 1, 100000); ImGui::DragFloat("Strength / flatten height", &s.BrushStrength, 1);
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
void Shutdown() { Tools() = ToolState{}; }
}
