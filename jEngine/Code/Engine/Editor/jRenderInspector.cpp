#include "pch.h"
#include "jRenderInspector.h"
#include "Code/Engine/jEditor.h"
#include "Profiler/jRenderTrace.h"
#include "Profiler/jPerformanceProfile.h"
#include "Renderer/jSceneRenderTargets.h"
#include "Scene/jCamera.h"
#include "RHI/jRHIUtil.h"
#include "RHI/Vulkan/jImGui_Vulkan.h"
#include "RHI/Vulkan/jRHI_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jImGui_DX12.h"
#include "RHI/DX12/jRHI_DX12.h"
#endif
#include "Shader/jShaderParameterSet.h"
#include <array>
#include <deque>
#include <cmath>
#include <fstream>
#include <cctype>

namespace jRenderInspector
{
namespace
{
constexpr size_t ViewCount = size_t(ViewMode::Count), ImageHistory = 8;
constexpr uint64_t ImageBudget = 128ull * 1024 * 1024;
const char* Names[] = {"Final color", "Scene HDR", "Base color", "World normal (post)",
    "Device depth", "Linear depth", "Metallic", "Roughness", "Emissive", "Surfel irradiance",
    "Ambient occlusion", "Base lighting HDR", "Subsurface mask", "Diffuse lighting HDR"};
static_assert(std::size(Names) == ViewCount);
const char* Meaning(ViewMode mode)
{
    switch (mode)
    {
    case ViewMode::Albedo: return "Linear RGB; preview encoded for display";
    case ViewMode::Normal: return "RGB * 2 - 1: world-space post-process normal";
    case ViewMode::DeviceDepth: return "Device depth in R; far/background = 1";
    case ViewMode::LinearDepth: return "View-space Z in R; preview uses the depth range";
    case ViewMode::Metallic: return "GBuffer2 B channel";
    case ViewMode::Roughness: return "GBuffer2 A channel";
    case ViewMode::AO: return "AO in R";
    case ViewMode::Final: return "Display color before editor overlays";
    default: return "Linear values; HDR preview uses exposure + Reinhard";
    }
}
std::string FormatName(ETextureFormat format)
{
    return int(format) >= 0 && format < ETextureFormat::MAX ? EnumToString(format) : "Unknown format";
}
BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jRenderInspectorDisplay)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Display)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Options)
END_SHADER_UNIFORM_BUFFER_STRUCT()
BEGIN_SHADER_PARAMETER_SET(jRenderInspectorPreviewParameters)
    SHADER_TEXTURE2D(InspectSource)
    SHADER_UNIFORM_BUFFER(jRenderInspectorDisplay, Inspector)
END_SHADER_PARAMETER_SET()
BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jRenderInspectorProbe)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvVP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Location)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, Options)
END_SHADER_UNIFORM_BUFFER_STRUCT()
BEGIN_SHADER_PARAMETER_SET(jRenderInspectorPixelParameters)
    SHADER_TEXTURE2D_SRV(PixelSource)
    SHADER_RW_TEXTURE2D(ProbeOutput)
    SHADER_UNIFORM_BUFFER(jRenderInspectorProbe, Probe)
END_SHADER_PARAMETER_SET()

struct Preview
{
    std::shared_ptr<jRenderTarget> Target;
    void* Id = nullptr;
    uint64_t LastFrame = 0, Bytes = 0, RetireFenceFrame = 0;
    uint32_t LastSwapImage = 0;
    float Aspect = 1;
};
struct FrameImages
{
    uint64_t Frame = 0;
    std::array<std::shared_ptr<Preview>, ViewCount> Images;
    std::array<bool, ViewCount> Present{};
};
struct Pixel
{
    uint64_t Frame = 0, Entity = 0;
    uint32_t RenderObject = 0;
    int X = 0, Y = 0, Width = 0, Height = 0, Submesh = -1;
    std::string Name, Material;
    std::array<bool, ViewCount> Available{};
    std::array<std::string, ViewCount> Formats;
    std::array<Vector4, 16> Values = [] { std::array<Vector4, 16> values;
        for (auto& value : values) value = Vector4(0, 0, 0, 0); return values; }();
};
struct ProbeSlot
{
    std::shared_ptr<jRenderTarget> Target;
    std::shared_ptr<jBuffer> Readback;
    bool Pending = false;
    Pixel Metadata;
};
struct State
{
    bool Visible = false, Picking = false, PixelTab = false, PixelRequested = false, ProbeActive = false, ProbeInitialized = false;
    bool CaptureNext = false;
    bool ShowInspectorWork = false;
    ViewMode Mode = ViewMode::Final, Selected = ViewMode::Final;
    uint32_t SelectedPass = 0, SwapImage = 0;
    uint64_t Frame = 0, CaptureFrame = 0, Bytes = 0;
    float PixelU = 0, PixelV = 0, Exposure = 0, DepthRange = 1500;
    char Filter[128]{};
    std::string Error, Unavailable, SelectedPassKey;
    std::array<FrameImages, ImageHistory> History;
    FrameImages FrozenImages;
    std::shared_ptr<const jRenderTrace::Frame> Frozen;
    std::vector<std::shared_ptr<Preview>> Previews;
    std::vector<std::shared_ptr<Preview>> ViewportImages;
    std::vector<ProbeSlot> Probes;
    std::shared_ptr<Preview> Viewport;
    std::optional<Pixel> LastPixel;
};
State& Get() { static State state; return state; }
void* Register(const std::shared_ptr<jTexture>& texture)
{
    if (IsUseVulkan()) return static_cast<jImGUI_Vulkan*>(g_ImGUI)->RegisterTexture(texture);
#ifdef _WIN32
    if (IsUseDX12()) return static_cast<jImGUI_DX12*>(g_ImGUI)->RegisterTexture(texture);
#endif
    return nullptr;
}
void Unregister(void* id)
{
    if (!id || !g_ImGUI) return;
    if (IsUseVulkan()) static_cast<jImGUI_Vulkan*>(g_ImGUI)->UnregisterTexture(id);
#ifdef _WIN32
    else if (IsUseDX12()) static_cast<jImGUI_DX12*>(g_ImGUI)->UnregisterTexture(id);
#endif
}
std::shared_ptr<Preview> Image(std::shared_ptr<Preview> image, int width, int height)
{
    auto& s = Get();
    // Registry + slot + this argument are the ordinary three references. A
    // frozen frame pins a fourth; never overwrite its pixels.
    if (image && image.use_count() <= 3 && image->Target->Info.Width == width && image->Target->Info.Height == height)
        return image;
    const uint64_t bytes = uint64_t(width) * height * 4;
    if (s.Bytes + bytes > ImageBudget) { s.Error = "Preview budget reached. Release the frozen frame or reduce the viewport size."; return {}; }
    auto result = std::make_shared<Preview>();
    result->Target = g_rhi->CreateRenderTarget({.Format = ETextureFormat::RGBA8, .Width = width, .Height = height,
        .TextureCreateFlag = ETextureCreateFlag::RTV, .ResourceName = jNameStatic("RenderInspectorPreview")});
    if (!result->Target) { s.Error = "Could not allocate the debug preview."; return {}; }
    result->Id = Register(result->Target->GetTexturePtr());
    if (!result->Id) { s.Error = "Could not register the debug preview."; return {}; }
    result->Bytes = bytes; s.Bytes += bytes; s.Previews.push_back(result); return result;
}
void Touch(const std::shared_ptr<Preview>& image)
{
    if (!image) return;
    image->LastFrame = Get().Frame; image->LastSwapImage = Get().SwapImage; image->RetireFenceFrame = 0;
}
bool Convert(const std::shared_ptr<jRenderFrameContext>& frame, jTexture& source,
    const std::shared_ptr<Preview>& image, ViewMode mode)
{
    if (!image) return false;
    SCOPE_GPU_PROFILE(frame, InspectorPreview);
    auto* command = frame->GetActiveCommandBuffer(); const auto restore = source.GetLayout();
    jRenderInspectorDisplay data;
    data.Display = Vector4(0, 0, 1.f / image->Target->Info.Width, 1.f / image->Target->Info.Height);
    data.Options = Vector4(float(mode), Get().Exposure, Get().DepthRange, 0);
    auto uniform = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("RenderInspectorDisplay"), jLifeTimeType::OneFrame, sizeof(data)));
    uniform->UpdateBufferData(&data, sizeof(data));
    g_rhi->TransitionLayout(command, &source, EResourceLayout::SHADER_READ_ONLY);
    jRHIUtil::DrawFullScreen(frame, image->Target,
        [&](const auto&, jShaderBindingArray& bindings, jShaderBindingResourceInlineAllocator& allocator)
        {
            jRenderInspectorPreviewParameters parameters;
            parameters.InspectSource = {&source, TSamplerStateInfo<ETextureFilter::NEAREST, ETextureFilter::NEAREST>::Create()};
            parameters.Inspector.Buffer = uniform;
            jShaderParameterSet::BuildShaderBindings(parameters, EShaderAccessStageFlag::FRAGMENT, bindings, allocator);
        }, [](const auto&)
        {
            jShaderInfo info; info.SetName(jNameStatic("RenderInspectorPreviewPS"));
            info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RenderInspectorPreview_ps.hlsl"));
            info.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
            jShaderParameterSet::AppendToShaderInfo<jRenderInspectorPreviewParameters>(info, 0);
            return g_rhi->CreateShader(info);
        });
    g_rhi->TransitionLayout(command, &source, restore);
    g_rhi->TransitionLayout(command, image->Target->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
    command->FlushBarrierBatch(); Touch(image); return true;
}
void Probe(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view, jTexture& source, int slot)
{
    auto& s = Get(); auto& probe = s.Probes[s.SwapImage];
    if (!probe.Target)
    {
        probe.Target = g_rhi->CreateRenderTarget({.Format = ETextureFormat::RGBA32F, .Width = 16, .Height = 1,
            .TextureCreateFlag = ETextureCreateFlag::UAV | ETextureCreateFlag::TransferSrc,
            .ResourceName = jNameStatic("RenderInspectorPixelValues")});
        probe.Readback = g_rhi->CreateRawBuffer(256, 512, EBufferCreateFlag::Readback,
            EResourceLayout::TRANSFER_DST, nullptr, 0, jNameStatic("RenderInspectorPixelReadback"));
    }
    if (!probe.Target || !probe.Readback) { s.Error = "Could not allocate the pixel readback."; return; }
    auto* command = frame->GetActiveCommandBuffer(); const auto restore = source.GetLayout();
    g_rhi->TransitionLayout(command, &source, EResourceLayout::SHADER_READ_ONLY);
    g_rhi->TransitionLayout(command, probe.Target->GetTexture(), EResourceLayout::UAV);
    const auto dispatch = [&](int index)
    {
        jRenderInspectorProbe data;
        if (view.Camera) data.InvVP = view.Camera->GetInverseViewProjectionMatrix();
        else data.InvVP.SetIdentity();
        data.Location = Vector4(s.PixelU, s.PixelV, 0, 0); data.Options = Vector4(float(index), 0, 0, 0);
        auto uniform = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("RenderInspectorProbe"), jLifeTimeType::OneFrame, sizeof(data)));
        uniform->UpdateBufferData(&data, sizeof(data));
        jRenderInspectorPixelParameters parameters;
        parameters.PixelSource = {&source}; parameters.ProbeOutput = {probe.Target->GetTexture()}; parameters.Probe.Buffer = uniform;
        auto binding = jShaderParameterSet::CreateShaderBindingInstance(parameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);
        jShaderInfo info; info.SetName(jNameStatic("RenderInspectorPixelCS"));
        info.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/RenderInspectorPixel_cs.hlsl"));
        info.SetShaderType(EShaderAccessStageFlag::COMPUTE);
        jShaderParameterSet::AppendToShaderInfo<jRenderInspectorPixelParameters>(info, 0);
        auto* shader = g_rhi->CreateShader(info);
        jShaderBindingInstanceGroup group; group.Add(binding);
        auto* pipeline = g_rhi->CreateComputePipelineStateInfo(shader, group.GetLayoutArray(), {});
        if (!pipeline) { s.Error = "Pixel probe shader is unavailable."; return false; }
        pipeline->Bind(frame);
        g_rhi->BindComputeShaderBindingInstances(command, pipeline, group.GetCombiner(), 0);
        g_rhi->DispatchCompute(frame, 1, 1, 1);
        g_rhi->UAVBarrier(command, probe.Target->GetTexture()); return true;
    };
    if (!s.ProbeInitialized) s.ProbeInitialized = dispatch(-1);
    if (s.ProbeInitialized && dispatch(slot)) probe.Metadata.Available[size_t(slot)] = true;
    g_rhi->TransitionLayout(command, &source, restore);
}
FrameImages* FindImages(uint64_t id)
{
    auto& entry = Get().History[id % ImageHistory]; return entry.Frame == id ? &entry : nullptr;
}
void ShowImage(const std::shared_ptr<Preview>& image, float heightLimit = 280.f)
{
    if (!image) return;
    const auto available = ImGui::GetContentRegionAvail();
    const float width = std::min(available.x, heightLimit * image->Aspect);
    if (width > 0) { ImGui::Image(image->Id, ImVec2(width, width / image->Aspect)); Touch(image); }
}
}

bool IsVisible() { return Get().Visible; }
void SetVisible(bool visible) { Get().Visible = visible; jRenderTrace::SetEnabled(visible || Get().Mode != ViewMode::Final); }
ViewMode GetViewMode() { return Get().Mode; }
void SetViewMode(ViewMode mode)
{
    if (int(mode) < 0 || mode >= ViewMode::Count) return;
    Get().Mode = mode; jRenderTrace::SetEnabled(Get().Visible || mode != ViewMode::Final);
}
void CaptureNextFrame() { auto& s = Get(); SetVisible(true); s.Frozen.reset(); s.FrozenImages = {}; s.CaptureNext = true; }
void ResumeLive() { auto& s = Get(); s.Frozen.reset(); s.FrozenImages = {}; s.CaptureFrame = 0; s.CaptureNext = false; }
uint64_t FrozenFrameId() { return Get().Frozen ? Get().Frozen->Id : 0; }
std::optional<PixelSample> GetPixelSample()
{
    if (!Get().LastPixel) return {};
    const auto& pixel = *Get().LastPixel;
    PixelSample sample; sample.Frame = pixel.Frame; sample.RenderObject = pixel.RenderObject;
    sample.Entity = pixel.Entity; sample.Submesh = pixel.Submesh;
    sample.X = pixel.X; sample.Y = pixel.Y; sample.Available = pixel.Available;
    for (size_t i = 0; i < sample.Values.size(); ++i)
        sample.Values[i] = {pixel.Values[i].x, pixel.Values[i].y, pixel.Values[i].z, pixel.Values[i].w};
    return sample;
}
bool IsPickingPixel() { return Get().Picking; }
bool IsPixelProbeActive() { return Get().ProbeActive; }
void ArmPixelPick() { SetVisible(true); Get().Picking = true; Get().PixelTab = true; }
void RequestPixel(int x, int y)
{
    auto& s = Get(); s.PixelU = (float(x) + .5f) / std::max(1, SCR_WIDTH);
    s.PixelV = (float(y) + .5f) / std::max(1, SCR_HEIGHT);
    s.PixelRequested = true; s.Picking = false; s.PixelTab = true; SetVisible(true);
}
void RecordPixelObject(uint64_t frame, uint32_t id, uint64_t entity, int submesh, const std::string& name, const std::string& material)
{
    auto& s = Get(); if (!s.ProbeActive || frame != s.Frame || s.SwapImage >= s.Probes.size()) return;
    auto& p = s.Probes[s.SwapImage].Metadata;
    p.RenderObject = id; p.Entity = entity; p.Submesh = submesh; p.Name = name; p.Material = material;
}
void BeginFrame(const std::shared_ptr<jRenderFrameContext>& frame)
{
    auto& s = Get(); s.Frame = g_rhi->GetCurrentFrameNumber(); s.SwapImage = frame->FrameIndex;
    if (!s.Visible) { s.CaptureFrame = 0; s.CaptureNext = false; }
    s.Viewport.reset(); s.Unavailable.clear();
    if (s.SwapImage > 16) return;
    // Detached ImGui windows are submitted after the main swap-image fence.
    // Observe one reuse, then wait for that slot's subsequent submission before
    // retiring an unused preview. Both adapters use the same graphics queue.
    for (auto it = s.Previews.begin(); it != s.Previews.end();)
    {
        if (it->use_count() == 1 && (*it)->LastSwapImage == s.SwapImage && (*it)->LastFrame < s.Frame)
        {
            if (!(*it)->RetireFenceFrame) { (*it)->RetireFenceFrame = s.Frame; ++it; }
            else if ((*it)->RetireFenceFrame < s.Frame)
            { Unregister((*it)->Id); s.Bytes -= (*it)->Bytes; it = s.Previews.erase(it); }
            else ++it;
        }
        else ++it;
    }
    if (s.Probes.size() <= s.SwapImage) s.Probes.resize(s.SwapImage + 1);
    auto& probe = s.Probes[s.SwapImage];
    if (probe.Pending)
    {
        const auto* values = static_cast<const uint8_t*>(probe.Readback->Map());
        if (values) { std::memcpy(probe.Metadata.Values.data(), values, 256); probe.Readback->Unmap(); s.LastPixel = probe.Metadata; }
        else s.Error = "Pixel readback mapping failed.";
        probe.Pending = false;
    }
    s.ProbeActive = std::exchange(s.PixelRequested, false); s.ProbeInitialized = false;
    if (s.ProbeActive)
    {
        probe.Metadata = {}; probe.Metadata.Frame = s.Frame; probe.Metadata.Width = SCR_WIDTH; probe.Metadata.Height = SCR_HEIGHT;
        probe.Metadata.X = std::clamp(int(s.PixelU * SCR_WIDTH), 0, std::max(0, SCR_WIDTH - 1));
        probe.Metadata.Y = std::clamp(int(s.PixelV * SCR_HEIGHT), 0, std::max(0, SCR_HEIGHT - 1));
    }
    auto& images = s.History[s.Frame % ImageHistory]; images.Frame = s.Frame; images.Present.fill(false);
    if (s.CaptureNext) { s.CaptureNext = false; s.CaptureFrame = s.Frame; s.Error.clear(); }
    jRenderTrace::SetEnabled(s.Visible || s.Mode != ViewMode::Final || s.ProbeActive || s.CaptureFrame != 0);
}
void Observe(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view, ViewMode mode,
    const char* producer, const std::shared_ptr<jTexture>& texture, const char* reason)
{
    auto& s = Get(); if (!jRenderTrace::IsEnabled() || int(mode) < 0 || mode >= ViewMode::Count || s.SwapImage > 16) return;
    const auto index = size_t(mode);
    const bool valid = texture && texture->Width > 0 && texture->Height > 0 && texture->Type == ETextureType::TEXTURE_2D && texture->SampleCount == EMSAASamples::COUNT_1;
    jRenderTrace::Resource resource;
    resource.Key = Names[index]; resource.Name = Names[index]; resource.Producer = producer;
    resource.Interpretation = Meaning(mode); resource.Available = valid;
    if (texture) { resource.Width = texture->Width; resource.Height = texture->Height; resource.Format = FormatName(texture->Format); }
    if (!valid) resource.Reason = texture ? "Preview requires a single-sample 2D texture" : reason;
    jRenderTrace::RecordResource(resource);
    if (!valid) { if (mode == s.Mode) s.Unavailable = resource.Reason; return; }
    if (mode == s.Mode) s.Unavailable.clear();
    if ((s.Visible && mode == s.Selected) || s.CaptureFrame == s.Frame)
    {
        auto& images = s.History[s.Frame % ImageHistory];
        const float scale = std::min(512.f / texture->Width, 288.f / texture->Height);
        images.Images[index] = Image(images.Images[index], std::max(1, int(texture->Width * std::min(1.f, scale))),
            std::max(1, int(texture->Height * std::min(1.f, scale))));
        if (images.Images[index]) images.Images[index]->Aspect = view.Camera && view.Camera->ProjectionAspectOverride > 0
            ? view.Camera->ProjectionAspectOverride : float(texture->Width) / texture->Height;
        images.Present[index] = Convert(frame, *texture, images.Images[index], mode);
    }
    if (mode != ViewMode::Final && mode == s.Mode)
    {
        if (s.ViewportImages.size() <= s.SwapImage) s.ViewportImages.resize(s.SwapImage + 1);
        auto& image = s.ViewportImages[s.SwapImage]; image = Image(image, texture->Width, texture->Height);
        if (Convert(frame, *texture, image, mode)) s.Viewport = image;
        else s.Unavailable = s.Error.empty() ? "Debug preview could not be created" : s.Error;
    }
    if (s.ProbeActive)
    {
        s.Probes[s.SwapImage].Metadata.Formats[index] = resource.Format;
        Probe(frame, view, *texture, int(mode));
    }
}
void PrepareUI(const std::shared_ptr<jRenderFrameContext>& frame, const jView&)
{
    auto& s = Get(); if (!s.ProbeActive || !s.ProbeInitialized || s.SwapImage >= s.Probes.size()) return;
    auto& probe = s.Probes[s.SwapImage]; auto* command = frame->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command, probe.Target->GetTexture(), EResourceLayout::TRANSFER_SRC);
    command->FlushBarrierBatch();
    jTextureCopyRegion region; region.Width = 16; region.Height = 1; region.DestRowPitchOverride = 256;
    g_rhi->CopyTextureRegionToBuffer(command, probe.Target->GetTexture(), region, probe.Readback.get(), probe.Readback->GetOffset());
    if (IsUseVulkan())
    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(command->GetHandle()), VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    probe.Pending = true;
}
void* ViewportTexture() { Touch(Get().Viewport); return Get().Viewport ? Get().Viewport->Id : nullptr; }
bool DrawViewportControls()
{
    auto& s = Get(); bool busy = false; int mode = int(s.Mode);
    ImGui::SetNextItemWidth(std::min(210.f, ImGui::GetContentRegionAvail().x));
    if (ImGui::Combo("##RenderDebugView", &mode, Names, int(ViewCount))) SetViewMode(ViewMode(mode));
    busy |= ImGui::IsItemActive();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Debug view. Values come from the actual active rendering path.");
    if (ImGui::GetContentRegionAvail().x > 190) ImGui::SameLine();
    if (ImGui::Button("Render passes")) SetVisible(true);
    const bool canPick = g_Editor && !g_Editor->GetDocument().IsPlaying();
    ImGui::SameLine(); ImGui::BeginDisabled(!canPick);
    if (ImGui::Button(s.Picking ? "Cancel pick" : "Pick pixel"))
    { if (s.Picking) s.Picking = false; else ArmPixelPick(); }
    ImGui::EndDisabled();
    if (!canPick && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Pick pixels in the Scene viewport while editing.");
    if (s.Picking) ImGui::TextDisabled("Click a pixel in Scene; Esc cancels.");
    if (s.Picking && ImGui::IsKeyPressed(ImGuiKey_Escape)) s.Picking = false;
    if (s.Mode != ViewMode::Final && !s.Viewport && !s.Unavailable.empty()) ImGui::TextWrapped("Unavailable: %s", s.Unavailable.c_str());
    return busy || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
}
void DrawWindow()
{
    auto& s = Get(); if (!s.Visible) return;
    if (s.CaptureFrame)
    {
        auto captured = jRenderTrace::CopyFrame(s.CaptureFrame);
        if (captured && captured->Ended && !captured->PendingGpu)
        {
            s.Frozen = captured;
            if (const auto* images = FindImages(captured->Id)) s.FrozenImages = *images;
            else s.FrozenImages = {};
            s.CaptureFrame = 0;
        }
        else if (s.Frame > s.CaptureFrame + 30) { s.Error = "Timed out collecting GPU timings for the captured frame."; s.CaptureFrame = 0; }
    }
    ImGui::SetNextWindowSize(ImVec2(850, 650), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.f);
    if (!ImGui::Begin("Render Pass Inspector", &s.Visible)) { ImGui::End(); return; }
    if (ImGui::Button("Capture next frame")) CaptureNextFrame();
    ImGui::SameLine(); if (ImGui::Button("Live")) ResumeLive();
    ImGui::SameLine(); ImGui::TextDisabled("%s | previews %.1f / 128 MiB", s.Frozen ? "Frozen" : s.CaptureFrame ? "Capturing" : "Live", s.Bytes / 1048576.0);
    if (!s.Error.empty()) ImGui::TextWrapped("%s", s.Error.c_str());
    if (ImGui::BeginTabBar("RenderInspectorTabs"))
    {
        if (ImGui::BeginTabItem("Passes"))
        {
            auto snapshot = s.Frozen ? s.Frozen : jRenderTrace::LatestCompletedFrame();
            if (!snapshot) ImGui::TextWrapped("Waiting for a completed frame and its GPU timestamps...");
            else
            {
                ImGui::Text("Frame %llu | %s / %s | %d x %d", (unsigned long long)snapshot->Id,
                    snapshot->Renderer.c_str(), snapshot->Api.c_str(), snapshot->Width, snapshot->Height);
                ImGui::TextWrapped("CPU: command recording. GPU: inclusive scope duration. Rows show recording order, not a GPU timeline.");
                ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##PassFilter", "Filter passes", s.Filter, sizeof(s.Filter));
                ImGui::Checkbox("Show inspector work", &s.ShowInspectorWork);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capturing previews adds GPU work. Show the tool's own scopes to see that cost.");
                if (ImGui::BeginTable("RenderPassTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 220)))
                {
                    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn("Queue", ImGuiTableColumnFlags_WidthFixed, 85);
                    ImGui::TableSetupColumn("Draw / Compute", ImGuiTableColumnFlags_WidthFixed, 110);
                    ImGui::TableHeadersRow();
                    for (const auto& pass : snapshot->Passes)
                    {
                        if (!s.ShowInspectorWork && pass.Name == "InspectorPreview") continue;
                        if (*s.Filter && std::search(pass.Name.begin(), pass.Name.end(), s.Filter, s.Filter + std::strlen(s.Filter),
                            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) == pass.Name.end()) continue;
                        ImGui::PushID(int(pass.Id)); ImGui::TableNextRow(); ImGui::TableNextColumn();
                        int depth = 0; auto parent = pass.Parent;
                        while (parent && parent <= snapshot->Passes.size() && depth < 16) { ++depth; parent = snapshot->Passes[parent - 1].Parent; }
                        if (depth) ImGui::Indent(depth * 12.f);
                        if (ImGui::Selectable(pass.Name.c_str(), s.SelectedPassKey == pass.Key, ImGuiSelectableFlags_SpanAllColumns))
                        {
                            s.SelectedPass = pass.Id;
                            s.SelectedPassKey = pass.Key;
                            for (const auto& resource : snapshot->Resources) if (resource.ProducerId == pass.Id)
                            {
                                for (size_t i = 0; i < ViewCount; ++i) if (resource.Key == Names[i]) { s.Selected = ViewMode(i); break; }
                                break;
                            }
                        }
                        if (depth) ImGui::Unindent(depth * 12.f);
                        ImGui::TableNextColumn(); ImGui::Text("%.3f", pass.CpuMilliseconds);
                        ImGui::TableNextColumn(); ImGui::Text("%.3f", pass.GpuMilliseconds);
                        ImGui::TableNextColumn();
                        const auto queue = ECommandBufferType(pass.Queue);
                        ImGui::TextUnformatted(queue == ECommandBufferType::GRAPHICS ? "Graphics" : queue == ECommandBufferType::COMPUTE ? "Compute" : "Copy");
                        ImGui::TableNextColumn(); ImGui::Text("%u / %u", pass.DrawCalls, pass.ComputeCalls);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("RHI draw/compute calls directly inside this scope. Child scopes and RT dispatches are not included.");
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                int selected = int(s.Selected); if (ImGui::Combo("Output", &selected, Names, int(ViewCount))) s.Selected = ViewMode(selected);
                const auto found = std::find_if(snapshot->Resources.begin(), snapshot->Resources.end(), [&](const auto& r) { return r.Key == Names[size_t(s.Selected)]; });
                if (found != snapshot->Resources.end())
                {
                    ImGui::Text("Producer: %s | %s | %d x %d", found->Producer.c_str(), found->Format.c_str(), found->Width, found->Height);
                    ImGui::TextWrapped("%s", found->Interpretation.c_str());
                    if (!found->Available) ImGui::TextWrapped("Unavailable: %s", found->Reason.c_str());
                }
                else ImGui::TextDisabled("This output was not recorded by the active rendering path.");
                const auto* images = s.Frozen ? &s.FrozenImages : FindImages(snapshot->Id);
                if (images && images->Frame == snapshot->Id && images->Present[size_t(s.Selected)]) ShowImage(images->Images[size_t(s.Selected)]);
                else ImGui::TextWrapped(s.Frozen ? "This output was not preserved in this capture." : "Selected output preview is warming up. It will appear with matching frame timings.");
                if (snapshot->DroppedPasses) ImGui::Text("Pass recording limit reached: %u omitted", snapshot->DroppedPasses);
            }
            ImGui::EndTabItem();
        }
        const auto pixelFlags = s.PixelTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Pixel", nullptr, pixelFlags))
        {
            s.PixelTab = false;
            ImGui::BeginDisabled(!g_Editor || g_Editor->GetDocument().IsPlaying());
            if (ImGui::Button("Pick a viewport pixel")) ArmPixelPick();
            ImGui::EndDisabled();
            if (s.Picking) ImGui::TextWrapped("Click in the Scene viewport. The next rendered frame is sampled.");
            if (s.PixelRequested || std::any_of(s.Probes.begin(), s.Probes.end(), [](const auto& probe) { return probe.Pending; }))
                ImGui::TextDisabled("GPU readback pending; values below belong to the previous completed probe.");
            if (s.LastPixel)
            {
                const auto& p = *s.LastPixel;
                ImGui::Text("Frame %llu | pixel (%d, %d) / %d x %d", (unsigned long long)p.Frame, p.X, p.Y, p.Width, p.Height);
                ImGui::Text("Render object %u | entity %llu | submesh %d", p.RenderObject, (unsigned long long)p.Entity, p.Submesh);
                if (!p.Name.empty()) ImGui::TextWrapped("Object: %s", p.Name.c_str());
                if (!p.Material.empty()) ImGui::TextWrapped("Material: %s", p.Material.c_str());
                if (p.Available[size_t(ViewMode::DeviceDepth)] && p.Values[15].w > .5f)
                    ImGui::Text("World position: %.4f, %.4f, %.4f", p.Values[15].x, p.Values[15].y, p.Values[15].z);
                else if (!p.RenderObject) ImGui::TextDisabled("Background / no picked surface. Buffer values below are still raw samples.");
                if (ImGui::BeginTable("PixelValues", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Buffer / interpretation"); ImGui::TableSetupColumn("Raw RGBA (before preview conversion)"); ImGui::TableHeadersRow();
                    for (size_t i = 0; i < ViewCount; ++i)
                    {
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(Names[i]);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s | %s", p.Formats[i].c_str(), Meaning(ViewMode(i)));
                        ImGui::TableNextColumn();
                        if (!p.Available[i]) ImGui::TextDisabled("Not produced");
                        else
                        {
                            const auto& v = p.Values[i]; ImGui::Text("%.6g  %.6g  %.6g  %.6g", v.x, v.y, v.z, v.w);
                            if (i == size_t(ViewMode::Normal)) ImGui::TextDisabled("N: %.4f, %.4f, %.4f", v.x * 2 - 1, v.y * 2 - 1, v.z * 2 - 1);
                        }
                    }
                    ImGui::EndTable();
                }
            }
            else ImGui::TextWrapped("No pixel sampled yet. Missing buffers are reported as unavailable, not as zero-valued materials.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator(); ImGui::SliderFloat("Preview exposure (EV)", &s.Exposure, -8, 8);
    ImGui::SliderFloat("Depth display range", &s.DepthRange, 1, 10000, "%.0f");
    ImGui::TextWrapped("Preview controls affect new images. Frozen captures keep their recorded display settings.");
    ImGui::End();
}
void Shutdown()
{
    auto& s = Get(); if (g_rhi && (!s.Previews.empty() || !s.Probes.empty())) g_rhi->Flush();
    for (const auto& image : s.Previews) Unregister(image->Id);
    s = State{}; jRenderTrace::Reset();
}

// Used by the existing --ci-export-shaders command without creating an RHI.
int ExportShaderFixtures(const char* directory)
{
    try
    {
        const std::filesystem::path target(directory);
        const auto write = [&](const char* name, const std::string& source)
        {
            std::ofstream file(target / name); file.exceptions(std::ios::badbit | std::ios::failbit); file << source;
        };
        write("inspector-preview.hlsl", jShaderParameterSet::GenerateHLSL<jRenderInspectorPreviewParameters>(0) +
            "\n#include \"RenderInspectorPreview_ps.hlsl\"\n");
        write("inspector-pixel.hlsl", jShaderParameterSet::GenerateHLSL<jRenderInspectorPixelParameters>(0) +
            "\n#include \"RenderInspectorPixel_cs.hlsl\"\n");
        std::ofstream contract(target / "render-inspector-cpu-layout.json");
        contract.exceptions(std::ios::badbit | std::ios::failbit); contract << '{';
        const auto layout = [&]<typename T>(bool comma)
        {
            if (comma) contract << ',';
            contract << '"' << T::GetShaderUniformBufferTypeName() << "\":{\"size\":" << sizeof(T) << ",\"fields\":{";
            bool first = true;
            for (const auto& field : T::GetShaderUniformBufferFieldMembers())
            {
                if (!first) contract << ',';
                first = false; contract << '"' << field.Name << "\":" << field.ByteOffset;
            }
            contract << "}}";
        };
        layout.template operator()<jRenderInspectorDisplay>(false);
        layout.template operator()<jRenderInspectorProbe>(true);
        contract << "}\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << "Inspector shader export: " << error.what() << '\n'; return 1; }
}
}
