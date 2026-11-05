#include "pch.h"
#include "jEditorUiRenderer.h"
#include "jEditorCapture.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jImageFileLoader.h"
#include <set>
#include "RHI/jRHI.h"
#include "RHI/Vulkan/jImGui_Vulkan.h"
#include "RHI/Vulkan/jRHI_Vulkan.h"
#include "RHI/Vulkan/jTexture_Vulkan.h"
#include "Renderer/jSceneRenderTargets.h"
#include "Scene/jCamera.h"
#include "imgui_internal.h"
#ifdef _WIN32
#include "RHI/DX12/jImGui_DX12.h"
#include "RHI/DX12/jRHI_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"
#include "RHI/DX12/jCommandBufferManager_DX12.h"
#endif

namespace jEditorUiRenderer
{
namespace
{
struct Preview { std::shared_ptr<jRenderTarget> Target; void* Id = nullptr; };
std::vector<Preview> Previews;
void* CurrentPreview = nullptr;
std::set<void*> Thumbnails;
void* Register(const std::shared_ptr<jTexture>& texture)
{
    if (g_rhi == g_rhi_vk)
        return static_cast<jImGUI_Vulkan*>(g_ImGUI)->RegisterTexture(texture);
#ifdef _WIN32
    if (g_rhi == g_rhi_dx12)
        return static_cast<jImGUI_DX12*>(g_ImGUI)->RegisterTexture(texture);
#endif
    return nullptr;
}
void Unregister(void* id)
{
    if (!id || !g_rhi || !g_ImGUI) return;
    g_rhi->Flush();
    if (g_rhi == g_rhi_vk) static_cast<jImGUI_Vulkan*>(g_ImGUI)->UnregisterTexture(id);
#ifdef _WIN32
    else if (g_rhi == g_rhi_dx12) static_cast<jImGUI_DX12*>(g_ImGUI)->UnregisterTexture(id);
#endif
}
}
void PrepareScenePreview(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view, bool drawSelection)
{
    CurrentPreview = nullptr;
    if (!g_rhi || !g_ImGUI || !frame || !frame->SceneRenderTargetPtr ||
        !frame->SceneRenderTargetPtr->FinalColorPtr) return;
    auto* source = frame->SceneRenderTargetPtr->FinalColorPtr->GetTexture();
    if (!source || source->Width <= 0 || source->Height <= 0 || frame->FrameIndex > 16) return;
    if (Previews.size() <= frame->FrameIndex) Previews.resize(frame->FrameIndex + 1);
    auto& preview = Previews[frame->FrameIndex];
    if (!preview.Target || preview.Target->Info.Width != source->Width ||
        preview.Target->Info.Height != source->Height || preview.Target->Info.Format != source->Format)
    {
        if (preview.Id) { Unregister(preview.Id); preview.Id = nullptr; preview.Target.reset(); }
        // Old frame users have drained before replacing this generation.
        const auto texture = g_rhi->Create2DTexture(source->Width, source->Height, 1, 1, source->Format,
            ETextureCreateFlag::RTV | ETextureCreateFlag::TransferDst | ETextureCreateFlag::TransferSrc, EResourceLayout::UNDEFINED);
        if (texture)
        {
            preview.Target = jRenderTarget::CreateFromTexture(texture);
            preview.Id = Register(texture);
        }
    }
    if (!preview.Id) return;
    auto* texture = preview.Target->GetTexture();
    auto* command = frame->GetActiveCommandBuffer();
    const EResourceLayout restore = source->GetLayout();
    g_rhi->TransitionLayout(command, source, EResourceLayout::TRANSFER_SRC);
    g_rhi->TransitionLayout(command, texture, EResourceLayout::TRANSFER_DST);
    command->FlushBarrierBatch();
    if (g_rhi == g_rhi_vk)
    {
        auto* from = static_cast<jTexture_Vulkan*>(source);
        auto* to = static_cast<jTexture_Vulkan*>(texture);
        VkImageCopy copy{};
        copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {uint32(source->Width), uint32(source->Height), 1};
        vkCmdCopyImage((VkCommandBuffer)command->GetHandle(), from->Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy);
    }
#ifdef _WIN32
    else if (g_rhi == g_rhi_dx12)
    {
        auto* from = static_cast<jTexture_DX12*>(source);
        auto* to = static_cast<jTexture_DX12*>(texture);
        static_cast<jCommandBuffer_DX12*>(command)->CommandList->CopyResource(
            to->Texture->Get(), from->Texture->Get());
    }
#endif
    g_rhi->TransitionLayout(command, source, restore);
#ifdef ENABLE_EDITOR_FEATURES
    jEditorCapture::ReadFrame(frame, *texture, view.Camera ? view.Camera->ProjectionAspectOverride : 0.f);
    if (drawSelection) DrawSelectionOutline(frame, view, preview.Target);
#endif
    g_rhi->TransitionLayout(command, texture, EResourceLayout::SHADER_READ_ONLY);
    command->FlushBarrierBatch();
    CurrentPreview = preview.Id;
}
void* ScenePreviewTexture() { return CurrentPreview; }
std::shared_ptr<jTexture> AcquireScenePreview()
{
    for (const auto& preview : Previews) if (preview.Id && preview.Id == CurrentPreview) return preview.Target->GetTexturePtr();
    return {};
}
void* CreateThumbnail(const jEditorThumbnail& pixels)
{
    if (!g_rhi || !g_ImGUI || !pixels.Width || !pixels.Height || pixels.Width > 256 ||
        pixels.Height > 256 || pixels.Rgba.size() != size_t(pixels.Width) * pixels.Height * 4) return nullptr;
    jImageData image;
    image.Width = pixels.Width; image.Height = pixels.Height;
    image.CreateMipmapIfPossible = false;
    image.ImageBulkData.ImageData = pixels.Rgba;
    auto texture = g_rhi->CreateTextureFromData(&image);
#ifdef _WIN32
    // Existing DX12 convenience uploads return before their copy queue completes.
    // Complete that ticket before first UI consumption until the shared uploader
    // exposes a graphics wait; never publish an unfinished thumbnail.
    if (texture && g_rhi == g_rhi_dx12) g_rhi->Flush();
#endif
    void* id = texture ? Register(texture) : nullptr;
    if (id) Thumbnails.insert(id);
    return id;
}
void ReleaseThumbnail(void* id)
{
    if (!Thumbnails.erase(id)) return;
    Unregister(id);
}
void Shutdown()
{
    // Detached users must finish and release their viewport data before texture
    // IDs and scene previews can be retired. Backend Release is idempotent later.
    if (ImGui::GetCurrentContext())
    {
        if (g_rhi) g_rhi->Flush();
        ImGui::DestroyPlatformWindows();
    }
    Thumbnails.clear(); Previews.clear(); CurrentPreview = nullptr;
}
bool SupportsDocking()
{
#ifdef IMGUI_HAS_DOCK
    return true;
#else
    return false;
#endif
}
bool SupportsPlatformWindows()
{
    if (!ImGui::GetCurrentContext()) return false;
    // The official Vulkan secondary-window backend renders and presents with
    // one queue family. Do not advertise it on a split graphics/present device.
    if (g_rhi == g_rhi_vk && g_rhi_vk->GraphicsQueue.QueueIndex != g_rhi_vk->PresentQueue.QueueIndex) return false;
    const auto flags = ImGui::GetIO().BackendFlags;
    const auto& platform = ImGui::GetPlatformIO();
    return platform.Monitors.Size > 0 && (flags & ImGuiBackendFlags_PlatformHasViewports) &&
        (flags & ImGuiBackendFlags_RendererHasViewports) && platform.Platform_CreateWindow &&
        platform.Platform_DestroyWindow && platform.Renderer_CreateWindow &&
        platform.Renderer_RenderWindow && platform.Renderer_SwapBuffers;
}
void RenderPlatformWindows()
{
    // Call after the engine has submitted/presented the main viewport. Both
    // adapters submit detached draws to the same graphics queue as its copy.
    const auto* context = ImGui::GetCurrentContext();
    if (!context || context->FrameCountEnded != context->FrameCount || context->FrameCountPlatformEnded >= context->FrameCount)
        return;

    // ImGui records the completed frame even when viewports are disabled. The
    // next NewFrame checks that record if a tool opens a native window.
    ImGui::UpdatePlatformWindows();
    if (context->ConfigFlagsCurrFrame & ImGuiConfigFlags_ViewportsEnable)
        ImGui::RenderPlatformWindowsDefault();
}
}
