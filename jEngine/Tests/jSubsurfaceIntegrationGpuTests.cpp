#include "pch.h"
#include "jSubsurfaceIntegrationGpuTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "jEditorIntegrationTests.h"
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "FileLoader/jEditorAssets.h"
#include "Material/jMaterial.h"
#include "Renderer/jSubsurfacePass.h"
#include "RHI/jRenderTarget.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/Light/jLight.h"
#include "jEngine.h"
#include "jOptions.h"
#include "lodepng.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace jEditorAuthoring;
using namespace jEditorTools;
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t FrameBudget = 96;
constexpr float ThicknessWorld = 50.0f;
constexpr std::size_t ProbeRowCount = 3;

struct Bounds { Vector Minimum, Maximum; };
Bounds WorldBounds(const jObject* object)
{
    Bounds result{Vector(std::numeric_limits<float>::max()), Vector(-std::numeric_limits<float>::max())};
    std::size_t count = 0;
    if (object) for (const auto* mesh : object->RenderObjects)
    {
        const auto geometry = mesh ? mesh->GeometryDataPtr : nullptr;
        const auto stream = geometry ? geometry->VertexStream_PositionOnlyPtr : nullptr;
        if (!stream || stream->Params.empty() || stream->ElementCount <= 0)
            throw std::runtime_error("Subsurface fixture has no CPU-readable position stream");
        const auto& parameter = stream->Params.front();
        const auto vertices = std::size_t(stream->ElementCount);
        const auto stride = parameter->Stride > 0 ? std::size_t(parameter->Stride) : sizeof(float) * 3;
        if (vertices > 1000000 || stride < sizeof(float) * 3 || parameter->Attributes.empty() ||
            parameter->Attributes.front().UnderlyingType != EBufferElementType::FLOAT ||
            parameter->GetBufferSize() < (vertices - 1) * stride + sizeof(float) * 3)
            throw std::runtime_error("Subsurface fixture position stream layout is unsupported");
        const auto* data = static_cast<const unsigned char*>(parameter->GetBufferData());
        for (std::size_t i = 0; i < vertices; ++i)
        {
            float xyz[3]; std::memcpy(xyz, data + i * stride, sizeof(xyz));
            const auto position = mesh->World.TransformPoint(Vector(xyz[0], xyz[1], xyz[2]));
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                throw std::runtime_error("Subsurface fixture bounds are nonfinite");
            result.Minimum.x = std::min(result.Minimum.x, position.x); result.Maximum.x = std::max(result.Maximum.x, position.x);
            result.Minimum.y = std::min(result.Minimum.y, position.y); result.Maximum.y = std::max(result.Maximum.y, position.y);
            result.Minimum.z = std::min(result.Minimum.z, position.z); result.Maximum.z = std::max(result.Maximum.z, position.z);
            ++count;
        }
    }
    if (!count) throw std::runtime_error("Subsurface fixture geometry is empty");
    return result;
}

bool OccludedByBounds(const Vector& eye, const Vector& point, const Bounds& box)
{
    const auto direction = point - eye;
    const std::array<float, 3> origin{eye.x, eye.y, eye.z}, ray{direction.x, direction.y, direction.z};
    const std::array<float, 3> minimum{box.Minimum.x, box.Minimum.y, box.Minimum.z};
    const std::array<float, 3> maximum{box.Maximum.x, box.Maximum.y, box.Maximum.z};
    float nearDistance = .00001f, farDistance = .9999f;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (std::abs(ray[axis]) < 1.0e-8f)
        {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
            continue;
        }
        float a = (minimum[axis] - origin[axis]) / ray[axis], b = (maximum[axis] - origin[axis]) / ray[axis];
        if (a > b) std::swap(a, b);
        nearDistance = std::max(nearDistance, a); farDistance = std::min(farDistance, b);
        if (nearDistance > farDistance) return false;
    }
    return true;
}

struct ReceiverProbe { Vector Shadow, Lit, FarShadow, InactiveShadow; };

void Write(const fs::path& path, const Json& value)
{
    std::ofstream file(path, std::ios::binary);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << value.dump(2) << '\n';
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
        for (auto i = Actions.rbegin(); i != Actions.rend(); ++i) (*i)();
        Actions.clear();
    }
    ~RestoreFields() { Restore(); }
};

void RememberOptions(RestoreFields& restore)
{
    restore.Remember(gOptions.AdvancedShadows); restore.Remember(gOptions.SceneFeatures);
    restore.Remember(gOptions.Subsurface); restore.Remember(gOptions.HiZOcclusion);
    restore.Remember(gOptions.RequestedMSAASamples); restore.Remember(gOptions.CameraPos);
    restore.Remember(gOptions.UseDeferredRenderer); restore.Remember(gOptions.UseSubpass);
    restore.Remember(gOptions.UseMemoryless); restore.Remember(gOptions.UseHWRTDirectLighting);
    restore.Remember(gOptions.UseSurfelGI); restore.Remember(gOptions.UseSSGI);
    restore.Remember(gOptions.UseSSGITemporalAccumulation); restore.Remember(gOptions.AOType);
    restore.Remember(gOptions.AOIntensity); restore.Remember(gOptions.ShowAOOnly);
    restore.Remember(gOptions.ShowSSGIOnly); restore.Remember(gOptions.BloomEyeAdaptation);
    restore.Remember(gOptions.UseAtmosphericShadowing); restore.Remember(gOptions.UseVRS);
    restore.Remember(gOptions.ShowVRSArea); restore.Remember(gOptions.ShowGrid);
    restore.Remember(gOptions.ShowDebugObject); restore.Remember(gOptions.ShowDebugRT);
    restore.Remember(gOptions.EarthQuake); restore.Remember(gOptions.ShadowFilterMode);
    restore.Remember(gOptions.ShadowFilterRadiusTexels);
}

void ControlledOptions()
{
    gOptions.AdvancedShadows = {}; gOptions.SceneFeatures = {};
    gOptions.Subsurface = {}; gOptions.HiZOcclusion = {}; gOptions.RequestedMSAASamples = 1;
    gOptions.UseDeferredRenderer = true; gOptions.UseSubpass = false; gOptions.UseMemoryless = false;
    gOptions.UseHWRTDirectLighting = false; gOptions.UseSurfelGI = false; gOptions.UseSSGI = false;
    gOptions.UseSSGITemporalAccumulation = false; gOptions.AOType = 0; gOptions.AOIntensity = 0;
    gOptions.ShowAOOnly = false; gOptions.ShowSSGIOnly = false; gOptions.BloomEyeAdaptation = false;
    gOptions.UseAtmosphericShadowing = false; gOptions.UseVRS = false; gOptions.ShowVRSArea = false;
    gOptions.ShowGrid = false; gOptions.ShowDebugObject = false; gOptions.ShowDebugRT = false;
    gOptions.EarthQuake = false; gOptions.ShadowFilterMode = 0; gOptions.ShadowFilterRadiusTexels = 0;
    // A deterministic actual geometric shadow supplies the step in incoming
    // diffuse illumination. Every SSS/control case retains this exact setup.
    gOptions.AdvancedShadows.DirectionalProjection = jAdvancedShadows::Projection::Cascaded;
    gOptions.AdvancedShadows.MapResolution = 512; gOptions.AdvancedShadows.CascadeCount = 4;
    gOptions.AdvancedShadows.MaxDistance = 400; gOptions.AdvancedShadows.CasterPadding = 100;
    gOptions.AdvancedShadows.BlurRadius = 0;
}

void Change(jEditor& editor, EntityId id, const std::function<void(Entity&)>& change)
{
    const auto* entity = editor.GetDocument().Find(id);
    if (!entity) throw std::runtime_error("Subsurface fixture entity disappeared");
    auto value = *entity; change(value);
    if (!(value == *entity) && !editor.GetDocument().Update(value, "Subsurface fixture"))
        throw std::runtime_error(editor.GetDocument().LastError());
}

struct Image
{
    std::uint32_t Width = 0, Height = 0;
    std::vector<float> Values;
    bool Valid() const { return Width && Height && Values.size() == std::size_t(Width) * Height * 4; }
};

Json Patch(const Image& image, const Matrix& vp, const Vector& world)
{
    Json result{{"world", {world.x, world.y, world.z}}, {"valid", false}, {"radiusPixels", 1}};
    if (!image.Valid()) return result;
    const auto clip = vp.Transform(Vector4(world, 1));
    if (!std::isfinite(clip.w) || clip.w <= 1.0e-7f || !std::isfinite(clip.x) ||
        !std::isfinite(clip.y) || !std::isfinite(clip.z) || clip.z < 0 || clip.z > clip.w) return result;
    const int x = int(std::floor((double(clip.x) / clip.w * .5 + .5) * image.Width));
    const int y = int(std::floor((.5 - double(clip.y) / clip.w * .5) * image.Height));
    result["pixel"] = {x, y};
    if (x < 1 || y < 1 || x + 1 >= int(image.Width) || y + 1 >= int(image.Height)) return result;
    std::array<double, 4> mean{};
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
    {
        const auto index = (std::size_t(y + dy) * image.Width + std::size_t(x + dx)) * 4;
        for (std::size_t c = 0; c < 4; ++c) mean[c] += image.Values[index + c] / 9.0;
    }
    result["meanRgba"] = mean;
    result["meanLuminance"] = .2126 * mean[0] + .7152 * mean[1] + .0722 * mean[2];
    result["valid"] = true;
    return result;
}

struct Case
{
    const char* Name;
    bool Enabled = true;
    float Strength = 1.0f, MaterialWeight = 1.0f, Thickness = ThicknessWorld;
    std::uint32_t Profile = 0;
    bool Graph = false;
    std::uint32_t Samples = 1;
};

struct Run
{
    fs::path Output, Fixture, ProjectFile;
    Json Report{{"schemaVersion", 1}, {"test", "subsurface-gpu-integration"},
        {"checks", Json::array()}, {"cases", Json::array()}, {"captures", Json::object()}};
    std::uint32_t Frames = 0;
    int Failures = 0;
    bool FixtureReady = false;
    EntityId Floor = 0, Caster = 0, Sun = 0, Camera = 0, SpecularWitness = 0, EmissiveWitness = 0;
    Vector CameraPosition{0, 100, -160}, CameraTarget{0, 0, 0};
    std::array<ReceiverProbe, ProbeRowCount> Probes;
    std::array<Vector, 2> MetadataPoints{Vector(-75, 0, -35), Vector(75, 0, -35)};
    Vector SpecularPoint, EmissivePoint;
    std::map<std::string, Image> Captures;

    void Progress()
    {
        Report["renderedFrames"] = Frames; Report["failureCount"] = Failures;
        if (!Output.empty()) Write(Output / "subsurface-progress.json", Report);
    }
    void Expect(bool condition, const std::string& name, const Json& detail = Json::object())
    {
        Report["checks"].push_back({{"name", name}, {"pass", condition}, {"frame", Frames}, {"detail", detail}});
        Failures += !condition;
        std::cout << (condition ? "PASS " : "FAIL ") << "subsurface: " << name << std::endl;
        Progress();
    }
    void PlaceCamera()
    {
        if (!FixtureReady) return;
        auto* camera = g_Engine->Game.MainCamera;
        if (!camera) throw std::runtime_error("Subsurface fixture lost its camera");
        jCamera::SetCamera(camera, CameraPosition, CameraTarget, CameraPosition + Vector::UpVector,
            DegreeToRadian(55), 1, 800, SCR_WIDTH, SCR_HEIGHT, true, 250);
        camera->UpdateCamera(); gOptions.CameraPos = CameraPosition;
    }
    double Frame()
    {
        if (Frames >= FrameBudget) throw std::runtime_error("Subsurface harness exceeded its 96-frame budget");
        const auto start = Clock::now();
        ResetAllKeyState(); ResetMouseAllState(); g_timeDeltaSecond = 1.0f / 60.0f;
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
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    void Warm(unsigned frames) { while (frames--) Frame(); }
    void UpdateProbes(const std::string& name)
    {
        const auto caster = WorldBounds(g_Editor->ObjectForEntity(Caster));
        const auto specular = WorldBounds(g_Editor->ObjectForEntity(SpecularWitness));
        const auto emissive = WorldBounds(g_Editor->ObjectForEntity(EmissiveWitness));
        const auto* light = g_Editor->LightForEntity(Sun);
        if (!light) throw std::runtime_error("Subsurface fixture lost its directional light");
        const auto direction = light->GetDirection().GetNormalize();
        // This fixture is deliberately axis-aligned with a light in the YZ
        // plane. Compute its actual projected box footprint, rather than assume
        // a shadow length or select dark pixels from an already rendered image.
        if (direction.y >= -.1f || std::abs(direction.x) > .0001f || caster.Minimum.y <= 0)
            throw std::runtime_error("Subsurface fixture light/caster does not match its geometric edge contract");
        float firstZ = std::numeric_limits<float>::max(), lastZ = -firstZ;
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vector point(corner & 1 ? caster.Maximum.x : caster.Minimum.x,
                corner & 2 ? caster.Maximum.y : caster.Minimum.y,
                corner & 4 ? caster.Maximum.z : caster.Minimum.z);
            const auto floor = point - direction * (point.y / direction.y);
            firstZ = std::min(firstZ, floor.z); lastZ = std::max(lastZ, floor.z);
        }
        const float begin = firstZ + 2.0f;
        const float end = std::min(lastZ, caster.Minimum.z) - 2.0f;
        if (end - begin < 6.0f) throw std::runtime_error("Subsurface fixture has insufficient camera-visible shadow edge");
        Json rows = Json::array();
        bool receiversVisible = true;
        for (std::size_t i = 0; i < Probes.size(); ++i)
        {
            const float z = begin + (end - begin) * float(i + 1) / float(Probes.size() + 1);
            Probes[i] = {Vector(caster.Minimum.x + 1, 0, z), Vector(caster.Minimum.x - 1, 0, z),
                Vector(caster.Minimum.x + 4, 0, z), Vector(caster.Maximum.x - 1, 0, z)};
            receiversVisible &= !OccludedByBounds(CameraPosition, Probes[i].Shadow, caster) &&
                !OccludedByBounds(CameraPosition, Probes[i].Lit, caster);
            rows.push_back(z);
        }
        SpecularPoint = Vector((specular.Minimum.x + specular.Maximum.x) * .5f,
            specular.Maximum.y, (specular.Minimum.z + specular.Maximum.z) * .5f);
        EmissivePoint = Vector((emissive.Minimum.x + emissive.Maximum.x) * .5f,
            emissive.Maximum.y, (emissive.Minimum.z + emissive.Maximum.z) * .5f);
        const bool witnessesVisible = !OccludedByBounds(CameraPosition, SpecularPoint, specular) &&
            !OccludedByBounds(CameraPosition, SpecularPoint, caster) &&
            !OccludedByBounds(CameraPosition, EmissivePoint, emissive) &&
            !OccludedByBounds(CameraPosition, EmissivePoint, caster);
        bool metadataVisible = true;
        for (const auto& point : MetadataPoints)
            metadataVisible &= !OccludedByBounds(CameraPosition, point, caster) &&
                !OccludedByBounds(CameraPosition, point, specular) &&
                !OccludedByBounds(CameraPosition, point, emissive);
        const Json geometry{{"casterMinimum", {caster.Minimum.x, caster.Minimum.y, caster.Minimum.z}},
            {"casterMaximum", {caster.Maximum.x, caster.Maximum.y, caster.Maximum.z}},
            {"projectedShadowZ", {firstZ, lastZ}}, {"cameraVisibleInsetEdgeZ", {begin, end}},
            {"receiverWorldZ", rows}, {"receiversVisible", receiversVisible}, {"witnessesVisible", witnessesVisible},
            {"metadataFloorPoints", {{-75, 0, -35}, {75, 0, -35}}}, {"metadataVisible", metadataVisible},
            {"witnessPolicy", "Actual transformed top-face center; ray/bounds visibility plus actual HDR material-content check."}};
        Report["fixtureGeometry"][name] = geometry;
        Expect(receiversVisible && witnessesVisible && metadataVisible,
            name + " uses actual projected caster geometry and visible material/witness probes", geometry);
    }
    void Measure(const std::string& name)
    {
        Warm(2);
        std::vector<double> frames;
        for (int i = 0; i < 4; ++i) frames.push_back(Frame());
        auto sorted = frames; std::sort(sorted.begin(), sorted.end());
        Report["timing"][name] = {{"warmupFramesExcluded", 2}, {"sampleFrames", 4},
            {"frameCpuMilliseconds", frames}, {"medianMilliseconds", (sorted[1] + sorted[2]) * .5},
            {"scope", "Same executable ProcessInput/Update/Draw/EndOfFrame/present/event-poll CPU latency. Captures/readback and scene edits excluded; no GPU-only speedup claim."}};
    }
    Image Read(const std::shared_ptr<jRenderTarget>& target, const std::string& name)
    {
        Image image; std::string error;
        const bool read = target && ReadSceneFeatureTextureFloats(target->GetTexturePtr(),
            image.Values, image.Width, image.Height, error);
        std::size_t nonfinite = 0;
        for (float value : image.Values) nonfinite += !std::isfinite(value);
        const Json summary{{"readback", read}, {"width", image.Width}, {"height", image.Height},
            {"nonfiniteValues", nonfinite}, {"error", error}};
        Expect(read && image.Valid() && nonfinite == 0, name + " has actual finite GPU float pixels", summary);
        Report["floatTargets"][name] = summary;
        if (!read || nonfinite) image = {};
        return image;
    }
    void Capture(const Case& test)
    {
        UpdateProbes(test.Name);
        const auto path = Output / (std::string(test.Name) + ".png");
        const auto summary = Json::parse(CaptureSceneFeaturePixels(jEditorAssetPath::ToUtf8(path).c_str()));
        Expect(summary.value("readback", false) && summary.value("pngSaved", false) &&
            summary.value("litPixels", std::size_t(0)) > 4, std::string(test.Name) + " saves actual nonblack scene PNG", summary);
        unsigned char* raw = nullptr; unsigned width = 0, height = 0;
        const auto error = LodePNG_decode32_file(&raw, &width, &height, jEditorAssetPath::ToUtf8(path).c_str());
        const std::unique_ptr<unsigned char, decltype(&std::free)> decoded(raw, &std::free);
        Image png;
        if (!error && decoded && width && height)
        {
            png.Width = width; png.Height = height; png.Values.resize(std::size_t(width) * height * 4);
            for (std::size_t i = 0; i < png.Values.size(); ++i) png.Values[i] = decoded.get()[i];
        }
        Expect(png.Valid(), std::string(test.Name) + " decodes its actual PNG for receiver measurements");
        Captures[test.Name] = std::move(png);
        Report["captures"][test.Name] = summary;
        Report["captures"][test.Name]["png"] = path.generic_string();
        auto& rois = Report["receiverRois"][test.Name];
        const auto vp = g_Engine->Game.MainCamera->ViewProjection;
        rois["cameraVP"] = Json::array(); for (float value : vp.mm) rois["cameraVP"].push_back(value);
        rois["geometry"] = Json::array();
        for (auto id : {Floor, Caster, SpecularWitness, EmissiveWitness})
        {
            const auto* object = g_Editor->ObjectForEntity(id);
            Json meshes = Json::array();
            if (object) for (const auto* mesh : object->RenderObjects)
            {
                Json matrix = Json::array();
                if (mesh) for (float value : mesh->World.mm) matrix.push_back(value);
                meshes.push_back(matrix);
            }
            rois["geometry"].push_back(meshes);
        }
        if (const auto* light = g_Editor->LightForEntity(Sun))
        {
            const auto direction = light->GetDirection();
            const auto* entity = g_Editor->GetDocument().Find(Sun);
            rois["light"] = {{"direction", {direction.x, direction.y, direction.z}},
                {"intensity", entity->Light.Intensity},
                {"color", {entity->Light.Color.X, entity->Light.Color.Y, entity->Light.Color.Z}}};
        }
        rois["rows"] = Json::array();
        for (const auto& probe : Probes)
            rois["rows"].push_back({{"shadow", Patch(Captures[test.Name], vp, probe.Shadow)},
                {"lit", Patch(Captures[test.Name], vp, probe.Lit)},
                {"farShadow", Patch(Captures[test.Name], vp, probe.FarShadow)},
                {"inactiveShadow", Patch(Captures[test.Name], vp, probe.InactiveShadow)}});
        rois["specularWitness"] = Patch(Captures[test.Name], vp, SpecularPoint);
        rois["emissiveWitness"] = Patch(Captures[test.Name], vp, EmissivePoint);
        rois["inactiveFloor"] = Patch(Captures[test.Name], vp, MetadataPoints[1]);
    }
};

void MakeGraphFixture(Run& run)
{
    fs::create_directories(run.Fixture / "Assets");
    fs::create_directories(run.Fixture / "Materials");
    constexpr unsigned size = 32;
    std::vector<unsigned char> pixels(size * size * 4, 255);
    for (unsigned y = 0; y < size; ++y) for (unsigned x = size / 2; x < size; ++x)
        for (unsigned c = 0; c < 3; ++c) pixels[(y * size + x) * 4 + c] = 0;
    const auto path = run.Fixture / "Assets/sss-mask.png";
    if (LodePNG_encode32_file(jEditorAssetPath::ToUtf8(path).c_str(), pixels.data(), size, size))
        throw std::runtime_error("Subsurface fixture mask data could not be encoded");
    MaterialGraph graph; graph.Version = 2;
    MaterialNode base; base.Id = 1; base.Kind = NodeKind::Color; base.Color = {.55f, .55f, .55f};
    MaterialNode rough; rough.Id = 2; rough.Kind = NodeKind::Scalar; rough.Scalar = .8f;
    MaterialNode uv; uv.Id = 3; uv.Kind = NodeKind::UV; uv.Parameters = {1, 1, 0, 0};
    MaterialNode texture; texture.Id = 4; texture.Kind = NodeKind::TextureObject;
    texture.Texture = "Assets/sss-mask.png"; texture.Semantic = jMaterialMapping::TextureSemantic::Linear;
    MaterialNode sample; sample.Id = 5; sample.Kind = NodeKind::TextureSample; sample.InputA = 4; sample.InputB = 3;
    MaterialNode channel; channel.Id = 6; channel.Kind = NodeKind::Channel; channel.InputA = 5; channel.Parameters = {0, 0, 0, 0};
    MaterialNode thickness; thickness.Id = 7; thickness.Kind = NodeKind::Scalar; thickness.Scalar = ThicknessWorld;
    MaterialNode profile; profile.Id = 8; profile.Kind = NodeKind::DiffusionProfile; profile.Parameters = {1, 0, 0, 0};
    MaterialNode white; white.Id = 9; white.Kind = NodeKind::Color; white.Color = {1, 1, 1};
    graph.Nodes = {base, rough, uv, texture, sample, channel, thickness, profile, white};
    graph.BaseColor = 1; graph.Roughness = 2; graph.SubsurfaceWeight = 6;
    graph.SubsurfaceColor = 9; graph.SubsurfaceThickness = 7; graph.SubsurfaceProfile = 8;
    std::string error;
    const auto asset = run.Fixture / "Materials/masked-wax.jmaterial";
    if (!SaveMaterial(graph, asset, error)) throw std::runtime_error(error);
    MaterialGraph loaded;
    if (!LoadMaterial(asset, loaded, error)) throw std::runtime_error(error);
    run.Expect(loaded.SubsurfaceWeight == 6 && loaded.SubsurfaceThickness == 7 && loaded.SubsurfaceProfile == 8,
        "texture-driven SSS graph outputs persist through the actual asset loader");
}

void MakeFixture(Run& run, jEditor& editor)
{
    editor.CancelPendingImports(); editor.ResetWorldBindings(false);
    g_Engine->Game.ClearImportedSceneForAuthoring();
    if (!editor.GetDocument().NewScene("Subsurface integration")) throw std::runtime_error(editor.GetDocument().LastError());
    run.Fixture = run.Output / ("fixture-" + std::to_string(Clock::now().time_since_epoch().count()));
    jEditorProject project; std::string error;
    if (!project.Create(run.Fixture, "Subsurface diffusion", error)) throw std::runtime_error(error);
    Document empty;
    if (!empty.Save(run.Fixture / "Scenes/start.jscene")) throw std::runtime_error(empty.LastError());
    project.EditConfig().StartupScene = "Scenes/start.jscene";
    project.EditConfig().Settings["render.deferred"] = "true";
    project.EditConfig().Settings["render.surfelgi"] = "false";
    project.EditConfig().Settings["render.ssgi"] = "false";
    if (!project.Save(error) || !OpenAuthoringProject(editor, project.GetProjectFile(), error)) throw std::runtime_error(error);
    run.ProjectFile = project.GetProjectFile(); run.Report["fixtureProject"] = run.ProjectFile.generic_string();
    SetAuthoringRuntimeMode(false); SetAuthoringWorkspaceVisible(true); editor.SetAuthoringEnabled(true);
    editor.Placement.EnablePlacementMode = true; editor.SetSelectedEntities({});
    if (!editor.GetDocument().Entities().empty()) throw std::runtime_error("Subsurface owned project was not empty");
    run.Floor = editor.CreateEntity(EntityKind::Terrain, "Authored subsurface receiver");
    run.Caster = editor.CreateEntity(EntityKind::Cube, "Sharp illumination edge caster");
    run.SpecularWitness = editor.CreateEntity(EntityKind::Cube, "Metallic specular witness, zero SSS diffuse");
    run.EmissiveWitness = editor.CreateEntity(EntityKind::Cube, "Non-SSS emission witness");
    run.Sun = editor.CreateEntity(EntityKind::DirectionalLight, "Fixed diffuse illumination");
    run.Camera = editor.CreateEntity(EntityKind::Camera, "Fixed receiver camera");
    Change(editor, run.Floor, [](Entity& e) {
        e.Local = {}; e.Terrain.Resolution = 9; e.Terrain.SizeX = 200; e.Terrain.SizeZ = 200;
        e.ShadowCaster = false; e.Material.OverrideEnabled = true;
        e.Material.BaseColor = {.55f, .55f, .55f}; e.Material.Roughness = .8f;
        e.Material.SubsurfaceOverrideEnabled = true; e.Material.SubsurfaceWeight = 1;
        e.Material.SubsurfaceColor = {1, 1, 1}; e.Material.SubsurfaceThicknessWorld = ThicknessWorld;
    });
    Change(editor, run.Caster, [](Entity& e) {
        e.Local = {}; e.Local.Position = {0, 18, 15}; e.Local.Scale = {1.5f, 1.5f, 1.5f};
        e.ShadowCaster = true; e.Material.OverrideEnabled = true;
        e.Material.BaseColor = {.08f, .12f, .16f}; e.Material.SubsurfaceOverrideEnabled = true;
    });
    Change(editor, run.SpecularWitness, [](Entity& e) {
        e.Local = {}; e.Local.Position = {55, 12, -10}; e.ShadowCaster = false;
        e.Material.OverrideEnabled = true; e.Material.BaseColor = {.8f, .85f, .9f};
        e.Material.Metallic = 1; e.Material.Roughness = .18f;
        e.Material.SubsurfaceOverrideEnabled = true; e.Material.SubsurfaceWeight = 1;
    });
    Change(editor, run.EmissiveWitness, [](Entity& e) {
        e.Local = {}; e.Local.Position = {-55, 12, -10}; e.ShadowCaster = false;
        e.Material.OverrideEnabled = true; e.Material.BaseColor = {.05f, .05f, .05f};
        e.Material.Emissive = {.2f, .05f, .01f}; e.Material.SubsurfaceOverrideEnabled = true;
    });
    Change(editor, run.Sun, [](Entity& e) {
        e.Local = {}; e.Local.RotationDegrees = {40, 0, 0}; e.Light.Intensity = 1.8f;
        e.Light.CastShadows = true;
    });
    const auto forward = (run.CameraTarget - run.CameraPosition).GetNormalize();
    Change(editor, run.Camera, [&](Entity& e) {
        e.Local = {}; e.Local.Position = {run.CameraPosition.x, run.CameraPosition.y, run.CameraPosition.z};
        e.Local.RotationDegrees = {RadianToDegree(std::atan2(-forward.y,
            std::sqrt(forward.x * forward.x + forward.z * forward.z))), 0, 0};
        e.Camera.Primary = true; e.Camera.VerticalFovDegrees = 55; e.Camera.NearPlane = 1; e.Camera.FarPlane = 800;
    });
    MakeGraphFixture(run);
    run.FixtureReady = true;
}

void Configure(Run& run, jEditor& editor, const Case& test)
{
    ControlledOptions();
    gOptions.Subsurface.Enabled = test.Enabled; gOptions.Subsurface.Strength = test.Strength;
    gOptions.RequestedMSAASamples = int32(test.Samples);
    // One explicit support scale for the entire tiny world-unit fixture. It is
    // never tuned per profile/case; the engine's normal defaults stay untouched.
    gOptions.Subsurface.RadiusScale = 6; gOptions.Subsurface.MaxRadiusPixels = 64;
    Change(editor, run.Floor, [&](Entity& e) {
        e.Material.GraphAsset = test.Graph ? "Materials/masked-wax.jmaterial" : "";
        e.Material.GraphOverrides.reset(); e.Material.SubsurfaceWeight = test.Graph ? 0.0f : test.MaterialWeight;
        e.Material.SubsurfaceThicknessWorld = test.Graph ? 1.0f : test.Thickness;
        e.Material.SubsurfaceProfile = test.Graph ? 0 : test.Profile;
    });
    Change(editor, run.Sun, [](Entity& e) { e.Light.ShadowOverride = gOptions.AdvancedShadows; });
    if (!editor.SyncDocumentToWorld()) throw std::runtime_error(editor.LastError());
    jObject::FlushDirtyState(); run.PlaceCamera();
    std::string error;
    if (!g_Engine->Game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Deferred, error))
        throw std::runtime_error(error);
    const auto file = run.Fixture / (std::string("Scenes/") + test.Name + ".jscene");
    if (!editor.GetDocument().Save(file)) throw std::runtime_error(editor.GetDocument().LastError());
    Document reopened;
    const bool loaded = reopened.Load(file);
    const auto* material = loaded ? reopened.Find(run.Floor) : nullptr;
    run.Expect(material && material->Material == editor.GetDocument().Find(run.Floor)->Material,
        std::string(test.Name) + " saves/reloads the complete material SSS/graph data");
}

void ValidateGpu(Run& run, const Case& test)
{
    run.Expect(GetRendererFeatureDiagnostics().AppliedMSAASamples == int32(test.Samples),
        std::string(test.Name) + " applies the actual requested native sample count",
        {{"requested", test.Samples}, {"applied", GetRendererFeatureDiagnostics().AppliedMSAASamples}});
    jSubsurface::Inputs inputs; jSubsurface::Result output; std::uint64_t frame = 0;
    const bool fresh = jSubsurface::GetLastDebugOutput(frame, inputs, output);
    const bool active = test.Enabled && test.Strength > 0;
    run.Expect(fresh == active, std::string(test.Name) + " observes only current-frame SSS publication",
        {{"fresh", fresh}, {"frame", frame}, {"actualFrame", g_rhi->GetCurrentFrameNumber()}});
    if (!active || !fresh) return;
    run.Expect(output.Applied && output.ComputePasses == 3 && output.FilteredDiffuse && output.Output,
        std::string(test.Name) + " records both diffusion axes and separate composition",
        {{"passes", output.ComputePasses}, {"scratchBytes", output.AdditionalTargetBytes}, {"diagnostic", output.Diagnostic}});
    const auto single = [](const std::shared_ptr<jRenderTarget>& target) {
        return target && target->Info.SampleCount == EMSAASamples::COUNT_1 && !target->Info.IsMemoryless;
    };
    run.Expect(single(inputs.Mask) && single(inputs.DiffuseLighting) && single(inputs.Depth) && single(inputs.Normal),
        std::string(test.Name) + " consumes actual coherent stored single-sample inputs");
    const auto vp = g_Engine->Game.MainCamera->ViewProjection;
    const auto mask = run.Read(inputs.Mask, std::string(test.Name) + "-mask");
    const auto diffuse = run.Read(inputs.DiffuseLighting, std::string(test.Name) + "-original-diffuse");
    const auto filtered = run.Read(output.FilteredDiffuse, std::string(test.Name) + "-filtered-diffuse");
    const auto composed = run.Read(output.Output, std::string(test.Name) + "-composed");
    auto& gpu = run.Report["gpuRois"][test.Name];
    gpu["rows"] = Json::array();
    for (const auto& probe : run.Probes)
        gpu["rows"].push_back({{"originalShadow", Patch(diffuse, vp, probe.Shadow)},
            {"filteredShadow", Patch(filtered, vp, probe.Shadow)},
            {"originalLit", Patch(diffuse, vp, probe.Lit)},
            {"filteredLit", Patch(filtered, vp, probe.Lit)},
            {"filteredFarShadow", Patch(filtered, vp, probe.FarShadow)}});
    const auto specularInput = Patch(diffuse, vp, run.SpecularPoint);
    const auto specularResult = Patch(composed, vp, run.SpecularPoint);
    const auto emissiveInput = Patch(diffuse, vp, run.EmissivePoint);
    const auto emissiveResult = Patch(composed, vp, run.EmissivePoint);
    gpu["specularWitness"] = {{"originalDiffuse", specularInput}, {"composed", specularResult}};
    gpu["emissiveWitness"] = {{"originalDiffuse", emissiveInput}, {"composed", emissiveResult}};
    bool actualSpecular = specularInput.value("valid", false) && specularResult.value("valid", false);
    if (actualSpecular)
        actualSpecular = specularInput["meanLuminance"].get<double>() < .001 &&
            specularResult["meanLuminance"].get<double>() >= .01;
    run.Expect(actualSpecular, std::string(test.Name) + " samples actual visible metallic specular, not the diffuse floor", gpu["specularWitness"]);
    bool actualEmission = emissiveInput.value("valid", false) && emissiveResult.value("valid", false);
    if (actualEmission)
    {
        const double input = emissiveInput["meanLuminance"].get<double>();
        const auto& rgba = emissiveResult["meanRgba"];
        actualEmission = input < .05 && emissiveResult["meanLuminance"].get<double>() > input + .015 &&
            rgba[0].get<double>() > rgba[1].get<double>() * 1.3 && rgba[0].get<double>() > rgba[2].get<double>() * 1.3;
    }
    run.Expect(actualEmission, std::string(test.Name) + " samples actual visible red emission, not the neutral floor", gpu["emissiveWitness"]);
    for (std::size_t side = 0; side < run.MetadataPoints.size(); ++side)
    {
        const char* label = side == 0 ? "left" : "right";
        const auto& world = run.MetadataPoints[side];
        const auto material = Patch(mask, vp, world), lighting = Patch(diffuse, vp, world);
        gpu[label] = {{"mask", material}, {"diffuse", lighting}};
        const float weight = test.Graph ? (std::string(label) == "left" ? 1.0f : 0.0f) : test.MaterialWeight;
        const float thickness = test.Graph ? ThicknessWorld : test.Thickness;
        const float encodedProfile = weight > 0 && thickness > 0 ? float((test.Graph ? 1 : test.Profile) + 1) : 0.0f;
        bool correct = material.value("valid", false) && lighting.value("valid", false);
        if (correct)
        {
            for (int c = 0; c < 3; ++c)
                correct &= std::abs(material["meanRgba"][c].get<double>() - weight) < .01;
            correct &= std::abs(material["meanRgba"][3].get<double>() - encodedProfile) < .01;
            correct &= std::abs(lighting["meanRgba"][3].get<double>() - thickness) < .01;
        }
        run.Expect(correct, std::string(test.Name) + " publishes actual " + label + " weight/profile/thickness GPU pixels", gpu[label]);
    }
    if (test.Graph)
    {
        const auto* object = g_Editor->ObjectForEntity(run.Floor);
        bool actualProgram = object && !object->RenderObjects.empty();
        if (actualProgram)
        {
            const auto& material = object->RenderObjects.front()->MaterialPtr;
            actualProgram = material && material->MappingProgram && material->MappingProgram->Active;
        }
        run.Expect(actualProgram, "masked-wax actually renders the compiled texture/profile material graph");
    }
}

double Luma(const Json& patch)
{
    return patch.is_object() && patch.value("valid", false) ?
        patch.value("meanLuminance", 0.0) : std::numeric_limits<double>::quiet_NaN();
}

void IdentityControl(Run& run, const std::string& name)
{
    const auto& baseline = run.Captures.at("off"); const auto& control = run.Captures.at(name);
    bool valid = baseline.Valid() && control.Valid() && baseline.Width == control.Width && baseline.Height == control.Height;
    double maximum = 0, sum = 0;
    if (valid) for (std::size_t i = 0; i < baseline.Values.size(); ++i)
    {
        if (i % 4 == 3) continue;
        const double difference = std::abs(double(baseline.Values[i]) - control.Values[i]);
        maximum = std::max(maximum, difference); sum += difference;
    }
    const Json metrics{{"off", "off"}, {"control", name}, {"maximumByteDifference", maximum},
        {"meanByteDifference", valid ? sum / (std::size_t(baseline.Width) * baseline.Height * 3) : 0.0},
        {"allowedMaximumByteDifference", 1}};
    run.Report["negativeControls"][name] = metrics;
    run.Expect(valid && maximum <= 1.0, name + " reproduces actual OFF pixels with the same geometry/light", metrics);
}

void VisibleControl(Run& run, const std::string& name, const std::string& baseline)
{
    std::size_t readable = 0, pngGain = 0, pngLoss = 0, hdrGain = 0, hdrLoss = 0;
    Json result{{"baseline", baseline}, {"on", name}, {"minimumDisplayShadowGainBytes", 2.0},
        {"minimumDisplayLitLossBytes", 1.0}, {"minimumHdrTransferFractionOfLit", .02}, {"rows", Json::array()}};
    const auto& off = run.Report["receiverRois"][baseline];
    const auto& on = run.Report["receiverRois"][name];
    bool same = off.contains("cameraVP") && off["cameraVP"] == on["cameraVP"] &&
        off.contains("geometry") && off["geometry"] == on["geometry"] &&
        off.contains("light") && off["light"] == on["light"];
    if (off.contains("rows") && on.contains("rows") && run.Report["gpuRois"].contains(name))
    {
        const auto& gpu = run.Report["gpuRois"][name]["rows"];
        for (std::size_t i = 0; i < ProbeRowCount; ++i)
        {
            const auto& a = off["rows"][i]; const auto& b = on["rows"][i];
            const double originalShadow = Luma(a["shadow"]), originalLit = Luma(a["lit"]);
            const bool control = std::isfinite(originalShadow) && std::isfinite(originalLit) &&
                originalLit >= 20 && originalLit < 235 && originalShadow <= originalLit * .35;
            readable += control;
            const double displayGain = Luma(b["shadow"]) - originalShadow;
            const double displayLoss = originalLit - Luma(b["lit"]);
            const double inputLit = Luma(gpu[i]["originalLit"]);
            const double rawGain = Luma(gpu[i]["filteredShadow"]) - Luma(gpu[i]["originalShadow"]);
            const double rawLoss = inputLit - Luma(gpu[i]["filteredLit"]);
            pngGain += control && displayGain >= 2.0;
            pngLoss += control && displayLoss >= 1.0;
            hdrGain += control && inputLit > .01 && rawGain >= inputLit * .02;
            hdrLoss += control && inputLit > .01 && rawLoss >= inputLit * .02;
            same &= a["shadow"].value("pixel", Json{}) == b["shadow"].value("pixel", Json{});
            result["rows"].push_back({{"offReadableShadowStep", control}, {"displayShadowGain", displayGain},
                {"displayLitLoss", displayLoss}, {"hdrLitInput", inputLit}, {"hdrShadowGain", rawGain}, {"hdrLitLoss", rawLoss}});
        }
    }
    result["sameCameraAndReceiverPixels"] = same; result["readableRows"] = readable;
    result["displayGainRows"] = pngGain; result["displayLossRows"] = pngLoss;
    result["hdrGainRows"] = hdrGain; result["hdrLossRows"] = hdrLoss;
    run.Report["visibleDiffusionControls"][name] = result;
    run.Expect(same && readable >= 2 && pngGain >= 2 && pngLoss >= 2 && hdrGain >= 2 && hdrLoss >= 2,
        name + " visibly transfers only diffuse light across known geometric shadow edges", result);
    for (const std::string witness : {"specularWitness", "emissiveWitness"})
    {
        const double oldValue = Luma(off[witness]), newValue = Luma(on[witness]);
        run.Expect(std::isfinite(oldValue) && std::isfinite(newValue) && oldValue >= 8 &&
            std::abs(oldValue - newValue) <= 1.0,
            name + " preserves the actual inactive " + witness,
            {{"offLuma", oldValue}, {"onLuma", newValue}, {"maximumDifference", 1.0}});
    }
}

void FinalizeFixture(Run& run)
{
    jEditorProject project; std::string error;
    if (!project.Open(run.ProjectFile, error)) throw std::runtime_error(error);
    project.EditConfig().StartupScene = "Scenes/skin.jscene";
    auto& settings = project.EditConfig().Settings;
    settings["render.sss.enabled"] = "true"; settings["render.sss.strength"] = "1";
    settings["render.sss.radiusScale"] = "6"; settings["render.sss.maxRadiusPixels"] = "64";
    settings["render.msaa.samples"] = "1";
    if (!project.Save(error)) throw std::runtime_error(error);
    std::ofstream guide(run.Fixture / "GUIDE.md");
    guide.exceptions(std::ios::failbit | std::ios::badbit);
    guide << "# Subsurface diffusion controls\n\nOpen project.jproject: startup is the Skin receiver, not the last tested scene. "
        "All lengths use this scene's world units; these presets are approximate surface diffusion, with no hidden-surface transmission. "
        "The fixture uses one radiusScale6 and 64-pixel cap in all active cases to resolve the profiles at this camera scale.\n\n"
        "Scenes retain actual material fields, caster policy and graph assets. off/global-zero additionally require disabling project SSS or setting its strength0; "
        "material-zero/thickness-zero retain enabled SSS and exercise real material neutral controls. "
        "masked-wax's graph reads a linear mask texture, a thickness scalar and a Diffusion Profile node; its scalar material fallback deliberately differs. "
        "skin-msaa4 combines real per-sample Deferred lighting and SSS; select4 samples in project settings when opening that material scene by itself.\n\n"
        "Raw PNGs, camera-projected receivers, actual HDR target metrics, untouched metallic/emissive witnesses and all thresholds are in subsurface-gpu-report.json beside this fixture. "
        "GPU validation logs are owned by the native runner.\n";
}
}
#endif

int RunSubsurfaceIntegrationGpuTests(const char* ownedOutputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    Run run; RestoreFields restore;
    const auto oldKeys = g_KeyState; const auto oldMouse = g_MouseState; const float oldDelta = g_timeDeltaSecond;
    const bool oldWorkspace = IsAuthoringWorkspaceVisible(), oldRuntime = IsAuthoringRuntimeMode();
    const bool oldAuthoring = g_Editor && g_Editor->IsAuthoringEnabled();
    try
    {
        if (!ownedOutputDirectory || !*ownedOutputDirectory || !g_Engine || !g_rhi || !g_Editor || !ImGui::GetCurrentContext())
            throw std::runtime_error("Subsurface tests require initialized engine/RHI/editor and owned output");
        run.Output = fs::absolute(jEditorAssetPath::FromUtf8(ownedOutputDirectory)); fs::create_directories(run.Output);
        run.Report["status"] = "running"; run.Report["rhi"] = g_rhi->GetRHIName().ToStr();
        run.Report["frameBudget"] = FrameBudget;
        run.Report["validationScope"] = "Caller collects real native Vulkan/Metal/DX12 validation logs; image success alone does not prove validation cleanliness.";
        run.Report["projectionAndMaterialScope"] = "Deferred actual normal pipeline with1x controls plus combined4x MSAA+SSS case. Separable RGB approximate world-unit profiles, authored thickness bound, opaque geometry and a real texture-driven graph mask. No backface transmission or RT/PT parity is claimed.";
        if (!g_Engine->Game.WaitForSceneImportForTesting()) throw std::runtime_error("Startup scene import did not finish");
        RememberOptions(restore); ControlledOptions();
        auto& editor = *g_Editor; restore.Remember(editor.Placement.EnablePlacementMode);
        MakeFixture(run, editor);
        const std::array<Case, 9> cases = {{
            {"off", false}, {"global-zero", true, 0}, {"material-zero", true, 1, 0},
            {"thickness-zero", true, 1, 1, 0}, {"skin"}, {"wax", true, 1, 1, ThicknessWorld, 1},
            {"graph-off", false, 1, 1, ThicknessWorld, 1, true},
            {"graph-masked-wax", true, 1, 1, ThicknessWorld, 1, true},
            {"skin-msaa4", true, 1, 1, ThicknessWorld, 0, false, 4}
        }};
        for (const auto& test : cases)
        {
            Configure(run, editor, test); run.Warm(3);
            if (std::string(test.Name) == "off" || std::string(test.Name) == "skin") run.Measure(test.Name);
            const auto frame = g_rhi->GetCurrentFrameNumber();
            run.Capture(test); ValidateGpu(run, test);
            run.Expect(g_rhi->GetCurrentFrameNumber() == frame, "PNG capture/readback adds no hidden render frames");
            run.Report["cases"].push_back({{"name", test.Name}, {"enabled", test.Enabled}, {"strength", test.Strength},
                {"materialWeight", test.MaterialWeight}, {"thicknessWorld", test.Thickness}, {"profile", test.Profile},
                {"graph", test.Graph}, {"requestedSamples", test.Samples}});
        }
        IdentityControl(run, "global-zero"); IdentityControl(run, "material-zero"); IdentityControl(run, "thickness-zero");
        VisibleControl(run, "skin", "off"); VisibleControl(run, "wax", "off");
        VisibleControl(run, "graph-masked-wax", "graph-off");
        std::size_t changed = 0, inactiveStable = 0;
        for (std::size_t i = 0; i < ProbeRowCount; ++i)
        {
            const double skin = Luma(run.Report["gpuRois"]["skin"]["rows"][i]["filteredFarShadow"]);
            const double wax = Luma(run.Report["gpuRois"]["wax"]["rows"][i]["filteredFarShadow"]);
            const double reference = Luma(run.Report["gpuRois"]["skin"]["rows"][i]["originalLit"]);
            changed += std::isfinite(skin) && std::isfinite(wax) && reference > .01 && wax - skin >= reference * .01;
            const double masked = Luma(run.Report["receiverRois"]["graph-masked-wax"]["rows"][i]["inactiveShadow"]);
            const double off = Luma(run.Report["receiverRois"]["graph-off"]["rows"][i]["inactiveShadow"]);
            inactiveStable += std::isfinite(masked) && std::isfinite(off) && std::abs(masked - off) <= 1.0;
        }
        run.Expect(changed >= 2, "Wax and Skin produce distinct actual HDR diffusion widths", {{"meaningfullyDifferentRows", changed}});
        run.Expect(inactiveStable >= 2, "texture-masked inactive receivers retain their actual sharp shadow", {{"stableRows", inactiveStable}});
        const double inactiveFloor = Luma(run.Report["receiverRois"]["graph-masked-wax"]["inactiveFloor"]);
        const double inactiveFloorOff = Luma(run.Report["receiverRois"]["graph-off"]["inactiveFloor"]);
        run.Expect(std::isfinite(inactiveFloor) && std::isfinite(inactiveFloorOff) && inactiveFloorOff >= 20 &&
            std::abs(inactiveFloor - inactiveFloorOff) <= 1.0,
            "texture-masked visible inactive floor preserves actual baseline pixels",
            {{"world", {75, 0, -35}}, {"offLuma", inactiveFloorOff}, {"onLuma", inactiveFloor}, {"maximumDifference", 1.0}});
        FinalizeFixture(run); run.Report["caseCount"] = cases.size();
    }
    catch (const std::exception& error)
    {
        ++run.Failures; run.Report["exception"] = error.what();
        std::cerr << "FAIL subsurface GPU integration: " << error.what() << std::endl;
    }
    if (g_Editor)
    {
        g_Editor->SetAuthoringEnabled(oldAuthoring);
        if (run.FixtureReady)
        {
            g_Editor->CancelPendingImports(); g_Editor->ResetWorldBindings(false);
            g_Engine->Game.ClearImportedSceneForAuthoring(); g_Editor->GetDocument().NewScene();
        }
    }
    SetAuthoringRuntimeMode(oldRuntime); SetAuthoringWorkspaceVisible(oldWorkspace);
    restore.Restore(); g_KeyState = oldKeys; g_MouseState = oldMouse; g_timeDeltaSecond = oldDelta;
    run.Report["renderedFrames"] = run.Frames; run.Report["failureCount"] = run.Failures;
    run.Report["status"] = run.Failures ? "failed" : "passed";
    if (!run.Output.empty()) Write(run.Output / "subsurface-gpu-report.json", run.Report);
    std::cout << "Subsurface GPU workflow: " << run.Frames << " actual frames, " << run.Failures << " failures" << std::endl;
    return run.Failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)ownedOutputDirectory;
    return EXIT_FAILURE;
#endif
}
