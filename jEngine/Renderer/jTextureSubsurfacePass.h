#pragma once

#include "Features/jTextureSubsurfaceSettings.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include <array>
#include <memory>
#include <span>
#include <vector>

class jView;
class jDrawCommand;
struct jRenderFrameContext;
struct jRenderTarget;
struct jTexture;

namespace jTextureSubsurface
{
struct Light
{
    // One finite key or directional light per invocation. CaptureVP is an
    // ordinary forward-Z projection covering all selected surfaces/occluders.
    Matrix CaptureVP = [] { Matrix m; m.SetIdentity(); return m; }();
    // Fit around the validated actual transformed surface bounds. Explicit VP
    // is used only when false. A finite key inside those bounds is rejected.
    bool AutoFitCapture = true;
    // xyz finite key position; w reference distance for radiance scale^2/r^2.
    Vector4 PositionAndReferenceDistance = Vector4(0, 0, 0, 400);
    // xyz linear key colour; w 1 finite key, 0 directional constant radiance.
    Vector4 ColorAndType = Vector4(.6f, .6f, .6f, 1);
    // xyz normalized ray direction; w capture far distance (positive).
    Vector4 DirectionAndFar = Vector4(0, -1, 0, 1000);
    // Finite-key smooth range window, exactly the point-light convention.
    // Zero means unbounded. Ignored by directional keys.
    float MaxDistance = 0.0f;
    // Shared core hemispherical KS energy rho_dt(NoL,roughness), linear data.
    std::shared_ptr<jTexture> EnergyLUT;
};

struct Inputs
{
    // Borrowed only while recording, never retained by the debug snapshot.
    // Material/mesh leases and submesh draw semantics come from normal draws.
    std::span<const jDrawCommand> SkinDraws;
    // All opaque light occluders. Non-texture materials block transport but
    // cannot donate entry radiance to a selected surface through its ID guard.
    std::span<const jDrawCommand> CaptureDraws;
    Light Key;
    std::shared_ptr<jRenderTarget> SceneAfterScreen;
    // Completed diffuse RGB including destination albedo; null means the
    // screen stage did not apply and OriginalDiffuse is the baseline instead.
    std::shared_ptr<jRenderTarget> ScreenFilteredDiffuse;
    std::shared_ptr<jRenderTarget> OriginalDiffuse;
    std::shared_ptr<jRenderTarget> Albedo;
    std::shared_ptr<jRenderTarget> Mask;
    // RG source UV, BA exact low/high10-bit RenderObjectID components.
    std::shared_ptr<jRenderTarget> SkinUV;
    float Strength = 1.0f;
};

struct SurfaceStats
{
    std::uint32_t RenderObjectID = 0;
    bool Prepared = false;
    std::string Diagnostic;
};
struct FrameStats
{
    std::uint32_t SurfaceCount = 0, CaptureDrawCount = 0;
    std::uint32_t RasterPasses = 0, ComputePasses = 0;
    std::uint32_t AtlasWidth = 0, AtlasHeight = 0;
    std::size_t AdditionalTargetBytes = 0;
    std::vector<SurfaceStats> Surfaces;
};
struct Result
{
    std::shared_ptr<jRenderTarget> Output;
    // Actual completed per-pixel diffuse, including local screen fallbacks.
    std::shared_ptr<jRenderTarget> FilteredDiffuse;
    bool Applied = false;
    std::string Diagnostic;
    FrameStats Stats;
};

// Owner-thread graphics queue recording, pooled OneFrame targets/bindings;
// no submission, GPU wait, history or normal-path readback. At most4 surfaces,
// <=2048 atlas dimensions, <=512MiB work. Rejection retains SceneAfterScreen.
Result BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame,
    const jView& view, const Inputs& inputs, const Settings& settings = {});

struct DebugOutput
{
    std::uint64_t FrameNumber = 0;
    FrameStats Stats;
    // Capture0=(front distance,UV,profile+1); Capture1=(IDlo,IDhi,oct normal).
    std::shared_ptr<jRenderTarget> Capture0, Capture1;
    std::shared_ptr<jRenderTarget> Stretch, EntryUV, Finish, Coverage, SurfaceFlags;
    std::array<std::shared_ptr<jRenderTarget>, 6> Irradiance;
    // FinalUV.a copies the actual atomic rejection bits for bounded diagnostics.
    std::shared_ptr<jRenderTarget> FinalUV, Output, CompletedDiffuse;
    std::shared_ptr<jRenderTarget> SkinUV, Albedo;
};
bool GetLastDebugOutput(DebugOutput& output);
// After GPU drain, before pool/RHI teardown.
void ReleaseResources();
}
