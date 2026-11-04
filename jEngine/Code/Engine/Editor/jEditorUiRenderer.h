#pragma once
#include <cstdint>
#include <memory>
struct jRenderFrameContext;
struct jEditorThumbnail;
struct jTexture;
// Render-owner only. Prepare copies the completed scene BEFORE UI writes to it.
// Preview IDs stay valid until resize/shutdown. Release thumbnail IDs on the
// owner thread before drawing; queued GPU users are drained before release.
namespace jEditorUiRenderer
{
void PrepareScenePreview(const std::shared_ptr<jRenderFrameContext>& frame);
void* ScenePreviewTexture();
std::shared_ptr<jTexture> AcquireScenePreview();
void* CreateThumbnail(const jEditorThumbnail& pixels);
void ReleaseThumbnail(void* id); // Owner Tick before drawing; drains prior users.
void Shutdown();
bool SupportsDocking();
bool SupportsPlatformWindows();
void RenderPlatformWindows();
}
