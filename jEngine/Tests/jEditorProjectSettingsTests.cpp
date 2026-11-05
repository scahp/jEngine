#include "pch.h"
#include "Code/Engine/Editor/jEditorProjectSettings.h"
#include "Code/Engine/Editor/jEditorDocument.h"
#include "Code/Engine/jEditor.h"
#include "FileLoader/jEditorAssets.h"
#include "jOptions.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "Code/Engine/ConsoleVariables/jConsoleVariable.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

int RunEditorProjectSettingsTests()
{
#ifdef ENABLE_EDITOR_FEATURES
    namespace fs = std::filesystem;
    namespace Settings = jEditorProjectSettings;
    int failures = 0, checks = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << "project settings: " << name << '\n';
        ++checks; failures += !condition;
    };
    const auto fixture = fs::temp_directory_path() / ("jengine-settings-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto oldKeys = g_KeyState;
    const jOptions oldOptions = gOptions;
    const bool oldInline = GSupportInlineRaytracing, oldVsyncSupport = GRHISupportVsync, oldVsync = GUseVsync;
    const bool oldVisible = Settings::IsVisible();
    auto* oldContext = ImGui::GetCurrentContext();
    constexpr const char* actions[] = {"camera.forward", "camera.back", "camera.left", "camera.right", "camera.up", "camera.down"};
    constexpr EInputKey defaults[] = {EInputKey::W, EInputKey::S, EInputKey::A, EInputKey::D, EInputKey::E, EInputKey::Q};
    std::array<EInputKey, 6> oldBindings{};
    for (size_t i = 0; i < oldBindings.size(); ++i) oldBindings[i] = Settings::Binding(actions[i], defaults[i]);
    jEditor editor;
    jEditorProject project;
    std::string error;
    const auto text = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    try
    {
        expect(project.Create(fixture, "Settings fixture", error), "create real project manifest");
        jEditorAuthoring::Document scene;
        scene.Create(jEditorAuthoring::EntityKind::Cube, "Startup cube");
        expect(scene.Save(fixture / "Scenes/start.jscene"), "write actual authored startup scene");
        auto candidate = project.GetConfig();
        candidate.StartupScene = "Scenes/start.jscene";
        candidate.Settings = {{"render.deferred", "false"}, {"render.surfelgi", "false"}, {"render.surfelgi.inline", "true"},
            {"render.ssgi", "true"}, {"render.bloom", "true"}, {"render.vsync", "true"},
            {"render.surfelgi.intensity", "2.5"}, {"render.surfelgi.inline.rays", "8"},
            {"render.shadow.filter", "1"}, {"render.shadow.radiusTexels", "2.25"},
            {"input.camera.forward", "I"}, {"input.camera.back", "K"}, {"input.camera.left", "J"},
            {"input.camera.right", "L"}, {"input.camera.up", "U"}, {"input.camera.down", "O"},
            {"custom.future.option", "preserve this value"}};
        jAdvancedShadows::Settings advanced;
        advanced.DirectionalProjection = jAdvancedShadows::Projection::Cascaded;
        advanced.PointProjection = jAdvancedShadows::PointProjection::DualParaboloid;
        advanced.Method = jAdvancedShadows::Representation::ExponentialVariance;
        advanced.MapResolution = 128; advanced.MinimumVariance = 1e-8f;
        expect(Settings::WriteAdvancedShadows(candidate, advanced, error), "encode full independent shadow defaults without losing small variance values");
        candidate.Settings.insert({{"render.msaa.samples","4"}, {"render.sss.enabled","true"}, {"render.sss.strength","0.6"},
            {"render.sss.radiusScale","3"}, {"render.sss.depthRejectWorld","0.2"}, {"render.sss.normalThreshold","0.7"},
            {"render.sss.maxRadiusPixels","24"}, {"render.hiz.enabled","true"}, {"render.hiz.depthBias","0.00002"}, {"render.hiz.maxCandidates","2048"}});
        jTextureSubsurface::Settings textureTransport;
        textureTransport.TileResolution = 512; textureTransport.CaptureResolution = 2048; textureTransport.GaussianStepScale = 1.5f;
        textureTransport.WorldUnitsPerMillimeter = .3f; textureTransport.EnableTransmission = false;
        textureTransport.TransmissionExtinctionPerMillimeter = .4f; textureTransport.TransmissionThicknessScale = .8f; textureTransport.CaptureBiasWorld = .1f;
        expect(Settings::WriteTextureSubsurface(candidate, textureTransport, error),
            "encode bounded texture diffusion with an explicit source-specific world-unit conversion");
        GSupportInlineRaytracing = false; GRHISupportVsync = false;
        const auto documentBeforeSettings = editor.GetDocument().ToJson();
        const auto revisionBeforeSettings = editor.GetDocument().Revision();
        expect(Settings::Validate(candidate, error, fixture), "validated typed renderer defaults and portable camera bindings");
        expect(Settings::Save(project, editor, candidate, error), "save candidate manifest then apply actual settings");
        expect(!gOptions.UseDeferredRenderer && !gOptions.UseSurfelGI && gOptions.UseSSGI && gOptions.BloomEyeAdaptation
            && gOptions.SurfelGIIntensity == 2.5f && gOptions.SurfelGIInlineRayCount == 8,
            "renderer options reflect persisted values rather than a UI-only copy");
        expect(gOptions.ShadowFilterMode == 1 && gOptions.ShadowFilterRadiusTexels == 2.25f
            && editor.GetDocument().ToJson() == documentBeforeSettings && editor.GetDocument().Revision() == revisionBeforeSettings,
            "saved Poisson shadow-map settings apply without changing the scene document or undo revision");
        expect(gOptions.AdvancedShadows == advanced,
            "advanced shadow settings publish the exact saved typed snapshot rather than a UI-only copy");
        expect(gOptions.RequestedMSAASamples==4&&gOptions.Subsurface.Enabled&&gOptions.Subsurface.Strength==.6f
            &&gOptions.Subsurface.RadiusScale==3&&gOptions.Subsurface.MaxRadiusPixels==24
            &&gOptions.HiZOcclusion.Enabled&&gOptions.HiZOcclusion.MaxCandidates==2048&&gOptions.HiZOcclusion.DepthBias==.00002f,
            "saved MSAA SSS and Hi-Z requests reach actual renderer options without depending on current device capability");
        expect(gOptions.TextureSubsurface.TileResolution == 512 && gOptions.TextureSubsurface.CaptureResolution == 2048
            && gOptions.TextureSubsurface.GaussianStepScale == 1.5f && gOptions.TextureSubsurface.WorldUnitsPerMillimeter == .3f
            && !gOptions.TextureSubsurface.EnableTransmission && gOptions.TextureSubsurface.TransmissionExtinctionPerMillimeter == .4f
            && gOptions.TextureSubsurface.TransmissionThicknessScale == .8f && gOptions.TextureSubsurface.CaptureBiasWorld == .1f,
            "saved UV transport settings publish every actual runtime field without silently applying a skin-specific scale");
        expect(!gOptions.SurfelGIInlineRayEnable && !GUseVsync
            && project.GetConfig().Settings.at("render.surfelgi.inline") == "true"
            && project.GetConfig().Settings.at("render.vsync") == "true", "unsupported requested features retained while effective states remain disabled");
        jEditorProject reopened;
        expect(reopened.Open(project.GetProjectFile(), error) && Settings::Validate(reopened, error)
            && reopened.GetConfig().Settings == candidate.Settings && reopened.GetConfig().StartupScene == candidate.StartupScene,
            "saved renderer/input/startup settings survive a real project reopen");
        jTextureSubsurface::Settings readTexture;
        expect(Settings::ReadTextureSubsurface(reopened.GetConfig(), readTexture, error)
            && readTexture.WorldUnitsPerMillimeter == .3f && readTexture.TileResolution == 512
            && readTexture.CaptureResolution == 2048 && !readTexture.EnableTransmission && readTexture.CaptureBiasWorld == .1f,
            "real project reopen preserves the texture-transport unit conversion and capture controls");
        gOptions.ShadowFilterMode = 0; gOptions.ShadowFilterRadiusTexels = 1.0f;
        GSupportInlineRaytracing = true; GRHISupportVsync = true;
        expect(Settings::Apply(reopened, editor, error) && gOptions.SurfelGIInlineRayEnable && GUseVsync,
            "same request enables actual inline/VSync options when capabilities are present");
        expect(gOptions.ShadowFilterMode == 1 && gOptions.ShadowFilterRadiusTexels == 2.25f,
            "reopened project restores actual shadow filtering options");
        expect(gOptions.AdvancedShadows == advanced && gOptions.AdvancedShadows.MinimumVariance == 1e-8f,
            "actual project reopen and Apply preserve the smallest supported variance exactly");
        expect(Settings::ConsumePresentModeChange() && !Settings::ConsumePresentModeChange(), "VSync queues exactly one safe-boundary swapchain refresh request");
        ResetAllKeyState(); SetKeyDownState(EInputKey::I, true);
        expect(Settings::ActionDown("camera.forward", EInputKey::W) && !Settings::ActionDown("camera.back", EInputKey::S),
            "ActionDown reads configured real engine key state");
        SetKeyDownState(EInputKey::I, false); SetKeyDownState(EInputKey::W, true);
        expect(!Settings::ActionDown("camera.forward", EInputKey::W), "configured action stops using its old default key");
        SetKeyDownState(EInputKey::I, true);
        bool modifiersBlock = true;
        for (auto modifier : {EInputKey::CTRL, EInputKey::ALT, EInputKey::SHIFT})
        {
            SetKeyDownState(modifier, true); modifiersBlock &= !Settings::ActionDown("camera.forward", EInputKey::W);
            SetKeyDownState(modifier, false);
        }
        expect(modifiersBlock, "Ctrl Alt and Shift chords never move the camera");
        expect(!Settings::ActionDown("camera.unknown", EInputKey::MAX) && !Settings::ActionDown(nullptr, EInputKey::UNKNOWN),
            "invalid fallbacks never index outside engine key storage");
        EInputKey parsed = EInputKey::UNKNOWN;
        expect(Settings::ParseKey("up", parsed) && parsed == EInputKey::UP && std::string(Settings::KeyName(parsed)) == "UP",
            "portable key parser accepts canonical case-insensitive names");
        expect(Settings::ParseKey("7", parsed) && parsed == EInputKey::NUM7 && !Settings::ParseKey("CTRL", parsed)
            && !Settings::ParseKey("CMD+W", parsed) && !Settings::ParseKey("F24", parsed), "modifier chords and unsupported names are rejected as camera keys");
        for (const auto& values : {std::pair{"0", "0"}, std::pair{"1", "4"}})
        {
            auto boundary = project.GetConfig();
            boundary.Settings["render.shadow.filter"] = values.first;
            boundary.Settings["render.shadow.radiusTexels"] = values.second;
            expect(Settings::Save(project, editor, boundary, error)
                && gOptions.ShadowFilterMode == std::stoi(values.first) && gOptions.ShadowFilterRadiusTexels == std::stof(values.second),
                "both shadow filter modes and inclusive radius endpoints save and publish");
        }
        expect(Settings::Save(project, editor, candidate, error), "restore the valid Poisson project before transactional rejection checks");
        const auto validConfig = project.GetConfig();
        const auto validDisk = text(project.GetProjectFile());
        const auto reject = [&](const jEditorProjectConfig& bad, const char* name) {
            const bool deferred = gOptions.UseDeferredRenderer, surfel = gOptions.UseSurfelGI;
            const int shadowMode = gOptions.ShadowFilterMode;
            const float shadowRadius = gOptions.ShadowFilterRadiusTexels;
            const auto advancedBefore = gOptions.AdvancedShadows;
            const jOptions allOptionsBefore = gOptions;
            const auto binding = Settings::Binding("camera.forward", EInputKey::W);
            expect(!Settings::Save(project, editor, bad, error) && !error.empty()
                && project.GetConfig().Settings == validConfig.Settings && project.GetConfig().Name == validConfig.Name
                && project.GetConfig().StartupScene == validConfig.StartupScene && text(project.GetProjectFile()) == validDisk
                && gOptions.UseDeferredRenderer == deferred && gOptions.UseSurfelGI == surfel
                && gOptions.ShadowFilterMode == shadowMode && gOptions.ShadowFilterRadiusTexels == shadowRadius
                && gOptions.AdvancedShadows == advancedBefore
                && gOptions == allOptionsBefore
                && Settings::Binding("camera.forward", EInputKey::W) == binding, name);
        };
        for (const std::string invalid : {"NaN", "inf", "-1", "8.1", "2garbage", "", " 1", "1 "})
        { auto bad = validConfig; bad.Settings["render.surfelgi.intensity"] = invalid; reject(bad, "invalid numeric setting preserves current config/disk/runtime"); }
        for (const std::string invalid : {"0", "17", "1.5", "3x", "-1"})
        { auto bad = validConfig; bad.Settings["render.surfelgi.inline.rays"] = invalid; reject(bad, "invalid integer/ray count rejected transactionally"); }
        for (const std::string invalid : {"2", "-1", "1.0", "1x", "NaN", "", " 1", "1 "})
        { auto bad = validConfig; bad.Settings["render.shadow.filter"] = invalid; reject(bad, "invalid shadow mode preserves config disk and actual runtime options"); }
        for (const std::string invalid : {"NaN", "inf", "-0.01", "4.01", "2garbage", "", " 1", "1 "})
        { auto bad = validConfig; bad.Settings["render.shadow.radiusTexels"] = invalid; reject(bad, "nonfinite out-of-range or malformed shadow radius rejected transactionally"); }
        for (const std::string invalid : {"3", "-1", "1.5", "1x", "NaN", "", " 1", "1 "})
        { auto bad = validConfig; bad.Settings["render.shadow.projection"] = invalid; reject(bad, "advanced shadow enums require complete bounded integers"); }
        for (const std::string invalid : {"NaN", "inf", "0", "1e-9", "1.01", "1x", " 0.2"})
        { auto bad = validConfig; bad.Settings["render.shadow.moments.minimumVariance"] = invalid; reject(bad, "invalid advanced moments preserve manifest runtime and camera state"); }
        for (const auto pair : {std::pair{"render.msaa.samples","3"}, std::pair{"render.msaa.samples","2.0"},
            std::pair{"render.sss.enabled","1"}, std::pair{"render.sss.strength","NaN"}, std::pair{"render.sss.radiusScale","101"},
            std::pair{"render.sss.depthRejectWorld","0"}, std::pair{"render.sss.normalThreshold","1.1"}, std::pair{"render.sss.maxRadiusPixels","65"},
            std::pair{"render.hiz.depthBias","-0.1"}, std::pair{"render.hiz.maxCandidates","16385"}, std::pair{"render.hiz.maxCandidates","1.5"}})
        { auto bad=validConfig;bad.Settings[pair.first]=pair.second;reject(bad,"invalid quality MSAA and Hi-Z values retain disk, all requested options and camera bindings transactionally"); }
        for (const auto pair : {std::pair{"render.textureSubsurface.tileResolution","96"}, std::pair{"render.textureSubsurface.captureResolution","4096"},
            std::pair{"render.textureSubsurface.gaussianStepScale","11"}, std::pair{"render.textureSubsurface.worldUnitsPerMillimeter","0"},
            std::pair{"render.textureSubsurface.worldUnitsPerMillimeter","NaN"}, std::pair{"render.textureSubsurface.enableTransmission","1"},
            std::pair{"render.textureSubsurface.transmissionExtinctionPerMillimeter","0"}, std::pair{"render.textureSubsurface.transmissionThicknessScale","-1"},
            std::pair{"render.textureSubsurface.captureBiasWorld","101"}})
        { auto bad = validConfig; bad.Settings[pair.first] = pair.second; reject(bad, "invalid UV transport units resolution or physical controls retain manifest and every runtime request"); }
        {
            auto bad = validConfig; bad.Settings["render.textureSubsurface.worldUnitsPerMillimeter"] = "NaN";
            auto retained = textureTransport;
            expect(!Settings::ReadTextureSubsurface(bad, retained, error) && retained.WorldUnitsPerMillimeter == .3f && retained.TileResolution == 512,
                "failed texture transport read preserves the caller's last validated snapshot");
            auto invalid = textureTransport; invalid.TileResolution = 96; auto retainedConfig = validConfig;
            expect(!Settings::WriteTextureSubsurface(retainedConfig, invalid, error) && retainedConfig.Settings == validConfig.Settings,
                "failed texture transport write preserves all existing known and unknown project entries");
        }
        for (const auto pair : {std::pair{"render.shadow.resolution", "96"}, std::pair{"render.shadow.cascade.count", "5"},
            std::pair{"render.shadow.volume.maxTriangles", "65537"}, std::pair{"render.shadow.deep.tauMax", "33"},
            std::pair{"render.shadow.cascade.stabilize", "1"}})
        { auto bad = validConfig; bad.Settings[pair.first] = pair.second; reject(bad, "advanced shadow capacity and strict boolean limits reject transactionally"); }
        {
            auto bad = validConfig; bad.Settings["render.shadow.resolution"] = "96";
            auto retained = advanced; const auto before = retained;
            expect(!Settings::ReadAdvancedShadows(bad, retained, error) && retained == before,
                "failed shadow codec leaves the caller's previous snapshot intact");
            auto invalid = advanced; invalid.DeepTauMax = 33;
            auto retainedConfig = validConfig;
            expect(!Settings::WriteAdvancedShadows(retainedConfig, invalid, error) && retainedConfig.Settings == validConfig.Settings,
                "failed shadow encoding preserves every existing known and unknown project setting");
        }
        { auto bad = validConfig; bad.Settings["render.ssgi"] = "yes"; reject(bad, "ambiguous boolean cannot silently enable renderer settings"); }
        { auto bad = validConfig; bad.Settings["input.camera.forward"] = "CTRL+I"; reject(bad, "invalid camera key cannot replace active bindings"); }
        { auto bad = validConfig; bad.Settings["input.camera.forward"] = "K"; reject(bad, "conflicting camera bindings rejected with diagnostic"); }
        for (const std::string invalid : {"../escape.jscene", "/tmp/external.jscene", "Scenes/missing.jscene", "Scenes/start.txt", "C:/scene.jscene", "Scenes\\start.jscene"})
        { auto bad = validConfig; bad.StartupScene = invalid; reject(bad, "unsafe missing or wrong-type startup scene cannot mutate project"); }
        { auto bad = validConfig; bad.StartupScene = std::string("Scenes/start.jscene\0ignored", 26); reject(bad, "startup path containing NUL rejected before filesystem access"); }
        { auto bad = validConfig; bad.Name.clear(); reject(bad, "empty project name rejected transactionally"); }
        { auto bad = validConfig; bad.Name = std::string("\xc0\xaf", 2); reject(bad, "invalid UTF-8 project name rejected before JSON or filesystem publication"); }
        { auto bad = validConfig; bad.Settings["custom.invalid"] = std::string("\xff", 1); reject(bad, "invalid UTF-8 unknown settings cannot throw from JSON save"); }
        { auto bad = validConfig; bad.Id = "other"; reject(bad, "settings save cannot replace persistent project identity"); }
        { auto bad = validConfig; bad.AssetRoot = "OtherAssets"; reject(bad, "settings save cannot silently relocate project assets"); }
        auto preserve = validConfig;
        preserve.Settings.erase("custom.future.option"); preserve.Settings["render.ssgi"] = "false";
        expect(Settings::Save(project, editor, preserve, error) && project.GetConfig().Settings.at("custom.future.option") == "preserve this value",
            "unknown project settings survive an older editor candidate omitting them");
        auto defaultsConfig = project.GetConfig();
        defaultsConfig.Settings = {{"custom.future.option", "preserve this value"}};
        const jOptions defaults;
        expect(Settings::Save(project, editor, defaultsConfig, error) && gOptions.UseDeferredRenderer == defaults.UseDeferredRenderer
            && gOptions.UseSurfelGI == defaults.UseSurfelGI && gOptions.UseSSGI == defaults.UseSSGI
            && gOptions.BloomEyeAdaptation == defaults.BloomEyeAdaptation && gOptions.SurfelGIIntensity == defaults.SurfelGIIntensity
            && gOptions.SurfelGIInlineRayCount == defaults.SurfelGIInlineRayCount && !GUseVsync
            && gOptions.ShadowFilterMode == defaults.ShadowFilterMode && gOptions.ShadowFilterRadiusTexels == defaults.ShadowFilterRadiusTexels
            && gOptions.AdvancedShadows == defaults.AdvancedShadows
            && gOptions.RequestedMSAASamples==1&&!gOptions.Subsurface.Enabled&&!gOptions.HiZOcclusion.Enabled
            && gOptions.TextureSubsurface.WorldUnitsPerMillimeter == 1.f
            && gOptions.TextureSubsurface.TileResolution == defaults.TextureSubsurface.TileResolution
            && Settings::Binding("camera.forward", EInputKey::I) == EInputKey::W, "omitted known settings restore engine defaults rather than previous project values");
        expect(Settings::ConsumePresentModeChange(), "resetting VSync defaults also schedules frame-boundary refresh");
        RegisterAdvancedShadowConsoleVariables(); RegisterAdvancedShadowConsoleVariables();
        RegisterRendererFeatureConsoleVariables(); RegisterRendererFeatureConsoleVariables();
        auto* msaa=jConsole::Get().FindVariable("r.msaa.samples");auto* sssStrength=jConsole::Get().FindVariable("r.sss.strength");
        auto* hizCandidates=jConsole::Get().FindVariable("r.hiz.max_candidates");
        expect(msaa&&sssStrength&&hizCandidates,"real MSAA SSS and Hi-Z console adapters register idempotently");
        if(msaa&&sssStrength&&hizCandidates)
        {
            msaa->SetFromString("2");sssStrength->SetFromString("0.75");hizCandidates->SetFromString("123");
            expect(gOptions.RequestedMSAASamples==2&&gOptions.Subsurface.Strength==.75f&&gOptions.HiZOcclusion.MaxCandidates==123,
                "actual quality console adapters publish validated request values");
            const jOptions accepted=gOptions;
            msaa->SetFromString("3");sssStrength->SetFromString("NaN");hizCandidates->SetFromString("16385");
            expect(gOptions==accepted,"invalid feature console values retain all previous requested options");
            std::thread foreignFeature([&]{msaa->SetFromString("4");});foreignFeature.join();
            expect(gOptions==accepted,"foreign threads cannot publish renderer requests through the console");
            gOptions.RequestedMSAASamples=defaults.RequestedMSAASamples;gOptions.Subsurface=defaults.Subsurface;gOptions.HiZOcclusion=defaults.HiZOcclusion;
        }
        auto* representation = jConsole::Get().FindVariable("r.shadow.representation");
        auto* variance = jConsole::Get().FindVariable("r.shadow.moments.min_variance");
        expect(representation&&variance,"validated advanced shadow console variables register idempotently");
        if(representation&&variance)
        {
            representation->SetFromString("3");variance->SetFromString("1e-8");
            const auto accepted=gOptions.AdvancedShadows;
            expect(accepted.Method==jAdvancedShadows::Representation::ExponentialVariance&&accepted.MinimumVariance==1e-8f,
                "real typed console adapters publish bounded enum and small float values");
            representation->SetFromString("6");variance->SetFromString("NaN");variance->SetFromString("1e-9");
            expect(gOptions.AdvancedShadows==accepted,"invalid console edits preserve the last valid complete shadow snapshot");
            std::thread foreignShadow([&]{representation->SetFromString("1");});foreignShadow.join();
            expect(gOptions.AdvancedShadows==accepted,"foreign thread cannot publish shadow settings through console adapters");
            gOptions.AdvancedShadows=defaults.AdvancedShadows;
        }
        const auto savedSettings = project.GetConfig();
        fs::rename(project.GetProjectFile(), fixture / "saved-manifest.jproject");
        fs::create_directory(project.GetProjectFile());
        auto failedIo = savedSettings; failedIo.Settings["render.bloom"] = "true";
        failedIo.Settings["render.shadow.filter"] = "1"; failedIo.Settings["render.shadow.radiusTexels"] = "4";
        expect(!Settings::Save(project, editor, failedIo, error) && project.GetConfig().Settings == savedSettings.Settings
            && gOptions.BloomEyeAdaptation == defaults.BloomEyeAdaptation
            && gOptions.ShadowFilterMode == defaults.ShadowFilterMode && gOptions.ShadowFilterRadiusTexels == defaults.ShadowFilterRadiusTexels,
            "real manifest write failure preserves last valid config and renderer state");
        fs::remove(project.GetProjectFile()); fs::rename(fixture / "saved-manifest.jproject", project.GetProjectFile());
        auto* context = ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr; io.DisplaySize = ImVec2(900, 900); io.DeltaTime = 1.0f / 60;
        unsigned char* pixels = nullptr; int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Settings::SetVisible(true);
        for (int frame = 0; frame < 3; ++frame) { ImGui::NewFrame(); Settings::Render(editor, &project); ImGui::Render(); }
        expect(Settings::IsVisible() && ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0,
            "real ImGui project settings panel renders controls without GPU publication");
        ImGui::DestroyContext(context); ImGui::SetCurrentContext(oldContext);
        const auto beforeForeign = gOptions.UseDeferredRenderer;
        bool foreignAccepted = true;
        std::thread foreign([&] { std::string local; foreignAccepted = Settings::Apply(project, editor, local); }); foreign.join();
        expect(!foreignAccepted && gOptions.UseDeferredRenderer == beforeForeign, "foreign thread cannot apply renderer or camera input settings");
        // Restore the process's previous camera bindings before the fixture is removed.
        auto restore = project.GetConfig();
        for (size_t i = 0; i < oldBindings.size(); ++i)
            if (const char* key = Settings::KeyName(oldBindings[i])) restore.Settings[std::string("input.") + actions[i]] = key;
        project.EditConfig() = restore;
        Settings::Apply(project, editor, error);
    }
    catch (const std::exception& exception) { std::cout << "FAIL project settings: " << exception.what() << '\n'; ++failures; }
    ImGui::SetCurrentContext(oldContext);
    GSupportInlineRaytracing = oldInline; GRHISupportVsync = oldVsyncSupport; GUseVsync = oldVsync;
    gOptions.UseDeferredRenderer = oldOptions.UseDeferredRenderer; gOptions.UseSurfelGI = oldOptions.UseSurfelGI;
    gOptions.ShadowFilterMode = oldOptions.ShadowFilterMode; gOptions.ShadowFilterRadiusTexels = oldOptions.ShadowFilterRadiusTexels;
    gOptions.SurfelGIInlineRayEnable = oldOptions.SurfelGIInlineRayEnable; gOptions.UseSSGI = oldOptions.UseSSGI;
    gOptions.BloomEyeAdaptation = oldOptions.BloomEyeAdaptation; gOptions.SurfelGIIntensity = oldOptions.SurfelGIIntensity;
    gOptions.SurfelGIInlineRayCount = oldOptions.SurfelGIInlineRayCount;
    Settings::ConsumePresentModeChange(); Settings::SetVisible(oldVisible); g_KeyState = oldKeys;
    std::error_code ignored; fs::remove_all(fixture, ignored);
    std::cout << "Project settings tests: " << checks << " checks, " << failures << " failures\n";
    return failures;
#else
    return 0;
#endif
}
