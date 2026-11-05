#include "pch.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "FileLoader/jEditorAssets.h"
#include "Renderer/jRasterFeatureDebug.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jResourceBarrierBatcher.h"
#include "Shader/jMultisampleInspectParameters.h"
#include "Scene/jCamera.h"
#include "jEngine.h"
#include "jOptions.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <chrono>
#include <fstream>

std::string CaptureSceneFeaturePixels(const char*);
bool ReadSceneFeatureTextureFloats(const std::shared_ptr<jTexture>&, std::vector<float>&,
    uint32_t&, uint32_t&, std::string&);

namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace jEditorAuthoring;
using namespace jEditorTools;
struct Image { uint32_t Width = 0, Height = 0; std::vector<float> Values; };

std::array<Image, 4> InspectSamples(const jRasterFeatureDebug::MultisampleOutput& debug)
{
    auto frame = g_rhi->BeginRenderFrame();
    if (!frame) throw std::runtime_error("MSAA inspection could not acquire a native frame");
    const auto samples = uint32_t(debug.Sources[0]->Info.SampleCount);
    const int width = debug.Sources[0]->Info.Width, height = debug.Sources[0]->Info.Height;
    std::array<std::shared_ptr<jRenderTarget>, 4> targets;
    for (std::size_t i = 0; i < targets.size(); ++i)
        targets[i] = jRenderTargetPool::GetRenderTarget({.Type = ETextureType::TEXTURE_2D,
            .Format = (i == 1 || i == 3) ? ETextureFormat::RGBA32F : ETextureFormat::RGBA16F, .Width = width,
            .Height = i < 2 ? int(height * samples) : height, .SampleCount = EMSAASamples::COUNT_1,
            .TextureCreateFlag = ETextureCreateFlag::UAV | ETextureCreateFlag::TransferSrc,
            .ResourceName = jNameStatic("ActualMultisampleInspection")});
    jMultisampleInspectParameters p;
    p.ColorMS = {debug.Sources[0]->GetTexture()}; p.NormalMS = {debug.Sources[1]->GetTexture()};
    p.DepthMS = {debug.SourceDepth->GetTexture()}; p.ResolvedColor = {debug.Resolved[0]->GetTexture()};
    p.ResolvedNormal = {debug.Resolved[1]->GetTexture()}; p.ResolvedDepth = {debug.ResolvedDepth->GetTexture()};
    p.SampleColors = {targets[0]->GetTexture()}; p.SampleSurfaces = {targets[1]->GetTexture()};
    p.ResolvedColors = {targets[2]->GetTexture()}; p.ResolvedSurfaces = {targets[3]->GetTexture()};
    auto* command = frame->GetActiveCommandBuffer();
    for (auto& target : debug.Sources) g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    for (auto& target : {debug.SourceDepth, debug.ResolvedDepth, debug.Resolved[0], debug.Resolved[1]})
        g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    for (auto& target : targets) g_rhi->TransitionLayout(command, target->GetTexture(), EResourceLayout::UAV);
    jShaderInfo info; info.SetName(jNameStatic("ActualMSAAInspectCS"));
    info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/msaa_inspect_cs.hlsl"));
    info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    jShaderParameterSet::AppendToShaderInfo<jMultisampleInspectParameters>(info, 0);
    auto* shader = g_rhi->CreateShader(info);
    if (!shader || !shader->GetCompiledShader()) throw std::runtime_error("Actual MSAA inspection shader failed");
    jShaderBindingInstanceGroup group;
    group.Add(jShaderParameterSet::CreateShaderBindingInstance(p, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame));
    auto* pipeline = g_rhi->CreateComputePipelineStateInfo(shader, group.GetLayoutArray(), {});
    if (!pipeline) throw std::runtime_error("Actual MSAA inspection pipeline failed");
    pipeline->Bind(frame); g_rhi->BindComputeShaderBindingInstances(command, pipeline, group.GetCombiner(), 0);
    g_rhi->DispatchCompute(frame, (width + 7) / 8, (height * samples + 7) / 8, 1);
    g_rhi->EndRenderFrame(frame); g_rhi->Flush();
    std::array<Image, 4> images;
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        std::string error;
        if (!ReadSceneFeatureTextureFloats(targets[i]->TexturePtr, images[i].Values,
            images[i].Width, images[i].Height, error)) throw std::runtime_error(error);
        targets[i]->Return();
    }
    return images;
}
}
#endif

int RunHiZOcclusionIntegrationGpuTests(const char*);
int RunRasterVisibilityIntegrationGpuTests(const char* directory)
{
    const auto root = std::filesystem::absolute(directory);
    std::filesystem::create_directories(root);
    int failures = RunHiZOcclusionIntegrationGpuTests((root / "hiz").string().c_str());
#ifdef ENABLE_EDITOR_FEATURES
    Json report{{"checks", Json::array()}, {"status", "running"}};
    std::size_t frames = 0;
    const auto saved = gOptions;
    const bool oldRuntime = IsAuthoringRuntimeMode(), oldWorkspace = IsAuthoringWorkspaceVisible();
    const auto oldKeys = g_KeyState; const auto oldMouse = g_MouseState;
    const auto oldDelta = g_timeDeltaSecond;
    auto expect = [&](bool pass, const char* name, Json detail = Json::object())
    { report["checks"].push_back({{"name", name}, {"pass", pass}, {"detail", detail}}); failures += !pass;
      std::cout << (pass ? "PASS " : "FAIL ") << "deferred MSAA: " << name << std::endl; };
    auto frame = [&]
    {
        if (++frames > 70) throw std::runtime_error("MSAA fixture exceeded its native frame budget");
        const auto start = Clock::now();
        ResetAllKeyState(); ResetMouseAllState(); g_timeDeltaSecond = 0;
        g_Engine->ProcessInput(0); g_rhi->IncrementFrameNumber(); g_Engine->Update(0); g_Engine->Draw(); g_Engine->EndOfFrame();
#ifdef _WIN32
        if (IsUseDX12()) { MSG m{}; while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); } }
        else
#endif
            glfwPollEvents();
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    try
    {
        auto& editor = *g_Editor; auto& game = g_Engine->Game;
        gOptions.AdvancedShadows = {}; gOptions.SceneFeatures = {}; gOptions.Subsurface = {}; gOptions.HiZOcclusion = {};
        gOptions.UseDeferredRenderer = true; gOptions.UseSubpass = true; gOptions.UseMemoryless = true;
        gOptions.UseHWRTDirectLighting = false; gOptions.UseSurfelGI = false; gOptions.UseSSGI = false;
        gOptions.AOIntensity = 0; gOptions.AOType = 0; gOptions.ShowAOOnly = false; gOptions.ShowSSGIOnly = false;
        gOptions.UseAtmosphericShadowing = false; gOptions.BloomEyeAdaptation = false;
        gOptions.ShowGrid = false; gOptions.UseVRS = false; gOptions.EarthQuake = false;
        editor.CancelPendingImports(); editor.ResetWorldBindings(false); game.ClearImportedSceneForAuthoring();
        jEditorProject project; std::string error;
        const auto fixture = root / "msaa-fixture";
        if (!project.Create(fixture, "Per-sample deferred MSAA", error)) throw std::runtime_error(error);
        Document empty;
        if (!empty.Save(fixture / "Scenes/start.jscene")) throw std::runtime_error(empty.LastError());
        project.EditConfig().StartupScene = "Scenes/start.jscene";
        project.EditConfig().Settings["render.deferred"] = "true";
        if (!project.Save(error) || !OpenAuthoringProject(editor, project.GetProjectFile(), error)) throw std::runtime_error(error);
        SetAuthoringRuntimeMode(false); SetAuthoringWorkspaceVisible(true); editor.SetAuthoringEnabled(true);
        editor.Placement.EnablePlacementMode = true;
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Deferred, error)) throw std::runtime_error(error);
        auto change = [&](EntityId id, auto edit)
        { auto entity = *editor.GetDocument().Find(id); edit(entity); if (!editor.GetDocument().Update(entity, "MSAA fixture")) throw std::runtime_error(editor.GetDocument().LastError()); };
        const auto cube = editor.CreateEntity(EntityKind::Cube, "Slanted coverage and normal boundary");
        const auto sun = editor.CreateEntity(EntityKind::DirectionalLight, "Stable diffuse and specular sun");
        change(cube, [](Entity& e) { e.Local = {}; e.Local.Position = {0, 20, 70}; e.Local.Scale = {7, 7, 7};
            e.Local.RotationDegrees = {13, 32, 19}; e.Material.OverrideEnabled = true;
            e.Material.BaseColor = {.7f, .28f, .08f}; e.Material.Metallic = .25f; e.Material.Roughness = .17f; });
        change(sun, [](Entity& e) { e.Local.RotationDegrees = {-35, -35, 0}; e.Light.Intensity = 1.5f; e.Light.CastShadows = false; });
        auto* camera = game.MainCamera;
        jCamera::SetCamera(camera, Vector(0, 90, -230), Vector(0, 20, 70), Vector(0, 91, -230),
            DegreeToRadian(50), 1, 2000, SCR_WIDTH, SCR_HEIGHT, true, 487);
        camera->UpdateCamera(); gOptions.CameraPos = camera->Pos;
        // Opening a project applies its own defaults; freeze the actual options
        // only after that transaction, for every measured sample count.
        gOptions.AdvancedShadows = {}; gOptions.SceneFeatures = {}; gOptions.Subsurface = {}; gOptions.HiZOcclusion = {};
        gOptions.UseSurfelGI = false; gOptions.UseSSGI = false; gOptions.UseHWRTDirectLighting = false;
        gOptions.AOIntensity = 0; gOptions.AOType = 0; gOptions.BloomEyeAdaptation = false;
        gOptions.UseAtmosphericShadowing = false; gOptions.ShowGrid = false; gOptions.UseVRS = false;
        editor.SetSelectedEntities({});
        for (int requested : {1, 2, 4})
        {
            gOptions.RequestedMSAASamples = requested;
            gOptions.UseSubpass = requested > 1;
            gOptions.UseMemoryless = requested > 1;
            for (int warm = 0; warm < 4; ++warm) frame();
            std::vector<double> times;
            for (int measured = 0; measured < 8; ++measured) times.push_back(frame());
            std::sort(times.begin(), times.end());
            report["cases"][std::to_string(requested)] = {{"requested", requested},
                {"applied", GetRendererFeatureDiagnostics().AppliedMSAASamples}, {"cpuFrameMedianMs", times[times.size()/2]},
                {"scope", "Same executable full CPU frame including visibility and present, excluding warmup, capture and inspection; not GPU-only time."}};
            const auto capture = root / ("msaa-" + std::to_string(requested) + ".png");
            const auto captured = Json::parse(CaptureSceneFeaturePixels(capture.string().c_str()));
            report["cases"][std::to_string(requested)]["capture"] = captured;
            // This fixture has a constant material on flat cube faces. At 1x
            // the valid image is background plus two lit face colours; the
            // sample/normal/lighting checks below establish multisampling.
            expect(captured.value("litPixels", std::size_t(0)) > 100 && captured.value("maximumRgb",0) >= 20 &&
                captured.value("distinctColorsAtMost1024",0) >= 3,
                "actual multisample fixture contains a visible shaded object", {{"samples",requested},{"capture",captured}});
            expect(GetRendererFeatureDiagnostics().AppliedMSAASamples == requested, "requested native attachment sample count is applied",
                {{"requested", requested}, {"applied", GetRendererFeatureDiagnostics().AppliedMSAASamples}});
            if (requested == 1) continue;
            jRasterFeatureDebug::MultisampleOutput debug;
            if (!jRasterFeatureDebug::GetLastMultisampleOutput(debug)) throw std::runtime_error("Actual MSAA resolve debug images missing");
            const auto images = InspectSamples(debug);
            const auto& colors = images[0]; const auto& surfaces = images[1];
            const auto& resolved = images[2]; const auto& resolvedSurface = images[3];
            std::size_t fractional = 0, distinctSurfaces = 0, resolveMismatches = 0, metadataMismatches = 0;
            std::size_t uncoveredLitSamples = 0, distinctSurfaceLighting = 0;
            double maximumError = 0;
            for (uint32_t y = 0; y < resolved.Height; ++y) for (uint32_t x = 0; x < resolved.Width; ++x)
            {
                float mean[3]{}; int covered = 0, nearest = 0; float depth = 1;
                float n0[3]{}; bool haveNormal = false, different = false;
                float firstColor[3]{}; bool differentLight = false;
                for (int s = 0; s < requested; ++s)
                {
                    const auto i = ((std::size_t(y) * requested + s) * colors.Width + x) * 4;
                    for (int c = 0; c < 3; ++c) mean[c] += colors.Values[i+c] / requested;
                    if (surfaces.Values[i+3] < 1)
                    {
                        ++covered;
                        if (!haveNormal)
                        {
                            for (int c=0;c<3;++c) { n0[c]=surfaces.Values[i+c]; firstColor[c]=colors.Values[i+c]; }
                            haveNormal=true;
                        }
                        else for (int c=0;c<3;++c)
                        {
                            different |= std::abs(n0[c]-surfaces.Values[i+c]) > .05f;
                            differentLight |= std::abs(firstColor[c]-colors.Values[i+c]) > .001f;
                        }
                    }
                    else uncoveredLitSamples += std::max({std::abs(colors.Values[i]),std::abs(colors.Values[i+1]),std::abs(colors.Values[i+2])}) > .00001f;
                    if (surfaces.Values[i+3] < depth) { depth=surfaces.Values[i+3]; nearest=s; }
                }
                fractional += covered > 0 && covered < requested;
                distinctSurfaces += different;
                distinctSurfaceLighting += different && differentLight;
                const auto out = (std::size_t(y) * resolved.Width + x) * 4;
                bool mismatch=false, metadata=false;
                const auto selected = ((std::size_t(y) * requested + nearest) * surfaces.Width + x) * 4;
                for (int c=0;c<3;++c)
                {
                    const double error = std::abs(mean[c]-resolved.Values[out+c]); maximumError=std::max(maximumError,error);
                    mismatch |= !std::isfinite(resolved.Values[out+c]) || error > .02 * std::max(.01f,std::abs(mean[c])) + .001;
                    metadata |= std::abs(surfaces.Values[selected+c]-resolvedSurface.Values[out+c]) > .002f;
                }
                metadata |= std::abs(depth-resolvedSurface.Values[out+3]) > .002f;
                resolveMismatches += mismatch; metadataMismatches += metadata;
            }
            expect(fractional > 10, "actual multisample coverage contains fractional silhouette pixels", {{"samples",requested},{"pixels",fractional}});
            expect(distinctSurfaces > 2, "different covered surface samples survive until deferred lighting", {{"samples",requested},{"pixels",distinctSurfaces}});
            expect(uncoveredLitSamples == 0, "uncovered samples receive no phantom deferred lighting", {{"samples",requested},{"samplesIncorrectlyLit",uncoveredLitSamples}});
            expect(distinctSurfaceLighting > 2, "covered samples with different surface normals receive distinct lighting", {{"samples",requested},{"pixels",distinctSurfaceLighting}});
            expect(resolveMismatches == 0, "resolved HDR equals the mean of actual lit samples", {{"samples",requested},{"mismatches",resolveMismatches},{"maximumError",maximumError}});
            expect(metadataMismatches == 0, "resolved normal and depth come from one foreground sample", {{"samples",requested},{"mismatches",metadataMismatches}});
        }
        gOptions.RequestedMSAASamples=4;
        std::string error2;
        if (!game.RequestSceneRenderPipeline(jGame::ESceneRenderPipeline::Forward,error2)) throw std::runtime_error(error2);
        frame();
        expect(GetRendererFeatureDiagnostics().AppliedMSAASamples==1 && !GetRendererFeatureDiagnostics().MSAASupportReason.empty(),
            "unsupported Forward mode preserves the request and reports the 1-sample fallback");
        expect(gOptions.RequestedMSAASamples==4,"capability fallback does not overwrite the requested quality");
    }
    catch (const std::exception& error) { expect(false, "native fixture completed", {{"error", error.what()}}); }
    static_assert(std::is_trivially_copyable_v<jOptions>);
    std::memcpy(&gOptions, &saved, sizeof(gOptions));
    g_KeyState=oldKeys; g_MouseState=oldMouse; g_timeDeltaSecond=oldDelta;
    SetAuthoringRuntimeMode(oldRuntime); SetAuthoringWorkspaceVisible(oldWorkspace);
    report["renderedFrames"]=frames; report["failureCount"]=failures; report["status"]=failures ? "failed" : "passed";
    std::ofstream(root/"raster-visibility-gpu-report.json") << report.dump(2) << '\n';
#endif
    return failures;
}
