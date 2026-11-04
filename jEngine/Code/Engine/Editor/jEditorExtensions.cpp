#ifndef JENGINE_EXTENSION_LOADER_ONLY
#include "pch.h"
#endif
#include "jEditorExtensions.h"
#include "FileLoader/jEditorAssetPath.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <thread>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace jEditorExtensions
{
namespace
{
using Clock=std::chrono::steady_clock;
struct Stamp {
    std::filesystem::file_time_type Time{};uintmax_t Size=0;
    bool operator==(const Stamp&)const=default;
};
std::optional<Stamp> Signature(const std::filesystem::path& path)
{
    std::error_code ec;if(!std::filesystem::is_regular_file(path,ec)||ec)return {};
    Stamp s;s.Size=std::filesystem::file_size(path,ec);if(ec||!s.Size)return {};
    s.Time=std::filesystem::last_write_time(path,ec);return ec?std::optional<Stamp>{}:s;
}
bool ValidString(const char* text,size_t limit)
{
    return text&&std::memchr(text,0,limit)!=nullptr;
}
std::atomic<uint64_t> NextStage{1};
}
struct Manager::Impl
{
    struct Module {
        Info Details;
#ifdef _WIN32
        HMODULE Handle=nullptr;
#else
        void* Handle=nullptr;
#endif
        JEnginePluginV1 Api{};
        void* State=nullptr;
        bool Started=false;
        Stamp Accepted{},Observed{},Attempted{};
        Clock::time_point Changed=Clock::now();
        std::filesystem::path StageDirectory;
        ~Module(){Close();}
        void Close()
        {
#ifdef _WIN32
            if(Handle)FreeLibrary(Handle);
#else
            if(Handle)dlclose(Handle);
#endif
            Handle=nullptr;
            if(!StageDirectory.empty()){
                std::error_code ec;std::filesystem::remove(StageDirectory/Details.Source.filename(),ec);
                std::filesystem::remove(StageDirectory,ec);
            }
        }
    };
    JEnginePluginHostV1 Host;
    std::filesystem::path Cache;
    EditHooks Hooks;
    const std::thread::id Owner=std::this_thread::get_id();
    std::map<ExtensionId,std::unique_ptr<Module>> Loaded;
    std::string Error;
    ExtensionId Next=1;
    int CallDepth=0;
    Clock::time_point LastWatch=Clock::now();
    bool ValidHost=false;
    Impl(JEnginePluginHostV1 host,std::filesystem::path cache,EditHooks hooks):Host(host),Cache(std::move(cache)),Hooks(std::move(hooks))
    {
        ValidHost=Host.AbiVersion==JENGINE_EDITOR_PLUGIN_ABI_V1&&Host.StructSize>=sizeof(JEnginePluginHostV1)
            &&Host.CreateEntity&&Host.SetTransform&&Host.Log;
        if((Hooks.Begin||Hooks.Commit||Hooks.Cancel)&&(!Hooks.Begin||!Hooks.Commit||!Hooks.Cancel))ValidHost=false;
        if(!ValidHost)Error="Host ABI/version, required callback or document transaction hook set is invalid.";
        if(Cache.empty())Cache=std::filesystem::temp_directory_path()/"jengine-extension-cache";
    }
    bool Ready()const{return Owner==std::this_thread::get_id()&&CallDepth==0&&ValidHost;}
    template<class Function>auto Call(Function&& callback)->decltype(callback())
    {
        struct Guard {int& Depth;Guard(int& d):Depth(d){++Depth;}~Guard(){--Depth;}} guard(CallDepth);
        return callback();
    }
    void Stop(Module& module)
    {
        if(!module.Started)return;
        module.Started=false;
        try{Call([&]{module.Api.OnUnload(module.State);});}
        catch(...){Error="Plugin OnUnload raised an exception.";}
        module.State=nullptr;
    }
    std::unique_ptr<Module> Open(const std::filesystem::path& source)
    {
        const auto before=Signature(source);if(!before){Error="Plugin library is missing, empty or not a regular file.";return {};}
        auto module=std::make_unique<Module>();module->Details.Source=source;
        try{
            std::filesystem::create_directories(Cache);
            for(int attempt=0;attempt<8;++attempt){
                const auto token=std::to_string(Clock::now().time_since_epoch().count())+"-"+std::to_string(NextStage++);
                auto directory=Cache/("module-"+token);
                if(std::filesystem::create_directory(directory)){module->StageDirectory=directory;break;}
            }
            if(module->StageDirectory.empty())throw std::runtime_error("Could not create an exclusive staging directory.");
            const auto staged=module->StageDirectory/source.filename();
            std::filesystem::copy_file(source,staged);
            const auto after=Signature(source);
            if(!after||!(*before==*after)||std::filesystem::file_size(staged)!=before->Size)throw std::runtime_error("Plugin changed during staging; wait for the build to finish.");
#ifdef _WIN32
            module->Handle=LoadLibraryExW(staged.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!module->Handle)throw std::runtime_error("LoadLibraryEx failed (code "+std::to_string(GetLastError())+").");
            auto query=reinterpret_cast<JEnginePluginQueryV1>(GetProcAddress(module->Handle,"jengine_plugin_v1"));
#else
            module->Handle=dlopen(staged.c_str(),RTLD_NOW|RTLD_LOCAL);
            if(!module->Handle){const char* error=dlerror();throw std::runtime_error(error?error:"dlopen failed");}
            auto query=reinterpret_cast<JEnginePluginQueryV1>(dlsym(module->Handle,"jengine_plugin_v1"));
#endif
            if(!query)throw std::runtime_error("Missing jengine_plugin_v1 export.");
            const JEnginePluginV1* api=Call([&]{return query();});
            if(!api||api->AbiVersion!=JENGINE_EDITOR_PLUGIN_ABI_V1||api->StructSize<sizeof(JEnginePluginV1)
                ||!api->OnLoad||!api->OnUnload||!ValidString(api->Name,129)
                ||static_cast<bool>(api->SaveState)!=static_cast<bool>(api->RestoreState))
                throw std::runtime_error("Plugin ABI, descriptor size, name, lifecycle callbacks or state pair is invalid.");
            module->Api=*api;module->Details.Name=api->Name;
            module->Accepted=module->Observed=module->Attempted=*before;
            return module;
        }catch(const std::exception& exception){Error=exception.what();return {};}
        catch(...){Error="Plugin descriptor callback raised an exception.";return {};}
    }
    bool Start(Module& module,const std::vector<uint8_t>* state)
    {
        bool editing=false;
        try{
            if(Hooks.Begin){if(!Hooks.Begin()){Error="Finish the current document edit before loading/reloading a plugin.";return false;}editing=true;}
            module.Started=true;
            if(!Call([&]{return module.Api.OnLoad(&Host,&module.State);}))throw std::runtime_error("Plugin OnLoad rejected the host.");
            if(state){
                if(!module.Api.RestoreState||!Call([&]{return module.Api.RestoreState(module.State,state->data(),state->size());}))
                    throw std::runtime_error("Replacement plugin rejected the saved state.");
            }
            if(editing&&Hooks.Commit&&!Hooks.Commit())throw std::runtime_error("Plugin document transaction could not be committed.");
            return true;
        }catch(const std::exception& exception){Error=exception.what();}
        catch(...){Error="Plugin startup/restore callback raised an exception.";}
        Stop(module);if(editing&&Hooks.Cancel)Hooks.Cancel();return false;
    }
    bool Snapshot(Module& module,std::vector<uint8_t>& state)
    {
        state.clear();if(!module.Api.SaveState)return true;
        try{
            state.resize(JENGINE_EDITOR_PLUGIN_MAX_STATE);
            const size_t count=Call([&]{return module.Api.SaveState(module.State,state.data(),state.size());});
            if(count>state.size()){state.clear();Error="Plugin state exceeds the 64 KiB limit.";return false;}
            state.resize(count);return true;
        }catch(...){state.clear();Error="Plugin SaveState raised an exception.";return false;}
    }
};
Manager::Manager(JEnginePluginHostV1 host,std::filesystem::path cache,EditHooks hooks):Data(std::make_unique<Impl>(host,std::move(cache),std::move(hooks))){}
Manager::~Manager(){Shutdown();}
ExtensionId Manager::Load(const std::filesystem::path& input)
{
    auto& s=*Data;if(!s.Ready())return 0;
    try{
        const auto source=std::filesystem::weakly_canonical(std::filesystem::absolute(input));
        for(const auto& [id,module]:s.Loaded){std::error_code ec;if(module->Details.Source==source||std::filesystem::equivalent(module->Details.Source,source,ec))return id;}
        if(s.Loaded.size()>=32){s.Error="At most 32 native extensions may be loaded.";return 0;}
        auto module=s.Open(source);if(!module||!s.Start(*module,nullptr))return 0;
        module->Details.Id=s.Next++;module->Details.Generation=1;module->Details.AutoReload=true;
        const auto id=module->Details.Id;s.Loaded.emplace(id,std::move(module));s.Error.clear();return id;
    }catch(const std::exception& exception){s.Error=exception.what();return 0;}
}
bool Manager::Unload(ExtensionId id)
{
    auto& s=*Data;if(!s.Ready())return false;const auto found=s.Loaded.find(id);if(found==s.Loaded.end())return false;
    s.Stop(*found->second);s.Loaded.erase(found);return true;
}
bool Manager::Reload(ExtensionId id)
{
    auto& s=*Data;if(!s.Ready())return false;auto found=s.Loaded.find(id);if(found==s.Loaded.end())return false;
    std::vector<uint8_t> state;const bool hasState=found->second->Api.SaveState!=nullptr;
    if(hasState&&!s.Snapshot(*found->second,state))return false;
    auto candidate=s.Open(found->second->Details.Source);
    if(!candidate||!s.Start(*candidate,hasState?&state:nullptr))return false;
    candidate->Details.Id=id;candidate->Details.Generation=found->second->Details.Generation+1;
    candidate->Details.AutoReload=found->second->Details.AutoReload;
    s.Error.clear();s.Stop(*found->second);found->second=std::move(candidate);return true;
}
bool Manager::SetAutoReload(ExtensionId id,bool enabled)
{
    auto& s=*Data;if(!s.Ready())return false;auto it=s.Loaded.find(id);if(it==s.Loaded.end())return false;it->second->Details.AutoReload=enabled;return true;
}
void Manager::Tick(float deltaTime)
{
    auto& s=*Data;if(!s.Ready())return;
    const auto now=Clock::now();
    if(now-s.LastWatch>=std::chrono::milliseconds(250)){
        s.LastWatch=now;std::vector<ExtensionId> reload;
        for(auto& [id,module]:s.Loaded)if(module->Details.AutoReload){
            const auto current=Signature(module->Details.Source);if(!current)continue;
            if(!(*current==module->Observed)){module->Observed=*current;module->Changed=now;}
            else if(!(*current==module->Attempted)&&now-module->Changed>=std::chrono::milliseconds(500)){
                module->Attempted=*current;reload.push_back(id);
            }
        }
        for(auto id:reload)Reload(id);
    }
    if(!std::isfinite(deltaTime)||deltaTime<0)deltaTime=0;
    for(auto& [id,module]:s.Loaded)if(!module->Details.Faulted&&module->Api.Tick){
        try{s.Call([&]{module->Api.Tick(module->State,deltaTime);});}
        catch(...){module->Details.Faulted=true;s.Error="Plugin Tick raised an exception; callbacks paused until unload/reload.";}
    }
}
void Manager::Draw()
{
    auto& s=*Data;if(!s.Ready())return;
    for(auto& [id,module]:s.Loaded)if(!module->Details.Faulted&&module->Api.Draw){
        if(s.Hooks.DrawScope)s.Hooks.DrawScope(true,id);
        try{s.Call([&]{module->Api.Draw(module->State);});}
        catch(...){module->Details.Faulted=true;s.Error="Plugin Draw raised an exception; callbacks paused until unload/reload.";}
        if(s.Hooks.DrawScope)s.Hooks.DrawScope(false,id);
    }
}
void Manager::Shutdown()
{
    auto& s=*Data;if(!s.Ready())return;for(auto& [id,module]:s.Loaded)s.Stop(*module);s.Loaded.clear();
}
bool Manager::SaveState(ExtensionId id,std::vector<uint8_t>& bytes)
{
    bytes.clear();auto& s=*Data;if(!s.Ready())return false;auto it=s.Loaded.find(id);return it!=s.Loaded.end()&&s.Snapshot(*it->second,bytes);
}
std::vector<Info> Manager::Plugins()const{std::vector<Info> result;for(const auto& [id,m]:Data->Loaded)result.push_back(m->Details);return result;}
const std::string& Manager::LastError()const{return Data->Error;}
} // namespace jEditorExtensions

// The loader is also built by CPU-only tooling without the UI/runtime adapter.
// Both builds execute the identical Manager and real shared-library lifecycle.
#ifndef JENGINE_EXTENSION_LOADER_ONLY
#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Core/jLog.h"
#include <cstdio>
namespace jEditorExtensions
{
namespace
{
struct HostContext {jEditor* Editor=nullptr;std::thread::id Owner=std::this_thread::get_id();bool Drawing=false;};
bool OnOwner(HostContext* c){return c&&c->Owner==std::this_thread::get_id();}
void HostLog(void* context,int32_t level,const char* text)
{
    auto* c=static_cast<HostContext*>(context);if(!OnOwner(c)||!ValidString(text,4097))return;
    jLog::Get().Write(static_cast<EjLogLevel>(std::clamp(level,0,5)),"Extension",text);
}
uint64_t HostCreate(void* context,uint32_t kind,const char* name)
{
    auto* c=static_cast<HostContext*>(context);if(!OnOwner(c)||kind>9||kind==3||!ValidString(name,257))return 0;
    try{return c->Editor->CreateEntity(static_cast<jEditorAuthoring::EntityKind>(kind),name);}catch(...){return 0;}
}
int32_t HostTransform(void* context,uint64_t id,const float p[3],const float r[3],const float scale[3])
{
    auto* c=static_cast<HostContext*>(context);if(!OnOwner(c)||!p||!r||!scale)return 0;
    const auto* old=c->Editor->GetDocument().Find(id);if(!old)return 0;auto entity=*old;
    entity.Local.Position={p[0],p[1],p[2]};entity.Local.RotationDegrees={r[0],r[1],r[2]};entity.Local.Scale={scale[0],scale[1],scale[2]};
    try{return c->Editor->GetDocument().Update(entity,"Extension transform")?1:0;}catch(...){return 0;}
}
void HostText(void* context,const char* text){auto* c=static_cast<HostContext*>(context);if(OnOwner(c)&&c->Drawing&&ImGui::GetCurrentContext()&&ValidString(text,4097))ImGui::TextUnformatted(text);}
int32_t HostButton(void* context,const char* label){auto* c=static_cast<HostContext*>(context);return OnOwner(c)&&c->Drawing&&ImGui::GetCurrentContext()&&ValidString(label,257)&&ImGui::Button(label);}
int32_t HostDrag(void* context,const char* label,float v[3],float speed)
{
    auto* c=static_cast<HostContext*>(context);if(!OnOwner(c)||!c->Drawing||!ImGui::GetCurrentContext()||!v||!ValidString(label,257)||!std::isfinite(speed)||speed<=0)return 0;
    return ImGui::DragFloat3(label,v,speed)?1:0;
}
struct Holder {
    HostContext Context;
    std::unique_ptr<Manager> Loader;
    bool Visible=false;
    char Path[1024]={};
    explicit Holder(jEditor& editor){
        Context.Editor=&editor;
        JEnginePluginHostV1 host{JENGINE_EDITOR_PLUGIN_ABI_V1,sizeof(JEnginePluginHostV1),&Context,HostLog,HostCreate,HostTransform,HostText,HostButton,HostDrag};
        EditHooks hooks;
        hooks.Begin=[&editor]{return editor.GetDocument().BeginTransaction("Load or reload native extension");};
        hooks.Commit=[&editor]{return editor.GetDocument().CommitTransaction();};
        hooks.Cancel=[&editor]{editor.GetDocument().CancelTransaction();};
        hooks.DrawScope=[this](bool draw,ExtensionId id){Context.Drawing=draw;if(draw){ImGui::PushID(static_cast<int>(id));ImGui::Separator();ImGui::Text("Extension %llu UI",static_cast<unsigned long long>(id));}else ImGui::PopID();};
        Loader=std::make_unique<Manager>(host,std::filesystem::path{},std::move(hooks));
    }
};
std::map<jEditor*,std::unique_ptr<Holder>>& Holders(){static std::map<jEditor*,std::unique_ptr<Holder>> holders;return holders;}
Holder& Get(jEditor& editor){auto& entry=Holders()[&editor];if(!entry)entry=std::make_unique<Holder>(editor);return *entry;}
}
Manager& For(jEditor& editor){return *Get(editor).Loader;}
void Tick(jEditor& editor,float deltaTime){For(editor).Tick(deltaTime);}
void SetVisible(jEditor& editor,bool visible){Get(editor).Visible=visible;}
bool IsVisible(jEditor& editor){return Get(editor).Visible;}
void Render(jEditor& editor)
{
    auto& h=Get(editor);if(!h.Visible)return;
    if(ImGui::Begin("Extensions",&h.Visible)){
        ImGui::TextWrapped("Load trusted native libraries. No background callbacks; UI uses a versioned host API.");
        ImGui::InputText("Library path",h.Path,sizeof(h.Path));
        if(ImGui::Button("Load extension")) {
            try { h.Loader->Load(jEditorAssetPath::FromUtf8(h.Path)); }
            catch(const std::exception& e) {jLog::Get().Write(EjLogLevel::Error,"Extensions",e.what());}
        }
        if(!h.Loader->LastError().empty())ImGui::TextWrapped("%s",h.Loader->LastError().c_str());
        for(const auto& info:h.Loader->Plugins()){
            ImGui::PushID(static_cast<int>(info.Id));ImGui::Separator();ImGui::Text("%s | generation %llu%s",info.Name.c_str(),static_cast<unsigned long long>(info.Generation),info.Faulted?" | callbacks paused":"");
            ImGui::TextWrapped("%s",jEditorAssetPath::ToUtf8(info.Source).c_str());bool reload=info.AutoReload;
            if(ImGui::Checkbox("Watch and reload",&reload))h.Loader->SetAutoReload(info.Id,reload);
            if(ImGui::Button("Reload"))h.Loader->Reload(info.Id);ImGui::SameLine();
            if(ImGui::Button("Unload"))h.Loader->Unload(info.Id);ImGui::PopID();
        }
        ImGui::Separator();h.Loader->Draw();
    }
    ImGui::End();
}
void Shutdown(jEditor& editor){auto& holders=Holders();const auto it=holders.find(&editor);if(it!=holders.end()){it->second->Loader->Shutdown();holders.erase(it);}}
}
#endif
#endif
