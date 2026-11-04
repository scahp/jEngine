#include "pch.h"
#include "FileLoader/jModelLoader.h"
#include "FileLoader/jModelUploadSession.h"
#include "FileLoader/jModelUploadPlanning.h"
#include "FileLoader/jImageFileLoader.h"
#include "Scene/jMeshObject.h"
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jBufferUtil_DX12.h"
#endif
#include "lodepng.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;
    void WriteModel(const fs::path& folder, const char* stem, bool green)
    {
        std::ofstream obj(folder / (std::string(stem) + ".obj"));
        obj << "mtllib " << stem << ".mtl\nusemtl color\n";
        for (int triangle = 0; triangle < 128; ++triangle)
        {
            obj << "v " << triangle << " 0 0\nv " << triangle + 1 << " 0 0\nv " << triangle << " 1 0\n";
            const int first = triangle * 3 + 1;
            obj << "f " << first << ' ' << first + 1 << ' ' << first + 2 << '\n';
        }
        std::ofstream mtl(folder / (std::string(stem) + ".mtl"));
        mtl << "newmtl color\nmap_Kd " << stem << ".png\n";
        std::vector<unsigned char> pixels(64 * 64 * 4);
        for (size_t i = 0; i < pixels.size(); i += 4)
        { pixels[i] = green ? 0 : 255; pixels[i + 1] = green ? 255 : 0; pixels[i + 2] = 0; pixels[i + 3] = 255; }
        if (LodePNG_encode32_file((folder / (std::string(stem) + ".png")).string().c_str(), pixels.data(), 64, 64))
            throw std::runtime_error("Cannot write upload PNG fixture");
    }

    jTexture* MaterialTexture(const jMeshObject& object)
    {
        for (const auto& material : object.MeshData->Materials)
            for (const auto& texture : material.second->TexData) if (texture.Texture) return texture.Texture;
        return nullptr;
    }

    bool BufferMatches(jBuffer* source, const IStreamParam& expected)
    {
        const uint64 bytes = expected.GetBufferSize();
        if (!source || !bytes || source->GetBufferSize() < bytes) return false;
        auto readback = g_rhi->CreateRawBuffer(bytes, 0, EBufferCreateFlag::Readback,
            IsUseVulkan() ? EResourceLayout::UNDEFINED : EResourceLayout::TRANSFER_DST);
        if (!readback || !readback->GetHandle()) return false;
        auto* command = g_rhi->BeginSingleTimeCommands();
        g_rhi->TransitionLayout(command, source, EResourceLayout::TRANSFER_SRC);
        g_rhi->TransitionLayout(command, readback.get(), EResourceLayout::TRANSFER_DST);
        command->FlushBarrierBatch();
        if (IsUseVulkan())
        {
            const auto native = static_cast<jCommandBuffer_Vulkan*>(command)->GetRef();
            VkBufferCopy copy{source->GetOffset(), readback->GetOffset(), bytes};
            vkCmdCopyBuffer(native, static_cast<jBuffer_Vulkan*>(source)->Buffer,
                static_cast<jBuffer_Vulkan*>(readback.get())->Buffer, 1, &copy);
            VkBufferMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = static_cast<jBuffer_Vulkan*>(readback.get())->Buffer;
            barrier.offset = readback->GetOffset(); barrier.size = bytes;
            vkCmdPipelineBarrier(native, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                0, nullptr, 1, &barrier, 0, nullptr);
        }
#ifdef _WIN32
        else
            static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyBufferRegion(
                static_cast<ID3D12Resource*>(readback->GetHandle()), 0,
                static_cast<ID3D12Resource*>(source->GetHandle()), 0, bytes);
#endif
        g_rhi->TransitionLayout(command, source, IsUseVulkan() ? EResourceLayout::GENERAL : EResourceLayout::READ_ONLY);
        g_rhi->EndSingleTimeCommands(command); // Independent validation readback only.
        const auto* mapped = readback->Map();
        if (!mapped) return false;
        const bool matches = memcmp(mapped, expected.GetBufferData(), static_cast<size_t>(bytes)) == 0;
        readback->Unmap(); return matches;
    }

    bool TextureMatches(jTexture* texture, const jImageData& image, uint32 mip = 0)
    {
        if (!texture) return false;
        const auto& fp = image.ImageBulkData.SubresourceFootprints[0];
        uint32 width = std::max(1, image.Width >> mip), height = std::max(1, image.Height >> mip);
        const bool compressed = image.Format >= ETextureFormat::BC1_UNORM && image.Format <= ETextureFormat::BC7_UNORM;
        const uint32 blockBytes = image.Format == ETextureFormat::BC1_UNORM || image.Format == ETextureFormat::BC4_UNORM
            || image.Format == ETextureFormat::BC4_SNORM ? 8 : 16;
        uint32 rowPitch = compressed ? ((width + 3) / 4) * blockBytes : width * 4;
        uint32 rows = compressed ? (height + 3) / 4 : height;
        uint64 size = uint64(rowPitch) * rows;
#ifdef _WIN32
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        if (!IsUseVulkan())
        {
            const auto desc = static_cast<jTexture_DX12*>(texture)->Texture->Get()->GetDesc();
            uint64 rowSize = 0;
            g_rhi_dx12->Device->GetCopyableFootprints(&desc, mip, 1, 0, &footprint, &rows, &rowSize, &size);
            rowPitch = footprint.Footprint.RowPitch;
        }
#endif
        auto readback = g_rhi->CreateRawBuffer(size, 0, EBufferCreateFlag::Readback,
            IsUseVulkan() ? EResourceLayout::UNDEFINED : EResourceLayout::TRANSFER_DST);
        if (!readback || !readback->GetHandle()) return false;
        auto* command = g_rhi->BeginSingleTimeCommands();
        g_rhi->TransitionLayout(command, texture, EResourceLayout::TRANSFER_SRC);
        g_rhi->TransitionLayout(command, readback.get(), EResourceLayout::TRANSFER_DST);
        command->FlushBarrierBatch();
        if (IsUseVulkan())
        {
            VkBufferImageCopy region{};
            region.bufferOffset = readback->GetOffset();
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(static_cast<jCommandBuffer_Vulkan*>(command)->GetRef(),
                static_cast<jTexture_Vulkan*>(texture)->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                static_cast<jBuffer_Vulkan*>(readback.get())->Buffer, 1, &region);
            VkBufferMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = static_cast<jBuffer_Vulkan*>(readback.get())->Buffer;
            barrier.offset = readback->GetOffset(); barrier.size = size;
            vkCmdPipelineBarrier(static_cast<jCommandBuffer_Vulkan*>(command)->GetRef(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        }
#ifdef _WIN32
        else
        {
            D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
            source.pResource = static_cast<ID3D12Resource*>(texture->GetHandle());
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; source.SubresourceIndex = mip;
            destination.pResource = static_cast<ID3D12Resource*>(readback->GetHandle());
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
            static_cast<jCommandBuffer_DX12*>(command)->Get()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
#endif
        g_rhi->TransitionLayout(command, texture, EResourceLayout::SHADER_READ_ONLY);
        g_rhi->EndSingleTimeCommands(command); // Validation readback only; admission never waits queue idle.
        const auto* pixels = static_cast<const uint8*>(readback->Map());
        if (!pixels) return false;
        bool matches = true;
        if (mip == 0)
            for (uint32 row = 0; row < rows; ++row)
                matches &= memcmp(pixels + uint64(row) * rowPitch,
                    image.ImageBulkData.ImageData.data() + fp.Offset + uint64(row) * fp.RowPitch, fp.RowPitch) == 0;
        else
            matches = memcmp(pixels, image.ImageBulkData.ImageData.data() + fp.Offset, 4) == 0;
        readback->Unmap();
        return matches;
    }
}

int RunModelUploadCpuTests()
{
    int failures = 0;
    const auto expect = [&](bool value, const char* name) { std::cout << (value ? "PASS " : "FAIL ") << name << std::endl; failures += !value; };
    using namespace jModelUploadPlanning;
    expect(ValidBudget(8 * 1024 * 1024, 2, 2), "upload budget accepts byte time and in-flight bounds");
    expect(!ValidBudget(0, 2, 2) && !ValidBudget(1024, 0, 2) && !ValidBudget(1024, -1, 2)
        && !ValidBudget(1024, std::numeric_limits<double>::quiet_NaN(), 2)
        && !ValidBudget(1024, std::numeric_limits<double>::infinity(), 2)
        && !ValidBudget(1024, 2, 0) && !ValidBudget(1024, 2, MaxTickets + 1),
        "invalid upload budgets cannot produce unbounded or stalled submissions");
    bool bounded = true, covered = true;
    for (const uint64 pitch : {4ull, 256ull, 512ull, 4096ull})
        for (const uint32 totalRows : {1u, 17u, 2057u})
            for (const uint64 budget : {pitch, pitch * 3 + 17, 8 * 1024 * 1024ull})
            {
                uint32 done = 0;
                while (done < totalRows)
                {
                    const auto rows = Rows(pitch, totalRows - done, budget);
                    if (!rows) { covered = false; break; }
                    bounded &= uint64(rows) * pitch <= budget && uint64(rows) * pitch <= MaxStagingBytes;
                    done += rows;
                }
                covered &= done == totalRows;
            }
    expect(bounded && covered, "row chunk planning covers odd padded footprints without exceeding per-step or staging bytes");
    expect(!Rows(256, 1, 255) && !Rows(0, 10, 1024) && !Rows(256, 0, 1024),
        "undersized row budgets and completed footprints admit no partial block row");
    std::string error;
    auto& loader = jModelLoader::GetInstance();
    expect(!loader.BeginFinalize({}, &error) && !error.empty(), "begin upload rejects missing prepared data without RHI access");
    auto worker = std::async(std::launch::async, [&] {
        std::string workerError;
        return !loader.BeginFinalize(std::make_unique<jPreparedModel>(), &workerError)
            && workerError.find("owner thread") != std::string::npos;
    });
    expect(worker.get(), "begin upload rejects worker-thread GPU ownership");
    return failures;
}

int RunModelUploadGpuTests()
{
    int failures = 0;
    const auto expect = [&](bool value, const char* name) { std::cout << (value ? "PASS " : "FAIL ") << name << std::endl; failures += !value; };
    if (!g_rhi) { expect(false, "GPU upload tests require initialized RHI"); return failures; }
    const auto folder = fs::temp_directory_path() / ("jengine-upload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup { fs::path Folder; ~Cleanup() { jModelUploadSession::DrainRetired(); std::error_code ec; fs::remove_all(Folder, ec); } } cleanup{folder};
    auto& loader = jModelLoader::GetInstance();
    auto& images = jImageFileLoader::GetInstance();
    WriteModel(folder, "model", false);
    std::string error;
    const auto source = folder / "model.obj", texturePath = folder / "model.png";
    auto prepare = [&] { return loader.PrepareModel(source.string().c_str(), folder.string().c_str(), {}, &error); };
    auto firstImage = images.LoadImageDataFromFile(jName(texturePath.string())).lock();
    auto session = loader.BeginFinalize(prepare(), &error);
    expect(!!session, "real model upload session starts from prepared geometry and PNG pixels");
    if (!session) { std::cout << error << std::endl; return failures + 1; }
    jModelUploadSession::Budget budget;
    budget.MaxBytes = 1024; budget.MaxPendingChunks = 2; budget.MaxAdmissionMilliseconds = 100;
    bool withinBudget = true;
    size_t steps = 0;
    size_t measuredUploads = 0, admissionOverruns = 0;
    double totalUploadWall = 0, totalStepWall = 0, worstStepWall = 0;
    const auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    const auto complete = [&](jModelUploadSession& upload, const char* scenario) {
        // Wall time starts at the first Step and includes the harness's 1 ms
        // polling pauses. It excludes preparation, BeginFinalize, TakeModel and
        // validation readback. Step/admission elapsed times include driver calls
        // on the owner; they are not GPU timestamp measurements or CPU cycles.
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(20);
        size_t uploadSteps = 0, overruns = 0, maximumPending = 0;
        uint64 maximumStepBytes = 0;
        double stepWallTotal = 0, stepWallMaximum = 0, admissionTotal = 0, admissionMaximum = 0, pauseWall = 0;
        const auto finish = [&](bool ready, const char* state) {
            const double wall = milliseconds(std::chrono::steady_clock::now() - started);
            ++measuredUploads; admissionOverruns += overruns;
            totalUploadWall += wall; totalStepWall += stepWallTotal; worstStepWall = std::max(worstStepWall, stepWallMaximum);
            std::ostringstream line;
            line << std::fixed << std::setprecision(3)
                << "UPLOAD_MEASUREMENT scenario=" << scenario << " state=" << state
                << " scope=first_step_to_terminal steps=" << uploadSteps
                << " upload_wall_ms=" << wall << " poll_pause_wall_ms=" << pauseWall
                << " step_call_wall_total_ms=" << stepWallTotal << " step_call_wall_max_ms=" << stepWallMaximum
                << " reported_admission_total_ms=" << admissionTotal << " reported_admission_max_ms=" << admissionMaximum
                << " admission_budget_ms=" << budget.MaxAdmissionMilliseconds << " admission_overrun_steps=" << overruns
                << " actual_max_step_bytes=" << maximumStepBytes << " actual_max_pending_chunks=" << maximumPending;
            std::cout << line.str() << std::endl;
            return ready;
        };
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto stepStarted = std::chrono::steady_clock::now();
            const auto progress = upload.Step(budget); ++steps; ++uploadSteps;
            const double stepWall = milliseconds(std::chrono::steady_clock::now() - stepStarted);
            stepWallTotal += stepWall; stepWallMaximum = std::max(stepWallMaximum, stepWall);
            admissionTotal += progress.LastStepMilliseconds; admissionMaximum = std::max(admissionMaximum, progress.LastStepMilliseconds);
            overruns += progress.AdmissionOverrun;
            maximumStepBytes = std::max(maximumStepBytes, progress.LastStepBytes); maximumPending = std::max(maximumPending, progress.PendingChunks);
            withinBudget &= progress.LastStepBytes <= budget.MaxBytes && progress.PendingChunks <= budget.MaxPendingChunks;
            if (progress.Current == jModelUploadSession::State::Ready) return finish(true, "ready");
            if (progress.Current == jModelUploadSession::State::Failed) { std::cout << progress.Error << std::endl; return finish(false, "failed"); }
            const auto pauseStarted = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            pauseWall += milliseconds(std::chrono::steady_clock::now() - pauseStarted);
        }
        return finish(false, "timeout");
    };
    expect(complete(*session, "initial") && steps > 2 && withinBudget, "real GPU uploads span multiple steps and obey actual byte/in-flight limits");
    auto first = session->TakeModel();
    expect(first && !first->RenderObjects.empty() && session->GetProgress().CompletedBytes == session->GetProgress().TotalBytes,
        "GPU model is published only after all upload fences complete");
    if (!first) return failures + 1;
    const auto& geometry = *first->RenderObjectGeometryDataPtr;
    expect(geometry.VertexStreamPtr->Params[0]->GetBufferSize() > budget.MaxBytes
        && BufferMatches(geometry.VertexBufferPtr->GetBuffer(0), *geometry.VertexStreamPtr->Params[0])
        && BufferMatches(geometry.VertexBuffer_PositionOnlyPtr->GetBuffer(0), *geometry.VertexStream_PositionOnlyPtr->Params[0])
        && BufferMatches(geometry.IndexBufferPtr->GetBuffer(), *geometry.IndexStreamPtr->Param),
        "GPU readback preserves vertex position and index bytes across geometry chunk boundaries");
    auto* firstTexture = MaterialTexture(*first);
    expect(firstImage && TextureMatches(firstTexture, *firstImage), "GPU readback preserves every uploaded texture row across chunk boundaries");
    if (IsUseVulkan() && firstTexture && firstImage && firstTexture->MipLevel > firstImage->MipLevel)
        expect(TextureMatches(firstTexture, *firstImage, firstTexture->MipLevel - 1), "incremental mip generation preserves the final one-pixel mip");
    auto duplicate = loader.BeginFinalize(prepare(), &error);
    expect(duplicate && complete(*duplicate, "cached_texture"), "another model may reuse a completed decoded-image texture lease");
    auto second = duplicate ? duplicate->TakeModel() : nullptr;
    expect(second && MaterialTexture(*second) == firstTexture,
        "managed image identity deduplicates GPU texture uploads across model prototypes");
    WriteModel(folder, "model", true);
    images.InvalidateImageData(jName(texturePath.string()));
    auto replacement = loader.BeginFinalize(prepare(), &error);
    auto newImage = images.LoadImageDataFromFile(jName(texturePath.string())).lock();
    expect(replacement && complete(*replacement, "reimport"), "invalidated image data uploads a new prepared revision");
    auto third = replacement ? replacement->TakeModel() : nullptr;
    expect(third && MaterialTexture(*third) != firstTexture && TextureMatches(firstTexture, *firstImage)
        && TextureMatches(MaterialTexture(*third), *newImage),
        "reimport keeps old texture leases valid while publishing fresh pixels");
    WriteModel(folder, "cancel", false);
    auto cancelled = loader.BeginFinalize(loader.PrepareModel((folder / "cancel.obj").string().c_str(), folder.string().c_str()), &error);
    expect(!!cancelled, "cancellation fixture has a distinct uncached upload session");
    if (cancelled)
    {
        cancelled->Step(budget); cancelled->Cancel();
        expect(!cancelled->TakeModel() && cancelled->GetProgress().Current == jModelUploadSession::State::Cancelled,
            "cancelled partially submitted model never becomes publishable");
        cancelled.reset(); jModelUploadSession::DrainRetired();
        expect(TextureMatches(firstTexture, *firstImage), "cancel retirement preserves earlier live model resources");
    }
    WriteModel(folder, "budget", false);
    auto invalid = loader.BeginFinalize(loader.PrepareModel((folder / "budget.obj").string().c_str(), folder.string().c_str()), &error);
    if (invalid)
    {
        auto tooSmall = budget; tooSmall.MaxBytes = 1;
        const auto progress = invalid->Step(tooSmall);
        expect(progress.Current == jModelUploadSession::State::Failed && !invalid->TakeModel() && !progress.Error.empty(),
            "a byte budget below one texture row returns an error without publishing a partial model");
    }
    else expect(false, "budget fixture session starts");
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(3)
        << "UPLOAD_TEST steps=" << steps << " max_bytes_per_step=" << budget.MaxBytes
        << " admission_budget_ms=" << budget.MaxAdmissionMilliseconds << " measured_uploads=" << measuredUploads
        << " upload_wall_ms_total=" << totalUploadWall << " step_call_wall_ms_total=" << totalStepWall
        << " step_call_wall_ms_max=" << worstStepWall << " admission_overrun_steps=" << admissionOverruns;
    std::cout << summary.str() << std::endl;
    return failures;
}
