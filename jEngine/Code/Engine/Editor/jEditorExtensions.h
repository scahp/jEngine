#pragma once
#include <stddef.h>
#include <stdint.h>

// Native code executes in the engine process. Load only trusted libraries.
// All callbacks and host calls are owner-thread only; no background callbacks,
// GPU threads or retained UI pointers. The host never exposes the ImGui ABI.
#define JENGINE_EDITOR_PLUGIN_ABI_V1 0x00010000u
#define JENGINE_EDITOR_PLUGIN_MAX_STATE (64u * 1024u)
#if defined(_WIN32)
#define JENGINE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define JENGINE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef enum JEnginePluginEntityKind {
    JENGINE_PLUGIN_EMPTY=0, JENGINE_PLUGIN_CUBE=1, JENGINE_PLUGIN_SPHERE=2,
    JENGINE_PLUGIN_POINT_LIGHT=4, JENGINE_PLUGIN_SPOT_LIGHT=5, JENGINE_PLUGIN_DIRECTIONAL_LIGHT=6,
    JENGINE_PLUGIN_CAMERA=7, JENGINE_PLUGIN_PARTICLES=8, JENGINE_PLUGIN_TERRAIN=9
} JEnginePluginEntityKind;

typedef struct JEnginePluginHostV1 {
    uint32_t AbiVersion;
    uint32_t StructSize;
    void* Context;
    void (*Log)(void* context, int32_t severity, const char* utf8Message);
    uint64_t (*CreateEntity)(void* context, uint32_t kind, const char* utf8Name);
    int32_t (*SetTransform)(void* context, uint64_t entity, const float position[3],
        const float rotationDegrees[3], const float scale[3]);
    // UI callbacks return zero/do nothing outside the plugin's Draw callback.
    void (*Text)(void* context, const char* utf8Text);
    int32_t (*Button)(void* context, const char* utf8Label);
    int32_t (*DragFloat3)(void* context, const char* utf8Label, float values[3], float speed);
} JEnginePluginHostV1;

typedef struct JEnginePluginV1 {
    uint32_t AbiVersion;
    uint32_t StructSize;
    const char* Name;
    int32_t (*OnLoad)(const JEnginePluginHostV1* host, void** state);
    void (*Tick)(void* state, float deltaTime);
    void (*Draw)(void* state);
    void (*OnUnload)(void* state);
    // Optional pair. Return bytes written, <= capacity. Restore validates its
    // own state schema. The host rejects snapshots larger than 64 KiB.
    size_t (*SaveState)(void* state, void* bytes, size_t capacity);
    int32_t (*RestoreState)(void* state, const void* bytes, size_t size);
} JEnginePluginV1;

typedef const JEnginePluginV1* (*JEnginePluginQueryV1)(void);
// Library exports: const JEnginePluginV1* jengine_plugin_v1(void).
#ifdef __cplusplus
}

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
class jEditor;
namespace jEditorExtensions
{
using ExtensionId=uint64_t;
struct Info {
    ExtensionId Id=0;
    uint64_t Generation=0;
    std::filesystem::path Source;
    std::string Name;
    bool AutoReload=false, Faulted=false;
};
// The same manager is used by the editor and real shared-library fixture tests.
// Lifecycle hooks wrap candidate OnLoad/Restore in one document transaction.
struct EditHooks {
    std::function<bool()> Begin;
    std::function<bool()> Commit;
    std::function<void()> Cancel;
    std::function<void(bool drawActive, ExtensionId id)> DrawScope;
};
class Manager {
public:
    explicit Manager(JEnginePluginHostV1 host, std::filesystem::path cacheRoot={}, EditHooks hooks={});
    ~Manager();
    Manager(const Manager&)=delete;
    Manager& operator=(const Manager&)=delete;
    ExtensionId Load(const std::filesystem::path& source);
    bool Unload(ExtensionId id);
    bool Reload(ExtensionId id);
    bool SetAutoReload(ExtensionId id,bool enabled);
    void Tick(float deltaTime);
    void Draw();
    void Shutdown();
    bool SaveState(ExtensionId id,std::vector<uint8_t>& bytes);
    std::vector<Info> Plugins() const;
    const std::string& LastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> Data;
};
Manager& For(jEditor& editor);
void Tick(jEditor& editor,float deltaTime);
// Opens the manager's own Extensions window only while visible. Draw callbacks
// receive a host-owned UI region, rather than manipulating editor panels.
void Render(jEditor& editor);
void SetVisible(jEditor& editor,bool visible);
bool IsVisible(jEditor& editor);
void Shutdown(jEditor& editor); // Before editor/world/RHI teardown, idempotent.
}
#endif
