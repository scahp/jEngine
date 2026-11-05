#pragma once

#include "Features/jSubsurfaceSettings.h"
#include "Math/Matrix.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct jRenderFrameContext;
struct jRenderTarget;

namespace jSubsurface
{
struct Inputs
{
    // All inputs are stored, single-sample, equally sized full-view images.
    // SceneColor contains the original complete HDR result. DiffuseLighting
    // contains only its diffuse contribution; alpha is authored ThicknessWorld.
    // Mask.rgb = saturate(Surface.Subsurface * SubsurfaceColor), and alpha is
    // ProfileID + 1 (zero means inactive). Normal is world-normal * .5 + .5.
    // MSAA resolves colour by mean, but depth/normal/profile/thickness must all
    // come from one nearest-depth foreground sample. Mask RGB is additionally
    // multiplied by the fraction of covered foreground samples.
    std::shared_ptr<jRenderTarget> SceneColor;
    std::shared_ptr<jRenderTarget> DiffuseLighting;
    std::shared_ptr<jRenderTarget> Mask;
    std::shared_ptr<jRenderTarget> Depth;
    std::shared_ptr<jRenderTarget> Normal;
    Matrix InvVP = [] { Matrix value; value.SetIdentity(); return value; }();
    float DepthClearValue = 1.0f;
};

struct Result
{
    // On an off/zero-strength/unsupported/error path this is the original
    // SceneColor, and no scene colour is modified by this module.
    std::shared_ptr<jRenderTarget> Output;
    std::shared_ptr<jRenderTarget> FilteredDiffuse;
    bool Applied = false;
    std::uint32_t ComputePasses = 0;
    std::size_t AdditionalTargetBytes = 0;
    std::string Diagnostic;
};

// Owner-thread graphics-command recording only. Uses the existing acquired
// frame pool/OneFrame bindings; no worker mutation, history, submission or wait.
// Three bounded compute passes produce a separate HDR Output; caller owns its
// composition/copy into the renderer's normal scene target after success.
Result BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame,
    const Inputs& inputs, const Settings& settings);

// Owner-thread diagnostics of actual current-frame output. The frame match
// prevents an OFF frame that bypassed BuildAndRecord from exposing stale data.
bool GetLastDebugOutput(std::uint64_t& frameNumber, Inputs& inputs, Result& result);
// Call only after draining GPU work, before the renderer/pool/RHI is destroyed.
void ReleaseResources();
}
