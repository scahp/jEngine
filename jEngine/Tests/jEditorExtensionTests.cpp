#ifndef JENGINE_EXTENSION_LOADER_ONLY
#include "pch.h"
#endif
#include "Code/Engine/Editor/jEditorExtensions.h"
#include "Code/Engine/Editor/jEditorDocument.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace
{
using namespace jEditorAuthoring;
struct HostState {Document Scene;size_t Logs=0;};
void Log(void* context,int32_t,const char*){++static_cast<HostState*>(context)->Logs;}
uint64_t Create(void* context,uint32_t kind,const char* name)
{
    if(kind>9||kind==3)return 0;return static_cast<HostState*>(context)->Scene.Create(static_cast<EntityKind>(kind),name?name:"");
}
int32_t TransformEntity(void* context,uint64_t id,const float p[3],const float r[3],const float s[3])
{
    auto& scene=static_cast<HostState*>(context)->Scene;const auto* previous=scene.Find(id);if(!previous)return 0;auto entity=*previous;
    entity.Local.Position={p[0],p[1],p[2]};entity.Local.RotationDegrees={r[0],r[1],r[2]};entity.Local.Scale={s[0],s[1],s[2]};return scene.Update(entity,"Native fixture transform");
}
}
int RunEditorExtensionTests()
{
#ifndef JENGINE_PLUGIN_FIXTURE_DIRECTORY
    std::cout<<"SKIP native extension shared-library fixtures are not configured for this build"<<std::endl;return 0;
#else
    namespace fs=std::filesystem;using namespace jEditorExtensions;
    int failures=0;const auto expect=[&](bool condition,const char* name){std::cout<<(condition?"PASS ":"FAIL ")<<name<<std::endl;failures+=!condition;};
#ifdef _WIN32
    const std::string suffix=".dll";
#elif defined(__APPLE__)
    const std::string suffix=".dylib";
#else
    const std::string suffix=".so";
#endif
    const fs::path fixtures=JENGINE_PLUGIN_FIXTURE_DIRECTORY;
    const auto directory=fs::temp_directory_path()/("jengine-plugin-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);const auto live=directory/("live"+suffix);int stamp=0;
    const auto install=[&](const char* kind){
        fs::copy_file(fixtures/(std::string("jengine_plugin_fixture_")+kind+suffix),live,fs::copy_options::overwrite_existing);
        fs::last_write_time(live,fs::file_time_type::clock::now()+std::chrono::seconds(++stamp));
    };
    try{
        HostState hostState;JEnginePluginHostV1 host{JENGINE_EDITOR_PLUGIN_ABI_V1,sizeof(JEnginePluginHostV1),&hostState,Log,Create,TransformEntity,nullptr,nullptr,nullptr};
        EditHooks hooks;hooks.Begin=[&]{return hostState.Scene.BeginTransaction("Load native fixture");};hooks.Commit=[&]{return hostState.Scene.CommitTransaction();};hooks.Cancel=[&]{hostState.Scene.CancelTransaction();};
        Manager manager(host,directory/"cache",hooks);
        install("v1");const auto id=manager.Load(live);
        expect(id&&manager.Plugins().size()==1,"real native shared library loads through versioned C descriptor");
        if(!id){std::cout<<manager.LastError()<<std::endl;throw std::runtime_error("fixture startup failed");}
        expect(manager.Load(live)==id&&manager.Plugins().size()==1,"loading the same source does not duplicate extension ownership");
        manager.Tick(.016f);manager.Tick(.016f);manager.Tick(.016f);
        expect(hostState.Scene.Entities().size()==1,"real plugin callback creates a plain authored entity through host API");
        const auto entity=hostState.Scene.Entities().begin()->first;
        expect(hostState.Scene.Find(entity)->Local.Position.X==3,"real plugin Tick updates the live document transform");
        std::vector<uint8_t> saved;expect(manager.SaveState(id,saved)&&!saved.empty()&&saved.size()<=JENGINE_EDITOR_PLUGIN_MAX_STATE,"actual plugin state snapshot obeys the bounded save contract");
        install("v2");expect(manager.Reload(id),"staged native replacement loads and restores optional state");manager.Tick(.016f);
        expect(manager.Plugins()[0].Generation==2&&manager.Plugins()[0].Name.find("v2")!=std::string::npos
            &&hostState.Scene.Entities().size()==1&&hostState.Scene.Find(entity)->Local.Position.X==5,
            "reload changes real code while preserving counter and authored entity identity");
        const float beforeAbi=hostState.Scene.Find(entity)->Local.Position.X;install("badabi");
        expect(!manager.Reload(id)&&manager.Plugins()[0].Generation==2,"bad replacement ABI is rejected without unloading current code");manager.Tick(.016f);
        expect(hostState.Scene.Find(entity)->Local.Position.X==beforeAbi+2,"old real plugin remains callable after rejected ABI");
        const auto count=hostState.Scene.Entities().size();install("failload");
        expect(!manager.Reload(id)&&hostState.Scene.Entities().size()==count&&!hostState.Scene.HasTransaction(),"failed OnLoad rolls back its actual host document edits and closes transaction");
        const float beforeRestore=hostState.Scene.Find(entity)->Local.Position.X;install("failrestore");
        expect(!manager.Reload(id),"real replacement RestoreState rejection preserves the old module");manager.Tick(.016f);
        expect(hostState.Scene.Find(entity)->Local.Position.X==beforeRestore+2,"failed state migration leaves old private state and entity identity intact");
        install("missingexport");expect(!manager.Reload(id)&&manager.Plugins()[0].Generation==2,"real library missing descriptor export is rejected safely");
        bool foreignAccepted=true;std::thread foreign([&]{foreignAccepted=manager.Reload(id);});foreign.join();
        expect(!foreignAccepted&&manager.Plugins()[0].Generation==2,"foreign-thread reload cannot enter callbacks or unload owner code");
        install("v1");manager.SetAutoReload(id,true);
        std::this_thread::sleep_for(std::chrono::milliseconds(280));manager.Tick(.016f);
        expect(manager.Plugins()[0].Generation==2,"watcher waits for a stable source instead of loading a changing build immediately");
        const float beforeWatch=hostState.Scene.Find(entity)->Local.Position.X;
        std::this_thread::sleep_for(std::chrono::milliseconds(560));manager.Tick(.016f);
        expect(manager.Plugins()[0].Generation==3&&manager.Plugins()[0].Name.find("v1")!=std::string::npos
            &&hostState.Scene.Find(entity)->Local.Position.X==beforeWatch+1,"stable-mtime watcher performs actual code replacement with state retained");
        const auto oversized=manager.Load(fixtures/("jengine_plugin_fixture_oversized"+suffix));
        expect(oversized&&!manager.SaveState(oversized,saved)&&saved.empty(),"real oversized state exporter is rejected at 64 KiB");
        expect(!manager.Reload(oversized),"oversized old state prevents replacement before old code is discarded");manager.Unload(oversized);
        expect(manager.Unload(id)&&manager.Plugins().empty(),"explicit native unload ends callbacks and releases all loaded module records");
        manager.Tick(.016f);expect(hostState.Scene.Find(entity)->Local.Position.X==beforeWatch+1,"unloaded plugin no longer ticks or mutates the authored scene");
        manager.Shutdown();manager.Shutdown();
        size_t staged=0;for(auto it=fs::directory_iterator(directory/"cache");it!=fs::directory_iterator();++it)++staged;
        expect(staged==0,"successful and failed staged libraries leave no live shadow-copy directories after unload");
    }catch(const std::exception& error){std::cout<<"FAIL native extension fixture exception: "<<error.what()<<std::endl;++failures;}
    std::error_code ignored;fs::remove_all(directory,ignored);return failures;
#endif
}
