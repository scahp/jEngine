#include "pch.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "jEditorCapture.h"
#include "jEditorCaptureEncoding.h"
#include "Core/jLog.h"
#include "FileLoader/jEditorAssetPath.h"
#include "RHI/jRHI.h"
#include "RHI/jRHIUtil.h"
#include "Shader/jShader.h"
#include "Shader/jShaderParameterSet.h"
#include <array>
#include <chrono>
#include <future>

namespace jEditorCapture
{
namespace
{
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr size_t ReadbackCount = 4;
constexpr size_t QueueLimit = 5; // CPU frames plus pending GPU readbacks.
constexpr uint64_t MaxStillPixels = 7680ull * 4320;
constexpr uint64_t MaxVideoPixels = 3840ull * 2160;

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jEditorCaptureUniforms)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, FitAndInvSize)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jEditorCaptureParameters)
    SHADER_TEXTURE2D(CaptureSource)
    SHADER_UNIFORM_BUFFER(jEditorCaptureUniforms, Capture)
END_SHADER_PARAMETER_SET()

struct jEditorCapturePixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jEditorCaptureParameters)
    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;
    DECLARE_SHADER_WITH_PERMUTATION(jEditorCapturePixelShader, Permutation)
};
IMPLEMENT_SHADER_WITH_PERMUTATION(jEditorCapturePixelShader, "EditorCapture_ps",
    "Resource/Shaders/hlsl/EditorCapture_ps.hlsl", "", "main", EShaderAccessStageFlag::FRAGMENT)

struct Readback
{
    std::shared_ptr<jBuffer> Buffer;
    bool InUse = false;
};
struct Transfer
{
    uint32_t SwapImage;
    size_t ReadbackIndex;
    uint64_t TimeSlot;
};
struct Capture
{
    Status Info;
    std::unique_ptr<Detail::Job> Encoding;
    std::future<Detail::Result> Worker;
    std::shared_ptr<jRenderTarget> Target;
    std::array<Readback, ReadbackCount> Readbacks;
    std::deque<Transfer> Transfers;
    std::optional<Clock::time_point> FirstFrame;
    uint64_t LastSlot = 0;
    uint64_t StopSlot = 0;
    uint32_t RowPitch = 0;
    bool Drain = false;
};
Capture& State() { static Capture capture; return capture; }

uint64_t TimelineSlot(const Capture& capture)
{
    if (!capture.FirstFrame) return 0;
    const double seconds = std::chrono::duration<double>(Clock::now() - *capture.FirstFrame).count();
    return static_cast<uint64_t>(seconds * capture.Encoding->Output.FramesPerSecond);
}

void CloseQueue(Capture& capture, std::string error = {})
{
    auto& job = *capture.Encoding;
    {
        std::lock_guard lock(job.Mutex);
        job.EndSlot = capture.StopSlot ? capture.StopSlot : std::max(TimelineSlot(capture), capture.LastSlot) + 1;
        job.Error = std::move(error);
        job.Closed = true;
    }
    job.Wake.notify_one();
}

void Fail(Capture& capture, const std::string& error)
{
    CloseQueue(capture, error);
    capture.Info.State = Phase::Saving;
    capture.Drain = true;
}

void Retire(Capture& capture, size_t count) try
{
    auto& job = *capture.Encoding;
    const size_t rowBytes = size_t(job.Output.Width) * 4;
    while (count--)
    {
        const auto transfer = capture.Transfers.front();
        capture.Transfers.pop_front();
        auto& readback = capture.Readbacks[transfer.ReadbackIndex];
        readback.InUse = false;
        if (job.Closed) continue;

        Detail::Pixels frame{transfer.TimeSlot, std::vector<uint8_t>(rowBytes * job.Output.Height)};
        const auto* mapped = static_cast<const uint8_t*>(readback.Buffer->Map());
        if (!mapped) { Fail(capture, "Could not map the rendered capture frame."); return; }
        for (int row = 0; row < job.Output.Height; ++row)
            std::memcpy(frame.Rgba.data() + size_t(row) * rowBytes, mapped + size_t(row) * capture.RowPitch, rowBytes);
        readback.Buffer->Unmap();
        {
            std::lock_guard lock(job.Mutex);
            job.Frames.push_back(std::move(frame));
        }
        ++capture.Info.Frames;
        if (job.Output.FileFormat != Format::Mp4)
        {
            capture.Info.State = Phase::Saving;
            CloseQueue(capture);
        }
        else job.Wake.notify_one();
    }
}
catch (const std::exception& error) { Fail(capture, error.what()); }

void DrainGpu(Capture& capture, bool deliver)
{
    if (capture.Transfers.empty()) return;
    // Called after submission, only when stopping or recovering from failure.
    // While recording we retire buffers through the existing swap-image waits.
    if (g_rhi) g_rhi->Flush();
    if (deliver) Retire(capture, capture.Transfers.size());
    else capture.Transfers.clear();
}

void PublishResult(Capture& capture)
{
    const auto result = capture.Worker.get();
    DrainGpu(capture, false);
    capture.Target.reset();
    capture.Readbacks = {};
    capture.Encoding.reset();
    capture.FirstFrame.reset();
    capture.Drain = false;
    capture.Info.State = Phase::Idle;
    capture.Info.Frames = result.Frames;
    capture.Info.Error = result.Error;
    const bool success = result.Error.empty();
    jLog::Get().Write(success ? EjLogLevel::Info : EjLogLevel::Error, "Capture",
        success ? "Saved " + jEditorAssetPath::ToUtf8(capture.Info.File) : result.Error);
}

void CopyFrame(Capture& capture, const std::shared_ptr<jRenderFrameContext>& frame,
    jTexture& source, float aspect, size_t index, uint64_t timeSlot)
{
    const auto& settings = capture.Encoding->Output;
    const int width = settings.Width, height = settings.Height;
    if (!capture.Target)
    {
        capture.Target = g_rhi->CreateRenderTarget({
            .Format = ETextureFormat::RGBA8, .Width = width, .Height = height,
            .RTClearValue = jRTClearValue(0.f, 0.f, 0.f, 1.f),
            .TextureCreateFlag = ETextureCreateFlag::RTV | ETextureCreateFlag::TransferSrc,
            .ResourceName = jNameStatic("EditorCaptureOutput")
        });
    }
    auto& readback = capture.Readbacks[index];
    if (!readback.Buffer)
        readback.Buffer = g_rhi->CreateRawBuffer(uint64_t(capture.RowPitch) * height, 0,
            EBufferCreateFlag::Readback, EResourceLayout::TRANSFER_DST);
    auto* shader = jEditorCapturePixelShader::CreateShader({});
    if (!shader || !capture.Target || !readback.Buffer || !readback.Buffer->GetHandle())
        throw std::runtime_error("Could not allocate the capture render pass or readback buffer.");

    jEditorCaptureUniforms data;
    const float outputAspect = float(width) / float(height);
    if (!(aspect > 0.f) || !std::isfinite(aspect)) aspect = float(source.Width) / float(source.Height);
    data.FitAndInvSize = Vector4(std::max(1.f, outputAspect / aspect), std::max(1.f, aspect / outputAspect),
        1.f / float(width), 1.f / float(height));
    auto uniforms = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
        jNameStatic("EditorCaptureFit"), jLifeTimeType::OneFrame, sizeof(data)));
    uniforms->UpdateBufferData(&data, sizeof(data));
    const auto* sampler = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR,
        ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE,
        0.f, 1.f, Vector4(0.f, 0.f, 0.f, 1.f)>::Create();

    // Record ownership before any GPU command can reference these resources.
    capture.Transfers.push_back({frame->FrameIndex, index, timeSlot});
    readback.InUse = true;
    auto* command = frame->GetActiveCommandBuffer();
    g_rhi->TransitionLayout(command, &source, EResourceLayout::SHADER_READ_ONLY);
    jRHIUtil::DrawFullScreen(frame, capture.Target,
        [&](const auto&, jShaderBindingArray& bindings, jShaderBindingResourceInlineAllocator& allocator) {
            jEditorCaptureParameters parameters;
            parameters.CaptureSource = {&source, sampler};
            parameters.Capture.Buffer = uniforms;
            jShaderParameterSet::BuildShaderBindings(parameters, EShaderAccessStageFlag::FRAGMENT, bindings, allocator);
        },
        [shader](const auto&) { return shader; },
        [](jRasterizationStateInfo*& raster, jBlendingStateInfo*& blend, jDepthStencilStateInfo*& depth) {
            raster = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::NONE, EFrontFace::CCW, false,
                0.f, 0.f, 0.f, 1.f, false, false, EMSAASamples::COUNT_1, false, 0.f, false, false>::Create();
            blend = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD,
                EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
            depth = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.f, 1.f>::Create();
        });
    auto* output = capture.Target->GetTexture();
    g_rhi->TransitionLayout(command, output, EResourceLayout::TRANSFER_SRC);
    command->FlushBarrierBatch();
    jTextureCopyRegion region;
    region.Width = width;
    region.Height = height;
    region.DestRowPitchOverride = capture.RowPitch;
    g_rhi->CopyTextureRegionToBuffer(command, output, region, readback.Buffer.get(), readback.Buffer->GetOffset());
    if (IsUseVulkan())
    {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(command->GetHandle()),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
}
}

bool Supports(Format format)
{
    switch (format)
    {
    case Format::Png: case Format::Bmp: return true;
#ifdef _WIN32
    case Format::Jpeg: case Format::Mp4: return true;
#endif
    default: return false;
    }
}
const char* Extension(Format format)
{
    switch (format)
    {
    case Format::Png: return ".png";
    case Format::Jpeg: return ".jpg";
    case Format::Bmp: return ".bmp";
    case Format::Mp4: return ".mp4";
    }
    return "";
}
bool Start(Options options, std::string& error) try
{
    auto& capture = State();
    if (!g_rhi || !Supports(options.FileFormat)) { error = "The capture format is unavailable on this platform."; return false; }
    if (capture.Encoding) { error = "A capture is already in progress."; return false; }
    const bool video = options.FileFormat == Format::Mp4;
    if (options.Width <= 0 || options.Height <= 0 || options.Width > 8192 || options.Height > 8192 ||
        uint64_t(options.Width) * options.Height > (video ? MaxVideoPixels : MaxStillPixels))
    { error = video ? "Video is limited to 8.3 megapixels per frame." : "Screenshots are limited to 33.2 megapixels and 8192 pixels per edge."; return false; }
    if (video && (options.Width < 16 || options.Height < 16 || options.Width > 4096 || options.Height > 4096 ||
        options.Width % 2 || options.Height % 2 || options.FramesPerSecond < 1 || options.FramesPerSecond > 60))
    { error = "Video needs even dimensions between 16 and 4096 and a frame rate between 1 and 60."; return false; }
    if (!options.File.is_absolute() || options.File.filename().empty() || options.File.filename() == "." || options.File.filename() == "..")
    { error = "Choose a capture filename."; return false; }
    options.File.replace_extension(Extension(options.FileFormat));
    if (fs::exists(options.File)) { error = "Choose an unused capture filename."; return false; }
    auto job = std::make_unique<Detail::Job>(std::move(options));
    Status status{video ? Phase::Recording : Phase::Screenshot, job->Output.File};
    auto worker = std::async(std::launch::async, [job = job.get()] { return Detail::Encode(*job); });
    capture.Encoding = std::move(job);
    capture.Worker = std::move(worker);
    capture.Info = std::move(status);
    capture.LastSlot = 0;
    capture.StopSlot = 0;
    const auto rowBytes = uint32_t(capture.Encoding->Output.Width) * 4;
    capture.RowPitch = IsUseVulkan() ? rowBytes : (rowBytes + 255) & ~255u;
    error.clear();
    return true;
}
catch (const std::exception& exception) { error = exception.what(); return false; }

const Status& GetStatus() { return State().Info; }
void Stop()
{
    auto& capture = State();
    if (capture.Info.State != Phase::Recording && capture.Info.State != Phase::Screenshot) return;
    capture.StopSlot = std::max(TimelineSlot(capture), capture.LastSlot) + 1;
    capture.Info.State = Phase::Saving;
    capture.Drain = true;
}

void ReadFrame(const std::shared_ptr<jRenderFrameContext>& frame, jTexture& source, float displayAspect) try
{
    auto& capture = State();
    if (!capture.Encoding) return;
    // BeginRenderFrame already waited on the reused swap image. Graphics queue
    // ordering also guarantees all capture copies scheduled before it are done.
    size_t ready = 0;
    for (size_t i = 0; i < capture.Transfers.size(); ++i)
        if (capture.Transfers[i].SwapImage == frame->FrameIndex) ready = i + 1;
    Retire(capture, ready);
    if (capture.Info.State == Phase::Saving) return;
    if (capture.Info.State == Phase::Screenshot && !capture.Transfers.empty()) return;
    const auto timeSlot = TimelineSlot(capture);
    if (capture.FirstFrame && timeSlot <= capture.LastSlot) return;
    const auto buffer = std::find_if(capture.Readbacks.begin(), capture.Readbacks.end(), [](const Readback& entry) { return !entry.InUse; });
    if (buffer == capture.Readbacks.end()) return;
    {
        std::lock_guard lock(capture.Encoding->Mutex);
        if (capture.Encoding->Frames.size() + capture.Transfers.size() >= QueueLimit) return;
    }
    CopyFrame(capture, frame, source, displayAspect, size_t(buffer - capture.Readbacks.begin()), timeSlot);
    if (!capture.FirstFrame) capture.FirstFrame = Clock::now();
    capture.LastSlot = timeSlot;
}
catch (const std::exception& error) { Fail(State(), error.what()); }

void EndFrame()
{
    auto& capture = State();
    if (!capture.Encoding) return;
    if (capture.FirstFrame && capture.Info.State == Phase::Recording)
        capture.Info.Seconds = std::chrono::duration<double>(Clock::now() - *capture.FirstFrame).count();
    if (capture.Worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        PublishResult(capture);
    else if (capture.Drain)
    {
        DrainGpu(capture, true);
        if (!capture.Encoding->Closed) CloseQueue(capture);
        capture.Drain = false;
    }
}

void Shutdown()
{
    auto& capture = State();
    if (!capture.Encoding) return;
    DrainGpu(capture, true);
    if (!capture.Encoding->Closed) CloseQueue(capture);
    PublishResult(capture);
}
}
#endif
