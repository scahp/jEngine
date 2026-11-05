#include "pch.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jImageFileLoader.h"
#include "lodepng.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "External/PathTracingDataLoader/stb_image_write.h"
#include "stb_image.h"
#include <filesystem>
#include <fstream>
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
    auto constants = graph;
    constants.Nodes[0].Kind = NodeKind::Scalar;
    constants.Nodes[0].Scalar = .2f;
    constants.Nodes.push_back({4, 3, 2, NodeKind::Add});
    constants.Roughness = 4;
    MaterialProperties arithmetic;
    expect(EvaluateMaterial(constants, arithmetic, error) &&
        std::abs(arithmetic.BaseColor.X - .1f) < .0001f && std::abs(arithmetic.Roughness - .6f) < .0001f,
        "connected scalar multiplication and addition use their actual input values");
    auto texture = graph; texture.Nodes[0].Kind = NodeKind::Texture; texture.Nodes[0].Texture = "Assets/Textures/color.png";
    expect(EvaluateMaterial(texture, material, error) && material.BaseColorTexture == "Assets/Textures/color.png" &&
        material.BaseColor == Vec3{.5f, .5f, .5f}, "texture multiplication keeps both the texture slot and its constant tint");
    texture.Nodes[0].Texture = "../private.png";
    expect(!EvaluateMaterial(texture, material, error), "material path traversal rejects");
    const auto fixture = std::filesystem::temp_directory_path() / ("jengine-tools-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixture);
    const auto file = fixture / "test.jmaterial"; MaterialGraph loaded;
    expect(SaveMaterial(graph, file, error) && LoadMaterial(file, loaded, error) && EvaluateMaterial(loaded, material, error) && material.BaseColor == previous.BaseColor, "node graph saves and reloads connected values");
    jEditorThumbnail preview;
    const bool previewReady = PrepareMaterialThumbnail(fixture, file, preview, error);
    const size_t middle = (size_t(preview.Height / 2) * preview.Width + preview.Width / 2) * 4;
    expect(previewReady && error.empty() && preview.Width == preview.Height &&
        preview.Rgba.size() > middle + 3 && preview.Rgba[3] == 0 &&
        preview.Rgba[middle + 3] == 255 && preview.Rgba[middle + 2] > preview.Rgba[middle],
        "material thumbnail draws a shaded sphere using the evaluated graph color");
    graph.Nodes.push_back({4, 0, 0, NodeKind::Texture, {1, 1, 1}, 1, 0, 0, "Assets/Textures/normal.png"});
    graph.Normal = 4;
    expect(SaveMaterial(graph, file, error) && LoadMaterial(file, loaded, error) && EvaluateMaterial(loaded, material, error) &&
        loaded.Normal == 4 && material.NormalTexture == "Assets/Textures/normal.png", "Normal output saves a texture in the material's normal slot");
    expect(PrepareMaterialThumbnail(fixture, file, preview, error) && !preview.Rgba.empty() &&
        error.find("Normal map preview unavailable") != std::string::npos,
        "material thumbnail keeps the available graph values when a map is missing");
    graph.Normal = 3;
    expect(!EvaluateMaterial(graph, material, error) && material.NormalTexture == "Assets/Textures/normal.png",
        "Normal output rejects a non-texture node without modifying the material");
    graph.Normal = 0;
    auto& surface = graph.Surface.emplace();
    surface.Shading.Transmission = 1.f;
    surface.Shading.Ior = 1.45f;
    surface.Shading.Subsurface = .6f; surface.Shading.SubsurfaceColor = {.8f,.4f,.2f};
    surface.Shading.SubsurfaceThicknessWorld = .5f; surface.Shading.SubsurfaceProfile = 5;
    surface.Shading.ShadingModel = jSurfaceShadingModel::SkinKelemen; surface.Shading.SubsurfaceSpace = jSubsurfaceSpace::Texture;
    surface.Shading.SpecularWeight = 2.5f; surface.Shading.DiffuseOcclusion = .75f; surface.Shading.PreScatterWeight = .25f;
    surface.Shading.ClearCoat = .75f;
    surface.Shading.AlphaMode = jSurfaceAlphaMode::Mask;
    surface.Shading.AlphaCutoff = .4f;
    surface.Emission = {.25f, .5f, 1.f};
    const auto glass = fixture / "glass.jmaterial";
    expect(SaveMaterial(graph, glass, error) && LoadMaterial(glass, loaded, error) && loaded.Surface &&
        loaded.Surface->Shading.Transmission == 1.f && loaded.Surface->Shading.Ior == 1.45f &&
        loaded.Surface->Shading.ClearCoat == .75f && loaded.Surface->Emission == surface.Emission,
        "scene material graph round-trips glass, coat and emission values");
    jEditorAuthoring::MaterialProperties sssProperties;
    expect(loaded.Surface && loaded.Surface->Shading.Subsurface == .6f && loaded.Surface->Shading.SubsurfaceProfile == 5
        && EvaluateMaterial(loaded, sssProperties, error) && sssProperties.SubsurfaceOverrideEnabled
        && sssProperties.SubsurfaceWeight == .6f && sssProperties.SubsurfaceColor == jEditorAuthoring::Vec3{.8f,.4f,.2f}
        && sssProperties.SubsurfaceThicknessWorld == .5f && sssProperties.SubsurfaceProfile == 5,
        "actual v1 asset roundtrip exposes the same canonical subsurface weight color thickness and profile to authoring");
    expect(sssProperties.SkinOverrideEnabled && sssProperties.ShadingModel == jSurfaceShadingModel::SkinKelemen
        && sssProperties.SubsurfaceSpace == jSubsurfaceSpace::Texture && sssProperties.SpecularWeight == 2.5f
        && sssProperties.DiffuseOcclusion == .75f && sssProperties.PreScatterWeight == .25f,
        "optional v1 skin metadata reaches actual authoring values without changing old material node numbering");
    {
        const auto source = fixture / "example.scene";
        std::ofstream sidecar(fixture / "example.scene.materials.json");
        sidecar << R"({"format":"jEngineSceneMaterials","version":1,"materials":{"Glass":"glass.jmaterial"}})";
        sidecar.close();
        jObject object;
        for (const char* name : {"Glass", "Glass", "Wall"})
        {
            auto* mesh = new jRenderObject();
            mesh->MaterialPtr = std::make_shared<jMaterial>();
            mesh->MaterialPtr->SourceMaterialName = name;
            object.RenderObjects.push_back(mesh);
        }
        const auto wall = object.RenderObjects[2]->MaterialPtr;
        size_t bound = 0;
        const bool applied = ApplySceneMaterialLibrary(source, {&object}, bound, error);
        const auto& glassMaterial = object.RenderObjects[0]->MaterialPtr;
        expect(applied && bound == 1 && glassMaterial == object.RenderObjects[1]->MaterialPtr &&
            glassMaterial->Surface && glassMaterial->Surface->Ior == 1.45f &&
            glassMaterial->IsRaytracingAlphaTestEnabled() && object.RenderObjects[2]->MaterialPtr == wall,
            "scene library shares one material across matching source slots and leaves other slots intact");
        expect(glassMaterial->Surface && glassMaterial->Surface->Subsurface == .6f
            && glassMaterial->Surface->SubsurfaceColor == std::array<float,3>{.8f,.4f,.2f}
            && glassMaterial->Surface->SubsurfaceThicknessWorld == .5f && glassMaterial->Surface->SubsurfaceProfile == 5,
            "real scene-library material publication retains canonical SSS data for both raster uniforms and legacy PT evaluation");
        expect(glassMaterial->Surface && glassMaterial->Surface->ShadingModel == jSurfaceShadingModel::SkinKelemen
            && glassMaterial->Surface->SubsurfaceSpace == jSubsurfaceSpace::Texture && glassMaterial->Surface->SpecularWeight == 2.5f
            && glassMaterial->Surface->DiffuseOcclusion == .75f && glassMaterial->Surface->PreScatterWeight == .25f,
            "actual scene-library COW publication binds the canonical skin metadata to both matching meshes");
    }
    {
        MaterialGraph dynamic;
        dynamic.Nodes = {{1, 0, 0, NodeKind::Color, {.2f, .4f, .6f}}};
        MaterialNode channel; channel.Id = 2; channel.Kind = NodeKind::Channel; channel.InputA = 1; channel.Parameters = {1, 0, 0, 0};
        dynamic.Nodes.push_back(channel); dynamic.BaseColor = 2;
        const auto mapped = fixture / "mapped.jmaterial";
        dynamic.SubsurfaceWeight = 2; dynamic.SubsurfaceColor = 1; dynamic.SubsurfaceThickness = 2;
        MaterialNode profile; profile.Id = 20; profile.Kind = NodeKind::DiffusionProfile; profile.Parameters = {7,0,0,0};
        dynamic.Nodes.push_back(profile); dynamic.SubsurfaceProfile = 20;
        dynamic.SpecularWeight = dynamic.DiffuseOcclusion = dynamic.PreScatterWeight = 2;
        MaterialGraph reopened;
        jMaterialMapping::Program compiled, recompiled;
        expect(SaveMaterial(dynamic, mapped, error) && LoadMaterial(mapped, reopened, error) && reopened.Version == 2
            && reopened.Nodes[1].Parameters == channel.Parameters && jMaterialMapping::Compile(dynamic, compiled, error)
            && jMaterialMapping::Compile(reopened, recompiled, error) && compiled.Hlsl == recompiled.Hlsl && compiled.Values == recompiled.Values,
            "actual v2 material file preserves typed pins parameters and regenerated program data");
        expect(reopened.SubsurfaceWeight == 2 && reopened.SubsurfaceColor == 1 && reopened.SubsurfaceThickness == 2
            && reopened.SubsurfaceProfile == 20 && reopened.Nodes.back().Kind == NodeKind::DiffusionProfile,
            "actual v2 graph persistence retains composed SSS outputs and the typed profile node");
        expect(reopened.SpecularWeight == 2 && reopened.DiffuseOcclusion == 2 && reopened.PreScatterWeight == 2,
            "actual v2 graph file preserves the three typed skin scalar output connections");
        {
            MaterialProperties defaults; defaults.SkinOverrideEnabled = true; defaults.ShadingModel = jSurfaceShadingModel::SkinKelemen;
            defaults.SubsurfaceSpace = jSubsurfaceSpace::Texture; defaults.SpecularWeight = 2; defaults.DiffuseOcclusion = .6f; defaults.PreScatterWeight = .3f;
            MaterialProperties instance; instance.SkinOverrideEnabled = true; instance.SpecularWeight = 0;
            instance.GraphOverrides.emplace().SpecularWeight = true;
            InheritMaterialGraph(instance, defaults);
            expect(instance.ShadingModel == defaults.ShadingModel && instance.SubsurfaceSpace == defaults.SubsurfaceSpace
                && instance.SpecularWeight == 0 && instance.DiffuseOcclusion == .6f && instance.PreScatterWeight == .3f
                && MaterialMappingOverrides(instance) == jMaterialMapping::OverrideSpecularWeight,
                "skin instance inheritance preserves an explicit zero specular output override while inheriting model space and other scalars");
        }
        {
            auto normalGraph = dynamic;
            MaterialNode uv; uv.Id = 3; uv.Kind = NodeKind::UV;
            MaterialNode texture; texture.Id = 4; texture.Kind = NodeKind::TextureObject; texture.Texture = "Assets/source-normal.png";
            MaterialNode normal; normal.Id = 5; normal.Kind = NodeKind::TextureSampleNormal; normal.InputA = 4; normal.InputB = 3;
            normal.NormalSpace = jMaterialMapping::NormalSpace::Object; normal.InputBasisSignXYZ = {1,1,-1}; normal.FlipNormalY = false;
            normalGraph.Nodes.insert(normalGraph.Nodes.end(), {uv, texture, normal}); normalGraph.Normal = 5;
            const auto normalFile = fixture / "normal-spaces.jmaterial"; MaterialGraph reopenedNormal;
            expect(SaveMaterial(normalGraph, normalFile, error) && LoadMaterial(normalFile, reopenedNormal, error)
                && reopenedNormal.Nodes.back().NormalSpace == jMaterialMapping::NormalSpace::Object
                && reopenedNormal.Nodes.back().InputBasisSignXYZ == std::array<float,3>{1,1,-1}
                && reopenedNormal.Nodes.back().FlipNormalY == std::optional<bool>(false),
                "actual material file roundtrips object normal space source handedness and an explicit false flip");
            nlohmann::json normalJson; { std::ifstream in(normalFile); in >> normalJson; }
            const auto original = normalJson;
            normalJson["nodes"].back().erase("normalSpace"); normalJson["nodes"].back().erase("inputBasisSignXYZ"); normalJson["nodes"].back().erase("flipNormalY");
            { std::ofstream out(normalFile); out << normalJson.dump(); }
            expect(LoadMaterial(normalFile, reopenedNormal, error) && reopenedNormal.Nodes.back().NormalSpace == jMaterialMapping::NormalSpace::Tangent
                && reopenedNormal.Nodes.back().InputBasisSignXYZ == std::array<float,3>{1,1,1} && !reopenedNormal.Nodes.back().FlipNormalY
                && reopenedNormal.Nodes.back().Parameters[0] == 1,
                "older Sample Normal files retain Tangent and the original Y flip rather than reinterpreting Parameters.y");
            for (int field = 0; field < 4; ++field)
            {
                auto bad = original;
                if (field == 0) bad["nodes"].back()["normalSpace"] = 1.5;
                if (field == 1) bad["nodes"].back()["inputBasisSignXYZ"] = {1,1,0};
                if (field == 2) bad["nodes"].back()["flipNormalY"] = 0;
                if (field == 3) bad["skinOutputs"].erase("specularWeight");
                { std::ofstream out(normalFile); out << bad.dump(); }
                const auto priorSpace = reopenedNormal.Nodes.back().NormalSpace;
                expect(!LoadMaterial(normalFile, reopenedNormal, error) && !error.empty() && reopenedNormal.Nodes.back().NormalSpace == priorSpace,
                    "invalid typed normal metadata or incomplete skin outputs preserve the last loaded material graph");
            }
        }
        PreparedMaterialMapping prepared;
        expect(PrepareMaterialMapping(fixture, reopened, prepared, error) && prepared.Program && prepared.Program->Active,
            "texture-free compiled graph prepares its actual runtime material payload without an RHI");
        auto original = std::make_shared<jMaterial>();
        auto instance = std::make_shared<jMaterial>(*original);
        ApplyMaterialMapping(*instance, prepared, jMaterialMapping::OverrideBaseColor);
        expect(!original->MappingProgram && instance->MappingProgram == prepared.Program && instance->MappingValues[0].x == .2f
            && instance->MappingOutputOverridesMask == jMaterialMapping::OverrideBaseColor && instance->NeedToUpdateShaderBindingInstance,
            "compiled material publication binds values and output overrides to a copy without changing its shared source");
        const auto priorProgram = prepared.Program;
        auto missing = dynamic;
        MaterialNode uv; uv.Id = 3; uv.Kind = NodeKind::UV; missing.Nodes.push_back(uv);
        MaterialNode texture; texture.Id = 4; texture.Kind = NodeKind::TextureObject; texture.Texture = "Assets/missing.png"; missing.Nodes.push_back(texture);
        MaterialNode sample; sample.Id = 5; sample.Kind = NodeKind::TextureSample; sample.InputA = 4; sample.InputB = 3;
        missing.Nodes.push_back(sample); missing.BaseColor = 5;
        expect(!PrepareMaterialMapping(fixture, missing, prepared, error) && prepared.Program == priorProgram && !error.empty(),
            "real missing mapping asset rejects before replacing the prepared resource set");
        const auto source = fixture / "mapped.scene";
        { std::ofstream sidecar(fixture / "mapped.scene.materials.json"); sidecar << R"({"format":"jEngineSceneMaterials","version":1,"materials":{"Mapped":"mapped.jmaterial"}})"; }
        jObject object;
        for (int i = 0; i < 2; ++i)
        {
            auto* mesh = new jRenderObject(); mesh->MaterialPtr = std::make_shared<jMaterial>();
            mesh->MaterialPtr->SourceMaterialName = "Mapped"; object.RenderObjects.push_back(mesh);
        }
        size_t bound = 0;
        const auto beforeRayGate = object.RenderObjects[0]->MaterialPtr;
        bool mappingPending = true;
        expect(!ApplySceneMaterialLibrary(source, {&object}, bound, error, &mappingPending, false) && !mappingPending
            && object.RenderObjects[0]->MaterialPtr == beforeRayGate && !beforeRayGate->MappingProgram,
            "scene library rejects raster-only graph publication in ray mode before any material mutation or pending upload");
        expect(ApplySceneMaterialLibrary(source, {&object}, bound, error) && bound == 1
            && object.RenderObjects[0]->MaterialPtr == object.RenderObjects[1]->MaterialPtr
            && object.RenderObjects[0]->MaterialPtr->MappingProgram && object.RenderObjects[0]->MaterialPtr->MappingProgram->CodeHash == compiled.CodeHash,
            "real scene material sidecar publishes the compiled program into every matching shared source slot");
        const auto good = object.RenderObjects[0]->MaterialPtr;
        { std::ofstream corrupted(mapped); corrupted << R"({"format":"jEngineMaterial","version":2,"nodes":[]})"; }
        expect(!ApplySceneMaterialLibrary(source, {&object}, bound, error)
            && object.RenderObjects[0]->MaterialPtr == good && object.RenderObjects[1]->MaterialPtr == good,
            "invalid mapped scene library reload keeps every prior material binding atomically");
        const std::array<unsigned char, 16> pixels{0,64,128,16,255,192,32,224,5,127,250,128,90,20,200,255};
        std::vector<unsigned char> png;
        const auto dataFile = fixture / "linear-data.png";
        const bool encoded = LodePNG::encode(png, pixels.data(), 2, 2) == 0;
        { std::ofstream imageFile(dataFile, std::ios::binary); imageFile.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size())); }
        std::shared_ptr<jImageData> data;
        expect(encoded && DecodeMaterialDataTexture(dataFile, data, error) && data && !data->sRGB && data->Format == ETextureFormat::RGBA8
            && data->MipLevel == 1 && !data->CreateMipmapIfPossible && data->ImageBulkData.ImageData.size() == pixels.size()
            && std::equal(pixels.begin(), pixels.end(), data->ImageBulkData.ImageData.begin()),
            "actual PNG data decode preserves all four independent linear channels including non-opaque horizon alpha");
        const auto retainedData = data;
        { std::ofstream invalid(fixture / "bad-data.png"); invalid << "not a PNG"; }
        expect(!DecodeMaterialDataTexture(fixture / "bad-data.png", data, error) && data == retainedData,
            "invalid raw data decode preserves the previous immutable image");
        const auto jpegFile = fixture / "normal-data.jpeg";
        std::vector<unsigned char> jpeg;
        const auto appendJpeg = [](void* context, void* bytes, int size)
        {
            auto& output = *static_cast<std::vector<unsigned char>*>(context);
            const auto* first = static_cast<unsigned char*>(bytes); output.insert(output.end(), first, first + size);
        };
        const bool jpegEncoded = stbi_write_jpg_to_func(appendJpeg, &jpeg, 2, 2, 4, pixels.data(), 95) != 0;
        { std::ofstream imageFile(jpegFile, std::ios::binary); imageFile.write(reinterpret_cast<const char*>(jpeg.data()), std::streamsize(jpeg.size())); }
        int jpegWidth = 0, jpegHeight = 0, jpegChannels = 0;
        std::unique_ptr<stbi_uc, void(*)(void*)> jpegReference(stbi_load_from_memory(jpeg.data(), int(jpeg.size()),
            &jpegWidth, &jpegHeight, &jpegChannels, 4), stbi_image_free);
        std::shared_ptr<jImageData> jpegImage;
        expect(jpegEncoded && jpegReference && DecodeMaterialDataTexture(jpegFile, jpegImage, error) && jpegImage
            && !jpegImage->sRGB && jpegImage->Format == ETextureFormat::RGBA8 && jpegImage->MipLevel == 1
            && jpegImage->Width == jpegWidth && jpegImage->Height == jpegHeight && jpegImage->ImageBulkData.ImageData.size() == 16
            && std::equal(jpegImage->ImageBulkData.ImageData.begin(), jpegImage->ImageBulkData.ImageData.end(), jpegReference.get()),
            "real JPEG normal decode preserves decoded linear channel values without gamma or added BC compression");
        if (!g_rhi)
        {
            MaterialGraph dataGraph;
            MaterialNode uv; uv.Id = 1; uv.Kind = NodeKind::UV;
            MaterialNode texture; texture.Id = 2; texture.Kind = NodeKind::TextureObject; texture.Texture = "linear-data.png";
            MaterialNode sample; sample.Id = 3; sample.Kind = NodeKind::TextureSample; sample.InputA = 2; sample.InputB = 1;
            dataGraph.Nodes = {uv, texture, sample}; dataGraph.BaseColor = 3;
            PreparedMaterialMapping pendingResult = prepared;
            bool pending = false;
            const bool firstResult = PrepareMaterialMapping(fixture, dataGraph, pendingResult, error, &pending);
            expect(!firstResult && pending && pendingResult.Program == prepared.Program && PendingMaterialMappingTextureCount() > 0,
                "mapping preparation queues actual CPU decode without publishing partial GPU bindings");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (pending && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                PrepareMaterialMapping(fixture, dataGraph, pendingResult, error, &pending);
            }
            const auto epoch = PollMaterialMappingTexturePreparation();
            expect(!pending && PendingMaterialMappingTextureCount() == 0 && error.find("initialized rendering device") != std::string::npos
                && pendingResult.Program == prepared.Program && epoch == PollMaterialMappingTexturePreparation(),
                "Prepare itself harvests a real completed worker and retains decoded identity until an owner device is available");
            InvalidateMaterialMappingTextureCache(fixture, "linear-data.png");
            PrepareMaterialMapping(fixture, dataGraph, pendingResult, error, &pending);
            CancelMaterialMappingTexturePreparation();
            expect(pending && PendingMaterialMappingTextureCount() == 0 && pendingResult.Program == prepared.Program,
                "explicit reimport starts a new decode generation and cancellation never replaces the last-good material");
            dataGraph.Nodes[1].Texture = "normal-data.jpeg"; dataGraph.Nodes[2].Kind = NodeKind::TextureSampleNormal;
            dataGraph.BaseColor = 1; dataGraph.Normal = 3;
            // Base Color needs a numeric value rather than UV; retain the actual
            // normal sample as a separate reachable material output.
            MaterialNode color; color.Id = 4; color.Kind = NodeKind::Color; dataGraph.Nodes.push_back(color); dataGraph.BaseColor = 4;
            PrepareMaterialMapping(fixture, dataGraph, pendingResult, error, &pending);
            expect(pending && PendingMaterialMappingTextureCount() > 0 && pendingResult.Program == prepared.Program,
                "a Normal-semantic JPEG takes the same bounded asynchronous raw preparation route as linear PNG data");
            CancelMaterialMappingTexturePreparation();
        }
    }
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
