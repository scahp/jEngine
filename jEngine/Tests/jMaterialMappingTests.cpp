#include "pch.h"
#include "jMaterialMappingTests.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Material/jMaterialMapping.h"
#include <limits>
#if !defined(JENGINE_MATERIAL_MAPPING_TEST_MAIN) || defined(JENGINE_MATERIAL_MAPPING_EXPORT)
#include "Shader/jCommonShaderParameters.h"
#include "Shader/jLightingShaderParameters.h"
#include <fstream>
#endif

namespace
{
using namespace jEditorTools;
MaterialNode TextureObject(uint32_t id, const std::string& path, jMaterialMapping::TextureSemantic semantic = jMaterialMapping::TextureSemantic::Linear)
{
    MaterialNode node; node.Id = id; node.Kind = NodeKind::TextureObject; node.Texture = path; node.Semantic = semantic; return node;
}
MaterialGraph MappingGraph(NodeKind kind)
{
    MaterialGraph graph;
    MaterialNode uv; uv.Id = 1; uv.Kind = NodeKind::UV;
    MaterialNode mapping; mapping.Id = 4; mapping.Kind = kind; mapping.InputA = 1; mapping.InputB = 3;
    mapping.Parameters = {.05f, 32, kind == NodeKind::Parallax ? 0.f : 6.f, 0};
    MaterialNode sample; sample.Id = 5; sample.Kind = NodeKind::TextureSample; sample.InputA = 2; sample.InputB = 4;
    graph.Nodes = {uv, TextureObject(2, "Assets/color.png", jMaterialMapping::TextureSemantic::Color),
        TextureObject(3, "Assets/height.png"), mapping, sample};
    graph.BaseColor = 5; return graph;
}
}
int RunMaterialMappingTests()
{
    using namespace jMaterialMapping;
    int checks = 0, failures = 0;
    const auto expect = [&](bool pass, const char* name)
    { ++checks; failures += !pass; std::cout << (pass ? "PASS " : "FAIL ") << "material mapping: " << name << '\n'; };
    std::string error;
    Program compiled;
    for (const auto kind : {NodeKind::Parallax, NodeKind::POM, NodeKind::Relief})
    {
        const auto graph = MappingGraph(kind);
        expect(Compile(graph, compiled, error) && compiled.Active && compiled.Textures.size() == 2
            && compiled.ValueCount == 2 && !compiled.Hlsl.empty(), "mapping DAG compiles actual surface functions and resource bindings");
    }
    auto graph = MappingGraph(NodeKind::POM);
    expect(Compile(graph, compiled, error) && compiled.Hlsl.find("JE_POM(") != std::string::npos
        && compiled.Hlsl.find("RenderObjectParam.Metallic") != std::string::npos
        && compiled.Hlsl.find("RenderObjectParam.Roughness") != std::string::npos,
        "generated surface invokes bounded POM and preserves absent scalar outputs through object defaults");
    const auto original = compiled;
    auto changed = graph; changed.Nodes[1].Texture = "Assets/another.png"; changed.Nodes[3].Parameters[0] = .15f;
    Program edited;
    expect(Compile(changed, edited, error) && edited.CodeHash == original.CodeHash && edited.Hlsl == original.Hlsl
        && edited.Values != original.Values && edited.Dependencies != original.Dependencies,
        "constant and asset edits reuse shader code while updating real compiled data and dependencies");
    expect(compiled.Hlsl.find("Assets/") == std::string::npos && compiled.Hlsl.find("0.050") == std::string::npos,
        "paths and numeric node values never become shader source literals");
    auto opacity = graph; opacity.Surface.emplace().Opacity = .25f;
    expect(Compile(opacity, edited, error) && edited.Hlsl == original.Hlsl && edited.Values == original.Values
        && edited.Hlsl.find("BaseColor.a*=") == std::string::npos,
        "graph opacity stays in the existing object material path and is not applied twice in generated code");
    const auto reject = [&](const MaterialGraph& invalid, const char* name)
    {
        Program previous = original;
        expect(!Compile(invalid, previous, error) && !error.empty() && previous.Hlsl == original.Hlsl
            && previous.Values == original.Values && previous.Textures == original.Textures && previous.CodeHash == original.CodeHash, name);
    };
    auto cycle = graph; cycle.Nodes[3].InputA = 4; reject(cycle, "UV cycle rejects while preserving the last compiled program");
    auto disconnected = graph;
    disconnected.Nodes.push_back({100, 101, 1, NodeKind::Add}); disconnected.Nodes.push_back({101, 100, 1, NodeKind::Add});
    reject(disconnected, "disconnected cyclic subgraphs cannot persist");
    auto wrongPin = graph; wrongPin.Nodes[4].InputA = 1; reject(wrongPin, "UV cannot connect to a Texture Object input");
    auto wrongUV = graph; wrongUV.Nodes[3].InputA = 2; reject(wrongUV, "Texture Object cannot connect to a UV input");
    auto wrongKind = graph; wrongKind.Nodes[0].Kind = NodeKind(1000); reject(wrongKind, "unknown node kind rejects");
    auto duplicate = graph; duplicate.Nodes[1].Id = 1; reject(duplicate, "duplicate node identifiers reject");
    auto missing = graph; missing.Nodes[3].InputB = 999; reject(missing, "missing height reference rejects");
    for (const auto& path : {"../escape.png", "/outside.png", "Assets\\bad.png", "C:/bad.png"})
    { auto unsafe = graph; unsafe.Nodes[1].Texture = path; reject(unsafe, "unsafe texture paths cannot enter the compiled resource recipe"); }
    { auto unsafe = graph; unsafe.Nodes[1].Texture = std::string("\xff", 1); reject(unsafe, "invalid UTF-8 is rejected before shader or material JSON publication"); }
    for (const float steps : {0.f, 129.f, 1.5f, std::numeric_limits<float>::quiet_NaN()})
    { auto invalid = graph; invalid.Nodes[3].Parameters[1] = steps; reject(invalid, "nonfinite fractional or unbounded march steps reject"); }

    auto normal = graph;
    normal.Nodes.push_back(TextureObject(20, "Assets/normal.png", TextureSemantic::Normal));
    MaterialNode sampleNormal; sampleNormal.Id = 21; sampleNormal.Kind = NodeKind::TextureSampleNormal;
    sampleNormal.InputA = 20; sampleNormal.InputB = 4; normal.Nodes.push_back(sampleNormal); normal.Normal = 21;
    expect(Compile(normal, compiled, error) && compiled.Hlsl.find("JE_DecodeNormalTS(") != std::string::npos
        && compiled.Textures.back().Semantic == TextureSemantic::Normal,
        "signed tangent-space normal samples preserve explicit normal semantics");
    normal.Normal = 5; reject(normal, "raw RGBA sample cannot silently become a signed normal");
    auto horizon = graph;
    horizon.Nodes.push_back(TextureObject(6, "Assets/horizon0.png")); horizon.Nodes.push_back(TextureObject(7, "Assets/horizon1.png"));
    horizon.Nodes.push_back(TextureObject(8, "Assets/horizon-ao.png"));
    MaterialNode visibility; visibility.Id = 9; visibility.Kind = NodeKind::Horizon;
    visibility.InputA = 4; visibility.InputB = 6; visibility.InputC = 7; visibility.InputD = 8;
    visibility.Parameters = {.05f, 1, 1, 0}; horizon.Nodes.push_back(visibility); horizon.Horizon = 9;
    expect(Compile(horizon, compiled, error) && compiled.UsesHorizon && compiled.Textures.size() == 5
        && compiled.Hlsl.substr(0, compiled.Hlsl.find("float2 JE_EvaluateHorizon")).find("JE_Horizon(") == std::string::npos
        && compiled.Hlsl.find("mul(transpose(ctx.TBN),LightDirWS)") != std::string::npos,
        "Horizon generates a separate directional visibility and AO evaluator without changing albedo");
    horizon.BaseColor = 9; reject(horizon, "Horizon visibility cannot be wired into Base Color");
    auto interior = graph; interior.Nodes.erase(interior.Nodes.begin() + 3, interior.Nodes.end());
    MaterialNode room; room.Id = 4; room.Kind = NodeKind::Interior; room.InputA = 1; room.InputB = 2; room.Parameters = {2, 3, 1, 1};
    interior.Nodes.push_back(room); interior.BaseColor = 4;
    expect(Compile(interior, compiled, error) && compiled.UsesInterior && compiled.Hlsl.find("JE_Interior(") != std::string::npos,
        "interior node compiles a room projection with an explicit texture input");
    interior.Nodes.back().InputC = 3;
    expect(Compile(interior, compiled, error) && compiled.Hlsl.find("JE_InteriorDual(") != std::string::npos,
        "connecting dual-depth input selects the real relief interior recipe");

    const auto values = [](size_t count)
    {
        MaterialGraph result;
        MaterialNode uv; uv.Id = 900; uv.Kind = NodeKind::UV; result.Nodes.push_back(uv);
        uint32_t previous = 0;
        for (uint32_t i = 1; i <= count; ++i)
        {
            result.Nodes.push_back({i, 0, 0, NodeKind::Scalar, {}, .5f});
            if (!previous) previous = i;
            else { result.Nodes.push_back({1000 + i, previous, i, NodeKind::Add}); previous = 1000 + i; }
        }
        result.BaseColor = previous; return result;
    };
    expect(Compile(values(64), compiled, error) && compiled.ValueCount == 64, "all 64 float4 value slots can be used");
    reject(values(65), "65th live value rejects instead of overrunning the GPU uniform array");
    const auto textures = [](size_t count)
    {
        MaterialGraph result;
        MaterialNode uv; uv.Id = 1; uv.Kind = NodeKind::UV; result.Nodes.push_back(uv);
        uint32_t previous = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            result.Nodes.push_back(TextureObject(10 + i, "Assets/texture" + std::to_string(i) + ".png"));
            MaterialNode sample; sample.Id = 30 + i; sample.Kind = NodeKind::TextureSample; sample.InputA = 10 + i; sample.InputB = 1; result.Nodes.push_back(sample);
            if (!previous) previous = sample.Id;
            else { result.Nodes.push_back({50 + i, previous, sample.Id, NodeKind::Add}); previous = 50 + i; }
        }
        result.BaseColor = previous; return result;
    };
    expect(Compile(textures(8), compiled, error) && compiled.Textures.size() == 8, "all eight fixed texture bindings are usable");
    reject(textures(9), "ninth texture rejects before shader or descriptor publication");
    MaterialGraph legacy; legacy.Nodes = {{1, 0, 0, NodeKind::Color, {.2f, .4f, .6f}}}; legacy.BaseColor = 1;
    expect(Compile(legacy, compiled, error) && !compiled.Active && compiled.Hlsl.empty(), "legacy constant graphs keep their existing renderer path");
    std::cout << "Material mapping tests: " << checks << " checks, " << failures << " failures\n";
    return failures;
}
#if !defined(JENGINE_MATERIAL_MAPPING_TEST_MAIN) || defined(JENGINE_MATERIAL_MAPPING_EXPORT)
int ExportMaterialMappingShaderFixtures(const char* directory, bool vulkan)
{
    try
    {
        gAPIType = vulkan ? EAPIType::Vulkan : EAPIType::DX12;
        const std::filesystem::path output(directory); std::filesystem::create_directories(output);
        std::vector<std::pair<std::string, MaterialGraph>> recipes;
        recipes.emplace_back("parallax", MappingGraph(NodeKind::Parallax));
        recipes.emplace_back("pom", MappingGraph(NodeKind::POM));
        auto relief = MappingGraph(NodeKind::Relief); relief.Nodes[3].Parameters[3] = 1;
        recipes.emplace_back("relief", std::move(relief));
        auto horizon = MappingGraph(NodeKind::POM);
        horizon.Nodes.push_back(TextureObject(6, "Assets/h0.png")); horizon.Nodes.push_back(TextureObject(7, "Assets/h1.png"));
        horizon.Nodes.push_back(TextureObject(8, "Assets/ao.png"));
        MaterialNode visibility; visibility.Id = 9; visibility.Kind = NodeKind::Horizon;
        visibility.InputA = 4; visibility.InputB = 6; visibility.InputC = 7; visibility.InputD = 8; visibility.Parameters = {.05f, 1, 1, 0};
        horizon.Nodes.push_back(visibility); horizon.Horizon = 9; recipes.emplace_back("horizon", std::move(horizon));
        auto interior = MappingGraph(NodeKind::POM); interior.Nodes.resize(3);
        MaterialNode room; room.Id = 4; room.Kind = NodeKind::Interior; room.InputA = 1; room.InputB = 2; room.Parameters = {2, 3, 1, 1};
        interior.Nodes.push_back(room); interior.BaseColor = 4; recipes.emplace_back("interior", interior);
        interior.Nodes.back().InputC = 3; recipes.emplace_back("interior-dual", std::move(interior));
        const std::string flags = "#define COMPUTE_SHADER 0\n#define PIXEL_SHADER 1\n#define USE_PBR 1\n#define USE_ALBEDO_TEXTURE 1\n"
            "#define USE_VERTEX_BITANGENT 1\n#define USE_VERTEX_COLOR 0\n#define USE_SRGB_ALBEDO_TEXTURE 1\n#define USE_VARIABLE_SHADING_RATE 0\n";
        const auto view = jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0);
        const auto deferred = view + jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1)
            + jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
        const auto forward = view + jShaderParameterSet::GenerateHLSL<jDirectionalLightShaderParameters>(1)
            + jShaderParameterSet::GenerateHLSL<jPointLightShaderParameters>(2) + jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(3)
            + jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(4) + jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(5);
        for (const auto& [name, graph] : recipes)
        {
            jMaterialMapping::Program program; std::string error;
            if (!jMaterialMapping::Compile(graph, program, error)) throw std::runtime_error(error);
            for (bool useForward : {false, true})
            {
                std::ofstream file(output / ("mapping-" + name + (useForward ? "-forward.hlsl" : "-gbuffer.hlsl")));
                file.exceptions(std::ios::badbit | std::ios::failbit);
                file << flags << "#include \"common.hlsl\"\n" << (useForward ? forward : deferred) << program.Hlsl
                    << (useForward ? "\n#include \"shader_fs.hlsl\"\n" : "\n#include \"gbuffer_fs.hlsl\"\n");
            }
        }
        return 0;
    }
    catch (const std::exception& exception) { std::cerr << "Material mapping shader export failed: " << exception.what() << '\n'; return 1; }
}
#endif
#if defined(JENGINE_MATERIAL_MAPPING_TEST_MAIN) && !defined(JENGINE_MATERIAL_MAPPING_EXPORT)
int main() { return RunMaterialMappingTests(); }
#endif
