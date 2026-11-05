#include "pch.h"
#include "jSubsurfaceHumanIntegrationTests.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "jEditorIntegrationTests.h"
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Code/Engine/Editor/jEditorProjectSettings.h"
#include "FileLoader/jEditorAssets.h"
#include "Material/jMaterial.h"
#include "Renderer/jSubsurfacePass.h"
#include "Renderer/jTextureSubsurfacePass.h"
#include "Renderer/jSkinShading.h"
#include "Renderer/Features/jEnvironmentLighting.h"
#include "RHI/jRenderTarget.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
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
#include <thread>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace jEditorAuthoring;
using namespace jEditorTools;
using Clock = std::chrono::steady_clock;
constexpr unsigned FrameBudget = 192;
constexpr float ModelScale = 300.0f;
constexpr const char* ModelAsset = "Assets/InfiniteHead/Infinite-Level_02.OBJ";
constexpr const char* TextureAsset = "Assets/InfiniteHead/Infinite-Level_02.jpg";
constexpr const char* NormalAsset = "Assets/InfiniteHead/Infinite-Level_02_World_NoSmoothUV.jpg";
constexpr const char* SpecularAsset = "Assets/InfiniteHead/SpecularAOMap.png";
constexpr const char* SkinMaterialAsset = "Materials/reference-skin.jmaterial";
constexpr const char* BackgroundMaterialAsset = "Materials/opaque-blue-background.jmaterial";
constexpr std::array<const char*, 4> RegionNames = {"nose", "cheek", "ear-side", "neck"};

void Write(const fs::path& path, const Json& json)
{
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << json.dump(2) << '\n';
}

struct RestoreFields
{
    std::vector<std::function<void()>> Actions;
    template<class T> void Remember(T& value)
    {
        const T original = value;
        Actions.emplace_back([p = &value, original] { *p = original; });
    }
    void Restore() { for (auto i = Actions.rbegin(); i != Actions.rend(); ++i) (*i)(); Actions.clear(); }
    ~RestoreFields() { Restore(); }
};

void RememberOptions(RestoreFields& r)
{
    r.Remember(gOptions.Subsurface); r.Remember(gOptions.TextureSubsurface); r.Remember(gOptions.SceneFeatures); r.Remember(gOptions.AdvancedShadows);
    r.Remember(gOptions.HiZOcclusion); r.Remember(gOptions.RequestedMSAASamples); r.Remember(gOptions.CameraPos);
    r.Remember(gOptions.UseDeferredRenderer); r.Remember(gOptions.UseSubpass); r.Remember(gOptions.UseMemoryless);
    r.Remember(gOptions.UseHWRTDirectLighting); r.Remember(gOptions.UseSurfelGI); r.Remember(gOptions.UseSSGI);
    r.Remember(gOptions.UseSSGITemporalAccumulation); r.Remember(gOptions.AOType); r.Remember(gOptions.AOIntensity);
    r.Remember(gOptions.ShowAOOnly); r.Remember(gOptions.ShowSSGIOnly); r.Remember(gOptions.BloomEyeAdaptation);
    r.Remember(gOptions.UseAtmosphericShadowing); r.Remember(gOptions.UseVRS); r.Remember(gOptions.ShowVRSArea);
    r.Remember(gOptions.ShowGrid); r.Remember(gOptions.ShowDebugObject); r.Remember(gOptions.ShowDebugRT);
    r.Remember(gOptions.EarthQuake); r.Remember(gOptions.ShadowFilterMode); r.Remember(gOptions.ShadowFilterRadiusTexels);
}

void ControlledOptions()
{
    gOptions.Subsurface = {}; gOptions.Subsurface.Enabled = true;
    gOptions.Subsurface.RadiusScale = 2; gOptions.Subsurface.MaxRadiusPixels = 32;
    gOptions.TextureSubsurface = {};
    gOptions.TextureSubsurface.WorldUnitsPerMillimeter = .3f;
    gOptions.TextureSubsurface.CaptureResolution = 2048;
    gOptions.TextureSubsurface.GaussianStepScale = 1.f;
    gOptions.SceneFeatures = {}; gOptions.SceneFeatures.Environment.Enabled = false;
    gOptions.AdvancedShadows = {};
    gOptions.AdvancedShadows.DirectionalProjection = jAdvancedShadows::Projection::Cascaded;
    gOptions.AdvancedShadows.MapResolution = 1024; gOptions.AdvancedShadows.CascadeCount = 4;
    gOptions.AdvancedShadows.MaxDistance = 600; gOptions.AdvancedShadows.CasterPadding = 80;
    gOptions.AdvancedShadows.BlurRadius = 0;
    gOptions.HiZOcclusion = {}; gOptions.RequestedMSAASamples = 1;
    gOptions.UseDeferredRenderer = true; gOptions.UseSubpass = false; gOptions.UseMemoryless = false;
    gOptions.UseHWRTDirectLighting = false; gOptions.UseSurfelGI = false; gOptions.UseSSGI = false;
    gOptions.UseSSGITemporalAccumulation = false; gOptions.AOType = 0; gOptions.AOIntensity = 0;
    gOptions.ShowAOOnly = false; gOptions.ShowSSGIOnly = false; gOptions.BloomEyeAdaptation = false;
    gOptions.UseAtmosphericShadowing = false; gOptions.UseVRS = false; gOptions.ShowVRSArea = false;
    gOptions.ShowGrid = false; gOptions.ShowDebugObject = false; gOptions.ShowDebugRT = false;
    gOptions.EarthQuake = false; gOptions.ShadowFilterMode = 1; gOptions.ShadowFilterRadiusTexels = 2;
}

void Change(jEditor& editor, EntityId id, const std::function<void(Entity&)>& change)
{
    const auto* old = editor.GetDocument().Find(id);
    if (!old) throw std::runtime_error("Human subsurface fixture entity disappeared");
    auto value = *old; change(value);
    if (!(value == *old) && !editor.GetDocument().Update(value, "Human subsurface fixture"))
        throw std::runtime_error(editor.GetDocument().LastError());
}

std::uint64_t HashFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary); file.exceptions(std::ios::badbit);
    if (!file) throw std::runtime_error("Human source asset could not be opened");
    std::uint64_t value = 1469598103934665603ull;
    std::array<char, 65536> buffer;
    while (file)
    {
        file.read(buffer.data(), buffer.size());
        for (std::streamsize i = 0; i < file.gcount(); ++i)
        { value ^= static_cast<unsigned char>(buffer[std::size_t(i)]); value *= 1099511628211ull; }
    }
    return value;
}

fs::path SeedAssets()
{
    const auto relative = fs::path("examples/subsurface-human-demo/Assets/InfiniteHead");
    for (const auto& candidate : {fs::path(__FILE__).parent_path().parent_path().parent_path() / relative,
        fs::current_path() / relative, fs::current_path().parent_path() / relative})
        if (fs::is_regular_file(candidate / "Infinite-Level_02.OBJ") &&
            fs::is_regular_file(candidate / "Infinite-Level_02.jpg")) return fs::absolute(candidate);
    throw std::runtime_error("Pinned human assets are missing from examples/subsurface-human-demo/Assets/InfiniteHead");
}

struct FloatImage
{
    unsigned Width = 0, Height = 0;
    std::vector<float> Values;
    bool Valid() const { return Width && Height && Values.size() == std::size_t(Width) * Height * 4; }
};
struct PngImage
{
    unsigned Width = 0, Height = 0;
    std::vector<unsigned char> Values;
    bool Valid() const { return Width && Height && Values.size() == std::size_t(Width) * Height * 4; }
};
struct Snapshot
{
    FloatImage Mask, Diffuse, Filtered, Composed;
    PngImage Png;
    Matrix VP;
    Json Transport=Json::object();
    bool HasTextureTransport=false;
};
struct Case
{
    const char* Name;
    float Weight, Thickness, GlobalStrength;
    bool TextureSpace=true, Transmission=true;
    float GaussianStep=1.f;
};

// Imported-local coordinates retain source X/Y and flip source Z. Classification
// is for measurement only; no anatomical mask changes the material or texture.
unsigned char Anatomy(const Vector& local)
{
    const float x = std::abs(local.x), y = local.y;
    if (x < .027f && y >= -.078f && y <= -.015f && local.z < .005f) return 1;
    if (x >= .027f && x < .065f && y >= -.10f && y <= .005f && local.z < .055f) return 2;
    if (x >= .065f && x <= .11f && y >= -.10f && y <= .025f) return 3;
    if (x < .10f && y >= -.225f && y <= -.13f) return 4;
    return 0;
}

std::vector<unsigned char> VisibleAnatomy(const jObject& object, const Matrix& vp,
    unsigned width, unsigned height, Json& diagnostics)
{
    if (!width || !height || std::uint64_t(width) * height > 4194304)
        throw std::runtime_error("Human measurement projection exceeds its4Mpixel CPU budget");
    std::vector<float> nearest(std::size_t(width) * height, std::numeric_limits<float>::infinity());
    std::vector<unsigned char> regions(nearest.size(), 0);
    std::uint64_t tests = 0, triangles = 0;
    Vector minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
    for (const auto* mesh : object.RenderObjects)
    {
        const auto geometry = mesh ? mesh->GeometryDataPtr : nullptr;
        const auto stream = geometry ? geometry->VertexStream_PositionOnlyPtr : nullptr;
        const auto indices = geometry ? geometry->IndexStreamPtr : nullptr;
        if (!stream || stream->Params.empty() || !indices || !indices->Param ||
            stream->ElementCount <= 0 || indices->ElementCount <= 0)
            throw std::runtime_error("Human measurement requires the actual indexed imported position stream");
        const auto& positions = stream->Params.front();
        const auto count = std::size_t(stream->ElementCount);
        const auto stride = positions->Stride > 0 ? std::size_t(positions->Stride) : 12u;
        if (count > 200000 || stride < 12 || positions->Attributes.empty() ||
            positions->Attributes.front().UnderlyingType != EBufferElementType::FLOAT ||
            positions->GetBufferSize() < (count - 1) * stride + 12 || indices->Param->Attributes.empty())
            throw std::runtime_error("Unsupported human CPU position layout");
        const auto indexType = indices->Param->Attributes.front().UnderlyingType;
        const std::size_t indexBytes = indexType == EBufferElementType::UINT16 ? 2 :
            indexType == EBufferElementType::UINT32 ? 4 : 0;
        if (!indexBytes || std::size_t(indices->ElementCount) * indexBytes > indices->Param->GetBufferSize())
            throw std::runtime_error("Unsupported human CPU index layout");
        const auto* raw = static_cast<const unsigned char*>(positions->GetBufferData());
        const auto* rawIndices = static_cast<const unsigned char*>(indices->Param->GetBufferData());
        std::vector<Vector> local(count);
        std::vector<Vector4> clip(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            float xyz[3]; std::memcpy(xyz, raw + i * stride, sizeof(xyz));
            local[i] = Vector(xyz[0], xyz[1], xyz[2]);
            const auto world = mesh->World.TransformPoint(local[i]);
            minimum.x = std::min(minimum.x, world.x); minimum.y = std::min(minimum.y, world.y); minimum.z = std::min(minimum.z, world.z);
            maximum.x = std::max(maximum.x, world.x); maximum.y = std::max(maximum.y, world.y); maximum.z = std::max(maximum.z, world.z);
            clip[i] = vp.Transform(Vector4(world, 1));
        }
        auto index = [&](std::size_t i) {
            std::uint32_t value = 0;
            if (indexBytes == 2) { std::uint16_t v; std::memcpy(&v, rawIndices + i * 2, 2); value = v; }
            else std::memcpy(&value, rawIndices + i * 4, 4);
            if (value >= count) throw std::runtime_error("Human index is outside the imported position buffer");
            return value;
        };
        for (std::size_t face = 0; face + 2 < std::size_t(indices->ElementCount); face += 3)
        {
            const std::array<std::uint32_t, 3> ids{index(face), index(face + 1), index(face + 2)};
            std::array<float, 3> x, y, z, reciprocal;
            bool visible = true;
            for (int i = 0; i < 3; ++i)
            {
                const auto& c = clip[ids[i]];
                visible &= std::isfinite(c.w) && c.w > .00001f;
                if (!visible) break;
                reciprocal[i] = 1 / c.w; z[i] = c.z / c.w;
                x[i] = (c.x / c.w * .5f + .5f) * width;
                y[i] = (.5f - c.y / c.w * .5f) * height;
            }
            if (!visible) continue;
            const float denominator = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2]);
            if (!std::isfinite(denominator) || std::abs(denominator) < .00001f) continue;
            const int minX = std::max(0, int(std::floor(*std::min_element(x.begin(), x.end()))));
            const int maxX = std::min(int(width) - 1, int(std::ceil(*std::max_element(x.begin(), x.end()))));
            const int minY = std::max(0, int(std::floor(*std::min_element(y.begin(), y.end()))));
            const int maxY = std::min(int(height) - 1, int(std::ceil(*std::max_element(y.begin(), y.end()))));
            ++triangles;
            for (int py = minY; py <= maxY; ++py) for (int px = minX; px <= maxX; ++px)
            {
                if (++tests > 64000000) throw std::runtime_error("Human measurement exceeds its64Mtriangle/pixel-test CPU budget");
                const float a = ((y[1] - y[2]) * (px + .5f - x[2]) + (x[2] - x[1]) * (py + .5f - y[2])) / denominator;
                const float b = ((y[2] - y[0]) * (px + .5f - x[2]) + (x[0] - x[2]) * (py + .5f - y[2])) / denominator;
                const float c = 1 - a - b;
                if (a < 0 || b < 0 || c < 0) continue;
                const float depth = a * z[0] + b * z[1] + c * z[2];
                const auto pixel = std::size_t(py) * width + unsigned(px);
                if (depth < 0 || depth > 1 || depth >= nearest[pixel]) continue;
                nearest[pixel] = depth;
                const float w = a * reciprocal[0] + b * reciprocal[1] + c * reciprocal[2];
                regions[pixel] = Anatomy((local[ids[0]] * (a * reciprocal[0]) +
                    local[ids[1]] * (b * reciprocal[1]) + local[ids[2]] * (c * reciprocal[2])) / w);
            }
        }
    }
    diagnostics = {{"scope", "CPU projection/nearest-surface classification of actual imported triangles; no image is synthesized and all radiometry is read from actual GPU/PNG outputs"},
        {"triangles", triangles}, {"trianglePixelTests", tests}, {"worldMinimum", {minimum.x, minimum.y, minimum.z}},
        {"worldMaximum", {maximum.x, maximum.y, maximum.z}}, {"regions", RegionNames}};
    return regions;
}

struct Run
{
    fs::path Output, Fixture, ProjectFile;
    Json Report{{"schemaVersion", 1}, {"test", "subsurface-human-gpu"}, {"checks", Json::array()}};
    unsigned Frames = 0; int Failures = 0; bool Ready = false;
    EntityId Head = 0, Camera = 0, Sun = 0, Background = 0;
    Vector CameraPosition{0, 0, -150.63f}, CameraTarget{0, -20, 0};
    std::vector<unsigned char> Regions;
    void Progress()
    {
        Report["renderedFrames"] = Frames; Report["failureCount"] = Failures;
        if (!Output.empty()) Write(Output / "subsurface-human-progress.json", Report);
    }
    void Expect(bool pass, const std::string& name, const Json& details = Json::object())
    {
        Report["checks"].push_back({{"pass", pass}, {"name", name}, {"frame", Frames}, {"detail", details}});
        Failures += !pass;
        std::cout << (pass ? "PASS " : "FAIL ") << "human subsurface: " << name << std::endl;
        Progress();
    }
    void PoseCamera()
    {
        if (!Ready) return;
        auto* camera = g_Engine->Game.MainCamera;
        if (!camera) throw std::runtime_error("Human fixture camera is missing");
        jCamera::SetCamera(camera, CameraPosition, CameraTarget, CameraPosition + Vector::UpVector,
            DegreeToRadian(45), 10, 1000, SCR_WIDTH, SCR_HEIGHT, true, 150.63f);
        camera->UpdateCamera(); gOptions.CameraPos = CameraPosition;
    }
    void Frame()
    {
        if (Frames >= FrameBudget) throw std::runtime_error("Human harness exceeded192actual frames");
        ResetAllKeyState(); ResetMouseAllState(); g_timeDeltaSecond = 1.f / 60.f;
        g_Engine->ProcessInput(g_timeDeltaSecond); g_rhi->IncrementFrameNumber();
        g_Engine->Update(g_timeDeltaSecond); PoseCamera();
        g_Engine->Draw(); g_Engine->EndOfFrame(); ++Frames;
#ifdef _WIN32
        if (IsUseDX12())
        {
            MSG m{}; while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE))
            { if (m.message == WM_QUIT) throw std::runtime_error("Native window closed"); TranslateMessage(&m); DispatchMessage(&m); }
        }
        else
#endif
        {
            auto* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
            if (!window || glfwWindowShouldClose(window)) throw std::runtime_error("Native window closed");
            glfwPollEvents();
        }
    }
    void Warm(unsigned count) { while (count--) Frame(); }
    FloatImage Read(const std::shared_ptr<jRenderTarget>& target, const std::string& label)
    {
        FloatImage image; std::string error;
        const bool read = target && ReadSceneFeatureTextureFloats(target->GetTexturePtr(), image.Values, image.Width, image.Height, error);
        std::size_t bad = 0; for (float value : image.Values) bad += !std::isfinite(value);
        Expect(read && image.Valid() && bad == 0, label + " reads actual finite GPU pixels",
            {{"width", image.Width}, {"height", image.Height}, {"nonfinite", bad}, {"error", error}});
        if (!read || bad) image = {};
        return image;
    }
    Snapshot Capture(const Case& test)
    {
        Snapshot snapshot; snapshot.VP = g_Engine->Game.MainCamera->ViewProjection;
        const auto path = Output / (std::string(test.Name) + ".png");
        const auto capture = Json::parse(CaptureSceneFeaturePixels(jEditorAssetPath::ToUtf8(path).c_str()));
        Report["captures"][test.Name] = capture;
        Expect(capture.value("readback", false) && capture.value("pngSaved", false) &&
            capture.value("litPixels", std::size_t(0)) > 1000, std::string(test.Name) + " captures the actual textured human scene", capture);
        unsigned char* pixels = nullptr;
        const auto pngError = LodePNG_decode32_file(&pixels, &snapshot.Png.Width, &snapshot.Png.Height, jEditorAssetPath::ToUtf8(path).c_str());
        const std::unique_ptr<unsigned char, decltype(&std::free)> owned(pixels, &std::free);
        if (!pngError && owned) snapshot.Png.Values.assign(owned.get(), owned.get() + std::size_t(snapshot.Png.Width) * snapshot.Png.Height * 4);
        Expect(snapshot.Png.Valid(), std::string(test.Name) + " decodes its unmodified actual PNG");
        const auto& energy=jSkinShading::GetLastStats();
        Expect(energy.Ready && energy.Resolution==64 && energy.QuadratureSteps==80,
            std::string(test.Name)+" retains the actual source-model energy LUT even with scattering OFF",
            {{"ready",energy.Ready},{"resolution",energy.Resolution},{"quadratureTerms",energy.QuadratureSteps},{"diagnostic",energy.Diagnostic}});
        jSubsurface::Inputs input; jSubsurface::Result result; std::uint64_t frame = 0;
        const bool fresh = jSubsurface::GetLastDebugOutput(frame, input, result);
        Expect(fresh == (test.GlobalStrength > 0), std::string(test.Name) + " has only current-frame screen fallback output");
        if (fresh)
        {
            Expect(result.Applied && result.ComputePasses == 3, std::string(test.Name) + " records the independent screen fallback",
                {{"passes", result.ComputePasses}, {"diagnostic", result.Diagnostic}});
            snapshot.Mask = Read(input.Mask, std::string(test.Name) + " actual mask/profile");
            snapshot.Diffuse = Read(input.DiffuseLighting, std::string(test.Name) + " original Kelemen diffuse /no SH");
            snapshot.Filtered = Read(result.FilteredDiffuse, std::string(test.Name) + " screen fallback diffuse");
            snapshot.Composed = Read(result.Output, std::string(test.Name) + " screen fallback composed HDR");
        }
        jTextureSubsurface::DebugOutput uv;
        const bool current=jTextureSubsurface::GetLastDebugOutput(uv);
        const bool expected=test.GlobalStrength>0 && test.TextureSpace;
        Expect(current==expected,std::string(test.Name)+" publishes only current-frame UV transport",
            {{"expected",expected},{"current",current}});
        if(!current) return snapshot;
        snapshot.HasTextureTransport=true;
        Expect(uv.Stats.SurfaceCount==1 && uv.Stats.RasterPasses==2 && uv.Stats.ComputePasses>=20 &&
            uv.Stats.ComputePasses<=32 && uv.Stats.AdditionalTargetBytes<=jTextureSubsurface::MaximumWorkBytes,
            std::string(test.Name)+" executes bounded actual light capture, UV bake and six-lobe transport",
            {{"surfaces",uv.Stats.SurfaceCount},{"captureDraws",uv.Stats.CaptureDrawCount},{"rasterPasses",uv.Stats.RasterPasses},
                {"computePasses",uv.Stats.ComputePasses},{"logicalTargetBytes",uv.Stats.AdditionalTargetBytes}});
        auto final=Read(uv.FinalUV,std::string(test.Name)+" completed UV and atomic rejection flags");
        auto stretch=Read(uv.Stretch,std::string(test.Name)+" actual UV coverage and metric stretch");
        auto entry=Read(uv.EntryUV,std::string(test.Name)+" actual light-entry UV and geometric thickness");
        auto irradiance=Read(uv.Irradiance[0],std::string(test.Name)+" finite skin irradiance and encoded transmission");
        auto completed=Read(uv.CompletedDiffuse,std::string(test.Name)+" completed unweighted diffuse including local fallback");
        auto composed=Read(uv.Output,std::string(test.Name)+" final UV diffuse-only HDR composition");
        unsigned flags=0;std::size_t invalidFlags=0,overlap=0,covered=0,measured=0,encoded=0,localMiss=0;
        double minimumThickness=std::numeric_limits<double>::infinity(),maximumThickness=0,alphaMaximum=0;
        if(final.Valid()) for(std::size_t i=3;i<final.Values.size();i+=4)
        {
            const float value=final.Values[i];
            if(value<0 || value>7 || std::abs(value-std::round(value))>.01f)++invalidFlags;
            else flags|=unsigned(std::lround(value));
        }
        if(stretch.Valid())for(std::size_t i=0;i<stretch.Values.size();i+=4)
        {covered+=stretch.Values[i+2]>.5f;overlap+=stretch.Values[i+2]>1.01f;}
        if(entry.Valid())for(std::size_t i=0;i<entry.Values.size();i+=4)
        {
            localMiss+=entry.Values[i+3]<-1.5f;
            if(entry.Values[i+3]>.5f && entry.Values[i+2]>gOptions.TextureSubsurface.CaptureBiasWorld)
            {++measured;minimumThickness=std::min(minimumThickness,double(entry.Values[i+2]));maximumThickness=std::max(maximumThickness,double(entry.Values[i+2]));}
        }
        if(irradiance.Valid())for(std::size_t i=3;i<irradiance.Values.size();i+=4)
        {encoded+=irradiance.Values[i]>0;alphaMaximum=std::max(alphaMaximum,double(irradiance.Values[i]));}
        snapshot.Transport={{"atomicFlags",flags},{"invalidFlagPixels",invalidFlags},{"coveredAtlasTexels",covered},
            {"overlappingAtlasTexels",overlap},{"localMissingCaptureTexels",localMiss},{"measuredTransmissionEntries",measured},
            {"minimumEntryThicknessWorld",measured?minimumThickness:0},{"maximumEntryThicknessWorld",maximumThickness},
            {"encodedTransmissionTexels",encoded},{"maximumEncodedTransmission",alphaMaximum},
            {"gaussianStepScale",test.GaussianStep},{"transmissionEnabled",test.Transmission}};
        Expect(invalidFlags==0 && (flags&6u)==0 && covered>100000 && overlap>0 && (flags&1u)!=0,
            std::string(test.Name)+" permits only the real local eye-chart overlap and has no fatal capture/nonfinite rejection",snapshot.Transport);
        if(test.Transmission)
            Expect(measured>0 && encoded>0 && minimumThickness<3.0 && alphaMaximum<=1.001,
                std::string(test.Name)+" measures real thin entry-to-exit geometry and GPU transmission",snapshot.Transport);
        else
            Expect(measured==0 && encoded==0,std::string(test.Name)+" transmission OFF contributes no hidden-entry alpha",snapshot.Transport);
        if(test.Weight>0 && test.GaussianStep==1 && test.Transmission && std::string(test.Name)=="human-on")
        {
            const auto skinUV=Read(uv.SkinUV,"human-on actual screen UV and render-object identity");
            std::size_t fallback=0,checked=0;double maximumError=0;
            const bool compatible=skinUV.Valid() && stretch.Valid() && entry.Valid() && completed.Valid() && snapshot.Filtered.Valid() &&
                skinUV.Width==completed.Width && skinUV.Height==completed.Height &&
                snapshot.Filtered.Width==completed.Width && snapshot.Filtered.Height==completed.Height &&
                stretch.Width==entry.Width && stretch.Height==entry.Height;
            if(compatible)for(std::size_t pixel=0;pixel<std::size_t(skinUV.Width)*skinUV.Height;++pixel)
            {
                const float u=skinUV.Values[pixel*4],v=skinUV.Values[pixel*4+1];
                const unsigned id=unsigned(std::lround(skinUV.Values[pixel*4+2])) |
                    (unsigned(std::lround(skinUV.Values[pixel*4+3]))<<10u);
                if(uv.Stats.Surfaces.empty() || id!=uv.Stats.Surfaces.front().RenderObjectID || u<0 || u>1 || v<0 || v>1)continue;
                ++checked;bool safe=true;
                const int x=int(std::floor(u*stretch.Width-.5f)),y=int(std::floor(v*stretch.Height-.5f));
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                {
                    const auto q=(std::size_t(std::clamp(y+dy,0,int(stretch.Height)-1))*stretch.Width+
                        unsigned(std::clamp(x+dx,0,int(stretch.Width)-1)))*4;
                    safe &= stretch.Values[q+2]>=.99f && stretch.Values[q+2]<=1.01f &&
                        std::abs(stretch.Values[q+3]-1)<.25f && entry.Values[q+3]>=0;
                }
                if(safe)continue;
                ++fallback;
                for(std::size_t c=0;c<3;++c)maximumError=std::max(maximumError,
                    std::abs(double(completed.Values[pixel*4+c])-snapshot.Filtered.Values[pixel*4+c]));
            }
            Expect(compatible && checked>1000 && maximumError<=.00001,
                "invalid four-load UV footprints preserve the real screen diffuse instead of selecting an arbitrary donor",
                {{"visibleHeadPixels",checked},{"visibleFallbackPixels",fallback},{"maximumFallbackHdrError",maximumError},
                    {"scope","Eye-chart backs need not be visible; every encountered invalid footprint is checked."}});
            snapshot.Transport["visibleFallbackPixels"]=fallback;
        }
        snapshot.Filtered=std::move(completed);snapshot.Composed=std::move(composed);
        Report["uvTransport"][test.Name]=snapshot.Transport;
        return snapshot;
    }
};

void SaveSkinMaterial(const fs::path& fixture)
{
    MaterialGraph graph;graph.Version=2;graph.Surface.emplace();
    auto& shading=graph.Surface->Shading;
    shading.ShadingModel=jSurfaceShadingModel::SkinKelemen;shading.SubsurfaceSpace=jSubsurfaceSpace::Texture;
    shading.AlphaMode=jSurfaceAlphaMode::Opaque;shading.Subsurface=1.f;
    shading.SubsurfaceThicknessWorld=3.f;shading.SubsurfaceProfile=0;shading.PreScatterWeight=.5f;
    const auto add=[&](unsigned id,NodeKind kind,unsigned a=0,unsigned b=0)
    {MaterialNode node;node.Id=id;node.Kind=kind;node.InputA=a;node.InputB=b;graph.Nodes.push_back(node);return &graph.Nodes.back();};
    add(1,NodeKind::UV);
    add(2,NodeKind::TextureObject)->Texture=TextureAsset;
    graph.Nodes.back().Semantic=jMaterialMapping::TextureSemantic::Color;
    add(3,NodeKind::TextureSample,2,1);
    add(4,NodeKind::TextureObject)->Texture=NormalAsset;
    auto* normal=add(5,NodeKind::TextureSampleNormal,4,1);
    normal->NormalSpace=jMaterialMapping::NormalSpace::Object;
    normal->InputBasisSignXYZ={1,1,-1};normal->FlipNormalY=false;
    add(6,NodeKind::TextureObject)->Texture=SpecularAsset;
    add(7,NodeKind::TextureSample,6,1);
    add(8,NodeKind::Channel,7)->Parameters[0]=0;
    add(9,NodeKind::Channel,7)->Parameters[0]=1;
    add(10,NodeKind::Channel,7)->Parameters[0]=2;
    add(11,NodeKind::Scalar)->Scalar=1.99f;
    add(12,NodeKind::Scalar)->Scalar=.5f;
    add(13,NodeKind::Multiply,8,11);add(14,NodeKind::Multiply,9,12);
    add(15,NodeKind::Scalar)->Scalar=.5f;
    add(16,NodeKind::DiffusionProfile)->Parameters[0]=0;
    add(17,NodeKind::Scalar)->Scalar=0.f;
    graph.BaseColor=3;graph.Normal=5;graph.Roughness=14;graph.Metallic=17;
    graph.SpecularWeight=13;graph.DiffuseOcclusion=10;graph.PreScatterWeight=15;graph.SubsurfaceProfile=16;
    std::string error;fs::create_directories(fixture/"Materials");
    if(!SaveMaterial(graph,fixture/SkinMaterialAsset,error))throw std::runtime_error(error);
}

void SaveBackgroundMaterial(const fs::path& fixture)
{
    MaterialGraph graph; graph.Version=2; graph.Surface.emplace();
    graph.Surface->Shading.AlphaMode=jSurfaceAlphaMode::Opaque;
    graph.Surface->Emission={135.f/255.f,206.f/255.f,250.f/255.f};
    MaterialNode color; color.Id=1; color.Kind=NodeKind::Color; color.Color={0,0,0};
    graph.Nodes.push_back(color); graph.BaseColor=1;
    std::string error;
    if(!SaveMaterial(graph,fixture/BackgroundMaterialAsset,error))throw std::runtime_error(error);
}

void MakeFixture(Run& run, jEditor& editor)
{
    const auto seed = SeedAssets();
    editor.CancelPendingImports(); editor.ResetWorldBindings(false); g_Engine->Game.ClearImportedSceneForAuthoring();
    if (!editor.GetDocument().NewScene("Human subsurface")) throw std::runtime_error(editor.GetDocument().LastError());
    run.Fixture = run.Output / ("fixture-" + std::to_string(Clock::now().time_since_epoch().count()));
    jEditorProject project; std::string error;
    if (!project.Create(run.Fixture, "Human skin diffusion", error)) throw std::runtime_error(error);
    const auto assets = run.Fixture / "Assets/InfiniteHead"; fs::create_directories(assets);
    for (const auto* name : {"Infinite-Level_02.OBJ", "Infinite-Level_02.jpg", "Infinite-Level_02.mtl",
        "Infinite-Level_02_World_NoSmoothUV.jpg", "SpecularAOMap.png", "ATTRIBUTION.md", "provenance.json"})
    {
        fs::copy_file(seed / name, assets / name);
        const auto sourceHash=HashFile(seed/name),copiedHash=HashFile(assets/name);
        run.Expect(sourceHash==copiedHash,std::string(name)+" retains the approved source bytes");
        run.Report["copiedSourceFiles"][name]={{"bytes",fs::file_size(seed/name)},
            {"sourceFNV1a64",sourceHash},{"copiedFNV1a64",copiedHash}};
    }
    SaveSkinMaterial(run.Fixture); SaveBackgroundMaterial(run.Fixture);
    Document empty;
    if (!empty.Save(run.Fixture / "Scenes/start.jscene")) throw std::runtime_error(empty.LastError());
    project.EditConfig().StartupScene = "Scenes/start.jscene";
    project.EditConfig().Settings["render.deferred"] = "true";
    project.EditConfig().Settings["render.surfelgi"] = "false"; project.EditConfig().Settings["render.ssgi"] = "false";
    if (!project.Save(error) || !OpenAuthoringProject(editor, project.GetProjectFile(), error)) throw std::runtime_error(error);
    run.ProjectFile = project.GetProjectFile(); run.Report["fixtureProject"] = run.ProjectFile.generic_string();
    SetAuthoringRuntimeMode(false); SetAuthoringWorkspaceVisible(true); editor.SetAuthoringEnabled(true);
    editor.Placement.EnablePlacementMode = true; editor.SetSelectedEntities({});
    ControlledOptions();
    run.Head = editor.AddModel(ModelAsset);
    run.Sun = editor.CreateEntity(EntityKind::PointLight, "Source finite key /400 reference distance");
    run.Camera = editor.CreateEntity(EntityKind::Camera, "Source front camera /LH conversion");
    run.Background=editor.CreateEntity(EntityKind::Cube,"Emissive blue background /no diffuse fill");
    Change(editor, run.Head, [](Entity& e) {
        e.Name = "Infinite human head / original4k skin texture";
        e.Local = {}; e.Local.Scale = {ModelScale, ModelScale, ModelScale};
        e.ShadowCaster = true; e.Material.OverrideEnabled = true;
        e.Material.BaseColor = {1, 1, 1}; e.Material.Roughness = .5f; e.Material.Metallic = 0;
        e.Material.SubsurfaceOverrideEnabled = true; e.Material.SubsurfaceColor = {1, 1, 1};
        e.Material.SubsurfaceWeight = 0; e.Material.SubsurfaceThicknessWorld = 3; e.Material.SubsurfaceProfile = 0;
        e.Material.GraphAsset=SkinMaterialAsset;
        auto& overrides=e.Material.GraphOverrides.emplace();
        overrides.SubsurfaceWeight=true;overrides.SubsurfaceThickness=true;
        overrides.SubsurfaceSpace=true;
        e.Material.SkinOverrideEnabled=true;
        e.Material.ShadingModel=jSurfaceShadingModel::SkinKelemen;e.Material.SubsurfaceSpace=jSubsurfaceSpace::Texture;
    });
    Change(editor, run.Sun, [](Entity& e) {
        e.Local = {}; e.Local.Position={273,117,45};
        e.Light.Color = {.6f, .6f, .6f}; e.Light.Intensity = 160000.f; e.Light.Range=100000.f;e.Light.CastShadows = true;
        e.Light.ShadowOverride = gOptions.AdvancedShadows;
    });
    Change(editor,run.Background,[](Entity& e){
        e.Local={};e.Local.Position={0,-15,120};e.Local.Scale={30,30,1};e.ShadowCaster=false;
        e.Material.OverrideEnabled=true;e.Material.BaseColor={0,0,0};
        e.Material.Emissive={135.f/255.f,206.f/255.f,250.f/255.f};
        e.Material.GraphAsset=BackgroundMaterialAsset;
    });
    const auto forward = (run.CameraTarget - run.CameraPosition).GetNormalize();
    Change(editor, run.Camera, [&](Entity& e) {
        e.Local = {}; e.Local.Position = {run.CameraPosition.x, run.CameraPosition.y, run.CameraPosition.z};
        e.Local.RotationDegrees = {RadianToDegree(std::atan2(-forward.y,
            std::sqrt(forward.x * forward.x + forward.z * forward.z))), RadianToDegree(std::atan2(forward.x, forward.z)), 0};
        e.Camera.Primary = true; e.Camera.VerticalFovDegrees = 45; e.Camera.NearPlane = 10; e.Camera.FarPlane = 1000;
    });
    run.Ready = true;
    const auto preparationStarted=Clock::now();
    const auto preparationDeadline=preparationStarted+std::chrono::seconds(30);
    bool prepared=false;
    unsigned preparationFrames=0;
    for (unsigned attempt = 0; attempt < 120 && Clock::now()<preparationDeadline; ++attempt)
    {
        run.Frame();++preparationFrames;
        const auto* object = editor.ObjectForEntity(run.Head);
        if (object && !object->RenderObjects.empty() && !editor.PendingImportCount())
        {
            bool ready = PendingMaterialMappingTextureCount()==0;
            for (const auto* mesh : object->RenderObjects)
                ready &= mesh && mesh->MaterialPtr && mesh->MaterialPtr->HasAlbedoTexture() &&
                    mesh->MaterialPtr->MappingProgram && mesh->MaterialPtr->MappingProgram->Active &&
                    mesh->MaterialPtr->MappingProgram->Textures.size()==3 &&
                    mesh->MaterialPtr->Surface && !mesh->MaterialPtr->SubsurfaceOnlySurface &&
                    mesh->MaterialPtr->Surface->AlphaMode==jSurfaceAlphaMode::Opaque;
            const auto* background=editor.ObjectForEntity(run.Background);
            ready &= background && !background->RenderObjects.empty();
            if(background)for(const auto* mesh:background->RenderObjects)
                ready &= mesh && mesh->MaterialPtr && mesh->MaterialPtr->Surface &&
                    !mesh->MaterialPtr->SubsurfaceOnlySurface &&
                    mesh->MaterialPtr->Surface->AlphaMode==jSurfaceAlphaMode::Opaque;
            if (ready) { prepared=true;break; }
        }
        // The 6K JPEG is decoded/copied on CPU workers. Uncapped native test
        // frames must not consume their whole budget before workers get wall
        // time. This preparation-only wait is outside all rendered controls.
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(preparationDeadline-Clock::now());
        if(remaining.count()>0)std::this_thread::sleep_for(std::min(remaining,std::chrono::milliseconds(100)));
    }
    run.Report["preparation"]={{"frames",preparationFrames},
        {"seconds",std::chrono::duration<double>(Clock::now()-preparationStarted).count()},
        {"wallTimeLimitSeconds",30},{"frameLimit",120},{"pendingMappingTextures",PendingMaterialMappingTextureCount()}};
    if(!prepared)throw std::runtime_error("Human model/material preparation exceeded 30 seconds or 120 frames: "+editor.ImportStatus());
    const auto* object = editor.ObjectForEntity(run.Head);
    bool textured = object && !object->RenderObjects.empty();
    Json materials = Json::array();
    if (object) for (const auto* mesh : object->RenderObjects)
    {
        const auto material = mesh ? mesh->MaterialPtr : nullptr;
        const auto* texture = material ? material->GetTexture(jMaterial::EMaterialTextureType::Albedo) : nullptr;
        const bool sourceTexture = texture && texture->Width == 4096 && texture->Height == 4096;
        bool rawNormal=false,specularAO=false,canonical=false;
        if(material&&material->MappingProgram)
            for(std::size_t i=0;i<material->MappingProgram->Textures.size();++i)
            {
                const auto& binding=material->MappingProgram->Textures[i];
                const auto& data=material->MappingTextures[i];
                rawNormal|=binding.Path==NormalAsset&&data&&data->Width==6000&&data->Height==6000;
                specularAO|=binding.Path==SpecularAsset&&data&&data->Width==512&&data->Height==512;
            }
        canonical=material&&material->Surface&&!material->SubsurfaceOnlySurface&&material->Surface->AlphaMode==jSurfaceAlphaMode::Opaque;
        textured &= sourceTexture&&rawNormal&&specularAO&&canonical;
        materials.push_back({{"textureWidth", texture ? texture->Width : 0}, {"textureHeight", texture ? texture->Height : 0},
            {"sourceMaterial", material ? material->SourceMaterialName : ""}, {"actual4kDiffuse", sourceTexture},
            {"actual6kObjectNormal",rawNormal},{"actualSpecularAO",specularAO},{"canonicalOpaqueSurface",canonical}});
    }
    run.Expect(textured, "normal engine OBJ/material graph pipeline publishes the real human and all three original skin maps", materials);
    run.Expect(object && jObject::GetStaticRenderObject().size() == object->RenderObjects.size()+1,
        "the actual raster scene contains the source human and one emissive background, with no stand-in skin geometry",
        {{"actualRasterMeshes", jObject::GetStaticRenderObject().size()},
            {"humanMeshes", object ? object->RenderObjects.size() : 0}});
    run.Report["humanAsset"] = {{"source", ModelAsset}, {"texture", TextureAsset},
        {"normalPolicy", "Generated smooth mesh normals remain separate from the original linear NoSmoothUV object-normal map. Explicit (1,1,-1) input signs match importer MakeLeftHanded; no extra UV/normalY flip."},
        {"normal",NormalAsset},{"specularAO",SpecularAsset},{"materialGraph",SkinMaterialAsset},
        {"eyePolicy", "Pinned scan has genuine overlapping eye UV charts. Every ambiguous four-load footprint keeps screen diffuse; head UV transport remains active. No anatomical mask or source-byte rewrite is invented."},
        {"sourceModelScale",ModelScale},{"modelTranslation",{0,0,0}},{"cameraPosition",{0,0,-150.63}},
        {"cameraTarget",{0,-20,0}},{"verticalFovDegrees",45},{"nearPlane",10},{"farPlane",1000},
        {"pointLightPosition",{273,117,45}},{"pointLightColor",{.6,.6,.6}},{"pointLightIntensity",160000},
        {"pointLightRange",100000},{"environmentFill",false},{"worldUnitsPerMillimeter",.3},
        {"specularWeightFormula","SpecularAOMap.r * 1.99"},{"beckmannRoughnessFormula","SpecularAOMap.g * 0.5"},
        {"diffuseOcclusionFormula","SpecularAOMap.b"},{"preScatterWeight",.5},
        {"displayScope","Raw native output; this harness does not reproduce the old demo's adaptive exposure or legacy fast-bloom U2 tone curve."}};
}

void PersistProjectOptions(jEditorProject& project,const Case& test,const std::string& scene)
{
    std::string error; auto& config=project.EditConfig();config.StartupScene=scene;
    if(!jEditorProjectSettings::WriteAdvancedShadows(config,gOptions.AdvancedShadows,error) ||
        !jEditorProjectSettings::WriteSceneFeatures(config,gOptions.SceneFeatures,"",error) ||
        !jEditorProjectSettings::WriteTextureSubsurface(config,gOptions.TextureSubsurface,error))throw std::runtime_error(error);
    auto& settings=config.Settings;
    settings["render.deferred"]="true";settings["render.bloom"]="false";
    settings["render.surfelgi"]="false";settings["render.ssgi"]="false";
    settings["render.hiz.enabled"]="false";settings["render.msaa.samples"]="1";
    settings["render.shadow.filter"]="1";settings["render.shadow.radiusTexels"]="2";
    settings["render.sss.enabled"]="true";settings["render.sss.strength"]=std::to_string(test.GlobalStrength);
    settings["render.sss.radiusScale"]="2";settings["render.sss.maxRadiusPixels"]="32";
    settings["render.sss.depthRejectWorld"]="1";settings["render.sss.normalThreshold"]=".6";
    settings["render.environment.enabled"]="false";settings["render.environment.intensity"]="1";
    if(!project.Save(error))throw std::runtime_error(error);
}

void Configure(Run& run, const Case& test)
{
    ControlledOptions();gOptions.Subsurface.Strength=test.GlobalStrength;
    gOptions.TextureSubsurface.EnableTransmission=test.Transmission;
    gOptions.TextureSubsurface.GaussianStepScale=test.GaussianStep;
    Change(*g_Editor,run.Head,[&](Entity& e){
        e.Material.SubsurfaceWeight=test.Weight;e.Material.SubsurfaceThicknessWorld=test.Thickness;
        e.Material.SubsurfaceSpace=test.TextureSpace?jSubsurfaceSpace::Texture:jSubsurfaceSpace::Screen;
    });
    if(!g_Editor->SyncDocumentToWorld())throw std::runtime_error(g_Editor->LastError());
    jObject::FlushDirtyState();run.PoseCamera();run.Warm(3);
    const auto scene=std::string("Scenes/")+test.Name+".jscene";
    const auto file=run.Fixture/scene;
    if(!g_Editor->GetDocument().Save(file))throw std::runtime_error(g_Editor->GetDocument().LastError());
    Document reopened;const bool loaded=reopened.Load(file);const auto* head=loaded?reopened.Find(run.Head):nullptr;
    run.Expect(head && head->SourceAsset==ModelAsset &&
        head->Material==g_Editor->GetDocument().Find(run.Head)->Material && head->Local.Scale==Vec3{ModelScale,ModelScale,ModelScale},
        std::string(test.Name)+" saves/reopens the actual source geometry and canonical skin/UV material");
    jEditorProject project;std::string error;
    if(!project.Open(run.ProjectFile,error))throw std::runtime_error(error);
    PersistProjectOptions(project,test,scene);
    const auto variant=run.Fixture/(std::string(test.Name)+".jproject");
    fs::copy_file(project.GetProjectFile(),variant,fs::copy_options::overwrite_existing);
    jEditorProject restored;jTextureSubsurface::Settings settings;
    const bool opens=restored.Open(variant,error) && jEditorProjectSettings::ReadTextureSubsurface(restored.GetConfig(),settings,error);
    run.Expect(opens && restored.GetConfig().StartupScene==scene && settings.WorldUnitsPerMillimeter==.3f &&
        settings.CaptureResolution==2048 && settings.TileResolution==1024 &&
        settings.GaussianStepScale==test.GaussianStep && settings.EnableTransmission==test.Transmission,
        std::string(test.Name)+" reopens a portable project with exact physical UV/TSM controls",
        {{"project",variant.generic_string()},{"scene",scene},{"error",error}});
    run.Report["controlProjects"][test.Name]=variant.generic_string();
}

bool Compatible(const FloatImage& a, const FloatImage& b)
{ return a.Valid() && b.Valid() && a.Width == b.Width && a.Height == b.Height; }

Json Metadata(const Snapshot& image, const std::vector<unsigned char>& regions, const Case& test)
{
    std::size_t eligible = 0, correct = 0;
    if (Compatible(image.Mask, image.Diffuse) && regions.size() == std::size_t(image.Mask.Width) * image.Mask.Height)
        for (std::size_t p = 0; p < regions.size(); ++p) if (regions[p])
        {
            ++eligible;
            bool pass = std::abs(image.Diffuse.Values[p * 4 + 3] - test.Thickness) < .01f;
            for (int c = 0; c < 3; ++c) pass &= std::abs(image.Mask.Values[p * 4 + c] - test.Weight) < .01f;
            pass &= std::abs(image.Mask.Values[p * 4 + 3] - (test.Weight > 0 ? 1.0f : 0.0f)) < .01f;
            correct += pass;
        }
    return {{"eligibleAnatomicalPixels", eligible}, {"correctGpuMaterialPixels", correct},
        {"correctFraction", eligible ? double(correct) / eligible : 0.0}};
}

void CompareVisible(Run& run, const Snapshot& off, const Snapshot& on)
{
    const bool compatible = Compatible(off.Composed, on.Composed) && Compatible(on.Diffuse, on.Filtered) &&
        Compatible(on.Mask, on.Diffuse) && off.Png.Valid() && on.Png.Valid() &&
        off.Png.Width == on.Png.Width && off.Png.Height == on.Png.Height &&
        std::memcmp(off.VP.mm, on.VP.mm, sizeof(off.VP.mm)) == 0;
    run.Expect(compatible, "Gaussian ON/OFF compares the same actual UV lighting, camera and viewport");
    if (!compatible) return;
    std::array<std::size_t, 4> count{}, hdrChanged{}, gain{}, loss{}, displayChanged{};
    std::array<double, 4> sum{}, displaySum{};
    for (std::size_t p = 0; p < run.Regions.size(); ++p)
    {
        const unsigned region = run.Regions[p];
        if (!region || on.Mask.Values[p * 4 + 3] < .5f || on.Mask.Values[p * 4] < .99f) continue;
        ++count[region - 1];
        double delta = 0, display = 0, signedLuma = 0;
        for (std::size_t c = 0; c < 3; ++c)
        {
            const auto i = p * 4 + c;
            const double value = on.Composed.Values[i] - off.Composed.Values[i];
            delta = std::max(delta, std::abs(value));
            if (on.Png.Width == on.Composed.Width && on.Png.Height == on.Composed.Height)
                display = std::max(display, double(std::abs(int(on.Png.Values[i]) - int(off.Png.Values[i]))));
            const double luminance = std::array<double, 3>{.2126, .7152, .0722}[c];
            signedLuma += luminance * value;
        }
        sum[region - 1] += delta; displaySum[region - 1] += display;
        hdrChanged[region - 1] += delta >= .001; displayChanged[region - 1] += display >= 2;
        gain[region - 1] += signedLuma >= .0005; loss[region - 1] += signedLuma <= -.0005;
    }
    std::size_t meaningfulRegions = 0;
    for (std::size_t r = 0; r < count.size(); ++r)
    {
        const bool meaningful = count[r] >= 64 && hdrChanged[r] >= std::max<std::size_t>(16, count[r] / 100) &&
            displayChanged[r] >= std::max<std::size_t>(16, count[r] / 100) && gain[r] >= 16 && loss[r] >= 16 &&
            sum[r] / std::max<std::size_t>(count[r], 1) >= .00015;
        meaningfulRegions += meaningful;
        run.Report["visibleAnatomy"][RegionNames[r]] = {{"eligiblePixels", count[r]}, {"hdrChangedPixels", hdrChanged[r]},
            {"displayChangedPixels", displayChanged[r]}, {"gainPixels", gain[r]}, {"lossPixels", loss[r]},
            {"meanMaximumChannelHdrDelta", sum[r] / std::max<std::size_t>(count[r], 1)},
            {"meanMaximumChannelDisplayDelta", displaySum[r] / std::max<std::size_t>(count[r], 1)}, {"meaningful", meaningful}};
    }
    run.Expect(meaningfulRegions >= 3, "actual UV Gaussian diffusion visibly redistributes light across at least3anatomical regions",
        {{"meaningfulRegions", meaningfulRegions}, {"regions", run.Report["visibleAnatomy"]},
            {"thresholds", "region>=64px; >=max(16,1%) pixels with HDRdelta>=.001 and displaydelta>=2; >=16gain/loss with luma delta.0005; meanHDRdelta>=.00015"}});

}

void CompareHalf(Run& run, const Snapshot& off, const Snapshot& on, const Snapshot& half)
{
    double maximum = 0, sum = 0, fullDelta = 0, halfDelta = 0; std::size_t count = 0;
    const bool valid = Compatible(off.Composed, half.Composed) && Compatible(on.Composed, half.Composed);
    if (valid) for (std::size_t p = 0; p < run.Regions.size(); ++p)
    {
        if (!run.Regions[p] || on.Mask.Values[p * 4 + 3] < .5f) continue;
        for (std::size_t c = 0; c < 3; ++c)
        {
            const auto i = p * 4 + c;
            const double expected = .5 * (off.Composed.Values[i] + on.Composed.Values[i]);
            const double error = std::abs(half.Composed.Values[i] - expected);
            maximum = std::max(maximum, error); sum += error; ++count;
            fullDelta += std::abs(on.Composed.Values[i] - off.Composed.Values[i]);
            halfDelta += std::abs(half.Composed.Values[i] - off.Composed.Values[i]);
        }
    }
    const double ratio = fullDelta > 0 ? halfDelta / fullDelta : 0;
    const Json detail{{"samples", count}, {"maximumHdrError", maximum}, {"meanHdrError", count ? sum / count : 1},
        {"halfToFullActualDelta", ratio}};
    run.Report["halfWeightControl"] = detail;
    run.Expect(valid && count >= 64 && maximum <= .0025 && sum / count <= .0003 && ratio >= .4 && ratio <= .6,
        "authored half weight produces the actual halfway human diffuse blend", detail);
}

void CompareRemainder(Run& run,const Snapshot& off,const Snapshot& on)
{
    const bool valid=Compatible(off.Composed,on.Composed) && Compatible(on.Diffuse,on.Filtered) &&
        Compatible(on.Mask,on.Diffuse) && Compatible(off.Diffuse,on.Diffuse);
    double maximum=0,sum=0,originalDifference=0;std::size_t samples=0,specular=0;
    if(valid)for(std::size_t pixel=0;pixel<run.Regions.size();++pixel)
    {
        if(!run.Regions[pixel] || on.Mask.Values[pixel*4+3]<.5f)continue;
        double remainder=0;
        for(std::size_t c=0;c<3;++c)
        {
            const auto i=pixel*4+c;
            const double expected=off.Composed.Values[i]+on.Mask.Values[i]*(on.Filtered.Values[i]-on.Diffuse.Values[i]);
            const double error=std::abs(on.Composed.Values[i]-expected);
            maximum=std::max(maximum,error);sum+=error;++samples;
            originalDifference=std::max(originalDifference,std::abs(double(off.Diffuse.Values[i])-on.Diffuse.Values[i]));
            remainder+=std::array<double,3>{.2126,.7152,.0722}[c]*(off.Composed.Values[i]-off.Diffuse.Values[i]);
        }
        specular+=remainder>.002;
    }
    run.Report["diffuseOnlyIdentity"]={{"samples",samples},{"specularWitnessPixels",specular},{"maximumHdrError",maximum},
        {"meanHdrError",samples?sum/samples:1},{"maximumOriginalDiffuseChange",originalDifference},
        {"formula","C_OFF + mask * strength * (CompletedDiffuse - OriginalDiffuse); CompletedDiffuse includes local screen fallback"}};
    run.Expect(valid && samples>=3000 && specular>=16 && maximum<=.0025 && sum/std::max<std::size_t>(samples,1)<=.0003 && originalDifference<=.00001,
        "actual completed UV diffuse replaces only diffuse and preserves the same original Kelemen specular",run.Report["diffuseOnlyIdentity"]);
}

void CompareTransmission(Run& run,const Snapshot& without,const Snapshot& on)
{
    const bool valid=Compatible(without.Filtered,on.Filtered) && Compatible(without.Composed,on.Composed);
    std::size_t positive=0,negative=0,samples=0;double maximum=0,gain=0;
    if(valid)for(std::size_t pixel=0;pixel<run.Regions.size();++pixel)
    {
        if(!run.Regions[pixel] || on.Mask.Values[pixel*4+3]<.5f)continue;
        double luma=0;
        for(std::size_t c=0;c<3;++c)
            luma+=std::array<double,3>{.2126,.7152,.0722}[c]*(on.Filtered.Values[pixel*4+c]-without.Filtered.Values[pixel*4+c]);
        ++samples;positive+=luma>.0001;negative+=luma<-.001;
        maximum=std::max(maximum,luma);gain+=std::max(luma,0.0);
    }
    const Json detail{{"eligiblePixels",samples},{"positiveTransmissionPixels",positive},{"unexpectedLossPixels",negative},
        {"maximumDiffuseLuminanceGain",maximum},{"summedPositiveDiffuseGain",gain},
        {"sourceEntryDiagnostics",on.Transport},{"scope","Same Gaussian and UV capture; only measured front-entry transmission is toggled."}};
    run.Report["transmissionControl"]=detail;
    run.Expect(valid && samples>=1000 && positive>=8 && maximum>.0001 && negative==0 &&
        on.Transport.value("measuredTransmissionEntries",std::size_t(0))>0,
        "actual finite-key TSM donates light through measured thin human geometry",detail);
}

void CompareZero(Run& run, const Snapshot& off, const Snapshot& zero)
{
    const bool valid = off.Png.Valid() && zero.Png.Valid() && off.Png.Width == zero.Png.Width && off.Png.Height == zero.Png.Height;
    unsigned maximum = 0;
    if (valid) for (std::size_t i = 0; i < off.Png.Values.size(); ++i)
        if (i % 4 != 3) maximum = std::max(maximum, unsigned(std::abs(int(off.Png.Values[i]) - int(zero.Png.Values[i]))));
    run.Report["globalZeroControl"] = {{"maximumDisplayByteDifference", maximum}, {"tolerance", 1}};
    run.Expect(valid && maximum <= 1, "global zero and material OFF reproduce the same actual human image", run.Report["globalZeroControl"]);
}

void FinalizeProject(Run& run)
{
    const auto onProject=run.Fixture/"human-on.jproject";
    fs::copy_file(onProject,run.ProjectFile,fs::copy_options::overwrite_existing);
    jEditorProject project;std::string error;jTextureSubsurface::Settings texture;
    const bool restored=project.Open(run.ProjectFile,error) &&
        jEditorProjectSettings::ReadTextureSubsurface(project.GetConfig(),texture,error);
    run.Expect(restored && project.GetConfig().StartupScene=="Scenes/human-on.jscene" &&
        texture.WorldUnitsPerMillimeter==.3f && texture.GaussianStepScale==1.f && texture.EnableTransmission,
        "delivered startup project preserves the source skin/UV units and full transmission settings",{{"error",error}});
    run.Report["portableStartupProject"]=run.ProjectFile.generic_string();
    std::ofstream guide(run.Fixture/"GUIDE.md");guide.exceptions(std::ios::failbit|std::ios::badbit);
    guide << "# Source skin / UV diffusion sample\n\nOpen `project.jproject` for the full ON scene. Each adjacent `human-*.jproject` stores its own startup scene and renderer settings while sharing this directory's unchanged Assets and Materials. "
        "Opening only a scene does not load that control's global renderer settings.\n\n"
        "The pinned Infinite head uses uniform scale300 with no translation. Its source RH coordinates are converted by the normal importer to LH. Camera `(0,0,-150.63)` targets `(0,-20,0)`,45deg FOV, near10/far1000. "
        "One finite point key `(273,117,45)` uses colour0.6, intensity160000, range100000 and real shadows. There is no SH/GI fill. An explicitly opaque emissive blue cube supplies the background only.\n\n"
        "The shared material reads the original4k diffuse as colour, the6k NoSmoothUV object normal as linear data with explicit `(1,1,-1)` basis signs and no extraY/UV flip, and SpecularAO R*1.99 as Kelemen rho_s, G*0.5 as Beckmann m and B as diffuse AO. "
        "Pre-scatter is0.5; the model remains SkinKelemen with the same maps/key in every control. Physical conversion is0.3world units/mm. UV atlas1024, capture2048, six-lobe GaussianStep1.\n\n"
        "- `human-off.jproject`: material diffusion weight0, same skin BRDF/normals/key.\n"
        "- `human-on.jproject`: full texture-space diffusion and measured finite-key transmission.\n"
        "- `human-uv-unfiltered.jproject`: GaussianStep0 and transmission OFF, retaining the identical UV light capture.\n"
        "- `human-transmission-off.jproject`: GaussianStep1 and transmission OFF, isolating Gaussian diffusion from transmission.\n"
        "- `human-half-weight.jproject`: material diffusion weight0.5.\n"
        "- `human-global-zero.jproject`: global strength0; the same skin BRDF remains active.\n\n"
        "Genuine overlap exists in the source eye charts. Four-load overlap/missing-capture footprints retain screen diffuse locally instead of inventing a UV donor; the remaining head UV transport stays active. "
        "No anatomical mask, replacement human mesh or source-byte rewrite is used. Actual measured entry-to-exit thickness drives transmission; authored thickness3world units also bounds diffusion support. "
        "These are bounded raster skin features; the current engine tone/exposure differs from the original GL demo, and pixel-identical presentation is not claimed. "
        "See `Assets/InfiniteHead/ATTRIBUTION.md` and `provenance.json` for CC BY scan terms separately from the software license.\n\n"
        "Unmodified native screenshots, actual finite GPU UV/capture/irradiance/completed-diffuse readbacks, local fallback, diffuse-only/specular identity, half/zero controls and source file hashes are recorded in the parent output directory's `subsurface-human-gpu-report.json`.\n";
}

}
#endif

int RunSubsurfaceHumanIntegrationGpuTests(const char* ownedOutputDirectory)
{
#ifdef ENABLE_EDITOR_FEATURES
    Run run; RestoreFields restore;
    const auto keys = g_KeyState; const auto mouse = g_MouseState; const float delta = g_timeDeltaSecond;
    const bool workspace = IsAuthoringWorkspaceVisible(), runtime = IsAuthoringRuntimeMode();
    const bool authoring = g_Editor && g_Editor->IsAuthoringEnabled();
    try
    {
        if (!ownedOutputDirectory || !*ownedOutputDirectory || !g_Engine || !g_rhi || !g_Editor || !ImGui::GetCurrentContext())
            throw std::runtime_error("Human SSS requires initialized normal engine/editor/RHI and an owned output directory");
        run.Output = fs::absolute(jEditorAssetPath::FromUtf8(ownedOutputDirectory)); fs::create_directories(run.Output);
        run.Report["status"] = "running"; run.Report["rhi"] = g_rhi->GetRHIName().ToStr(); run.Report["frameBudget"] = FrameBudget;
        run.Report["validationScope"] = "Caller collects actual Vulkan/Metal/DX12 messages. This harness creates no replacement renderer; readbacks/capture/CPU surface classification are outside frame timing.";
        if (!g_Engine->Game.WaitForSceneImportForTesting()) throw std::runtime_error("Startup import did not finish");
        RememberOptions(restore); restore.Remember(g_Editor->Placement.EnablePlacementMode);
        MakeFixture(run, *g_Editor);
        const std::array<Case,6> cases={{{"human-off",0,3,1}, {"human-on",1,3,1},
            {"human-uv-unfiltered",1,3,1,true,false,0}, {"human-transmission-off",1,3,1,true,false,1},
            {"human-half-weight",.5f,3,1}, {"human-global-zero",1,3,0}}};
        Snapshot off,on,unfiltered;
        for(std::size_t i=0;i<cases.size();++i)
        {
            const auto& test=cases[i];Configure(run,test);
            const auto frame=g_rhi->GetCurrentFrameNumber();auto snapshot=run.Capture(test);
            if(i==0 && snapshot.Composed.Valid())
            {
                Json projection;
                run.Regions=VisibleAnatomy(*g_Editor->ObjectForEntity(run.Head),snapshot.VP,
                    snapshot.Composed.Width,snapshot.Composed.Height,projection);
                run.Report["surfaceMeasurementProjection"]=projection;
                const auto& lo=projection["worldMinimum"];const auto& hi=projection["worldMaximum"];
                run.Expect(std::abs((hi[1].get<double>()-lo[1].get<double>())-126.326745)<.1,
                    "actual human preserves source scale300 and unmodified model bounds",projection);
            }
            if(test.GlobalStrength>0)
            {
                const auto metadata=Metadata(snapshot,run.Regions,test);run.Report["materialGpuPixels"][test.Name]=metadata;
                run.Expect(metadata["eligibleAnatomicalPixels"].get<std::size_t>()>=1000 &&
                    metadata["correctFraction"].get<double>()>=.98,
                    std::string(test.Name)+" publishes real source skin profile/weight/thickness metadata",metadata);
            }
            if(i==0)off=std::move(snapshot);
            else if(i==1){on=std::move(snapshot);CompareRemainder(run,off,on);}
            else if(i==2)unfiltered=std::move(snapshot);
            else if(i==3){CompareVisible(run,unfiltered,snapshot);CompareTransmission(run,snapshot,on);}
            else if(i==4)CompareHalf(run,off,on,snapshot);
            else CompareZero(run,off,snapshot);
            run.Expect(g_rhi->GetCurrentFrameNumber()==frame,"human readbacks/classification add no hidden render frames");
            run.Progress();
        }
        FinalizeProject(run); run.Report["caseCount"] = cases.size();
    }
    catch (const std::exception& e)
    {
        ++run.Failures; run.Report["exception"] = e.what();
        std::cerr << "FAIL human SSS: " << e.what() << std::endl;
    }
    if (g_Editor)
    {
        g_Editor->SetAuthoringEnabled(authoring);
        if (run.Ready) { g_Editor->CancelPendingImports(); g_Editor->ResetWorldBindings(false);
            g_Engine->Game.ClearImportedSceneForAuthoring(); g_Editor->GetDocument().NewScene(); }
    }
    SetAuthoringRuntimeMode(runtime); SetAuthoringWorkspaceVisible(workspace);
    restore.Restore(); g_KeyState = keys; g_MouseState = mouse; g_timeDeltaSecond = delta;
    run.Report["renderedFrames"] = run.Frames; run.Report["failureCount"] = run.Failures;
    run.Report["status"] = run.Failures ? "failed" : "passed";
    if (!run.Output.empty()) Write(run.Output / "subsurface-human-gpu-report.json", run.Report);
    std::cout << "Human SSS workflow: " << run.Frames << " actual frames, " << run.Failures << " failures" << std::endl;
    return run.Failures ? EXIT_FAILURE : EXIT_SUCCESS;
#else
    (void)ownedOutputDirectory;
    return EXIT_FAILURE;
#endif
}
