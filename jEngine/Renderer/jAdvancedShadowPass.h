#pragma once

#include "Renderer/Features/jAdvancedShadowSettings.h"
#include <memory>
#include <cstddef>
#include <string>
#include <vector>

class jLight;
class jView;
struct jRenderFrameContext;
struct jShaderBindingInstance;
struct jTexture;
struct jRenderTarget;
struct jAdvancedShadowData;
struct jDirectionalLightShaderParameters;
struct jPointLightShaderParameters;
struct jSpotLightShaderParameters;

namespace jAdvancedShadows
{
struct FrameStats
{
    struct Light
    {
        const jLight* Source = nullptr;
        Settings Requested;
        bool Active = false;
        bool ProjectionFallback = false;
        int AtlasWidth = 0, AtlasHeight = 0;
        int RasterPasses = 0, ComputePasses = 0;
        std::size_t InputTriangles = 0, OutputTriangles = 0;
        std::size_t AdditionalTargetBytes = 0;
        std::string Diagnostic;
    };
    std::vector<Light> Lights;
    std::size_t AdditionalTargetBytes = 0;
};

const FrameStats& GetLastFrameStats();
// Debug/test consumers receive the current view's actual output, never a copy
// synthesized from settings. Targets retain normal pool/frame ownership.
bool GetLastDebugOutput(const jLight* light, jAdvancedShadowData& values,
    std::shared_ptr<jRenderTarget>& map, std::shared_ptr<jRenderTarget>& deep0,
    std::shared_ptr<jRenderTarget>& deep1);
void ReleaseResources();
int RunAdapterTests();

// Typed light layouts always contain valid neutral resources. Unselected or
// rejected advanced methods use the existing shadow result through a zero flag.
void SetDefaultBindings(jDirectionalLightShaderParameters& parameters);
void SetDefaultBindings(jPointLightShaderParameters& parameters);
void SetDefaultBindings(jSpotLightShaderParameters& parameters);

// One owner-thread transaction per view: allocate, prepare and record complete
// per-light output before setup workers snapshot lighting descriptor groups.
class Frame
{
public:
    Frame();
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    void BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view, bool rasterShadows = true);
    bool HasOutput(const jLight* light) const;
    void SetBindings(jDirectionalLightShaderParameters& parameters, const jLight* light) const;
    void SetBindings(jPointLightShaderParameters& parameters, const jLight* light) const;
    void SetBindings(jSpotLightShaderParameters& parameters, const jLight* light) const;
    std::shared_ptr<jShaderBindingInstance> CreateLightBinding(jLight* light, jTexture* legacyShadow) const;
private:
    struct Impl;
    std::unique_ptr<Impl> Data;
};
}
