#include "jEditorExtensions.h"
#include <cstdio>
#include <cstring>
#include <new>
#ifndef JENGINE_PLUGIN_EXAMPLE_VERSION
#define JENGINE_PLUGIN_EXAMPLE_VERSION 1
#endif
namespace
{
struct State {JEnginePluginHostV1 Host{};uint32_t Counter=0;uint64_t Entity=0;float Position[3]={0,0,0};};
struct Packet {uint32_t Magic=0x4a455031,Counter=0;uint64_t Entity=0;float Position[3]={0,0,0};};
int32_t Load(const JEnginePluginHostV1* host,void** output)
{
    if(!host||!output||host->AbiVersion!=JENGINE_EDITOR_PLUGIN_ABI_V1||host->StructSize<sizeof(JEnginePluginHostV1))return 0;
    auto* state=new(std::nothrow)State;if(!state)return 0;state->Host=*host;*output=state;
#ifdef JENGINE_PLUGIN_FAIL_LOAD
    // Real host edit lets the integration test prove failed startup rolls back.
    host->CreateEntity(host->Context,JENGINE_PLUGIN_EMPTY,"Rejected plugin entity");return 0;
#endif
    if(host->Log)host->Log(host->Context,2,"Native example loaded");return 1;
}
void Tick(void* pointer,float)
{
    auto& state=*static_cast<State*>(pointer);state.Counter+=JENGINE_PLUGIN_EXAMPLE_VERSION;
#ifdef JENGINE_PLUGIN_FIXTURE
    if(!state.Entity)state.Entity=state.Host.CreateEntity(state.Host.Context,JENGINE_PLUGIN_EMPTY,"Native fixture entity");
    state.Position[0]=static_cast<float>(state.Counter);const float rotation[3]={0,0,0},scale[3]={1,1,1};
    state.Host.SetTransform(state.Host.Context,state.Entity,state.Position,rotation,scale);
#endif
}
void Draw(void* pointer)
{
    auto& state=*static_cast<State*>(pointer);auto& host=state.Host;
    if(host.Text)host.Text(host.Context,"Native extension: this UI uses the host C API.");
    if(host.Button&&host.Button(host.Context,"Create cube"))state.Entity=host.CreateEntity(host.Context,JENGINE_PLUGIN_CUBE,"Plugin cube");
    if(host.DragFloat3&&host.DragFloat3(host.Context,"Cube position",state.Position,0.5f)&&state.Entity){
        const float rotation[3]={0,0,0},scale[3]={1,1,1};host.SetTransform(host.Context,state.Entity,state.Position,rotation,scale);
    }
}
void Unload(void* pointer){delete static_cast<State*>(pointer);}
size_t Save(void* pointer,void* bytes,size_t capacity)
{
#ifdef JENGINE_PLUGIN_OVERSIZED_STATE
    return JENGINE_EDITOR_PLUGIN_MAX_STATE+1;
#else
    if(capacity<sizeof(Packet))return sizeof(Packet);const auto& state=*static_cast<State*>(pointer);Packet packet;
    packet.Counter=state.Counter;packet.Entity=state.Entity;std::memcpy(packet.Position,state.Position,sizeof(packet.Position));
    std::memcpy(bytes,&packet,sizeof(packet));return sizeof(packet);
#endif
}
int32_t Restore(void* pointer,const void* bytes,size_t size)
{
#ifdef JENGINE_PLUGIN_FAIL_RESTORE
    return 0;
#else
    if(!bytes||size!=sizeof(Packet))return 0;Packet packet;std::memcpy(&packet,bytes,size);if(packet.Magic!=0x4a455031)return 0;
    auto& state=*static_cast<State*>(pointer);state.Counter=packet.Counter;state.Entity=packet.Entity;std::memcpy(state.Position,packet.Position,sizeof(state.Position));return 1;
#endif
}
const JEnginePluginV1 Descriptor{
#ifdef JENGINE_PLUGIN_BAD_ABI
    JENGINE_EDITOR_PLUGIN_ABI_V1+1,
#else
    JENGINE_EDITOR_PLUGIN_ABI_V1,
#endif
    sizeof(JEnginePluginV1),
#if JENGINE_PLUGIN_EXAMPLE_VERSION == 2
    "Native example v2",
#else
    "Native example v1",
#endif
    Load,Tick,Draw,Unload,Save,Restore};
}
#ifdef JENGINE_PLUGIN_MISSING_EXPORT
extern "C" JENGINE_PLUGIN_EXPORT int missing_plugin_export(){return 1;}
#else
extern "C" JENGINE_PLUGIN_EXPORT const JEnginePluginV1* jengine_plugin_v1(){return &Descriptor;}
#endif
