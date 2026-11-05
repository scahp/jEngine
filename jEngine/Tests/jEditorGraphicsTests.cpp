#include "pch.h"
#include "Code/Engine/Editor/jEditorProcedural.h"
#include "Shader/jCommonShaderParameters.h"
#include "Shader/jLightingShaderParameters.h"
#include "Renderer/Features/jShadowFiltering.h"
#include "RHI/jRHI.h"
#include "Shader/jShader.h"
#include <cmath>
#include <limits>

int RunEditorGraphicsTests()
{
    using namespace jEditorAuthoring;
    using namespace jEditorProcedural;
    int failures = 0;
    auto expect = [&](bool pass, const char* label)
    { std::cout << (pass ? "PASS " : "FAIL ") << label << '\n'; failures += !pass; };
    {
        // Reproduce a backend allocating its native holder, then failing to
        // open a shader include. A partial holder must never reach a pipeline.
        struct PartialCompiled : jCompiledShader
        {
            int& Destroyed;
            explicit PartialCompiled(int& destroyed) : Destroyed(destroyed) {}
            ~PartialCompiled() override { ++Destroyed; }
        };
        struct FailingCompiler : jRHI
        {
            int& Destroyed;
            explicit FailingCompiler(int& destroyed) : Destroyed(destroyed) {}
            bool CreateShaderInternal(jShader* shader, const jShaderInfo&) const override
            { shader->CompiledShader = new PartialCompiled(Destroyed); return false; }
        };
        int destroyed = 0;
        FailingCompiler backend(destroyed);
        auto* previous = g_rhi;
        g_rhi = &backend;
        jShaderInfo info;
        info.SetShaderFilepath(jNameStatic("expected-missing-include-regression.hlsl"));
        jShader shader(info);
        shader.Initialize();
        g_rhi = previous;
        expect(shader.GetCompiledShader() == nullptr && destroyed == 1,
            "failed shader initialization destroys its partial native stage before pipeline publication");
    }
    expect(sizeof(RenderObjectUniformBuffer) == 192 && offsetof(RenderObjectUniformBuffer, BaseColorOpacity) == 144 &&
        offsetof(RenderObjectUniformBuffer, EmissiveOverride) == 160 && offsetof(RenderObjectUniformBuffer, SurfaceFlags) == 176 &&
        offsetof(RenderObjectUniformBuffer, IndexElementSize) == 184 && offsetof(RenderObjectUniformBuffer, SurfacePadding) == 188,
        "material and geometry UBO keeps its 192-byte layout with explicit index width");
    expect(sizeof(ViewUniformBuffer) == 560 && offsetof(ViewUniformBuffer, ShadowFilterParams) == 352
        && offsetof(ViewUniformBuffer, SHCoefficients) == 368
        && offsetof(ViewUniformBuffer, MaterialViewDirection) == 544,
        "shared view preserves existing offsets and adds aligned environment/material context");
    expect(sizeof(jMaterialGraphUniformBuffer) == 1184
        && offsetof(jMaterialGraphUniformBuffer, OutputOverridesMask) == 1024
        && offsetof(jMaterialGraphUniformBuffer, WaterWavesA) == 1040
        && offsetof(jMaterialGraphUniformBuffer, WaterControl) == 1168,
        "mapping constants and water waves share an aligned material block");
    jShadowFiltering::Settings filtering;
    expect(jShadowFiltering::TryMakeSettings(1, 4.0f, filtering) &&
        filtering.Method == jShadowFiltering::Mode::Poisson16 && filtering.RadiusTexels == 4.0f,
        "shadow filtering accepts the bounded Poisson mode and texel radius");
    const auto validFiltering = filtering;
    bool invalidFilteringRejected = true;
    for (int mode : {-1, 2})
        invalidFilteringRejected &= !jShadowFiltering::TryMakeSettings(mode, 1.0f, filtering);
    for (float radius : {-1.0f, 4.001f, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()})
        invalidFilteringRejected &= !jShadowFiltering::TryMakeSettings(1, radius, filtering);
    expect(invalidFilteringRejected && filtering.Method == validFiltering.Method &&
        filtering.RadiusTexels == validFiltering.RadiusTexels,
        "invalid and nonfinite shadow settings leave the last valid result unchanged");
    expect(jShadowFiltering::TryMakeSettings(0, 0.0f, filtering) &&
        filtering.Method == jShadowFiltering::Mode::Single && filtering.RadiusTexels == 0.0f,
        "existing comparison sampling and zero-radius settings remain valid");
    TerrainProperties terrain;
    terrain.Resolution = 3; terrain.SizeX = 20; terrain.SizeZ = 10;
    Mesh mesh; std::string error;
    expect(BuildTerrainMesh(terrain, mesh, error) && mesh.Vertices.size() == 9 && mesh.Indices.size() == 24,
        "terrain produces shared grid vertices and two triangles per cell");
    expect(mesh.Vertices.front().Position == Vec3{-10, 0, -5} && mesh.Vertices.back().Position == Vec3{10, 0, 5},
        "terrain covers centered world-unit extents");
    bool up = true;
    for (const auto& v : mesh.Vertices) up &= v.Normal == Vec3{0, 1, 0};
    expect(up, "flat terrain normals point upward");
    terrain.Heights = {0, 10, 20, 0, 10, 20, 0, 10, 20};
    expect(BuildTerrainMesh(terrain, mesh, error) && std::abs(mesh.Vertices[4].Normal.X + 0.70710678f) < 0.0001f &&
        std::abs(mesh.Vertices[4].Normal.Y - 0.70710678f) < 0.0001f,
        "sloped terrain derives normalized normals from height derivatives");
    const Mesh valid = mesh;
    terrain.Heights.resize(1);
    expect(!BuildTerrainMesh(terrain, mesh, error) && mesh.Vertices.size() == valid.Vertices.size(),
        "bad terrain height count rejects transactionally");
    terrain.Heights.clear(); terrain.Resolution = 258;
    expect(!BuildTerrainMesh(terrain, mesh, error), "terrain rejects excessive topology before allocation");
    terrain.Resolution = 3; terrain.SizeX = std::numeric_limits<float>::quiet_NaN();
    expect(!BuildTerrainMesh(terrain, mesh, error), "terrain rejects nonfinite dimensions");

    ParticleProperties settings;
    settings.MaxParticles = 8; settings.SpawnRate = 60; settings.Lifetime = 2; settings.Seed = 17;
    ParticleSimulation a, b; a.Reset(settings); b.Reset(settings);
    for (int i = 0; i < 12; ++i) { a.Advance(settings, 1.0f / 60); b.Advance(settings, 1.0f / 60); }
    bool identical = a.Particles().size() == b.Particles().size();
    for (size_t i = 0; i < a.Particles().size(); ++i)
        identical &= a.Particles()[i].Position == b.Particles()[i].Position && a.Particles()[i].Velocity == b.Particles()[i].Velocity;
    expect(identical && !a.Particles().empty(), "particle seed and time sequence reproduce positions and velocities");
    expect(a.Particles().size() == 8, "particle simulation enforces capacity without accumulating a spawn backlog");
    const auto before = a.Particles().front();
    settings.Enabled = false; settings.Gravity = {0, -10, 0}; a.Advance(settings, 0.1f);
    expect(a.Particles().front().Velocity.Y < before.Velocity.Y, "live particles integrate configured gravity");
    settings.Lifetime = 0.001f; a.Advance(settings, 0.1f);
    expect(a.Particles().empty(), "disabled emitters stop spawning and expired particles retire");
    settings.Enabled = true; settings.MaxParticles = 0; a.Reset(settings); a.Advance(settings, 1000);
    expect(a.Particles().empty(), "zero capacity and long frame pauses remain bounded");
    settings.MaxParticles = 8; a.Reset(settings); a.Advance(settings, std::numeric_limits<float>::quiet_NaN());
    expect(a.Particles().empty(), "nonfinite particle timestep does not poison simulation");
    return failures ? 1 : 0;
}

int ExportEditorPathTracingShaderFixtures(const char*, bool);
int ExportEditorGraphicsShaderFixtures(const char* directory, bool vulkan)
{
    try
    {
        gAPIType = vulkan ? EAPIType::Vulkan : EAPIType::DX12;
        const std::filesystem::path target(directory);
        std::filesystem::create_directories(target);
        const std::string declarations = jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0) +
            jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1) +
            jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
        const std::string flags = "#define COMPUTE_SHADER 0\n#define PIXEL_SHADER 1\n#define USE_PBR 1\n"
            "#define USE_ALBEDO_TEXTURE 1\n#define USE_VERTEX_BITANGENT 1\n#define USE_VERTEX_COLOR 0\n"
            "#define USE_SRGB_ALBEDO_TEXTURE 1\n#define USE_VARIABLE_SHADING_RATE 0\n";
        auto write = [&](const char* name, const std::string& source)
        {
            std::ofstream file(target / (std::string(name) + ".hlsl"));
            file.exceptions(std::ios::badbit | std::ios::failbit); file << source;
        };
        write("editor-gbuffer", flags + declarations + "\n#include \"gbuffer_fs.hlsl\"\n");
        write("editor-gbuffer-vs", "#define PIXEL_SHADER 0\n#define COMPUTE_SHADER 0\n"
            "#define USE_ALBEDO_TEXTURE 1\n#define USE_VERTEX_BITANGENT 1\n#define USE_VERTEX_COLOR 0\n" +
            declarations + "\n#include \"gbuffer_vs.hlsl\"\n");
        write("editor-forward", flags + "\n#include \"common.hlsl\"\n" +
            jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0) +
            jShaderParameterSet::GenerateHLSL<jDirectionalLightShaderParameters>(1) +
            jShaderParameterSet::GenerateHLSL<jPointLightShaderParameters>(2) +
            jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(3) +
            jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(4) +
            jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(5) + "\n#include \"shader_fs.hlsl\"\n");
        const std::string deferredFlags = flags + "#define USE_SHADOW_MAP 1\n#define USE_SUBPASS 0\n"
            "#define USE_REVERSEZ 0\n";
        const std::string deferredView = jShaderParameterSet::GenerateHLSL<jViewShaderParameters>(0);
        const std::string sceneTextures = jShaderParameterSet::GenerateHLSL<jSceneTexturesShaderParameters>(3);
        write("shadow-directional", deferredFlags + "#include \"common.hlsl\"\n" + deferredView +
            jShaderParameterSet::GenerateHLSL<jDirectionalLightShaderParameters>(1) +
            jShaderParameterSet::GenerateHLSL<jDirectionalLightIBLShaderParameters>(2) + sceneTextures +
            "\n#include \"directionallight_fs.hlsl\"\n");
        write("shadow-point", deferredFlags + "#include \"common.hlsl\"\n" + deferredView +
            jShaderParameterSet::GenerateHLSL<jPointLightShaderParameters>(1) + sceneTextures +
            "\n#include \"pointlight_fs.hlsl\"\n");
        write("shadow-spot", deferredFlags + "#include \"common.hlsl\"\n" + deferredView +
            jShaderParameterSet::GenerateHLSL<jSpotLightShaderParameters>(1) + sceneTextures +
            "\n#include \"spotlight_fs.hlsl\"\n");
        const std::string shadowPrefix = "#define COMPUTE_SHADER 0\n#define PIXEL_SHADER 0\n#include \"common.hlsl\"\n" +
            jShaderParameterSet::GenerateHLSL<jRenderObjectShaderParameters>(1) +
            jShaderParameterSet::GenerateHLSL<jMaterialShaderParameters>(2);
        write("editor-shadow-vs", shadowPrefix + jShaderParameterSet::GenerateHLSL<jDirectionalLightOnlyShaderParameters>(0) +
            "\n#include \"shadow_vs.hlsl\"\n");
        write("editor-shadow-instancing-vs", shadowPrefix + jShaderParameterSet::GenerateHLSL<jDirectionalLightOnlyShaderParameters>(0) +
            "\n#include \"shadow_instancing_vs.hlsl\"\n");
        write("editor-spot-shadow-vs", shadowPrefix + jShaderParameterSet::GenerateHLSL<jSpotLightOnlyShaderParameters>(0) +
            "\n#include \"spot_shadow_vs.hlsl\"\n");
        write("editor-omni-shadow-vs", shadowPrefix + jShaderParameterSet::GenerateHLSL<jPointLightOnlyShaderParameters>(0) +
            "\n#include \"omni_shadow_vs.hlsl\"\n");
        write("editor-shadow-ps", "#define PIXEL_SHADER 1\n#include \"shadow_fs.hlsl\"\n");
        write("editor-omni-shadow-ps", "#define PIXEL_SHADER 1\n#include \"common.hlsl\"\n" +
            jShaderParameterSet::GenerateHLSL<jPointLightOnlyShaderParameters>(0) + "\n#include \"omni_shadow_fs.hlsl\"\n");
        std::ofstream contract(target / "editor-cpu-layout.json");
        contract.exceptions(std::ios::badbit | std::ios::failbit);
        contract << "{\"RenderObjectUniformBuffer\":{\"size\":" << sizeof(RenderObjectUniformBuffer) << ",\"fields\":{";
        bool first = true;
        for (const auto& field : RenderObjectUniformBuffer::GetShaderUniformBufferFieldMembers())
        {
            if (!first) contract << ',';
            first = false; contract << '"' << field.Name << "\":" << field.ByteOffset;
        }
        contract << "}},\"ViewUniformBuffer\":{\"size\":" << sizeof(ViewUniformBuffer) << ",\"fields\":{";
        first = true;
        for (const auto& field : ViewUniformBuffer::GetShaderUniformBufferFieldMembers())
        {
            if (!first) contract << ',';
            first = false; contract << '"' << field.Name << "\":" << field.ByteOffset;
        }
        contract << "}},\"jMaterialGraphUniformBuffer\":{\"size\":" << sizeof(jMaterialGraphUniformBuffer) << ",\"fields\":{";
        first = true;
        for (const auto& field : jMaterialGraphUniformBuffer::GetShaderUniformBufferFieldMembers())
        {
            if (!first) contract << ',';
            first = false; contract << '"' << field.Name << "\":" << field.ByteOffset;
        }
        contract << "}}}\n";
        return ExportEditorPathTracingShaderFixtures(directory, vulkan);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor graphics shader export failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
