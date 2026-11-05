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
    expect(Compile(normal, compiled, error) && compiled.Hlsl.find("JE_DecodeMaterialNormal(") != std::string::npos
        && compiled.Textures.back().Semantic == TextureSemantic::Normal,
        "signed tangent-space normal samples preserve explicit normal semantics");
    const auto tangentNormal = compiled;
    const auto hasValue = [](const Program& program, const std::array<float, 4>& value)
    { return std::find(program.Values.begin(), program.Values.begin() + program.ValueCount, value) != program.Values.begin() + program.ValueCount; };
    auto objectNormal = normal;
    auto& objectNode = objectNormal.Nodes.back(); objectNode.NormalSpace = NormalSpace::Object; objectNode.InputBasisSignXYZ = {1, 1, -1};
    expect(Compile(objectNormal, compiled, error) && compiled.CodeHash == tangentNormal.CodeHash && compiled.Hlsl == tangentNormal.Hlsl
        && hasValue(compiled, {1,1,-1,1}) && hasValue(compiled, {0,1,0,0})
        && compiled.Textures.back().Semantic == TextureSemantic::Normal,
        "object-space source axes and no legacy Y flip publish as linear normal uniforms while reusing shader code");
    objectNode.FlipNormalY = true;
    expect(Compile(objectNormal, compiled, error) && hasValue(compiled, {1,1,0,0}) && compiled.CodeHash == tangentNormal.CodeHash,
        "an explicit object normal Y flip overrides its no-flip default without changing shader identity");
    auto worldNormal = objectNormal; worldNormal.Nodes.back().NormalSpace = NormalSpace::World; worldNormal.Nodes.back().FlipNormalY = false;
    expect(Compile(worldNormal, compiled, error) && hasValue(compiled, {1,1,-1,2}) && hasValue(compiled, {0,1,0,0})
        && compiled.CodeHash == tangentNormal.CodeHash,
        "world-space normals carry their distinct transform mode without masquerading as tangent normals");
    for (const float sign : {0.f, .5f, std::numeric_limits<float>::quiet_NaN()})
    { auto invalid = objectNormal; invalid.Nodes.back().InputBasisSignXYZ[2] = sign; reject(invalid, "normal source basis accepts only finite signed axes"); }
    { auto invalid = objectNormal; invalid.Nodes.back().NormalSpace = NormalSpace(3); reject(invalid, "unsupported normal coordinate enum rejects transactionally"); }
    { auto invalid = normal; invalid.Nodes[0].NormalSpace = NormalSpace::World; reject(invalid, "normal metadata cannot silently affect a non-normal node"); }
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
    auto legacySubsurface = legacy; legacySubsurface.Surface.emplace().Shading.Subsurface = .6f;
    expect(Compile(legacySubsurface, compiled, error) && !compiled.Active,
        "canonical legacy subsurface scalar remains material metadata without forcing a mapped shader");
    auto sss = legacy;
    MaterialNode weight; weight.Id = 2; weight.Kind = NodeKind::Scalar; weight.Scalar = .75f;
    MaterialNode thickness = weight; thickness.Id = 3; thickness.Scalar = 2.5f;
    MaterialNode profile; profile.Id = 4; profile.Kind = NodeKind::DiffusionProfile; profile.Parameters = {0, 0, 0, 0};
    sss.Nodes.insert(sss.Nodes.end(), {weight, thickness, profile});
    sss.SubsurfaceWeight = 2; sss.SubsurfaceColor = 1; sss.SubsurfaceThickness = 3; sss.SubsurfaceProfile = 4;
    Program subsurface;
    expect(Compile(sss, subsurface, error) && subsurface.Active && subsurface.ValueCount == 4
        && subsurface.Hlsl.find("surface.SubsurfaceWeight=") != std::string::npos
        && subsurface.Hlsl.find("surface.SubsurfaceColor=") != std::string::npos
        && subsurface.Hlsl.find("surface.SubsurfaceThickness=") != std::string::npos
        && subsurface.Hlsl.find("surface.SubsurfaceProfile=") != std::string::npos,
        "actual graph compiler emits all four bounded subsurface shader outputs");
    auto changedSSS = sss; changedSSS.Nodes[1].Scalar = .25f; changedSSS.Nodes[3].Parameters[0] = 7;
    expect(Compile(changedSSS, edited, error) && edited.CodeHash == subsurface.CodeHash
        && edited.Hlsl == subsurface.Hlsl && edited.Values != subsurface.Values,
        "profile and weight edits update uniforms while reusing the compiled shader variant");
    auto scalarSSS = legacy; scalarSSS.Nodes.push_back(weight); scalarSSS.SubsurfaceWeight = 2;
    expect(Compile(scalarSSS, edited, error) && edited.Active && edited.Hlsl.find("MaterialSubsurfaceColorWeight.rgb") != std::string::npos
        && edited.Hlsl.find("MaterialSubsurfaceParameters.x") != std::string::npos,
        "scalar-only subsurface output activates real GPU code and absent outputs use canonical material constants");
    auto wrongProfile = sss; wrongProfile.SubsurfaceProfile = 2; reject(wrongProfile, "a generic scalar cannot masquerade as a typed diffusion profile");
    auto wrongWeight = sss; wrongWeight.SubsurfaceWeight = 1; reject(wrongWeight, "subsurface weight requires a scalar rather than RGB");
    auto profileInMath = sss; profileInMath.Nodes.push_back({5,4,2,NodeKind::Multiply});
    reject(profileInMath, "diffusion profile values cannot enter numeric material arithmetic");
    for (float invalid : {-1.f, 8.f, .5f, std::numeric_limits<float>::quiet_NaN()})
    { auto bad = sss; bad.Nodes[3].Parameters[0] = invalid; reject(bad, "profile IDs must be finite bounded integers"); }
    auto invalidSurface = legacySubsurface; invalidSurface.Surface->Shading.SubsurfaceThicknessWorld = -1;
    reject(invalidSurface, "canonical legacy subsurface thickness is validated even without a mapped graph");
    auto skin = sss; skin.SpecularWeight = 2; skin.DiffuseOcclusion = 2; skin.PreScatterWeight = 2;
    skin.Surface.emplace().Shading.ShadingModel = jSurfaceShadingModel::SkinKelemen;
    skin.Surface->Shading.SubsurfaceSpace = jSubsurfaceSpace::Texture;
    expect(Compile(skin, compiled, error) && compiled.Active
        && compiled.Hlsl.find("surface.SpecularWeight=clamp(") != std::string::npos
        && compiled.Hlsl.find("surface.DiffuseOcclusion=saturate(") != std::string::npos
        && compiled.Hlsl.find("surface.PreScatterWeight=saturate(") != std::string::npos
        && compiled.Hlsl.find("surface.ShadingFlags=(uint)MaterialGraphParam.MaterialSkinParameters.x") != std::string::npos,
        "skin outputs compile real bounded scalar shader results with canonical model and scattering-space flags");
    const auto skinProgram = compiled;
    auto editedSkin = skin; editedSkin.Surface->Shading.SpecularWeight = 7; editedSkin.Surface->Shading.DiffuseOcclusion = .5f;
    editedSkin.Surface->Shading.PreScatterWeight = .25f; editedSkin.Surface->Shading.SubsurfaceSpace = jSubsurfaceSpace::Screen;
    expect(Compile(editedSkin, compiled, error) && compiled.CodeHash == skinProgram.CodeHash && compiled.Hlsl == skinProgram.Hlsl,
        "skin surface values and mode remain uniform data rather than code-hash literals");
    { auto invalid = skin; invalid.SpecularWeight = 1; reject(invalid, "skin scalar output rejects RGB or a raw texture sample"); }
    for (int field = 0; field < 5; ++field)
    {
        auto invalid = skin; auto& s = invalid.Surface->Shading;
        if (field == 0) s.ShadingModel = jSurfaceShadingModel(2);
        if (field == 1) s.SubsurfaceSpace = jSubsurfaceSpace(2);
        if (field == 2) s.SpecularWeight = 8.1f;
        if (field == 3) s.DiffuseOcclusion = -1;
        if (field == 4) s.PreScatterWeight = std::numeric_limits<float>::quiet_NaN();
        reject(invalid, "skin canonical model space and finite scalar ranges validate before compilation");
    }
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
        auto subsurface = MappingGraph(NodeKind::POM);
        MaterialNode strength; strength.Id = 20; strength.Kind = NodeKind::Scalar; strength.Scalar = .8f;
        MaterialNode thickness = strength; thickness.Id = 21; thickness.Scalar = .5f;
        MaterialNode profile; profile.Id = 22; profile.Kind = NodeKind::DiffusionProfile; profile.Parameters = {3,0,0,0};
        subsurface.Nodes.insert(subsurface.Nodes.end(), {strength, thickness, profile});
        subsurface.SubsurfaceWeight = 20; subsurface.SubsurfaceColor = 5; subsurface.SubsurfaceThickness = 21; subsurface.SubsurfaceProfile = 22;
        recipes.emplace_back("subsurface", std::move(subsurface));
        auto normalGraph = MappingGraph(NodeKind::POM);
        normalGraph.Nodes.push_back(TextureObject(30, "Assets/normal.png", jMaterialMapping::TextureSemantic::Normal));
        MaterialNode normalNode; normalNode.Id = 31; normalNode.Kind = NodeKind::TextureSampleNormal;
        normalNode.InputA = 30; normalNode.InputB = 4; normalNode.NormalSpace = jMaterialMapping::NormalSpace::Object;
        normalNode.InputBasisSignXYZ = {1,1,-1}; normalGraph.Nodes.push_back(normalNode); normalGraph.Normal = 31;
        recipes.emplace_back("normal-object", normalGraph);
        normalGraph.Nodes.back().NormalSpace = jMaterialMapping::NormalSpace::World;
        recipes.emplace_back("normal-world", normalGraph);
        normalGraph.Surface.emplace().Shading.ShadingModel = jSurfaceShadingModel::SkinKelemen;
        normalGraph.Surface->Shading.SubsurfaceSpace = jSubsurfaceSpace::Texture;
        MaterialNode skinWeight; skinWeight.Id = 32; skinWeight.Kind = NodeKind::Channel; skinWeight.InputA = 5; skinWeight.Parameters = {0,0,0,0};
        normalGraph.Nodes.push_back(skinWeight); normalGraph.SpecularWeight = normalGraph.DiffuseOcclusion = normalGraph.PreScatterWeight = 32;
        recipes.emplace_back("skin", std::move(normalGraph));
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
