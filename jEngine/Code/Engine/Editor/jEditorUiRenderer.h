#pragma once
#include <cstdint>
#include <memory>
struct jRenderFrameContext;
struct jEditorThumbnail;
struct jTexture;
struct jRenderTarget;
class jView;
// Render-owner only. Prepare copies the completed scene BEFORE UI writes to it.
// Preview IDs stay valid until resize/shutdown. Release thumbnail IDs on the
// owner thread before drawing; queued GPU users are drained before release.
namespace jEditorUiRenderer
{
void PrepareScenePreview(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view, bool drawSelection);
void DrawSelectionOutline(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
    const std::shared_ptr<jRenderTarget>& preview);
void* ScenePreviewTexture();
std::shared_ptr<jTexture> AcquireScenePreview();
void* CreateThumbnail(const jEditorThumbnail& pixels);
void ReleaseThumbnail(void* id); // Owner Tick before drawing; drains prior users.
void Shutdown();
bool SupportsDocking();
bool SupportsPlatformWindows();
void RenderPlatformWindows();
}
