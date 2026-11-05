#include "pch.h"
#include "jSceneFeatureIntegrationTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "FileLoader/jEditorAssets.h"
#include "Material/jMaterial.h"
#include "Material/jMaterialMappingShader.h"
#include "Material/jMaterialWater.h"
#include "Renderer/Features/jEnvironmentLighting.h"
#include "Renderer/Features/jHorizonMapping.h"
#include "Renderer/jSceneRenderTargets.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Shader/jShader.h"
#include "jEngine.h"
#include "jOptions.h"
#include "lodepng.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>

// Exported by the existing editor integration test. All native GPU readback and
// PNG conversion stay there; this harness consumes its actual preview metrics.
std::string CaptureSceneFeaturePixels(const char* pngPath);
std::string CaptureSceneFeatureTexturePixels(const std::shared_ptr<jTexture>& texture, const char* pngPath);

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace jEditorAuthoring;
using namespace jEditorTools;
using Clock = std::chrono::steady_clock;
constexpr uint32_t FrameBudget = 400;

void WriteText(const fs::path& file, const std::string& text)
{
    std::ofstream stream(file, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << text;
}

struct RestoreFields
{
    std::vector<std::function<void()>> Actions;
    template<class T> void Remember(T& field)
    {
        const T old = field;
        Actions.emplace_back([pointer = &field, old] { *pointer = old; });
    }
    void Restore()
    {
        for (auto it = Actions.rbegin(); it != Actions.rend(); ++it) (*it)();
        Actions.clear();
    }
    ~RestoreFields() { Restore(); }
};

void ControlledOptions(RestoreFields& restore)
{
    restore.Remember(gOptions.SceneFeatures);
    restore.Remember(gOptions.CameraPos);
    restore.Remember(gOptions.UseDeferredRenderer);
    restore.Remember(gOptions.UseSubpass); restore.Remember(gOptions.UseMemoryless);
    restore.Remember(gOptions.UseHWRTDirectLighting);
    restore.Remember(gOptions.UseSurfelGI); restore.Remember(gOptions.UseSSGI);
    restore.Remember(gOptions.SSGIIntensity); restore.Remember(gOptions.UseSSGITemporalAccumulation);
    restore.Remember(gOptions.AOType); restore.Remember(gOptions.AOIntensity);
    restore.Remember(gOptions.ShowAOOnly); restore.Remember(gOptions.ShowSSGIOnly);
    restore.Remember(gOptions.BloomEyeAdaptation); restore.Remember(gOptions.UseAtmosphericShadowing);
    restore.Remember(gOptions.UseVRS); restore.Remember(gOptions.ShowVRSArea);
    restore.Remember(gOptions.ShowGrid); restore.Remember(gOptions.ShowDebugObject);
    restore.Remember(gOptions.ShowDebugRT); restore.Remember(gOptions.EarthQuake);
    gOptions.SceneFeatures = {};
    gOptions.UseDeferredRenderer = true; gOptions.UseSubpass = false; gOptions.UseMemoryless = false;
    gOptions.UseHWRTDirectLighting = false; gOptions.UseSurfelGI = false; gOptions.UseSSGI = false;
    gOptions.AOType = 0; gOptions.AOIntensity = 0; gOptions.ShowAOOnly = false; gOptions.ShowSSGIOnly = false;
    gOptions.BloomEyeAdaptation = false; gOptions.UseAtmosphericShadowing = false;
    gOptions.UseVRS = false; gOptions.ShowVRSArea = false; gOptions.ShowGrid = false;
    gOptions.ShowDebugObject = false; gOptions.ShowDebugRT = false; gOptions.EarthQuake = false;
}

struct Run
{
    fs::path Output;
    Json Report{{"schemaVersion", 1}, {"test", "scene-feature-gpu-integration"},
        {"checks", Json::array()}, {"captures", Json::object()}, {"profiles", Json::object()}};
    uint32_t Frames = 0;
    int Failures = 0;
    std::optional<float> FrozenPlayTime;

    void Progress()
    {
        if (Output.empty()) return;
        Report["renderedFrames"] = Frames; Report["failureCount"] = Failures;
        WriteText(Output / "scene-features-progress.json", Report.dump(2) + "\n");
    }
    void Expect(bool passed, const std::string& name, const Json& detail = Json::object())
    {
        Report["checks"].push_back({{"name", name}, {"pass", passed}, {"frame", Frames}, {"detail", detail}});
        Failures += !passed;
        std::cout << (passed ? "PASS " : "FAIL ") << "scene features: " << name << std::endl;
        Progress();
    }
    double Frame()
    {
        if (Frames >= FrameBudget) throw std::runtime_error("Scene feature harness exceeded 400 frames");
        if (!g_Engine || !g_rhi || !g_Editor) throw std::runtime_error("Engine/RHI/editor lifetime ended");
        const auto start = Clock::now();
        ResetAllKeyState(); ResetMouseAllState();
        // Only explicit water seek snapshots freeze simulation. Normal A/B
        // measurements use the same fixed 1/60 update as existing GPU tests.
        g_timeDeltaSecond = FrozenPlayTime ? 0.0f : 1.0f / 60.0f;
        if (FrozenPlayTime && !g_Editor->SetPlayTime(*FrozenPlayTime))
            throw std::runtime_error("Could not seek the authored water timeline");
        g_Engine->ProcessInput(g_timeDeltaSecond);
        g_rhi->IncrementFrameNumber();
        g_Engine->Update(g_timeDeltaSecond);
        g_Engine->Draw();
        g_Engine->EndOfFrame();
        ++Frames;
#ifdef _WIN32
        if (IsUseDX12())
        {
            MSG message{};
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) throw std::runtime_error("Native window closed during test");
                TranslateMessage(&message); DispatchMessage(&message);
            }
        }
        else
#endif
        {
            auto* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
            if (!window || glfwWindowShouldClose(window)) throw std::runtime_error("GLFW window closed during test");
            glfwPollEvents();
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    void Warm(uint32_t count) { while (count--) Frame(); }
    void Measure(const std::string& name, uint32_t warmup = 4, uint32_t measured = 6)
    {
        Warm(warmup);
        std::vector<double> times;
        for (uint32_t i = 0; i < measured; ++i) times.push_back(Frame());
        auto sorted = times; std::sort(sorted.begin(), sorted.end());
        double sum = 0; for (double value : times) sum += value;
        Report["profiles"][name] = {{"warmupFramesExcluded", warmup}, {"measuredFrames", measured},
            {"frameCpuMilliseconds", times}, {"meanMilliseconds", sum / times.size()},
            {"medianMilliseconds", (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) * 0.5},
            {"maximumMilliseconds", sorted.back()}, {"simulationDeltaSeconds", FrozenPlayTime ? 0.0 : 1.0 / 60.0},
            {"scope", "Owner-thread ProcessInput, Update (including visibility), Draw, EndOfFrame/present and event polling; excludes PNG/readback and explicit setup/warmup. This is total CPU frame latency, not GPU-only timing."}};
        Progress();
    }
    Json Capture(const std::string& name)
    {
        const auto path = Output / (name + ".png");
        auto pixels = Json::parse(CaptureSceneFeaturePixels(jEditorAssetPath::ToUtf8(path).c_str()));
        bool finiteMean = pixels.contains("meanRgb") && pixels["meanRgb"].is_array() && pixels["meanRgb"].size() == 3;
        if (finiteMean) for (const auto& channel : pixels["meanRgb"])
            finiteMean = finiteMean && channel.is_number() && std::isfinite(channel.get<double>());
        const bool hashPresent = pixels.contains("pixelHash") && pixels["pixelHash"].is_string()
            && !pixels["pixelHash"].get<std::string>().empty();
        Expect(pixels.value("readback", false) && pixels.value("pngSaved", false) &&
            pixels.value("width", 0) > 0 && pixels.value("height", 0) > 0 && pixels.value("litPixels", 0u) > 4 &&
            finiteMean && hashPresent, name + " has actual nonblack finite scene pixels and an owned PNG", pixels);
        Report["captures"][name] = pixels;
        Progress();
        return pixels;
    }
    void Different(const Json& a, const Json& b, const std::string& name)
    {
        const bool comparable = a.value("readback", false) && b.value("readback", false) &&
            a.value("width", 0) == b.value("width", 0) && a.value("height", 0) == b.value("height", 0)
            && a.contains("pixelHash") && b.contains("pixelHash");
        Expect(comparable && a["pixelHash"] != b["pixelHash"], name,
            {{"beforePixelHash", a.value("pixelHash", std::string{})}, {"afterPixelHash", b.value("pixelHash", std::string{})}});
    }
};

unsigned char Channel(float value)
{ return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); }

void SavePng(const fs::path& path, const std::vector<unsigned char>& bytes, uint32_t width, uint32_t height)
{
    if (bytes.size() != size_t(width) * height * 4 ||
        LodePNG_encode32_file(jEditorAssetPath::ToUtf8(path).c_str(), bytes.data(), width, height))
        throw std::runtime_error("Could not save owned texture fixture: " + path.generic_string());
}

jEnvironmentLighting::Coefficients MakeAssets(const fs::path& assets, Run& run)
{
    fs::create_directories(assets);
    constexpr uint32_t size = 32;
    std::vector<unsigned char> color(size * size * 4), height(color.size()), room(color.size());
    std::vector<float> heights(size * size);
    for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x)
    {
        const auto i = size_t(y) * size + x, p = i * 4;
        const bool checker = ((x / 4 + y / 4) & 1) != 0;
        color[p] = checker ? 220 : 35; color[p + 1] = checker ? 65 : 190;
        color[p + 2] = static_cast<unsigned char>(40 + 5 * y); color[p + 3] = 255;
        const float ridge = (x % 12 >= 4 && x % 12 <= 7) ? 0.9f : 0.12f;
        heights[i] = ridge;
        height[p] = Channel(ridge); height[p + 1] = 255; height[p + 2] = height[p]; height[p + 3] = 255;
        const bool frame = x < 3 || y < 3 || x >= size - 3 || y >= size - 3;
        room[p] = frame ? 35 : static_cast<unsigned char>(35 + x * 6);
        room[p + 1] = frame ? 30 : static_cast<unsigned char>(210 - y * 4);
        room[p + 2] = frame ? 45 : 200; room[p + 3] = 255;
    }
    SavePng(assets / "color.png", color, size, size);
    SavePng(assets / "height.png", height, size, size);
    SavePng(assets / "room.png", room, size, size);

    jHorizonMapping::HorizonMap horizon;
    jHorizonMapping::EncodedLayers encoded;
    std::string error;
    jHorizonMapping::HeightFieldView field{size, size, heights.data(), heights.size(), 5.0f, 1.0f, 1.0f};
    jHorizonMapping::BakeSettings settings; settings.RadiusTexels = 8; settings.Edges = jHorizonMapping::Boundary::Wrap;
    if (!jHorizonMapping::Bake(field, settings, horizon, error) ||
        !jHorizonMapping::EncodeLayers(horizon, encoded, error)) throw std::runtime_error(error);
    std::vector<unsigned char> layer0(color.size()), layer1(color.size()), ambient(color.size());
    for (size_t i = 0; i < heights.size(); ++i)
        for (size_t c = 0; c < 4; ++c)
        {
            layer0[i * 4 + c] = Channel(encoded.Layer0[i][c]);
            layer1[i * 4 + c] = Channel(encoded.Layer1[i][c]);
            ambient[i * 4 + c] = c == 3 ? 255 : Channel(encoded.AmbientVisibility[i]);
        }
    SavePng(assets / "horizon0.png", layer0, size, size);
    SavePng(assets / "horizon1.png", layer1, size, size);
    SavePng(assets / "horizon-ao.png", ambient, size, size);
    const auto minimumAO = *std::min_element(encoded.AmbientVisibility.begin(), encoded.AmbientVisibility.end());
    run.Expect(minimumAO < 0.9f, "the public Horizon baker produces nontrivial directional layers and AO",
        {{"size", {size, size}}, {"radiusTexels", settings.RadiusTexels}, {"minimumAO", minimumAO}});

    std::vector<jEnvironmentLighting::RGB> radiance(32 * 16);
    for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 32; ++x)
        radiance[y * 32 + x] = {0.18f + float(x) / 200.0f, 0.24f + float(y) / 180.0f, 0.42f};
    jEnvironmentLighting::Coefficients coefficients, reopened;
    if (!jEnvironmentLighting::ProjectEquirectangular({32, 16, radiance.data(), radiance.size()}, coefficients, error) ||
        !jEnvironmentLighting::SaveAsset(assets / "environment.jsh", coefficients, error) ||
        !jEnvironmentLighting::LoadAsset(assets / "environment.jsh", reopened, error)) throw std::runtime_error(error);
    run.Expect(coefficients == reopened, "the public HDR-to-SH bake and .jsh load preserve radiance coefficients");
    return coefficients;
}

MaterialNode Texture(uint32_t id, const char* file, jMaterialMapping::TextureSemantic semantic = jMaterialMapping::TextureSemantic::Linear)
{
    MaterialNode node; node.Id = id; node.Kind = NodeKind::TextureObject;
    node.Texture = std::string("Assets/") + file; node.Semantic = semantic; return node;
}

MaterialGraph Mapping(NodeKind kind, bool enabled, bool dual = false)
{
    MaterialGraph graph; graph.Version = 2;
    MaterialNode uv; uv.Id = 1; uv.Kind = NodeKind::UV; uv.Parameters = {1, 1, 0, 0};
    graph.Nodes = {uv, Texture(2, kind == NodeKind::Interior ? "room.png" : "color.png", jMaterialMapping::TextureSemantic::Color),
        Texture(3, "height.png")};
    MaterialNode mapping; mapping.Id = 4; mapping.Kind = kind; mapping.InputA = 1; mapping.InputB = 3;
    mapping.Parameters = {enabled ? 0.18f : 0.0f, 32, kind == NodeKind::Parallax ? 0.0f : 6.0f, dual ? 1.0f : 0.0f};
    if (kind == NodeKind::Interior)
    {
        mapping.InputB = 2; mapping.InputC = dual ? 3 : 0;
        mapping.Parameters = {3, 3, enabled ? 1.2f : 0.01f, 1};
        graph.Nodes.push_back(mapping); graph.BaseColor = 4;
    }
    else
    {
        graph.Nodes.push_back(mapping);
        MaterialNode sample; sample.Id = 5; sample.Kind = NodeKind::TextureSample; sample.InputA = 2; sample.InputB = 4;
        graph.Nodes.push_back(sample); graph.BaseColor = 5;
    }
    return graph;
}

MaterialGraph Horizon(bool enabled)
{
    auto graph = Mapping(NodeKind::POM, false);
    graph.Nodes.push_back(Texture(6, "horizon0.png")); graph.Nodes.push_back(Texture(7, "horizon1.png"));
    graph.Nodes.push_back(Texture(8, "horizon-ao.png"));
    MaterialNode horizon; horizon.Id = 9; horizon.Kind = NodeKind::Horizon;
    horizon.InputA = 1; horizon.InputB = 6; horizon.InputC = 7; horizon.InputD = 8;
    horizon.Parameters = {0.05f, enabled ? 1.0f : 0.0f, enabled ? 1.0f : 0.0f, 0};
    graph.Nodes.push_back(horizon); graph.Horizon = 9;
    return graph;
}

void SaveGraph(const fs::path& root, const std::string& relative, const MaterialGraph& graph, Run& run)
{
    std::string error; MaterialGraph loaded;
    const auto path = root / relative;
    if (!SaveMaterial(graph, path, error) || !LoadMaterial(path, loaded, error)) throw std::runtime_error(error);
    const auto copy = path.parent_path() / (path.stem().string() + "-roundtrip.jmaterial");
    if (!SaveMaterial(loaded, copy, error)) throw std::runtime_error(error);
    auto read = [](const fs::path& file)
    { std::ifstream in(file, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); };
    run.Expect(read(path) == read(copy), relative + " has an exact graph Save/Load round trip");
}

jRenderObject* FirstMesh(jEditor& editor, EntityId id)
{
    auto* object = editor.ObjectForEntity(id);
    return object && !object->RenderObjects.empty() ? object->RenderObjects.front() : nullptr;
}

uint64_t ActualGBufferShaderHash(jEditor& editor, EntityId id)
{
    const auto* mesh = FirstMesh(editor, id);
    if (!mesh || !mesh->GeometryDataPtr || !mesh->MaterialPtr) return 0;
    const auto* material = mesh->MaterialPtr.get();
    jShaderGBufferPixelShader::ShaderPermutation permutation;
    permutation.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(mesh->GeometryDataPtr->HasVertexColor());
    permutation.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(material->HasAlbedoTexture());
    permutation.SetIndex<jShaderGBufferPixelShader::USE_SRGB_ALBEDO_TEXTURE>(material->IsUseSRGBAlbedoTexture());
    permutation.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
    permutation.SetIndex<jShaderGBufferPixelShader::USE_PBR>(ENABLE_PBR);
    auto* shader = ResolveMaterialMappingShader(jShaderGBufferPixelShader::CreateShader(permutation), material);
    return shader ? shader->ShaderInfo.GetHash() : 0;
}

void WaitForMaterial(jEditor& editor, EntityId id, const jMaterialMapping::Program& expected, Run& run)
{
    // Material decoding is asynchronous and keeps the old live material until
    // its complete replacement is ready. Shader equality alone is insufficient:
    // the parameter A/B cases intentionally have identical generated code.
    for (uint32_t attempt = 0; attempt <= 12; ++attempt)
    {
        const auto* mesh = FirstMesh(editor, id);
        const auto material = mesh ? mesh->MaterialPtr : nullptr;
        const auto program = material ? material->MappingProgram : nullptr;
        const bool published = material && (expected.Active
            ? program && program->Active && program->CodeHash == expected.CodeHash && program->Values == expected.Values
            : !program || !program->Active);
        if (editor.PendingImportCount() == 0 && published) return;
        if (attempt != 12) run.Frame();
    }
    throw std::runtime_error("Bounded material publication wait failed: " + editor.ImportStatus() + " / " + editor.LastError());
}

void Update(jEditor& editor, EntityId id, const std::function<void(Entity&)>& change)
{
    const auto* previous = editor.GetDocument().Find(id);
    if (!previous) throw std::runtime_error("Fixture entity disappeared");
    auto entity = *previous; change(entity);
    if (!editor.GetDocument().Update(entity, "Scene feature fixture"))
        throw std::runtime_error(editor.GetDocument().LastError());
}
}
#endif

int RunSceneFeatureIntegrationGpuTests(const char* outputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    Run run; RestoreFields restore;
    const auto oldKeys = g_KeyState;
    const auto oldMouse = g_MouseState;
    const float oldDelta = g_timeDeltaSecond;
    const bool oldWorkspace = IsAuthoringWorkspaceVisible();
    const bool oldRuntimeMode = IsAuthoringRuntimeMode();
    const bool oldAuthoring = g_Editor && g_Editor->IsAuthoringEnabled();
    bool playing = false;
    try
    {
        if (!outputDirectory || !*outputDirectory || !g_Engine || !g_rhi || !g_Editor || !ImGui::GetCurrentContext())
            throw std::runtime_error("Scene feature test requires initialized real engine/RHI/editor and owned output");
        run.Output = fs::absolute(jEditorAssetPath::FromUtf8(outputDirectory)); fs::create_directories(run.Output);
        run.Report["status"] = "running"; run.Report["rhi"] = g_rhi->GetRHIName().ToStr();
        run.Report["frameBudget"] = FrameBudget;
        run.Report["validationScope"] = "Actual native validation/Metal diagnostics are collected by the external caller; this harness does not infer zero errors from a successful screenshot.";
        run.Report["timingScope"] = "CPU total-frame latency includes visibility and present; shader/pipeline warmup, captures/readback and asset/project preparation are excluded from measured samples. No GPU-only/FPS or speedup claim.";
        auto& editor = *g_Editor; auto& game = g_Engine->Game;
        if (!game.WaitForSceneImportForTesting()) throw std::runtime_error("Startup import did not finish before fixture replacement");
        ControlledOptions(restore);
        editor.CancelPendingImports(); editor.ResetWorldBindings(false); game.ClearImportedSceneForAuthoring();
        if (!editor.GetDocument().NewScene("Scene feature integration")) throw std::runtime_error(editor.GetDocument().LastError());
        const auto fixture = run.Output / ("fixture-" + std::to_string(Clock::now().time_since_epoch().count()));
        jEditorProject project; std::string error;
        if (!project.Create(fixture, "Scene feature modules", error)) throw std::runtime_error(error);
        const auto coefficients = MakeAssets(fixture / "Assets", run);
        Document empty;
        if (!empty.Save(fixture / "Scenes/start.jscene")) throw std::runtime_error(empty.LastError());
        project.EditConfig().StartupScene = "Scenes/start.jscene";
        project.EditConfig().Settings["render.deferred"] = "true";
        project.EditConfig().Settings["render.surfelgi"] = "false";
        project.EditConfig().Settings["render.ssgi"] = "false";
        project.EditConfig().Settings["render.environment.shAsset"] = "Assets/environment.jsh";
        if (!project.Save(error) || !OpenAuthoringProject(editor, project.GetProjectFile(), error)) throw std::runtime_error(error);
        restore.Remember(editor.Placement.EnablePlacementMode); editor.Placement.EnablePlacementMode = true;
        SetAuthoringRuntimeMode(false); SetAuthoringWorkspaceVisible(true); editor.SetAuthoringEnabled(true);
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Deferred, error)) throw std::runtime_error(error);
        auto& document = editor.GetDocument();
        if (!document.Entities().empty()) throw std::runtime_error("Fixture project did not begin from an empty document");
        const auto floor = editor.CreateEntity(EntityKind::Terrain, "Reflector and mapping surface");
        const auto cube = editor.CreateEntity(EntityKind::Cube, "Red reflection source");
        const auto blueCube = editor.CreateEntity(EntityKind::Cube, "Blue reflection source");
        const auto sun = editor.CreateEntity(EntityKind::DirectionalLight, "Primary sun");
        const auto point = editor.CreateEntity(EntityKind::PointLight, "Water point shadow light");
        const auto cameraEntity = editor.CreateEntity(EntityKind::Camera, "Fixture camera");
        if (!floor || !cube || !blueCube || !sun || !point || !cameraEntity)
            throw std::runtime_error(document.LastError());
        Update(editor, floor, [](Entity& e) {
            e.Local = {}; // CreateEntity otherwise places it near the startup camera.
            e.Terrain.Resolution = 33; e.Terrain.SizeX = 650; e.Terrain.SizeZ = 700;
            e.Material.OverrideEnabled = true; e.Material.BaseColor = {0.38f, 0.42f, 0.46f}; e.Material.Roughness = 0.08f; e.Material.Metallic = 0.2f;
        });
        Update(editor, cube, [](Entity& e) {
            e.Local.Position = {35, 90, 100}; e.Local.Scale = {4, 4, 4};
            e.Material.OverrideEnabled = true; e.Material.BaseColor = {0.9f, 0.08f, 0.025f}; e.Material.Emissive = {0.04f, 0, 0}; e.Material.Roughness = 0.3f;
        });
        Update(editor, blueCube, [](Entity& e) {
            e.Local.Position = {-115, 65, 35}; e.Local.Scale = {3, 3, 3};
            e.Material.OverrideEnabled = true; e.Material.BaseColor = {0.035f, 0.16f, 0.9f}; e.Material.Roughness = 0.3f;
        });
        Update(editor, sun, [](Entity& e) { e.Local.RotationDegrees = {55, -20, 0}; e.Light.Intensity = 1.5f; e.Light.CastShadows = true; });
        Update(editor, point, [](Entity& e) {
            e.Local.Position = {-150, 180, -130}; e.Light.Color = {0.25f, 0.55f, 1};
            e.Light.Intensity = 1800; e.Light.Range = 1200; e.Light.CastShadows = true;
        });
        const float pitch = RadianToDegree(std::atan2(160.0f, 460.0f));
        Update(editor, cameraEntity, [&](Entity& e) {
            e.Local.Position = {0, 190, -420}; e.Local.RotationDegrees = {pitch, 0, 0};
            e.Camera.Primary = true; e.Camera.NearPlane = 1; e.Camera.FarPlane = 5000; e.Camera.VerticalFovDegrees = 60;
        });
        auto* camera = game.MainCamera;
        if (!camera) throw std::runtime_error("Fixture requires a main camera");
        jCamera::SetCamera(camera, Vector(0, 190, -420), Vector(0, 30, 40), Vector(0, 191, -420),
            DegreeToRadian(60), 1, 5000, SCR_WIDTH, SCR_HEIGHT, true, 487);
        camera->UpdateCamera(); editor.SetSelectedEntities({}); run.Warm(4);
        run.Expect(game.GetActiveSceneRenderPipeline() == jGame::ESceneRenderPipeline::Deferred &&
            FirstMesh(editor, floor) && FirstMesh(editor, cube), "fresh authored fixture owns actual raster geometry");
        run.Report["fixtureProject"] = project.GetProjectFile().generic_string();
        run.Report["fixtureCamera"] = {{"position", {0, 190, -420}}, {"target", {0, 30, 40}}, {"fovDegrees", 60}};

        run.Measure("deferred-features-off"); const auto base = run.Capture("deferred-features-off");
        gOptions.SceneFeatures.Environment.Enabled = true; gOptions.SceneFeatures.Environment.Radiance = coefficients;
        run.Measure("deferred-sh-on"); const auto sh = run.Capture("deferred-sh-on");
        run.Different(base, sh, "SH diffuse environment changes actual Deferred pixels with AO/GI disabled");

        // A valid allocated GI target is not a lighting contribution when its
        // intensity is zero. Exercise the production composition decision.
        const bool temporalSSGI = gOptions.UseSSGITemporalAccumulation;
        gOptions.UseSSGI = true; gOptions.SSGIIntensity = 0;
        gOptions.UseSSGITemporalAccumulation = false;
        run.Warm(4); const auto zeroGIWithSH = run.Capture("zero-intensity-gi-sh-on");
        run.Expect(bool(jSceneRenderTarget::SSGI_RT), "zero-intensity GI control owns an actual resolved target");
        gOptions.SceneFeatures.Environment.Enabled = false;
        run.Warm(4); const auto zeroGIWithoutSH = run.Capture("zero-intensity-gi-sh-off");
        run.Different(zeroGIWithoutSH, zeroGIWithSH, "zero-intensity GI leaves SH environment fallback active");
        gOptions.UseSSGI = false; gOptions.UseSSGITemporalAccumulation = temporalSSGI;

        gOptions.SceneFeatures.Environment.Enabled = false;
        gOptions.SceneFeatures.Reflection.Enabled = true; gOptions.SceneFeatures.Reflection.PlaneThickness = 2;
        gOptions.SceneFeatures.Reflection.RoughnessThreshold = 0.4f; gOptions.SceneFeatures.Reflection.Intensity = 2;
        run.Measure("ppr-project-up"); const auto projected = run.Capture("ppr-project-up");
        run.Different(base, projected, "PPR clear/projection/resolve changes the actual reflector scene");
        gOptions.SceneFeatures.Reflection.Plane = {0, -1, 0, 0};
        run.Measure("ppr-empty-projection-fallback"); const auto fallback = run.Capture("ppr-empty-projection-fallback");
        run.Different(fallback, projected, "PPR projected geometry changes pixels beyond the identical offscreen fallback");
        run.Report["pprControl"] = "Flipping the plane preserves receiver abs-distance/normal mask and environment fallback while rejecting all above-floor source geometry; the pixel difference isolates projection contribution.";

        gOptions.SceneFeatures.Reflection.Enabled = false;
        gOptions.SceneFeatures.Atmosphere.Enabled = true; gOptions.SceneFeatures.Atmosphere.UseSceneDirectionalLight = false;
        gOptions.SceneFeatures.Atmosphere.SunDirection = {0.2f, 0.65f, 0.3f};
        run.Measure("atmosphere-on"); const auto atmosphere = run.Capture("atmosphere-on");
        run.Different(base, atmosphere, "Rayleigh/Mie sky and surface transmittance change actual scene pixels");
        gOptions.SceneFeatures.Atmosphere.Enabled = false;

        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Forward, error)) throw std::runtime_error(error);
        run.Measure("forward-features-off"); const auto forward = run.Capture("forward-features-off");
        gOptions.SceneFeatures.Environment.Enabled = true;
        run.Measure("forward-sh-on"); const auto forwardSH = run.Capture("forward-sh-on");
        run.Different(forward, forwardSH, "the same SH coefficients also change the existing Forward surface shader");
        run.Expect(jSceneFeatures::ReflectionUnavailableReason(true, false) != nullptr,
            "Forward exposes the real PPR GBuffer limitation");
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Deferred, error)) throw std::runtime_error(error);
        run.Warm(3);

        MaterialGraph legacy; MaterialNode constant; constant.Id = 1; constant.Kind = NodeKind::Color; constant.Color = {0.25f, 0.4f, 0.7f};
        legacy.Nodes = {constant}; legacy.BaseColor = 1;
        SaveGraph(fixture, "Assets/legacy.jmaterial", legacy, run);
        if (!AssignSharedMaterial(editor, floor, "Assets/legacy.jmaterial", error)) throw std::runtime_error(error);
        jMaterialMapping::Program legacyProgram;
        if (!jMaterialMapping::Compile(legacy, legacyProgram, error)) throw std::runtime_error(error);
        WaitForMaterial(editor, floor, legacyProgram, run);
        run.Warm(3);
        const auto* legacyMesh = FirstMesh(editor, floor);
        run.Expect(legacyMesh && legacyMesh->MaterialPtr && !legacyMesh->MaterialPtr->MappingProgram,
            "v1 constant materials retain the actual default raster shader path");
        run.Capture("legacy-v1-default");

        const auto expectRasterOnly = [&](const char* name)
        {
            std::string reason;
            const bool permitted = game.CanSwitchSceneRenderPipeline(jGame::ESceneRenderPipeline::PathTracing, reason);
            run.Expect(!permitted && !reason.empty() &&
                game.GetActiveSceneRenderPipeline() == jGame::ESceneRenderPipeline::Deferred,
                name, {{"reason", reason}, {"fullRaytracingSupported", GSupportRaytracing}});
        };

        struct Recipe { const char* Name; NodeKind Kind; bool Dual; };
        for (const auto& recipe : {Recipe{"parallax", NodeKind::Parallax, false}, Recipe{"pom", NodeKind::POM, false},
            Recipe{"relief", NodeKind::Relief, true}, Recipe{"interior", NodeKind::Interior, false},
            Recipe{"interior-dual", NodeKind::Interior, true}, Recipe{"horizon", NodeKind::Horizon, false}})
        {
            const auto disabled = recipe.Kind == NodeKind::Horizon ? Horizon(false) : Mapping(recipe.Kind, false, recipe.Dual);
            const auto enabled = recipe.Kind == NodeKind::Horizon ? Horizon(true) : Mapping(recipe.Kind, true, recipe.Dual);
            jMaterialMapping::Program a, b;
            if (!jMaterialMapping::Compile(disabled, a, error) || !jMaterialMapping::Compile(enabled, b, error)) throw std::runtime_error(error);
            run.Expect(a.Active && b.Active && a.CodeHash == b.CodeHash && a.Hlsl == b.Hlsl && a.Values != b.Values,
                std::string(recipe.Name) + " keeps generated shader code/hash stable while instance parameters change");
            const std::string offFile = std::string("Assets/") + recipe.Name + "-off.jmaterial";
            const std::string onFile = std::string("Assets/") + recipe.Name + "-on.jmaterial";
            SaveGraph(fixture, offFile, disabled, run); SaveGraph(fixture, onFile, enabled, run);
            if (!AssignSharedMaterial(editor, floor, offFile, error)) throw std::runtime_error(error);
            WaitForMaterial(editor, floor, a, run);
            run.Measure(std::string(recipe.Name) + "-off", 3, 4);
            const auto before = run.Capture(std::string(recipe.Name) + "-off");
            const auto offShader = ActualGBufferShaderHash(editor, floor);
            if (!AssignSharedMaterial(editor, floor, onFile, error)) throw std::runtime_error(error);
            WaitForMaterial(editor, floor, b, run);
            run.Measure(std::string(recipe.Name) + "-on", 3, 4);
            const auto after = run.Capture(std::string(recipe.Name) + "-on");
            const auto onShader = ActualGBufferShaderHash(editor, floor);
            run.Expect(offShader != 0 && offShader == onShader,
                std::string(recipe.Name) + " reuses the actual RHI material shader variant hash",
                {{"beforeShaderHash", offShader}, {"afterShaderHash", onShader}});
            const auto* mesh = FirstMesh(editor, floor);
            run.Expect(mesh && mesh->MaterialPtr && mesh->MaterialPtr->MappingProgram && mesh->MaterialPtr->MappingProgram->Active,
                std::string(recipe.Name) + " is published as a real material GPU program");
            run.Different(before, after, std::string(recipe.Name) + " parameters change actual generated-material pixels");
            if (recipe.Kind == NodeKind::Parallax)
                expectRasterOnly("generated mapping prevents PT selection with an explicit capability reason");
            if (!document.Save(fixture / "Scenes" / (std::string(recipe.Name) + ".jscene"), false))
                throw std::runtime_error(document.LastError());
        }

        // Exercise the generated-material binding path in Forward too; its
        // material group follows the three fixed light groups.
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Forward, error)) throw std::runtime_error(error);
        const auto forwardPomOff = Mapping(NodeKind::POM, false);
        const auto forwardPomOn = Mapping(NodeKind::POM, true);
        jMaterialMapping::Program forwardProgramOff, forwardProgramOn;
        if (!jMaterialMapping::Compile(forwardPomOff, forwardProgramOff, error) ||
            !jMaterialMapping::Compile(forwardPomOn, forwardProgramOn, error)) throw std::runtime_error(error);
        if (!AssignSharedMaterial(editor, floor, "Assets/pom-off.jmaterial", error)) throw std::runtime_error(error);
        WaitForMaterial(editor, floor, forwardProgramOff, run);
        run.Measure("forward-pom-off", 3, 4); const auto forwardMappingOff = run.Capture("forward-pom-off");
        if (!AssignSharedMaterial(editor, floor, "Assets/pom-on.jmaterial", error)) throw std::runtime_error(error);
        WaitForMaterial(editor, floor, forwardProgramOn, run);
        run.Measure("forward-pom-on", 3, 4); const auto forwardMappingOn = run.Capture("forward-pom-on");
        run.Different(forwardMappingOff, forwardMappingOn, "Forward evaluates the generated POM material with its actual light/material binding layout");
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Deferred, error)) throw std::runtime_error(error);

        // Same authored scene and camera, explicit frozen timeline seeks. The
        // static floor is hidden in both captures to expose wave troughs too.
        // Remove mapping before the water gate assertion to isolate that reason.
        if (!AssignSharedMaterial(editor, floor, "Assets/legacy.jmaterial", error)) throw std::runtime_error(error);
        WaitForMaterial(editor, floor, legacyProgram, run);
        run.Warm(2);
        Update(editor, floor, [](Entity& e) { e.Visible = false; });
        const auto water = editor.CreateEntity(EntityKind::Water, "Animated heightfield water");
        if (!water) throw std::runtime_error(document.LastError());
        Update(editor, water, [](Entity& e) {
            e.Local.Position = {0, 5, -70}; e.Water.Resolution = 33; e.Water.SizeX = 400; e.Water.SizeZ = 360;
            e.Material.OverrideEnabled = true; e.Material.BaseColor = {0.045f, 0.24f, 0.65f}; e.Material.Roughness = 0.18f;
        });
        // Later draw order must not make an occluded object win the ID pass.
        jWaterSimulation::SurfaceSample controlSample;
        if (!jWaterSimulation::Evaluate(document.Find(water)->Water.Waves, {0, -110}, 1.5, controlSample, error))
            throw std::runtime_error(error);
        const Vector controlSurface(controlSample.Position[0], controlSample.Position[1] + 5, controlSample.Position[2] - 70);
        const Vector behindWater = controlSurface + (controlSurface - camera->Pos) * .35f;
        const auto occludedControl = editor.CreateEntity(EntityKind::Cube, "Occluded pick control");
        if (!occludedControl) throw std::runtime_error(document.LastError());
        Update(editor, occludedControl, [&](Entity& e) {
            e.Local.Position = {behindWater.x, behindWater.y, behindWater.z}; e.Local.Scale = {.4f, .4f, .4f};
            e.Material.OverrideEnabled = true; e.Material.BaseColor = {1, 0, 1};
        });
        run.Report["occludedPickControl"] = {{"entity", occludedControl}, {"createdAfterWater", true},
            {"worldPosition", {behindWater.x, behindWater.y, behindWater.z}}};
        editor.SetSelectedEntities({}); run.Warm(2);
        expectRasterOnly("enabled water prevents PT selection with an explicit capability reason");
        if (!editor.BeginPlay()) throw std::runtime_error(editor.LastError());
        playing = true; run.FrozenPlayTime = 0.0f;
        run.Measure("water-time-zero"); const auto water0 = run.Capture("water-time-zero");
        const auto authoredWaterScene = document.ToJson();
        const auto* firstWater = FirstMesh(editor, water);
        const auto waterGeometry = firstWater ? firstWater->GeometryDataPtr : nullptr;
        run.FrozenPlayTime = 1.5f;
        run.Measure("water-seek-1_5"); const auto water1 = run.Capture("water-seek-1_5");
        run.Different(water0, water1, "water timeline seek changes the actual deformed raster surface");
        const auto* posedWater = FirstMesh(editor, water);
        run.Expect(posedWater && posedWater->GeometryDataPtr == waterGeometry && posedWater->MaterialPtr &&
            posedWater->MaterialPtr->Water && std::abs(posedWater->MaterialPtr->Water->TimeSeconds - 1.5) < 1.0e-5 &&
            document.ToJson() == authoredWaterScene,
            "water seek updates the live GPU material clock without rewriting authored data or rebuilding mesh geometry");

        jWaterSimulation::SurfaceSample sample;
        if (!jWaterSimulation::Evaluate(document.Find(water)->Water.Waves, {0, -110}, 1.5, sample, error)) throw std::runtime_error(error);
        const Vector world(sample.Position[0], sample.Position[1] + 5, sample.Position[2] - 70);
        const auto clip = camera->ViewProjection.Transform(Vector4(world, 1));
        const bool inside = clip.w > 0 && std::abs(clip.x / clip.w) < 1 && std::abs(clip.y / clip.w) < 1;
        run.Expect(inside, "the CPU water query projects inside the real picking viewport");
        if (inside)
        {
            editor.Placement.PickMouseX = int((clip.x / clip.w * 0.5f + 0.5f) * SCR_WIDTH);
            editor.Placement.PickMouseY = int((0.5f - clip.y / clip.w * 0.5f) * SCR_HEIGHT);
            editor.Placement.bPickRequested = true; run.Frame();
            run.Expect(!editor.Placement.bPickRequested && editor.Placement.PickedObject &&
                editor.EntityForObject(editor.Placement.PickedObject) == water,
                "GPU picking hits the water at its CPU-predicted displaced surface");
            run.Report["pickTargets"]["deferred"] = Json::parse(CaptureSceneFeatureTexturePixels(
                jSceneRenderTarget::HitObject_RT->GetTexturePtr(), (run.Output / "deferred-object-ids.png").string().c_str()));
        }
        run.Expect(editor.LightForEntity(point) && editor.LightForEntity(sun) && document.Find(point)->Light.CastShadows,
            "water captures exercise authored point and directional shadow lights");
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Forward, error)) throw std::runtime_error(error);
        run.FrozenPlayTime = 0.0f;
        run.Measure("forward-water-time-zero", 3, 4); const auto forwardWater0 = run.Capture("forward-water-time-zero");
        run.FrozenPlayTime = 1.5f;
        run.Measure("forward-water-seek-1_5", 3, 4); const auto forwardWater1 = run.Capture("forward-water-seek-1_5");
        run.Different(forwardWater0, forwardWater1, "Forward uses the same animated water deformation");
        if (inside)
        {
            const auto currentClip = camera->ViewProjection.Transform(Vector4(world, 1));
            editor.Placement.PickMouseX = int((currentClip.x / currentClip.w * 0.5f + 0.5f) * SCR_WIDTH);
            editor.Placement.PickMouseY = int((0.5f - currentClip.y / currentClip.w * 0.5f) * SCR_HEIGHT);
            editor.Placement.bPickRequested = true; run.Frame();
            run.Expect(!editor.Placement.bPickRequested && editor.Placement.PickedObject &&
                editor.EntityForObject(editor.Placement.PickedObject) == water,
                "Forward water picking keeps the fixed view/object/material layout despite its lighting groups",
                {{"requestConsumed", !editor.Placement.bPickRequested},
                 {"pickedEntity", editor.Placement.PickedObject ? editor.EntityForObject(editor.Placement.PickedObject) : 0},
                 {"expectedEntity", water}, {"pixel", {editor.Placement.PickMouseX, editor.Placement.PickMouseY}},
                 {"originalClip", {clip.x, clip.y, clip.w}}, {"currentClip", {currentClip.x, currentClip.y, currentClip.w}}});
            run.Report["pickTargets"]["forward"] = Json::parse(CaptureSceneFeatureTexturePixels(
                jSceneRenderTarget::HitObject_RT->GetTexturePtr(), (run.Output / "forward-object-ids.png").string().c_str()));
        }
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Deferred, error)) throw std::runtime_error(error);
        run.Warm(2);
        run.Report["waterSeek"] = {{"requestedSeconds", {0.0, 1.5}}, {"simulationDeltaSeconds", 0.0},
            {"scope", "Explicit frozen timeline snapshots; raster deformation/shadow/pick paths, not displaced BLAS/path tracing or running-game simulation timing."}};
        run.FrozenPlayTime.reset();
        if (!editor.StopPlay()) throw std::runtime_error(editor.LastError());
        playing = false; run.Warm(2);
        Update(editor, floor, [](Entity& e) { e.Visible = true; });
        Update(editor, water, [](Entity& e) { e.Visible = false; });
        if (!document.Save(fixture / "Scenes/scene-features.jscene", false)) throw std::runtime_error(document.LastError());
        project.EditConfig().StartupScene = "Scenes/scene-features.jscene";
        project.EditConfig().Settings["render.environment.enabled"] = "true";
        project.EditConfig().Settings["render.environment.intensity"] = "1";
        if (!project.Save(error)) throw std::runtime_error(error);
        run.Report["reopen"] = {{"project", project.GetProjectFile().generic_string()},
            {"startupScene", project.GetConfig().StartupScene}, {"mappingExamples", "Scenes/*.jscene"},
            {"water", "Select Animated heightfield water, enable visibility and Play/seek; snapshots and CPU query use the same wave settings."}};
        run.Expect(run.Frames <= FrameBudget, "the actual GPU workflow stays within its 400-frame budget");
    }
    catch (const std::exception& exception)
    {
        ++run.Failures; run.Report["exception"] = exception.what();
        std::cerr << "FAIL scene feature GPU workflow: " << exception.what() << std::endl;
    }
    if (playing && g_Editor) g_Editor->StopPlay();
    restore.Restore(); g_KeyState = oldKeys; g_MouseState = oldMouse; g_timeDeltaSecond = oldDelta;
    if (g_Editor) g_Editor->SetAuthoringEnabled(oldAuthoring);
    SetAuthoringRuntimeMode(oldRuntimeMode); SetAuthoringWorkspaceVisible(oldWorkspace);
    run.Report["optionsRestored"] = true;
    run.Report["renderedFrames"] = run.Frames; run.Report["failureCount"] = run.Failures;
    run.Report["status"] = run.Failures ? "failed" : "passed";
    if (!run.Output.empty()) WriteText(run.Output / "scene-features-gpu-report.json", run.Report.dump(2) + "\n");
    std::cout << "Scene feature GPU workflow: " << run.Frames << " actual frames, " << run.Failures << " failures" << std::endl;
    return run.Failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)outputDirectory;
    return EXIT_FAILURE;
#endif
}
