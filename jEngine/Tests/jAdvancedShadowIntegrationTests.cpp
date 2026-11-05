#include "pch.h"
#include "jAdvancedShadowIntegrationTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "FileLoader/jEditorAssets.h"
#include "Material/jMaterial.h"
#include "Renderer/jAdvancedShadowPass.h"
#include "RHI/jRenderTarget.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jLight.h"
#include "Shader/jAdvancedShadowShaderParameters.h"
#include "jEngine.h"
#include "jOptions.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>

// The existing readback owner handles native APIs, suballocation offsets,
// copy footprints, layout restoration and host visibility for both platforms.
std::string CaptureSceneFeaturePixels(const char* pngPath);
bool ReadSceneFeatureTextureFloats(const std::shared_ptr<jTexture>& texture,
    std::vector<float>& rgba, uint32_t& width, uint32_t& height, std::string& error);

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace jEditorAuthoring;
using namespace jEditorTools;
using Settings = jAdvancedShadows::Settings;
using Method = jAdvancedShadows::Representation;
using Projection = jAdvancedShadows::Projection;
using Pipeline = jGame::ESceneRenderPipeline;
using Clock = std::chrono::steady_clock;
constexpr uint32_t FrameBudget = 280;

void Write(const fs::path& path, const Json& value)
{
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << value.dump(2) << '\n';
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
    restore.Remember(gOptions.AdvancedShadows); restore.Remember(gOptions.SceneFeatures);
    restore.Remember(gOptions.CameraPos); restore.Remember(gOptions.UseDeferredRenderer);
    restore.Remember(gOptions.UseSubpass); restore.Remember(gOptions.UseMemoryless);
    restore.Remember(gOptions.UseHWRTDirectLighting); restore.Remember(gOptions.UseSurfelGI);
    restore.Remember(gOptions.UseSSGI); restore.Remember(gOptions.UseSSGITemporalAccumulation);
    restore.Remember(gOptions.AOType); restore.Remember(gOptions.AOIntensity);
    restore.Remember(gOptions.ShowAOOnly); restore.Remember(gOptions.ShowSSGIOnly);
    restore.Remember(gOptions.BloomEyeAdaptation); restore.Remember(gOptions.UseAtmosphericShadowing);
    restore.Remember(gOptions.UseVRS); restore.Remember(gOptions.ShowVRSArea);
    restore.Remember(gOptions.ShowGrid); restore.Remember(gOptions.ShowDebugObject);
    restore.Remember(gOptions.ShowDebugRT); restore.Remember(gOptions.EarthQuake);
    restore.Remember(gOptions.ShadowFilterMode); restore.Remember(gOptions.ShadowFilterRadiusTexels);
    gOptions.AdvancedShadows = {}; gOptions.SceneFeatures = {};
    gOptions.UseDeferredRenderer = true; gOptions.UseSubpass = false; gOptions.UseMemoryless = false;
    gOptions.UseHWRTDirectLighting = false; gOptions.UseSurfelGI = false; gOptions.UseSSGI = false;
    gOptions.UseSSGITemporalAccumulation = false; gOptions.AOType = 0; gOptions.AOIntensity = 0;
    gOptions.ShowAOOnly = false; gOptions.ShowSSGIOnly = false; gOptions.BloomEyeAdaptation = false;
    gOptions.UseAtmosphericShadowing = false; gOptions.UseVRS = false; gOptions.ShowVRSArea = false;
    gOptions.ShowGrid = false; gOptions.ShowDebugObject = false; gOptions.ShowDebugRT = false;
    gOptions.EarthQuake = false; gOptions.ShadowFilterMode = 1; gOptions.ShadowFilterRadiusTexels = 1.5f;
}

void Change(jEditor& editor, EntityId id, const std::function<void(Entity&)>& change)
{
    const auto* current = editor.GetDocument().Find(id);
    if (!current) throw std::runtime_error("Advanced shadow fixture entity disappeared");
    auto value = *current; change(value);
    if (!(value == *current) && !editor.GetDocument().Update(value, "Advanced shadow fixture"))
        throw std::runtime_error(editor.GetDocument().LastError());
}

uint64_t HashBytes(const void* bytes, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    const auto* values = static_cast<const unsigned char*>(bytes);
    for (size_t i = 0; i < count; ++i) { hash ^= values[i]; hash *= 1099511628211ull; }
    return hash;
}

struct FloatImage
{
    uint32_t Width = 0, Height = 0;
    std::vector<float> RGBA;
    Json Summary;
};

enum class SceneCase { Legacy, Normal, Cascades, MovedCascades, DeepHalf, DeepThreeQuarter,
    Dual, Perspective, Volume, InsideVolume, InvalidVolume, OverrideIsolation };
struct Case
{
    std::string Name;
    Settings Shadow;
    SceneCase Scene = SceneCase::Normal;
    Pipeline Renderer = Pipeline::Deferred;
    bool Profile = false;
};

struct Run
{
    fs::path Output, Fixture, ProjectFile;
    Json Report{{"schemaVersion", 1}, {"test", "advanced-shadow-gpu-integration"},
        {"checks", Json::array()}, {"captures", Json::object()}, {"profiles", Json::object()}, {"cases", Json::array()}};
    uint32_t Frames = 0;
    int Failures = 0;
    bool FixtureReady = false;
    bool SavedCasterFlagsCorrect = true, SavedLightModesCorrect = true, RuntimeCasterFlagsCorrect = true;
    size_t ReopenedScenes = 0;
    Vector CameraPosition{0, 220, -800}, CameraTarget{0, 80, 300};
    EntityId Floor = 0, Sun = 0, Point = 0, Camera = 0;
    std::array<EntityId, 3> Casters{};
    std::array<EntityId, 2> Layers{};
    std::array<EntityId, 4> Witnesses{};
    std::map<EntityId, Entity> BaseEntities;
    std::map<std::string, Json> Images;
    std::map<std::string, uint64_t> AtlasHashes;
    std::array<Matrix, 4> FirstCascadeMatrices{};

    void Progress()
    {
        Report["renderedFrames"] = Frames; Report["failureCount"] = Failures;
        if (!Output.empty()) Write(Output / "advanced-shadows-progress.json", Report);
    }
    void Expect(bool pass, const std::string& name, const Json& detail = Json::object())
    {
        Report["checks"].push_back({{"name", name}, {"pass", pass}, {"frame", Frames}, {"detail", detail}});
        Failures += !pass;
        std::cout << (pass ? "PASS " : "FAIL ") << "advanced shadows: " << name << std::endl;
        Progress();
    }
    void PlaceCamera()
    {
        if (!FixtureReady) return;
        auto* camera = g_Engine->Game.MainCamera;
        if (!camera) throw std::runtime_error("Advanced shadow fixture lost its main camera");
        jCamera::SetCamera(camera, CameraPosition, CameraTarget, CameraPosition + Vector::UpVector,
            DegreeToRadian(65), 1, 1800, SCR_WIDTH, SCR_HEIGHT, true, 1100);
        camera->UpdateCamera();
        gOptions.CameraPos = CameraPosition;
    }
    double Frame()
    {
        if (Frames >= FrameBudget) throw std::runtime_error("Advanced shadow harness exceeded its 280-frame budget");
        const auto begin = Clock::now();
        ResetAllKeyState(); ResetMouseAllState(); g_timeDeltaSecond = 1.f / 60.f;
        g_Engine->ProcessInput(g_timeDeltaSecond); g_rhi->IncrementFrameNumber();
        g_Engine->Update(g_timeDeltaSecond); PlaceCamera();
        g_Engine->Draw(); g_Engine->EndOfFrame(); ++Frames;
#ifdef _WIN32
        if (IsUseDX12())
        {
            MSG message{};
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) throw std::runtime_error("Native test window closed");
                TranslateMessage(&message); DispatchMessage(&message);
            }
        }
        else
#endif
        {
            auto* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
            if (!window || glfwWindowShouldClose(window)) throw std::runtime_error("Native test window closed");
            glfwPollEvents();
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    }
    void Warm(uint32_t count) { while (count--) Frame(); }
    void Measure(const std::string& name)
    {
        Warm(4);
        std::vector<double> times;
        for (int i = 0; i < 6; ++i) times.push_back(Frame());
        auto sorted = times; std::sort(sorted.begin(), sorted.end());
        double sum = 0; for (double value : times) sum += value;
        Report["profiles"][name] = {{"warmupFramesExcluded", 4}, {"measuredFrames", 6},
            {"frameCpuMilliseconds", times}, {"meanMilliseconds", sum / times.size()},
            {"medianMilliseconds", (sorted[2] + sorted[3]) * .5}, {"maximumMilliseconds", sorted.back()},
            {"simulationDeltaSeconds", 1.0 / 60.0},
            {"scope", "Same executable CPU ProcessInput/Update/visibility/Draw/EndOfFrame/present/event-poll latency. Captures, readback, fixture changes and warmup are excluded; no GPU-only timing or speedup claim."}};
        Progress();
    }
    Json Capture(const std::string& name)
    {
        const auto path = Output / (name + ".png");
        const auto image = Json::parse(CaptureSceneFeaturePixels(jEditorAssetPath::ToUtf8(path).c_str()));
        bool finite = image.contains("meanRgb") && image["meanRgb"].is_array() && image["meanRgb"].size() == 3;
        if (finite) for (const auto& value : image["meanRgb"])
            finite = finite && value.is_number() && std::isfinite(value.get<double>());
        Expect(image.value("readback", false) && image.value("pngSaved", false)
            && image.value("width", 0) > 0 && image.value("height", 0) > 0
            && image.value("litPixels", size_t(0)) > 4 && finite
            && !image.value("pixelHash", std::string{}).empty(), name + " has actual finite nonblack scene pixels", image);
        Report["captures"][name] = image; Images[name] = image; return image;
    }
    void Different(const std::string& before, const std::string& after, const std::string& reason)
    {
        const auto a = Images.find(before), b = Images.find(after);
        Expect(a != Images.end() && b != Images.end() && a->second.value("readback", false)
            && b->second.value("readback", false) && a->second.value("width", 0) == b->second.value("width", 0)
            && a->second.value("height", 0) == b->second.value("height", 0)
            && a->second.value("pixelHash", std::string{}) != b->second.value("pixelHash", std::string{}), reason);
    }
    FloatImage Read(const std::shared_ptr<jRenderTarget>& target, const std::string& label)
    {
        FloatImage result; std::string error;
        const bool read = target && ReadSceneFeatureTextureFloats(target->GetTexturePtr(),
            result.RGBA, result.Width, result.Height, error);
        const bool sized = read && result.Width && result.Height
            && result.RGBA.size() == size_t(result.Width) * result.Height * 4;
        std::array<double, 4> minimum, maximum, sum{};
        minimum.fill(std::numeric_limits<double>::infinity()); maximum.fill(-std::numeric_limits<double>::infinity());
        size_t nonfinite = 0;
        for (size_t i = 0; i < result.RGBA.size(); ++i)
        {
            const double value = result.RGBA[i];
            if (!std::isfinite(value)) { ++nonfinite; continue; }
            minimum[i % 4] = std::min(minimum[i % 4], value);
            maximum[i % 4] = std::max(maximum[i % 4], value); sum[i % 4] += value;
        }
        result.Summary = {{"readback", read}, {"width", result.Width}, {"height", result.Height},
            {"nonfiniteValues", nonfinite}, {"error", error}, {"rawFloatHash", std::to_string(HashBytes(result.RGBA.data(), result.RGBA.size() * sizeof(float)))}};
        if (sized && !nonfinite)
        {
            result.Summary["minimum"] = minimum; result.Summary["maximum"] = maximum;
            for (auto& value : sum) value /= double(result.Width) * result.Height;
            result.Summary["mean"] = sum;
        }
        Expect(sized && nonfinite == 0, label + " reads actual finite GPU float pixels", result.Summary);
        Report["floatTargets"][label] = result.Summary;
        return result;
    }
};

Json MatrixValues(const Matrix& matrix)
{
    Json values = Json::array();
    for (float value : matrix.mm) values.push_back(value);
    return values;
}

Json VectorValues(const Vector& value) { return Json::array({value.x, value.y, value.z}); }
Json VectorValues(const Vector4& value) { return Json::array({value.x, value.y, value.z, value.w}); }

void RecordPerspectiveDiagnostics(Run& run, const jEditor& editor, const Case& test,
    const jAdvancedShadowData& values, const std::shared_ptr<jRenderTarget>& target)
{
    Json diagnostic{{"matrixStorage", "Matrix.mm[16], engine column-major; CPU uses Matrix::Transform"},
        {"publishedShadowVP", MatrixValues(values.ShadowVP[0])},
        {"publishedCameraPositionAndType", VectorValues(values.CameraPositionAndType)},
        {"publishedCameraForwardAndOpacity", VectorValues(values.CameraForwardAndOpacity)},
        {"publishedLightPositionAndNear", VectorValues(values.LightPositionAndNear)},
        {"publishedFarAndDeepParams", VectorValues(values.FarAndDeepParams)},
        {"publishedMapSizeAndInvSize", VectorValues(values.MapSizeAndInvSize)},
        {"publishedModeParams", VectorValues(values.ModeParams)},
        {"publishedAtlasParams", VectorValues(values.AtlasParams)},
        {"requestedHarnessCameraPosition", VectorValues(run.CameraPosition)},
        {"requestedHarnessCameraTarget", VectorValues(run.CameraTarget)}};
    const auto* sun = editor.LightForEntity(run.Sun);
    if (sun) diagnostic["actualSunRayDirection"] = VectorValues(sun->GetDirection());
    const auto* camera = g_Engine->Game.MainCamera;
    if (camera)
    {
        const auto forward = camera->GetForwardVector().GetNormalize();
        const Vector publishedPosition(values.CameraPositionAndType.x,
            values.CameraPositionAndType.y, values.CameraPositionAndType.z);
        const Vector publishedForward(values.CameraForwardAndOpacity.x,
            values.CameraForwardAndOpacity.y, values.CameraForwardAndOpacity.z);
        diagnostic["actualMainCamera"] = {{"position", VectorValues(camera->Pos)},
            {"target", VectorValues(camera->Target)}, {"up", VectorValues(camera->Up)},
            {"forward", VectorValues(forward)}, {"near", camera->Near}, {"far", camera->Far},
            {"verticalFovRadians", camera->FOVRad}, {"width", camera->Width}, {"height", camera->Height},
            {"perspective", camera->IsPerspectiveProjection}, {"view", MatrixValues(camera->View)},
            {"projection", MatrixValues(camera->Projection)}, {"viewProjection", MatrixValues(camera->ViewProjection)},
            {"publishedPositionDelta", (publishedPosition - camera->Pos).Length()},
            {"publishedForwardDelta", (publishedForward - forward).Length()}};
    }
    const uint32_t width = target ? uint32_t(target->Info.Width) : 0;
    const uint32_t height = target ? uint32_t(target->Info.Height) : 0;
    diagnostic["actualAtlasSize"] = {width, height};
    diagnostic["casters"] = Json::array();
    for (const auto id : run.Casters)
    {
        const auto* object = editor.ObjectForEntity(id);
        Json caster{{"entity", id}, {"runtimeObjectPresent", bool(object)}, {"meshes", Json::array()}};
        if (!object) { diagnostic["casters"].push_back(caster); continue; }
        caster["objectId"] = object->GetObjectID();
        caster["skipShadowMapGen"] = object->SkipShadowMapGen;
        for (const auto* mesh : object->RenderObjects)
        {
            Json info{{"renderObjectId", mesh->RenderObjectID}, {"world", MatrixValues(mesh->World)}};
            const auto geometry = mesh->GeometryDataPtr;
            const auto stream = geometry ? geometry->VertexStream_PositionOnlyPtr : nullptr;
            if (!stream || stream->Params.empty() || stream->ElementCount <= 0)
            { info["boundsError"] = "No retained CPU position stream"; caster["meshes"].push_back(info); continue; }
            const auto& parameter = stream->Params.front();
            const size_t stride = parameter->Stride > 0 ? size_t(parameter->Stride) : sizeof(float) * 3;
            const size_t count = size_t(stream->ElementCount);
            if (stride < sizeof(float) * 3 || count > 1000000 || parameter->Attributes.empty()
                || parameter->Attributes.front().UnderlyingType != EBufferElementType::FLOAT
                || parameter->GetBufferSize() < (count - 1) * stride + sizeof(float) * 3)
            { info["boundsError"] = "Invalid retained CPU position layout"; caster["meshes"].push_back(info); continue; }
            Vector minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
            const auto* bytes = static_cast<const unsigned char*>(parameter->GetBufferData());
            for (size_t i = 0; i < count; ++i)
            {
                float xyz[3]; std::memcpy(xyz, bytes + i * stride, sizeof(xyz));
                minimum.x = std::min(minimum.x, xyz[0]); minimum.y = std::min(minimum.y, xyz[1]); minimum.z = std::min(minimum.z, xyz[2]);
                maximum.x = std::max(maximum.x, xyz[0]); maximum.y = std::max(maximum.y, xyz[1]); maximum.z = std::max(maximum.z, xyz[2]);
            }
            info["positionVertexCount"] = count; info["positionStrideBytes"] = stride;
            info["localBounds"] = {{"minimum", VectorValues(minimum)}, {"maximum", VectorValues(maximum)}};
            info["corners"] = Json::array();
            std::array<double, 4> clipMinimum, clipMaximum;
            std::array<double, 3> projectedMinimum, projectedMaximum;
            clipMinimum.fill(std::numeric_limits<double>::infinity()); clipMaximum.fill(-std::numeric_limits<double>::infinity());
            projectedMinimum.fill(std::numeric_limits<double>::infinity()); projectedMaximum.fill(-std::numeric_limits<double>::infinity());
            size_t finiteClips = 0, projectedCorners = 0, positiveWCorners = 0, insideCorners = 0;
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const Vector4 local(corner & 1 ? maximum.x : minimum.x,
                    corner & 2 ? maximum.y : minimum.y, corner & 4 ? maximum.z : minimum.z, 1);
                const auto world = mesh->World.Transform(local);
                const auto clip = values.ShadowVP[0].Transform(world);
                const std::array<double, 4> components{clip.x, clip.y, clip.z, clip.w};
                const bool finite = std::all_of(components.begin(), components.end(), [](double value) { return std::isfinite(value); });
                Json probe{{"local", VectorValues(local)}, {"world", VectorValues(world)},
                    {"cpuShadowClip", VectorValues(clip)}, {"finiteClip", finite}};
                if (finite)
                {
                    ++finiteClips;
                    for (size_t axis = 0; axis < 4; ++axis)
                    { clipMinimum[axis] = std::min(clipMinimum[axis], components[axis]); clipMaximum[axis] = std::max(clipMaximum[axis], components[axis]); }
                    if (std::abs(clip.w) > 1e-7f)
                    {
                        ++projectedCorners; positiveWCorners += clip.w > 0;
                        const std::array<double, 3> projected{(double(clip.x) / clip.w * .5 + .5) * width,
                            (.5 - double(clip.y) / clip.w * .5) * height, double(clip.z) / clip.w};
                        for (size_t axis = 0; axis < 3; ++axis)
                        { projectedMinimum[axis] = std::min(projectedMinimum[axis], projected[axis]); projectedMaximum[axis] = std::max(projectedMaximum[axis], projected[axis]); }
                        const bool inside = clip.w > 0 && std::abs(clip.x / clip.w) <= 1 && std::abs(clip.y / clip.w) <= 1
                            && clip.z / clip.w >= 0 && clip.z / clip.w <= 1;
                        insideCorners += inside;
                        probe["projectedAtlasPixelAndDepth"] = projected; probe["insideClip"] = inside;
                    }
                }
                info["corners"].push_back(probe);
            }
            info["finiteClipCorners"] = finiteClips; info["projectedCorners"] = projectedCorners;
            info["positiveWCorners"] = positiveWCorners; info["insideClipCorners"] = insideCorners;
            if (finiteClips) info["cpuShadowClipBounds"] = {{"minimum", clipMinimum}, {"maximum", clipMaximum}};
            if (projectedCorners) info["projectedAtlasPixelAndDepthBounds"] = {{"minimum", projectedMinimum}, {"maximum", projectedMaximum}};
            caster["meshes"].push_back(info);
        }
        diagnostic["casters"].push_back(caster);
    }
    run.Report["perspectiveDiagnostics"][test.Name] = std::move(diagnostic);
    run.Progress(); // Preserve published CPU/GPU inputs even if a later case crashes.
}

void MakeFixture(Run& run, jEditor& editor)
{
    auto& game = g_Engine->Game;
    editor.CancelPendingImports(); editor.ResetWorldBindings(false); game.ClearImportedSceneForAuthoring();
    if (!editor.GetDocument().NewScene("Advanced shadow integration")) throw std::runtime_error(editor.GetDocument().LastError());
    run.Fixture = run.Output / ("fixture-" + std::to_string(Clock::now().time_since_epoch().count()));
    jEditorProject project; std::string error;
    if (!project.Create(run.Fixture, "Advanced shadow modules", error)) throw std::runtime_error(error);
    Document empty;
    if (!empty.Save(run.Fixture / "Scenes/start.jscene")) throw std::runtime_error(empty.LastError());
    project.EditConfig().StartupScene = "Scenes/start.jscene";
    project.EditConfig().Settings["render.deferred"] = "true";
    project.EditConfig().Settings["render.surfelgi"] = "false";
    project.EditConfig().Settings["render.ssgi"] = "false";
    if (!project.Save(error) || !OpenAuthoringProject(editor, project.GetProjectFile(), error)) throw std::runtime_error(error);
    run.ProjectFile = project.GetProjectFile();
    run.Report["fixtureProject"] = run.ProjectFile.generic_string();
    SetAuthoringRuntimeMode(false); SetAuthoringWorkspaceVisible(true); editor.SetAuthoringEnabled(true);
    editor.Placement.EnablePlacementMode = true; editor.SetSelectedEntities({});
    if (!editor.GetDocument().Entities().empty()) throw std::runtime_error("Owned fixture project was not empty");
    run.Floor = editor.CreateEntity(EntityKind::Terrain, "Receiver (authored shadowCaster=false)");
    run.Sun = editor.CreateEntity(EntityKind::DirectionalLight, "Fixture directional light");
    run.Point = editor.CreateEntity(EntityKind::PointLight, "Fixture point light");
    run.Camera = editor.CreateEntity(EntityKind::Camera, "Fixture primary camera");
    for (size_t i = 0; i < run.Casters.size(); ++i)
        run.Casters[i] = editor.CreateEntity(EntityKind::Cube, "Closed caster " + std::to_string(i));
    for (size_t i = 0; i < run.Layers.size(); ++i)
        run.Layers[i] = editor.CreateEntity(EntityKind::Terrain, "Transparent optical-depth layer " + std::to_string(i));
    for (size_t i = 0; i < run.Witnesses.size(); ++i)
        run.Witnesses[i] = editor.CreateEntity(EntityKind::Cube, "Cascade depth witness " + std::to_string(i));
    Change(editor, run.Floor, [](Entity& e) {
        e.Local = {}; e.Terrain.Resolution = 17; e.Terrain.SizeX = 1500; e.Terrain.SizeZ = 1600;
        e.ShadowCaster = false;
        e.Material.OverrideEnabled = true; e.Material.BaseColor = {.55f, .57f, .6f}; e.Material.Roughness = .8f;
    });
    const std::array<Vec3, 3> positions{{{-100, 70, 60}, {100, 70, 130}, {30, 50, -100}}};
    for (size_t i = 0; i < run.Casters.size(); ++i) Change(editor, run.Casters[i], [&](Entity& e) {
        e.Local = {}; e.Local.Position = positions[i]; e.Local.Scale = {5, 5, 5};
        e.ShadowCaster = true;
        e.Material.OverrideEnabled = true; e.Material.BaseColor = {.65f, .25f + float(i) * .15f, .12f};
    });
    for (size_t i = 0; i < run.Layers.size(); ++i) Change(editor, run.Layers[i], [&](Entity& e) {
        e.Local = {}; e.Local.Position = {0, 100.f + float(i) * 40.f, 0}; e.Visible = false;
        e.ShadowCaster = true;
        e.Terrain.Resolution = 2; e.Terrain.SizeX = 500; e.Terrain.SizeZ = 500;
        e.Material.OverrideEnabled = true; e.Material.BaseColor = {.15f, .65f, .8f}; e.Material.Opacity = .5f;
    });
    for (auto id : run.Witnesses) Change(editor, id, [](Entity& e) {
        e.Local = {}; e.Local.Scale = {1.6f, 1.6f, 1.6f}; e.Visible = false;
        e.Material.OverrideEnabled = true; e.Material.BaseColor = {.15f, .8f, .3f};
    });
    Change(editor, run.Sun, [](Entity& e) {
        e.Local = {}; e.Local.RotationDegrees = {55, -20, 0}; e.Light.Intensity = 1.5f; e.Light.CastShadows = true;
    });
    Change(editor, run.Point, [](Entity& e) {
        e.Local = {}; e.Local.Position = {0, 180, 0}; e.Light.Range = 900; e.Light.Intensity = 0; e.Light.CastShadows = false;
    });
    Change(editor, run.Camera, [](Entity& e) {
        e.Local = {}; e.Local.Position = {0, 220, -800}; e.Camera.Primary = true;
        e.Camera.VerticalFovDegrees = 65; e.Camera.NearPlane = 1; e.Camera.FarPlane = 1800;
    });
    run.BaseEntities = editor.GetDocument().Entities(); run.FixtureReady = true;
}

Settings TestSettings(Method method = Method::Depth, Projection projection = Projection::Single)
{
    Settings settings; settings.Method = method; settings.DirectionalProjection = projection;
    settings.MapResolution = 256; settings.CascadeCount = 4; settings.MaxDistance = 1600;
    settings.DeepMaxDistance = 1600; settings.CasterPadding = 250; settings.VolumeRange = 4000;
    settings.BlurRadius = 3; return settings;
}

void ConfigureCase(Run& run, jEditor& editor, const Case& test)
{
    for (const auto& [id, entity] : run.BaseEntities)
        Change(editor, id, [&](Entity& value) { value = entity; });
    gOptions.AdvancedShadows = test.Shadow;
    run.CameraPosition = Vector(0, 220, -800); run.CameraTarget = Vector(0, 80, 300);
    const bool deep = test.Scene == SceneCase::DeepHalf || test.Scene == SceneCase::DeepThreeQuarter;
    const bool volume = test.Scene == SceneCase::Volume || test.Scene == SceneCase::InsideVolume
        || test.Scene == SceneCase::InvalidVolume || test.Scene == SceneCase::OverrideIsolation;
    const bool invalid = test.Scene == SceneCase::InvalidVolume || test.Scene == SceneCase::OverrideIsolation;
    Change(editor, run.Floor, [&](Entity& e) { e.ShadowCaster = invalid; });
    for (auto id : run.Casters) Change(editor, id, [&](Entity& e) { e.ShadowCaster = !deep; });
    for (auto id : run.Layers) Change(editor, id, [](Entity& e) { e.ShadowCaster = true; });
    // Authoring lights rotate their local -Y ray direction, unlike cameras'
    // local +Z forward vector. Identity therefore gives an exact downward sun.
    if (deep || volume) Change(editor, run.Sun, [](Entity& e) { e.Local.RotationDegrees = {0, 0, 0}; });
    if (deep) for (auto id : run.Layers) Change(editor, id, [&](Entity& e) {
        e.Visible = true; e.Material.Opacity = test.Scene == SceneCase::DeepHalf ? .5f : .75f;
    });
    if (test.Scene == SceneCase::Dual || test.Scene == SceneCase::OverrideIsolation)
    {
        Change(editor, run.Point, [](Entity& e) { e.Light.CastShadows = true; e.Light.Intensity = 1800; });
        if (test.Scene == SceneCase::Dual)
        {
            Change(editor, run.Sun, [](Entity& e) { e.Light.Intensity = 0; e.Light.CastShadows = false; });
            const std::array<Vec3, 3> points{{{0, 65, 150}, {0, 65, -150}, {140, 130, 0}}};
            for (size_t i = 0; i < points.size(); ++i) Change(editor, run.Casters[i], [&](Entity& e) {
                e.Local.Position = points[i]; e.Local.Scale = {3, 3, 3};
            });
        }
    }
    if (test.Scene == SceneCase::Cascades || test.Scene == SceneCase::MovedCascades)
    {
        if (test.Scene == SceneCase::MovedCascades)
        { run.CameraPosition += Vector(45, 0, 25); run.CameraTarget += Vector(45, 0, 25); }
        const Vector forward = (run.CameraTarget - run.CameraPosition).GetNormalize();
        float previous = 1;
        for (size_t i = 0; i < run.Witnesses.size(); ++i)
        {
            const float fraction = float(i + 1) / 4;
            const float split = (1.f - test.Shadow.SplitLambda) * (1.f + 1599.f * fraction)
                + test.Shadow.SplitLambda * std::pow(1600.f, fraction);
            const Vector position = run.CameraPosition + forward * ((previous + split) * .5f);
            Change(editor, run.Witnesses[i], [&](Entity& e) {
                e.Visible = true; e.Local.Position = {position.x, position.y, position.z};
            });
            previous = split;
        }
    }
    if (test.Scene == SceneCase::InsideVolume)
    {
        // The first cube spans x[-150,-50], y[20,120], z[10,110].
        // A downward directional extrusion contains this camera below its cap.
        run.CameraPosition = Vector(-100, 10, 60); run.CameraTarget = Vector(140, 0, 200);
    }
    if (test.Scene == SceneCase::OverrideIsolation)
    {
        gOptions.AdvancedShadows = {};
        const auto sun = TestSettings(Method::ShadowVolume), point = TestSettings(Method::Variance);
        Change(editor, run.Sun, [&](Entity& e) { e.Light.ShadowOverride = sun; });
        Change(editor, run.Point, [&](Entity& e) { e.Light.ShadowOverride = point; });
    }
    else for (auto id : {run.Sun, run.Point}) Change(editor, id, [&](Entity& e) {
        if (e.Light.CastShadows) e.Light.ShadowOverride = test.Shadow;
    });
    const auto cameraForward = (run.CameraTarget - run.CameraPosition).GetNormalize();
    Change(editor, run.Camera, [&](Entity& e) {
        e.Local.Position = {run.CameraPosition.x, run.CameraPosition.y, run.CameraPosition.z};
        e.Local.RotationDegrees = {RadianToDegree(std::atan2(-cameraForward.y,
            std::sqrt(cameraForward.x * cameraForward.x + cameraForward.z * cameraForward.z))),
            RadianToDegree(std::atan2(cameraForward.x, cameraForward.z)), 0};
    });
    if (!editor.SyncDocumentToWorld()) throw std::runtime_error(editor.LastError());
    auto* floor = editor.ObjectForEntity(run.Floor);
    if (!floor) throw std::runtime_error("Fixture floor has no real runtime geometry");
    if (!run.Report.contains("receiverPrototypeSkipUpdateShadowVolume"))
        run.Report["receiverPrototypeSkipUpdateShadowVolume"] = floor->SkipUpdateShadowVolume;
    // This separate legacy volume-preparation exclusion has no authored field.
    // Only rejection controls need to force inspection of the open receiver.
    if (invalid) floor->SetSkipUpdateShadowVolume(false);
    jObject::FlushDirtyState(); run.PlaceCamera();
    std::string error;
    if (!g_Engine->Game.RequestSceneRenderPipeline(test.Renderer, error)) throw std::runtime_error(error);
    const auto scene = run.Fixture / ("Scenes/" + test.Name + ".jscene");
    if (!editor.GetDocument().Save(scene)) throw std::runtime_error(editor.GetDocument().LastError());
    Document reopened;
    const bool loaded = reopened.Load(scene);
    run.ReopenedScenes += loaded;
    bool casterFlags = loaded, lightModes = loaded;
    const auto casterMatches = [&](EntityId id, bool expected) {
        const auto* entity = reopened.Find(id);
        return entity && entity->ShadowCaster.has_value() && *entity->ShadowCaster == expected;
    };
    casterFlags &= casterMatches(run.Floor, invalid);
    for (auto id : run.Casters) casterFlags &= casterMatches(id, !deep);
    for (auto id : run.Layers) casterFlags &= casterMatches(id, true);
    for (auto id : {run.Sun, run.Point})
    {
        const auto* saved = reopened.Find(id);
        const auto* authored = editor.GetDocument().Find(id);
        lightModes &= saved && authored && saved->Light.ShadowOverride == authored->Light.ShadowOverride
            && (!authored->Light.CastShadows || saved->Light.ShadowOverride.has_value());
    }
    run.SavedCasterFlagsCorrect &= casterFlags; run.SavedLightModesCorrect &= lightModes;
    run.Report["savedSceneChecks"][test.Name] = {{"loaded", loaded}, {"casterFlagsPreserved", casterFlags},
        {"activeLightModesPreserved", lightModes}, {"error", loaded ? std::string{} : reopened.LastError()}};
}

void FinalizeReusableExample(Run& run, size_t expectedScenes)
{
    jEditorProject project; std::string error;
    if (!project.Open(run.ProjectFile, error)) throw std::runtime_error(error);
    project.EditConfig().StartupScene = "Scenes/deferred-csm4.jscene";
    if (!project.Save(error)) throw std::runtime_error(error);
    const auto guide = run.Fixture / "GUIDE.md";
    std::ofstream output(guide, std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << "# Advanced shadow examples\n\n"
        "Open project.jproject. Its startup scene is Scenes/deferred-csm4.jscene (four stabilized cascades, Deferred). "
        "The harness ends its captures on deferred-legacy-restored.jscene; that final control is not the project startup scene.\n\n"
        "Each scene stores renderable shadowCaster flags and explicit settings on every active shadow-casting light. "
        "Those settings do not depend on the global console defaults used by the automated comparisons. "
        "Use the saved primary camera (Play) to reproduce the authored viewing direction. "
        "Choose Forward in the viewport renderer menu for scenes whose names start with forward-; otherwise use Deferred.\n\n"
        "| Feature | Scene files |\n| --- | --- |\n"
        "| CSM | deferred-csm4, deferred-csm4-camera-moved, forward-csm4 |\n"
        "| VSM / ESM / EVSM | deferred-vsm-*, deferred-esm-*, deferred-evsm-*, forward-evsm |\n"
        "| Deep opacity layers | deferred-deep-half, deferred-deep-three-quarter, forward-deep |\n"
        "| Dual Paraboloid | deferred-dual-front-back-seam, forward-dual |\n"
        "| Perspective shadow map | deferred-psm |\n"
        "| Closed shadow volumes | deferred-volume, deferred-volume-camera-inside, forward-volume |\n"
        "| Intentional rejection controls | deferred-volume-nonmanifold-fallback, per-light-override-isolation |\n\n"
        "All names above are Scenes/<name>.jscene. The normal receiver does not cast shadows; "
        "the intentional rejection controls enable its open-mesh caster flag to exercise a diagnosed fallback. "
        "The mixed override control keeps a separate point light's VSM active. "
        "Deep scenes contain exactly two flat layers at authored opacity 0.5 or 0.75; opaque cubes remain visible but do not cast.\n\n"
        "PSM may explicitly report a fitted-projection fallback for an unsafe configuration. "
        "The saved examples target the two raster renderers. Native measurements, diagnostics and raw scene captures "
        "are in advanced-shadows-gpu-report.json and PNGs in the parent output directory.\n";
    output.close();
    jEditorProject reopened;
    const bool loaded = reopened.Open(run.ProjectFile, error);
    run.Expect(run.ReopenedScenes == expectedScenes && run.SavedCasterFlagsCorrect,
        "all saved example scenes preserve their authored renderable caster flags",
        {{"reopenedScenes", run.ReopenedScenes}, {"expectedScenes", expectedScenes}});
    run.Expect(run.ReopenedScenes == expectedScenes && run.SavedLightModesCorrect,
        "all saved example scenes preserve explicit active-light shadow modes");
    run.Expect(run.RuntimeCasterFlagsCorrect,
        "actual runtime caster publication follows the authored flags without test-only caster setters");
    run.Expect(loaded && reopened.GetConfig().StartupScene == "Scenes/deferred-csm4.jscene"
        && fs::is_regular_file(run.Fixture / reopened.GetConfig().StartupScene) && fs::is_regular_file(guide),
        "the reusable project reopens on the CSM example with its scene guide",
        {{"startupScene", loaded ? reopened.GetConfig().StartupScene : std::string{}}, {"error", error}});
    run.Report["fixtureGuide"] = guide.generic_string();
    run.Report["fixtureStartupScene"] = "Scenes/deferred-csm4.jscene";
}

const jAdvancedShadows::FrameStats::Light* LightStats(const jLight* source)
{
    for (const auto& light : jAdvancedShadows::GetLastFrameStats().Lights) if (light.Source == source) return &light;
    return nullptr;
}

size_t Occupied(const FloatImage& image, uint32_t column, uint32_t row, uint32_t columns, uint32_t rows, bool seamOnly = false)
{
    if (!image.Width || !image.Height || image.RGBA.size() != size_t(image.Width) * image.Height * 4
        || !columns || !rows) return 0;
    const uint32_t w = image.Width / columns, h = image.Height / rows;
    size_t result = 0;
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x)
    {
        const float nx = (float(x) + .5f) * 2 / w - 1, ny = (float(y) + .5f) * 2 / h - 1;
        if (seamOnly && nx * nx + ny * ny < .8f) continue;
        const float depth = image.RGBA[(size_t(y + row * h) * image.Width + x + column * w) * 4];
        result += std::isfinite(depth) && depth >= 0 && depth < .9999f;
    }
    return result;
}

void ValidateOutput(Run& run, jEditor& editor, const Case& test)
{
    Json casterPublication = Json::array();
    std::vector<EntityId> authoredCasters{run.Floor};
    authoredCasters.insert(authoredCasters.end(), run.Casters.begin(), run.Casters.end());
    authoredCasters.insert(authoredCasters.end(), run.Layers.begin(), run.Layers.end());
    for (auto id : authoredCasters)
    {
        const auto* entity = editor.GetDocument().Find(id);
        const auto* object = editor.ObjectForEntity(id);
        const bool expected = entity && entity->ShadowCaster.value_or(true);
        const bool visible = entity && editor.GetDocument().IsEffectivelyVisible(id);
        bool matches = entity && entity->ShadowCaster.has_value() && object && object->SkipShadowMapGen == !expected;
        if (object) for (const auto* mesh : object->RenderObjects)
        {
            const auto& published = jObject::GetShadowCasterRenderObject();
            const bool present = std::find(published.begin(), published.end(), mesh) != published.end();
            matches &= present == (visible && expected);
        }
        run.RuntimeCasterFlagsCorrect &= matches;
        casterPublication.push_back({{"entity", id}, {"authoredCaster", expected}, {"visible", visible}, {"matchesRuntime", matches}});
    }
    run.Report["savedSceneChecks"][test.Name]["runtimeCasterPublication"] = casterPublication;
    size_t unownedStaticObjects = 0;
    for (const auto* object : jObject::GetStaticObject())
        unownedStaticObjects += !run.BaseEntities.contains(editor.EntityForObject(object));
    size_t unownedLights = 0;
    for (const auto* light : jLight::GetLights())
        unownedLights += light != editor.LightForEntity(run.Sun) && light != editor.LightForEntity(run.Point);
    run.Expect(unownedStaticObjects == 0 && unownedLights == 0,
        test.Name + " renders only the owned authored fixture world",
        {{"unownedStaticObjects", unownedStaticObjects}, {"unownedLights", unownedLights},
            {"registeredStaticObjects", jObject::GetStaticObject().size()}, {"registeredLights", jLight::GetLights().size()}});
    const bool pointCase = test.Scene == SceneCase::Dual;
    const auto* source = editor.LightForEntity(pointCase ? run.Point : run.Sun);
    const auto* stats = LightStats(source);
    Json detail = stats ? Json{{"active", stats->Active}, {"projectionFallback", stats->ProjectionFallback},
        {"rasterPasses", stats->RasterPasses}, {"computePasses", stats->ComputePasses},
        {"inputTriangles", stats->InputTriangles}, {"outputTriangles", stats->OutputTriangles},
        {"additionalTargetBytes", stats->AdditionalTargetBytes}, {"diagnostic", stats->Diagnostic}} : Json::object();
    run.Report["cases"].push_back({{"name", test.Name}, {"actualRenderer", g_Engine->Game.GetActiveSceneRenderPipelineName()}, {"light", detail}});
    run.Expect(g_Engine->Game.GetActiveSceneRenderPipeline() == test.Renderer, test.Name + " uses its actual requested raster renderer");
    if (test.Scene == SceneCase::Legacy)
    {
        bool active = false; for (const auto& value : jAdvancedShadows::GetLastFrameStats().Lights) active |= value.Active;
        run.Expect(!active, test.Name + " preserves the original default shadow path"); return;
    }
    if (test.Scene == SceneCase::InvalidVolume || test.Scene == SceneCase::OverrideIsolation)
    {
        run.Expect(stats && !stats->Active && !stats->Diagnostic.empty(), test.Name + " reports rejected open geometry and retains legacy visibility", detail);
        if (test.Scene == SceneCase::OverrideIsolation)
        {
            const auto* healthy = LightStats(editor.LightForEntity(run.Point));
            run.Expect(healthy && healthy->Active && healthy->Requested.Method == Method::Variance,
                "one invalid volume override leaves the other light's moment pass active");
            jAdvancedShadowData values{}; std::shared_ptr<jRenderTarget> map, deep0, deep1;
            const bool actual = jAdvancedShadows::GetLastDebugOutput(editor.LightForEntity(run.Point), values, map, deep0, deep1);
            run.Expect(actual && values.SamplingParams.z > .5f, "healthy per-light override publishes actual GPU output");
            if (actual) run.Read(map, test.Name + "-healthy-point");
            Document reopened;
            const auto file = run.Fixture / ("Scenes/" + test.Name + ".jscene");
            const bool loaded = reopened.Load(file);
            run.Expect(loaded && reopened.Find(run.Sun) && reopened.Find(run.Point)
                && reopened.Find(run.Sun)->Light.ShadowOverride == editor.GetDocument().Find(run.Sun)->Light.ShadowOverride
                && reopened.Find(run.Point)->Light.ShadowOverride == editor.GetDocument().Find(run.Point)->Light.ShadowOverride,
                "per-light overrides survive the owned scene's real save/load");
        }
        return;
    }
    run.Expect(stats && stats->Active, test.Name + " records a complete active light output", detail);
    jAdvancedShadowData values{}; std::shared_ptr<jRenderTarget> map, deep0, deep1;
    const bool actual = jAdvancedShadows::GetLastDebugOutput(source, values, map, deep0, deep1);
    run.Expect(actual && values.SamplingParams.z > .5f, test.Name + " exposes the actual published light resources");
    if (!actual) return;
    if (test.Scene == SceneCase::Perspective) RecordPerspectiveDiagnostics(run, editor, test, values, map);
    if (test.Scene == SceneCase::DeepHalf || test.Scene == SceneCase::DeepThreeQuarter)
    {
        const auto* sun = editor.LightForEntity(run.Sun);
        const auto sunDirection = sun ? sun->GetDirection() : Vector::ZeroVector;
        const float authoredOpacity = test.Scene == SceneCase::DeepHalf ? .5f : .75f;
        const auto& actualCasters = jObject::GetShadowCasterRenderObject();
        size_t expectedCasterMeshes = 0; bool correctLayers = true;
        Json layers = Json::array();
        for (const auto id : run.Layers)
        {
            const auto* object = editor.ObjectForEntity(id);
            correctLayers &= object && !object->RenderObjects.empty();
            if (!object) continue;
            for (const auto* mesh : object->RenderObjects)
            {
                const bool registered = std::find(actualCasters.begin(), actualCasters.end(), mesh) != actualCasters.end();
                const float opacity = mesh && mesh->MaterialPtr ? mesh->MaterialPtr->BaseColorOpacity.w : -1.f;
                const auto normal = mesh ? mesh->World.TransformDirection(Vector::UpVector).GetNormalize() : Vector::ZeroVector;
                const float facing = normal.DotProduct(-sunDirection);
                correctLayers &= registered && std::isfinite(opacity) && std::abs(opacity - authoredOpacity) < 1e-6f && facing > .99f;
                ++expectedCasterMeshes;
                layers.push_back({{"entity", id}, {"runtimeOpacity", opacity}, {"registeredShadowCaster", registered},
                    {"worldNormal", {normal.x, normal.y, normal.z}}, {"towardLightDotNormal", facing}});
            }
        }
        const bool downward = std::abs(sunDirection.x) < 1e-5f && sunDirection.y < -.99f && std::abs(sunDirection.z) < 1e-5f;
        run.Expect(downward && correctLayers && expectedCasterMeshes == 2 && actualCasters.size() == expectedCasterMeshes,
            test.Name + " captures exactly two live authored-opacity layers facing the actual downward sun",
            {{"sunRayDirection", {sunDirection.x, sunDirection.y, sunDirection.z}}, {"authoredOpacity", authoredOpacity},
                {"actualCasterMeshes", actualCasters.size()}, {"expectedCasterMeshes", expectedCasterMeshes}, {"layers", layers}});
        const auto a = run.Read(deep0, test.Name + "-bins0"), b = run.Read(deep1, test.Name + "-bins1");
        const double expected = test.Scene == SceneCase::DeepHalf ? .25 : .0625;
        size_t matching = 0, nonzero = 0;
        double minimumTau = std::numeric_limits<double>::infinity(), maximumTau = 0;
        double closestTransmittance = 0, closestError = std::numeric_limits<double>::infinity();
        std::map<int64_t, size_t> tauHistogram;
        Json samples = Json::array();
        if (a.Width == b.Width && a.Height == b.Height && a.RGBA.size() == b.RGBA.size())
            for (size_t p = 0; p + 3 < a.RGBA.size(); p += 4)
        {
            double tau = 0; for (size_t c = 0; c < 4; ++c) tau += a.RGBA[p + c] + b.RGBA[p + c];
            if (tau <= .01 || !std::isfinite(tau)) continue;
            const double transmittance = std::exp(-tau), error = std::abs(transmittance - expected);
            ++nonzero; matching += error < .012;
            minimumTau = std::min(minimumTau, tau); maximumTau = std::max(maximumTau, tau);
            ++tauHistogram[int64_t(std::llround(tau * 1024.0))];
            if (error < closestError) { closestError = error; closestTransmittance = transmittance; }
            if (samples.size() < 12)
                samples.push_back({{"pixel", {(p / 4) % a.Width, (p / 4) / a.Width}}, {"tau", tau},
                    {"transmittance", transmittance}, {"bins0", {a.RGBA[p], a.RGBA[p + 1], a.RGBA[p + 2], a.RGBA[p + 3]}},
                    {"bins1", {b.RGBA[p], b.RGBA[p + 1], b.RGBA[p + 2], b.RGBA[p + 3]}}});
        }
        Json histogram = Json::array();
        for (const auto& [tau, count] : tauHistogram)
        { if (histogram.size() == 16) break; histogram.push_back({{"tauRoundedTo1024", double(tau) / 1024.0}, {"texels", count}}); }
        run.Expect(matching > 8, test.Name + " two real transparent layers produce the expected optical transmittance",
            {{"expectedTransmittance", expected}, {"expectedTau", -std::log(expected)}, {"matchingGpuTexels", matching},
                {"nonzeroGpuTexels", nonzero}, {"minimumNonzeroTau", nonzero ? minimumTau : 0},
                {"maximumTau", maximumTau}, {"closestTransmittance", closestTransmittance},
                {"tauHistogram", histogram}, {"rawGpuSamples", samples}});
        return;
    }
    const auto pixels = run.Read(map, test.Name + "-atlas");
    const auto hash = HashBytes(pixels.RGBA.data(), pixels.RGBA.size() * sizeof(float));
    run.AtlasHashes[test.Name] = hash;
    if (test.Scene == SceneCase::Cascades || test.Scene == SceneCase::MovedCascades)
    {
        const std::array<float, 4> splits{values.CascadeSplits.x, values.CascadeSplits.y,
            values.CascadeSplits.z, values.CascadeSplits.w};
        Json tiles = Json::array(); bool occupied = true, ordered = true;
        for (uint32_t i = 0; i < 4; ++i)
        {
            const auto count = Occupied(pixels, i % 2, i / 2, 2, 2); tiles.push_back(count); occupied &= count > 0;
            ordered &= std::isfinite(splits[i]) && splits[i] > (i ? splits[i - 1] : 0.f);
        }
        run.Expect(occupied && ordered && int(values.ModeParams.z) == 4 && stats && stats->RasterPasses == 4,
            test.Name + " renders actual caster depth into all four ordered cascade tiles", {{"occupiedDepthTexels", tiles}});
        bool boundaryFinite = true;
        Json boundaries = Json::array();
        const auto forward = (run.CameraTarget - run.CameraPosition).GetNormalize();
        for (uint32_t i = 0; i < 3; ++i) for (int side : {-1, 1})
        {
            const auto point = run.CameraPosition + forward * (splits[i] + float(side) * .5f);
            const auto clip = values.ShadowVP[i + (side > 0)].Transform(Vector4(point, 1));
            const bool finite = std::isfinite(clip.x) && std::isfinite(clip.y) && std::isfinite(clip.z)
                && std::isfinite(clip.w) && std::abs(clip.w) > 1e-7;
            boundaryFinite &= finite; boundaries.push_back({{"split", i}, {"side", side}, {"finiteProjection", finite}});
        }
        run.Expect(boundaryFinite, "both sides of actual cascade split boundaries have finite receiver projections", boundaries);
        if (test.Scene == SceneCase::Cascades)
            std::copy(std::begin(values.ShadowVP), std::begin(values.ShadowVP) + 4, run.FirstCascadeMatrices.begin());
        else
        {
            bool changed = false;
            for (size_t i = 0; i < 4; ++i) changed |= std::memcmp(&run.FirstCascadeMatrices[i], &values.ShadowVP[i], sizeof(Matrix)) != 0;
            run.Expect(changed, "moving the actual camera recomputes cascade projections");
            run.Different("deferred-csm4", test.Name, "camera motion changes the actual CSM scene pixels");
        }
    }
    else if (test.Scene == SceneCase::Dual)
    {
        const auto front = Occupied(pixels, 0, 0, 2, 1), back = Occupied(pixels, 1, 0, 2, 1);
        const auto frontSeam = Occupied(pixels, 0, 0, 2, 1, true), backSeam = Occupied(pixels, 1, 0, 2, 1, true);
        run.Expect(front > 0 && back > 0 && frontSeam > 0 && backSeam > 0
            && stats && stats->RasterPasses == 2 && stats->OutputTriangles >= stats->InputTriangles,
            test.Name + " renders front/back and the overlap caster into both actual paraboloid tiles",
            {{"frontDepthTexels", front}, {"backDepthTexels", back}, {"frontSeamTexels", frontSeam}, {"backSeamTexels", backSeam}});
    }
    else if (test.Scene == SceneCase::Volume || test.Scene == SceneCase::InsideVolume)
    {
        size_t shadow = 0, lit = 0;
        for (size_t p = 0; p < pixels.RGBA.size(); p += 4) { shadow += pixels.RGBA[p] < .1f; lit += pixels.RGBA[p] > .9f; }
        run.Expect(shadow > 4 && lit > 4 && stats && stats->OutputTriangles > 0,
            test.Name + " resolves both shadowed and lit pixels from true closed-volume stencil geometry",
            {{"shadowMaskPixels", shadow}, {"litMaskPixels", lit}});
        if (test.Scene == SceneCase::InsideVolume)
            run.Expect(run.CameraPosition.x > -150 && run.CameraPosition.x < -50
                && run.CameraPosition.z > 10 && run.CameraPosition.z < 110 && run.CameraPosition.y < 20,
                "camera is geometrically inside the downward closed-cube shadow extrusion");
    }
    else
    {
        bool varied = false;
        if (pixels.Summary.contains("minimum"))
            varied = pixels.Summary["maximum"][0].get<double>() > pixels.Summary["minimum"][0].get<double>() + 1e-6;
        run.Expect(varied, test.Name + " has nonconstant actual depth or moment pixels");
        if (test.Scene == SceneCase::Perspective)
        {
            run.Report["perspectiveResult"] = stats && stats->ProjectionFallback ? "explicit-orthographic-fallback" : "post-perspective-projection";
            const auto& matrix = values.ShadowVP[0];
            const bool projectiveW = std::abs(matrix.m03) + std::abs(matrix.m13) + std::abs(matrix.m23) > 1e-8f;
            run.Expect(stats && (stats->ProjectionFallback ? !stats->Diagnostic.empty()
                : int(values.ModeParams.y) == 2 && projectiveW),
                "PSM reports real post-perspective output separately from its explicit fallback", detail);
        }
        if (test.Shadow.Method == Method::Variance || test.Shadow.Method == Method::Exponential
            || test.Shadow.Method == Method::ExponentialVariance)
            run.Expect(stats && stats->ComputePasses == (test.Shadow.BlurRadius ? 3 : 1),
                test.Name + " records the actual encode and selected separable blur passes", detail);
    }
}
} // namespace
#endif

int RunAdvancedShadowIntegrationGpuTests(const char* ownedOutputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    Run run; RestoreFields restore;
    const auto oldKeys = g_KeyState; const auto oldMouse = g_MouseState;
    const float oldDelta = g_timeDeltaSecond;
    const bool oldWorkspace = IsAuthoringWorkspaceVisible(), oldRuntime = IsAuthoringRuntimeMode();
    const bool oldAuthoring = g_Editor && g_Editor->IsAuthoringEnabled();
    try
    {
        if (!ownedOutputDirectory || !*ownedOutputDirectory || !g_Engine || !g_rhi || !g_Editor || !ImGui::GetCurrentContext())
            throw std::runtime_error("Advanced shadow test requires initialized engine/RHI/editor and owned output");
        run.Output = fs::absolute(jEditorAssetPath::FromUtf8(ownedOutputDirectory)); fs::create_directories(run.Output);
        run.Report["status"] = "running"; run.Report["rhi"] = g_rhi->GetRHIName().ToStr(); run.Report["frameBudget"] = FrameBudget;
        run.Report["validationScope"] = "Native Vulkan/DX12/Metal validation messages are collected by the caller. Successful screenshots do not imply zero validation errors.";
        run.Report["opacityScope"] = "Actual authored object opacity, two additive optical-depth layer planes. No alpha-texture or generated opacity-graph coverage is claimed.";
        run.Report["receiverCasterPolicy"] = "Renderable Entity.ShadowCaster is saved per scene and applied by the normal authoring runtime. The open receiver is false except in deliberate nonmanifold rejection controls; Deep's visible opaque cubes are false and both transparent layers true.";
        if (!g_Engine->Game.WaitForSceneImportForTesting()) throw std::runtime_error("Startup scene did not finish before fixture replacement");
        ControlledOptions(restore); run.Warm(3); run.Capture("startup-existing-default-scene");
        auto& editor = *g_Editor; restore.Remember(editor.Placement.EnablePlacementMode);
        MakeFixture(run, editor);
        // Opening a real project applies its settings. Reassert controlled
        // values without registering a second restore snapshot.
        gOptions.AdvancedShadows = {}; gOptions.SceneFeatures = {}; gOptions.UseSurfelGI = false;
        gOptions.UseSSGI = false; gOptions.UseHWRTDirectLighting = false;
        std::vector<Case> cases;
        auto add = [&](const std::string& name, Settings settings, SceneCase scene = SceneCase::Normal,
                       Pipeline pipeline = Pipeline::Deferred, bool profile = false) {
            cases.push_back({name, settings, scene, pipeline, profile});
        };
        add("deferred-legacy", {}, SceneCase::Legacy, Pipeline::Deferred, true);
        add("deferred-csm4", TestSettings(Method::Depth, Projection::Cascaded), SceneCase::Cascades, Pipeline::Deferred, true);
        add("deferred-csm4-camera-moved", TestSettings(Method::Depth, Projection::Cascaded), SceneCase::MovedCascades);
        for (auto pair : {std::pair{Method::Variance, "vsm"}, {Method::Exponential, "esm"}, {Method::ExponentialVariance, "evsm"}})
        {
            auto settings = TestSettings(pair.first); settings.BlurRadius = 0;
            add(std::string("deferred-") + pair.second + "-unblurred", settings);
            settings.BlurRadius = 3; add(std::string("deferred-") + pair.second + "-blurred", settings);
        }
        add("deferred-deep-half", TestSettings(Method::Deep), SceneCase::DeepHalf);
        add("deferred-deep-three-quarter", TestSettings(Method::Deep), SceneCase::DeepThreeQuarter);
        auto dual = TestSettings(); dual.PointProjection = jAdvancedShadows::PointProjection::DualParaboloid;
        add("deferred-dual-front-back-seam", dual, SceneCase::Dual);
        add("deferred-psm", TestSettings(Method::Depth, Projection::Perspective), SceneCase::Perspective);
        add("deferred-volume", TestSettings(Method::ShadowVolume), SceneCase::Volume);
        add("deferred-volume-camera-inside", TestSettings(Method::ShadowVolume), SceneCase::InsideVolume);
        add("deferred-volume-nonmanifold-fallback", TestSettings(Method::ShadowVolume), SceneCase::InvalidVolume);
        add("per-light-override-isolation", {}, SceneCase::OverrideIsolation);
        add("forward-legacy", {}, SceneCase::Legacy, Pipeline::Forward, true);
        add("forward-csm4", TestSettings(Method::Depth, Projection::Cascaded), SceneCase::Cascades, Pipeline::Forward);
        add("forward-evsm", TestSettings(Method::ExponentialVariance), SceneCase::Normal, Pipeline::Forward, true);
        add("forward-dual", dual, SceneCase::Dual, Pipeline::Forward);
        add("forward-deep", TestSettings(Method::Deep), SceneCase::DeepHalf, Pipeline::Forward);
        add("forward-volume", TestSettings(Method::ShadowVolume), SceneCase::Volume, Pipeline::Forward);
        add("deferred-legacy-restored", {}, SceneCase::Legacy);
        for (const auto& test : cases)
        {
            ConfigureCase(run, editor, test); run.Warm(3);
            if (test.Profile) run.Measure(test.Name);
            const auto frame = g_rhi->GetCurrentFrameNumber();
            run.Capture(test.Name); ValidateOutput(run, editor, test);
            run.Expect(g_rhi->GetCurrentFrameNumber() == frame, "captures/readback do not add hidden render frames to timing samples");
        }
        for (const std::string method : {"vsm", "esm", "evsm"})
        {
            const auto before = "deferred-" + method + "-unblurred", after = "deferred-" + method + "-blurred";
            run.Expect(run.AtlasHashes.count(before) && run.AtlasHashes.count(after)
                && run.AtlasHashes[before] != run.AtlasHashes[after], method + " blur changes actual GPU moment texels");
        }
        run.Different("deferred-deep-half", "deferred-deep-three-quarter", "authored opacity changes real Deep shadow scene pixels");
        run.Different("deferred-legacy", "deferred-csm4", "selected CSM changes actual Deferred scene output");
        run.Different("forward-legacy", "forward-evsm", "selected EVSM changes actual Forward scene output");
        run.Expect(run.Images["deferred-legacy"].value("pixelHash", std::string{})
            == run.Images["deferred-legacy-restored"].value("pixelHash", std::string{}),
            "returning to default shadow settings reproduces the same actual baseline pixels");
        FinalizeReusableExample(run, cases.size());
        run.Report["caseCount"] = cases.size();
    }
    catch (const std::exception& error)
    {
        ++run.Failures; run.Report["exception"] = error.what();
        std::cerr << "FAIL advanced shadow GPU integration: " << error.what() << std::endl;
    }
    if (g_Editor)
    {
        g_Editor->SetAuthoringEnabled(oldAuthoring);
        if (run.FixtureReady) { g_Editor->CancelPendingImports(); g_Editor->ResetWorldBindings(false);
            g_Engine->Game.ClearImportedSceneForAuthoring(); g_Editor->GetDocument().NewScene(); }
    }
    SetAuthoringRuntimeMode(oldRuntime); SetAuthoringWorkspaceVisible(oldWorkspace);
    restore.Restore(); g_KeyState = oldKeys; g_MouseState = oldMouse; g_timeDeltaSecond = oldDelta;
    run.Report["renderedFrames"] = run.Frames; run.Report["failureCount"] = run.Failures;
    run.Report["status"] = run.Failures ? "failed" : "passed";
    if (!run.Output.empty()) Write(run.Output / "advanced-shadows-gpu-report.json", run.Report);
    std::cout << "Advanced shadow GPU workflow: " << run.Frames << " actual frames, " << run.Failures << " failures" << std::endl;
    return run.Failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)ownedOutputDirectory;
    return EXIT_FAILURE;
#endif
}
