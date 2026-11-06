#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>

struct jRenderFrameContext;
struct jTexture;
class jView;

namespace jRenderInspector
{
enum class ViewMode : int
{
    Final, SceneHDR, Albedo, Normal, DeviceDepth, LinearDepth,
    Metallic, Roughness, Emissive, SurfelGI, AO, DirectLighting,
    SubsurfaceMask, DiffuseLighting, Count
};
bool IsVisible();
void SetVisible(bool visible);
ViewMode GetViewMode();
void SetViewMode(ViewMode mode);
void CaptureNextFrame();
void ResumeLive();
uint64_t FrozenFrameId();
struct PixelSample
{
    uint64_t Frame = 0, Entity = 0;
    uint32_t RenderObject = 0;
    int X = 0, Y = 0, Submesh = -1;
    std::array<bool, size_t(ViewMode::Count)> Available{};
    std::array<std::array<float, 4>, 16> Values{};
};
std::optional<PixelSample> GetPixelSample(); // Render owner only; copied CPU result.
bool IsPickingPixel();
bool IsPixelProbeActive();
void ArmPixelPick();
void RequestPixel(int x, int y);
void RecordPixelObject(uint64_t frame, uint32_t renderObjectId, uint64_t entity, int submesh,
    const std::string& name, const std::string& material);

// Render owner only. These observations are made at pass boundaries, before a
// later pass can overwrite the same allocation. UI never retains source pointers.
void BeginFrame(const std::shared_ptr<jRenderFrameContext>& frame);
void Observe(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
    ViewMode mode, const char* producer, const std::shared_ptr<jTexture>& texture,
    const char* unavailableReason = "Not produced by the active renderer/settings");
void PrepareUI(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view);
bool DrawViewportControls();
void DrawWindow();
void* ViewportTexture();
void Shutdown();
}
