#include "pch.h"
#include "jSceneFeatureProjectTests.h"
#include "Code/Engine/Editor/jEditorProjectSettings.h"
#include "Code/Engine/Editor/jSceneFeatureTools.h"
#include "Code/Engine/jEditor.h"
#include "FileLoader/jEditorAssets.h"
#include "Renderer/Features/jEnvironmentLighting.h"
#include "External/loadpng/lodepng.h"
#include "External/stb/stb_image.h"
#include "jOptions.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

int RunSceneFeatureProjectTests()
{
#ifdef ENABLE_EDITOR_FEATURES
    namespace fs=std::filesystem;
    namespace Settings=jEditorProjectSettings;
    namespace Tools=jSceneFeatureTools;
    int checks=0,failures=0;
    const auto expect=[&](bool condition,const char* label)
    {++checks;if(!condition){++failures;std::cerr<<"SceneFeatureProject FAIL: "<<label<<'\n';}};
    const auto root=fs::temp_directory_path()/("jengine-scene-project-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path Path;~Cleanup(){if(!Path.empty()){std::error_code ec;fs::remove_all(Path,ec);}}}cleanup;
    const jOptions old=gOptions;
    const bool oldVsync=GUseVsync,oldVisible=Tools::IsVisible();
    constexpr const char* actions[]={"camera.forward","camera.back","camera.left","camera.right","camera.up","camera.down"};
    constexpr EInputKey defaults[]={EInputKey::W,EInputKey::S,EInputKey::A,EInputKey::D,EInputKey::E,EInputKey::Q};
    std::array<EInputKey,6> bindings{};
    for(std::size_t i=0;i<bindings.size();++i)bindings[i]=Settings::Binding(actions[i],defaults[i]);
    jEditor editor;
    jEditorProject first,second;
    std::string error;
    const auto read=[](const fs::path& path)
    {std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
    const auto write=[](const fs::path& path,const std::string& data)
    {std::ofstream out(path,std::ios::binary);out.write(data.data(),std::streamsize(data.size()));if(!out)throw std::runtime_error("Fixture write failed.");};
    const auto wait=[&](jEditorProject* project)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        do
        {
            Tools::Tick(editor,project);
            if(!Tools::GetBakeStatus().Running)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }while(std::chrono::steady_clock::now()<deadline);
        Tools::CancelBake();Tools::Shutdown();return false;
    };
    try
    {
        if(!fs::create_directory(root))throw std::runtime_error("Scene project fixture already exists.");
        cleanup.Path=root;
        const bool created=first.Create(root/"first","Scene settings fixture",error)
            && second.Create(root/"second","Different active project",error);
        expect(created,"real project manifests create");
        if(!created)throw std::runtime_error("Cannot create scene feature fixture projects: "+error);
        jEnvironmentLighting::Coefficients coefficients{};
        const jEnvironmentLighting::RGB color{2,1,.5f};
        expect(jEnvironmentLighting::ProjectEquirectangular({1,1,&color,1},coefficients,error)
            && jEnvironmentLighting::SaveAsset(first.GetRoot()/"Assets/environment.jsh",coefficients,error),
            "actual finite SH coefficient asset prepares");
        auto config=first.GetConfig();
        config.Settings["custom.future"]="preserved";
        jSceneFeatures::Settings feature;
        feature.Environment.Enabled=true;feature.Environment.Intensity=2.5f;
        feature.Environment.Radiance=coefficients;feature.Environment.CombineWithDiffuseGI=true;
        feature.Reflection.Enabled=true;feature.Reflection.Plane={0,2,0,10};
        feature.Reflection.PlaneThickness=1.25f;feature.Reflection.RoughnessThreshold=.75f;
        feature.Atmosphere.Enabled=true;feature.Atmosphere.MetersPerWorldUnit=.01f;
        feature.Atmosphere.SunDirection={3,4,0};feature.Atmosphere.ViewSamples=24;feature.Atmosphere.LightSamples=6;
        expect(Settings::WriteSceneFeatures(config,feature,"Assets/environment.jsh",error)
            && Settings::Save(first,editor,config,error),"shared codec persists and publishes actual scene settings");
        expect(gOptions.SceneFeatures.Environment.Enabled && gOptions.SceneFeatures.Environment.Intensity==2.5f
            && gOptions.SceneFeatures.Environment.Radiance==coefficients
            && gOptions.SceneFeatures.Reflection.Plane[1]==1 && gOptions.SceneFeatures.Reflection.Plane[3]==5
            && gOptions.SceneFeatures.Atmosphere.ViewSamples==24,"runtime snapshot contains SH asset/normalized plane/atmosphere");
        jEditorProject reopened;
        expect(reopened.Open(first.GetProjectFile(),error) && Settings::Apply(reopened,editor,error)
            && reopened.GetConfig().Settings==first.GetConfig().Settings,"scene feature project settings reopen and apply");
        jSceneFeatures::Settings decoded;std::string asset;
        expect(Settings::ReadSceneFeatures(reopened.GetConfig(),reopened.GetRoot(),decoded,asset,error)
            && decoded.Atmosphere.RayleighScattering==feature.Atmosphere.RayleighScattering
            && asset=="Assets/environment.jsh","small scattering floats round trip without decimal truncation");
        const auto savedFile=read(first.GetProjectFile());
        const auto savedConfig=first.GetConfig().Settings;
        const auto savedFeature=gOptions.SceneFeatures;
        for(const auto& invalid:std::vector<std::pair<std::string,std::string>>{
            {"render.environment.enabled","1"},{"render.environment.intensity","nan"},
            {"render.ppr.plane","0,0,0,4"},{"render.ppr.roughness","1.5"},
            {"render.atmosphere.sun","0,0,0"},{"render.atmosphere.viewSamples","65"},
            {"render.atmosphere.lightSamples","0"},{"render.atmosphere.metersPerUnit","-1"},
            {"render.environment.shAsset","../escape.jsh"},{"render.environment.shAsset","Assets/missing.jsh"}})
        {
            auto bad=first.GetConfig();bad.Settings[invalid.first]=invalid.second;
            expect(!Settings::Save(first,editor,bad,error) && first.GetConfig().Settings==savedConfig
                && read(first.GetProjectFile())==savedFile
                && gOptions.SceneFeatures.Environment.Radiance==savedFeature.Environment.Radiance
                && gOptions.SceneFeatures.Atmosphere.ViewSamples==savedFeature.Atmosphere.ViewSamples,
                "invalid settings/path retain previous project file and runtime snapshot");
        }
        auto omitted=first.GetConfig();omitted.Settings.clear();
        expect(Settings::Save(first,editor,omitted,error)
            && first.GetConfig().Settings.at("custom.future")=="preserved"
            && !gOptions.SceneFeatures.Environment.Enabled && !gOptions.SceneFeatures.Reflection.Enabled
            && !gOptions.SceneFeatures.Atmosphere.Enabled,"omitted known scene keys reset defaults and unknown values survive");
        std::string hdr="#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
        for(unsigned char value:{128,64,32,130})hdr.push_back(char(value));
        write(first.GetRoot()/"Assets/source.hdr",hdr);
        Tools::BakeRequest sh;sh.SourcePath="Assets/source.hdr";sh.OutputPath="Assets/baked.jsh";
        expect(Tools::StartBake(first,sh,error),"real HDR projection schedules one CPU worker");
        std::string busy;
        expect(!Tools::StartBake(first,sh,busy),"second bake cannot race the active worker/publication");
        expect(wait(&first) && Tools::GetBakeStatus().Succeeded
            && first.GetConfig().Settings.at("render.environment.shAsset")=="Assets/baked.jsh"
            && gOptions.SceneFeatures.Environment.Enabled,"completed matching-project SH bake saves and applies coefficient asset");
        jEnvironmentLighting::Coefficients projected;
        expect(jEnvironmentLighting::LoadAsset(first.GetRoot()/"Assets/baked.jsh",projected,error)
            && std::abs(jEnvironmentLighting::EvaluateRadiance(projected,{0,1,0})[0]-2.f)<1e-4,
            "worker output came from actual HDR decode/projection");
        std::vector<unsigned char> pixels(8*8*4,255),png;
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)pixels[(y*8+x)*4]=x==4?255:0;
        if(LodePNG::encode(png,pixels,8,8,6,8))throw std::runtime_error("Cannot encode height fixture PNG.");
        write(first.GetRoot()/"Assets/height.png",std::string(reinterpret_cast<const char*>(png.data()),png.size()));
        Tools::BakeRequest horizon;horizon.Kind=Tools::BakeKind::Horizon;horizon.SourcePath="Assets/height.png";
        horizon.OutputPath="Assets/horizon";horizon.HorizonResolution=32;horizon.RadiusTexels=4;
        expect(Tools::StartBake(first,horizon,error) && wait(&first) && Tools::GetBakeStatus().Succeeded
            && Tools::GetBakeStatus().Outputs.size()==3,"real height decode and bounded horizon bake publishes three image assets");
        bool validPng=true;
        for(const auto& output:Tools::GetBakeStatus().Outputs)
        {
            const auto bytes=read(first.GetRoot()/output);int width=0,height=0,channels=0;
            validPng=validPng && stbi_info_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                int(bytes.size()),&width,&height,&channels) && width==32 && height==32;
        }
        expect(validPng,"generated horizon/AO files are actual decodable PNG payloads");
        const auto firstLayer=first.GetRoot()/"Assets/horizon/layer0.png";
        const auto oldLayer=read(firstLayer);
        jEditorAssetRegistry registry(first);
        expect(registry.Scan(error),"actual registry creates generated image sidecars");
        const auto* registered=registry.FindBySourcePath("Assets/horizon/layer0.png");
        const std::string assetId=registered?registered->Id:std::string();
        auto sidecar=firstLayer;sidecar+=".jasset";
        const auto originalSidecar=read(sidecar);
        horizon.HorizonResolution=64;
        expect(Tools::StartBake(first,horizon,error) && wait(&first) && Tools::GetBakeStatus().Succeeded
            && read(firstLayer)!=oldLayer,"managed bake folder replaces previous complete result");
        expect(!assetId.empty() && read(sidecar)==originalSidecar && registry.Scan(error)
            && registry.FindBySourcePath("Assets/horizon/layer0.png")
            && registry.FindBySourcePath("Assets/horizon/layer0.png")->Id==assetId,
            "rebake preserves exact registry sidecar and stable asset ID through rescan");
        const auto retainedLayer=read(firstLayer);
        write(first.GetRoot()/"Assets/horizon/keep.txt","user file");
        expect(Tools::StartBake(first,horizon,error) && wait(&first) && !Tools::GetBakeStatus().Succeeded
            && read(firstLayer)==retainedLayer && read(first.GetRoot()/"Assets/horizon/keep.txt")=="user file",
            "non-bake content blocks replacement and preserves previous assets");
        fs::remove(first.GetRoot()/"Assets/horizon/keep.txt");
        horizon.HorizonResolution=512;horizon.RadiusTexels=64;
        expect(Tools::StartBake(first,horizon,error),"bounded expensive bake schedules for cancellation race");
        Tools::CancelBake();
        expect(wait(&first) && (Tools::GetBakeStatus().Cancelled
            ? read(firstLayer)==retainedLayer : Tools::GetBakeStatus().Succeeded),
            "cancel/commit race yields either previous assets or a complete successful publication");
        const auto secondBefore=second.GetConfig().Settings;
        const auto featureBeforeSwitch=gOptions.SceneFeatures;
        sh.OutputPath="Assets/previous-project.jsh";
        expect(Tools::StartBake(first,sh,error),"previous-project job schedules before context switch");
        expect(wait(&second) && second.GetConfig().Settings==secondBefore
            && gOptions.SceneFeatures.Environment.Radiance==featureBeforeSwitch.Environment.Radiance,
            "project switch discards stale completion settings and reimport publication");
        auto bad=sh;bad.OutputPath="../outside.jsh";
        expect(!Tools::StartBake(first,bad,error) && !Tools::GetBakeStatus().Running,"bake output traversal rejected before work");
        bad=horizon;bad.HorizonResolution=513;
        expect(!Tools::StartBake(first,bad,error),"bake resolution budget rejected before decode");
        bool noStages=true;
        for(const auto& item:fs::recursive_directory_iterator(first.GetRoot()))
            noStages=noStages && item.path().filename().string().find(".jengine-bake-")!=0;
        expect(noStages,"success/failure/cancellation leave no temporary bake directories");
        Tools::Shutdown();
        expect(!Tools::GetBakeStatus().Running,"shutdown cancels and drains the worker");
    }
    catch(const std::exception& exception){expect(false,exception.what());}
    Tools::Shutdown();Tools::SetVisible(oldVisible);
    if(!first.GetRoot().empty())
    {
        auto restore=first.GetConfig();restore.Settings.clear();
        const auto boolean=[&](const char* key,bool value){restore.Settings[key]=value?"true":"false";};
        boolean("render.deferred",old.UseDeferredRenderer);boolean("render.surfelgi",old.UseSurfelGI);
        boolean("render.surfelgi.inline",old.SurfelGIInlineRayEnable);boolean("render.ssgi",old.UseSSGI);
        boolean("render.bloom",old.BloomEyeAdaptation);boolean("render.vsync",oldVsync);
        restore.Settings["render.surfelgi.intensity"]=std::to_string(old.SurfelGIIntensity);
        restore.Settings["render.surfelgi.inline.rays"]=std::to_string(old.SurfelGIInlineRayCount);
        restore.Settings["render.shadow.filter"]=std::to_string(old.ShadowFilterMode);
        restore.Settings["render.shadow.radiusTexels"]=std::to_string(old.ShadowFilterRadiusTexels);
        for(std::size_t i=0;i<bindings.size();++i)restore.Settings[std::string("input.")+actions[i]]=Settings::KeyName(bindings[i]);
        first.EditConfig()=restore;Settings::Apply(first,editor,error);
    }
    gOptions.SceneFeatures=old.SceneFeatures;GUseVsync=oldVsync;
    std::cout<<"Scene feature project tests: "<<checks-failures<<"/"<<checks<<" passed\n";
    return failures?1:0;
#else
    return 0;
#endif
}
