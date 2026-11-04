#include "pch.h"
#include "jModelUploadSession.h"
#include "jModelUploadPlanning.h"
#include "jPreparedModelData.h"
#include "RHI/Vulkan/jBufferUtil_Vulkan.h"
#include "RHI/Vulkan/jVertexBuffer_Vulkan.h"
#include "RHI/Vulkan/jIndexBuffer_Vulkan.h"
#ifdef _WIN32
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"
#include "RHI/DX12/jTextureUploadChunk_DX12.h"
#include "RHI/DX12/jVertexBuffer_DX12.h"
#include "RHI/DX12/jIndexBuffer_DX12.h"
#endif
#include <cmath>
#include <limits>

namespace
{
    constexpr size_t MaxGlobalTickets = jModelUploadPlanning::MaxTickets;
    constexpr uint64 MaxChunkBytes = jModelUploadPlanning::MaxStagingBytes;

    struct UploadTicket
    {
        jCommandBufferManager* Manager = nullptr;
        jCommandBuffer* Command = nullptr;
        std::shared_ptr<jBuffer> Staging, Buffer;
        std::shared_ptr<jTexture> Texture;
        uint64 Bytes = 0;
        bool Vulkan = true, Submitted = false, Done = false, SignalFailed = false;
        std::string Error;
        VkDevice Device = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
#ifdef _WIN32
        Microsoft::WRL::ComPtr<ID3D12Fence> D3DFence;
        HANDLE Event = nullptr;
#endif
        ~UploadTicket()
        {
            // Submitted tickets are always retained by the bounded global table
            // until Poll has retired their fence, including after cancellation.
            if (Command && !Submitted)
            {
                Command->End();
                Manager->ReturnCommandBuffer(Command);
            }
            if (Fence) vkDestroyFence(Device, Fence, nullptr);
#ifdef _WIN32
            if (Event) CloseHandle(Event);
#endif
        }
        bool Poll(bool wait = false)
        {
            if (Done) return true;
            if (!Submitted) return false;
            if (Vulkan)
            {
                const auto status = wait ? vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX)
                    : vkGetFenceStatus(Device, Fence);
                if (status == VK_NOT_READY || status == VK_TIMEOUT) return false;
                if (status != VK_SUCCESS) Error = "Vulkan upload fence failed: " + std::to_string(status);
            }
#ifdef _WIN32
            else
            {
                if (SignalFailed)
                {
                    if (SUCCEEDED(g_rhi_dx12->Device->GetDeviceRemovedReason()))
                    {
                        if (!wait) return false;
                        // Exceptional shutdown recovery only: preserve in-use
                        // staging until a subsequent backend drain has completed.
                        g_rhi_dx12->Flush();
                    }
                    Manager->ReturnCommandBuffer(Command);
                    Command = nullptr; Done = true; Staging.reset();
                    return true;
                }
                auto value = D3DFence->GetCompletedValue();
                if (wait && value < 1)
                {
                    if (!Event) Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                    if (!Event || FAILED(D3DFence->SetEventOnCompletion(1, Event)))
                        Error = "DX12 upload completion event failed";
                    else
                    {
                        const auto result = WaitForSingleObject(Event, INFINITE);
                        if (result != WAIT_OBJECT_0) Error = "DX12 upload fence wait failed";
                    }
                    value = D3DFence->GetCompletedValue();
                }
                if (value == UINT64_MAX) Error = "DX12 device removed during upload";
                else if (value < 1 && Error.empty()) return false;
            }
#endif
            Manager->ReturnCommandBuffer(Command);
            Command = nullptr; Done = true;
            if (Fence) { vkDestroyFence(Device, Fence, nullptr); Fence = VK_NULL_HANDLE; }
            Staging.reset();
            return true;
        }
    };

    std::array<std::shared_ptr<UploadTicket>, MaxGlobalTickets>& Tickets()
    {
        static std::array<std::shared_ptr<UploadTicket>, MaxGlobalTickets> tickets;
        return tickets;
    }
    std::thread::id& UploadOwner() { static std::thread::id owner; return owner; }
    size_t PendingGlobal()
    {
        return std::count_if(Tickets().begin(), Tickets().end(), [](const auto& ticket) { return !!ticket; });
    }
    void CollectTickets(bool wait)
    {
        if (UploadOwner() != std::thread::id() && UploadOwner() != std::this_thread::get_id()) return;
        for (auto& ticket : Tickets()) if (ticket && ticket->Poll(wait)) ticket.reset();
    }

    std::shared_ptr<UploadTicket> BeginTicket(std::string& error)
    {
        if (PendingGlobal() >= MaxGlobalTickets) return {};
        auto ticket = std::make_shared<UploadTicket>();
        ticket->Vulkan = IsUseVulkan();
        ticket->Manager = g_rhi->GetGraphicsCommandBufferManager();
        if (!ticket->Manager) { error = "No graphics upload command manager"; return {}; }
        if (ticket->Vulkan)
        {
            ticket->Device = g_rhi_vk->Device;
            VkFenceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            const auto result = vkCreateFence(ticket->Device, &info, nullptr, &ticket->Fence);
            if (result != VK_SUCCESS) { error = "Cannot allocate Vulkan upload fence: " + std::to_string(result); return {}; }
        }
#ifdef _WIN32
        else if (FAILED(g_rhi_dx12->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ticket->D3DFence))))
        { error = "Cannot allocate DX12 upload fence"; return {}; }
#endif
        ticket->Command = ticket->Manager->GetOrCreateCommandBuffer();
        if (!ticket->Command) { error = "Cannot allocate upload command buffer"; return {}; }
        return ticket;
    }

    bool SubmitTicket(const std::shared_ptr<UploadTicket>& ticket, std::string& error)
    {
        std::shared_ptr<UploadTicket>* reserved = nullptr;
        for (auto& slot : Tickets()) if (!slot) { reserved = &slot; break; }
        if (!reserved) { error = "Upload retirement capacity exceeded"; return false; }
        *reserved = ticket; // Retention is installed before any GPU execution.
        if (!ticket->Command->End()) { reserved->reset(); error = "Cannot close upload command buffer"; return false; }
        if (ticket->Vulkan)
        {
            const VkCommandBuffer command = static_cast<jCommandBuffer_Vulkan*>(ticket->Command)->GetRef();
            VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            const auto result = vkQueueSubmit(g_rhi_vk->GetQueue(ECommandBufferType::GRAPHICS).Queue, 1, &submit, ticket->Fence);
            if (result != VK_SUCCESS) { reserved->reset(); error = "Vulkan upload submission failed: " + std::to_string(result); return false; }
        }
#ifdef _WIN32
        else
        {
            auto queue = g_rhi_dx12->GraphicsCommandBufferManager->GetCommandQueue();
            ID3D12CommandList* commands[]{static_cast<jCommandBuffer_DX12*>(ticket->Command)->Get()};
            queue->ExecuteCommandLists(1, commands);
            ticket->Submitted = true;
            if (FAILED(queue->Signal(ticket->D3DFence.Get(), 1)))
            {
                error = "DX12 upload queue signal failed";
                ticket->SignalFailed = true;
                ticket->Error = error;
                // Keep staging/target in the bounded table until device removal
                // or an explicit shutdown drain; never recycle an in-use list.
                return false;
            }
        }
#endif
        ticket->Submitted = true;
        return true;
    }

    std::shared_ptr<jBuffer> AllocateStaging(uint64 size, std::string& error)
    {
        if (!size || size > MaxChunkBytes) { error = "Invalid staging chunk size"; return {}; }
        if (IsUseVulkan())
        {
            // Standalone offset-zero staging also satisfies compressed/3-byte
            // format copy alignment; its entire lifetime is tied to one fence.
            auto buffer = std::make_shared<jBuffer_Vulkan>();
            buffer->RealBufferSize = size;
            if (!jBufferUtil_Vulkan::CreateBuffer_LowLevel(EVulkanBufferBits::TRANSFER_SRC,
                EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT, size,
                buffer->Buffer, buffer->BufferMemory, buffer->AllocatedSize))
            { error = "Cannot allocate Vulkan staging chunk"; return {}; }
            buffer->Layout = EResourceLayout::TRANSFER_SRC;
            return buffer;
        }
#ifdef _WIN32
        auto buffer = jBufferUtil_DX12::CreateBuffer(size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
            EBufferCreateFlag::CPUAccess, EResourceLayout::READ_ONLY);
        if (!buffer || !buffer->GetHandle()) { error = "Cannot allocate DX12 staging chunk"; return {}; }
        return buffer;
#else
        error = "Unsupported upload backend"; return {};
#endif
    }

    struct FormatBlock { uint32 Width = 1, Height = 1, Bytes = 0; };
    FormatBlock Block(ETextureFormat format)
    {
        switch (format)
        {
        case ETextureFormat::BC1_UNORM: case ETextureFormat::BC4_UNORM: case ETextureFormat::BC4_SNORM: return {4,4,8};
        case ETextureFormat::BC2_UNORM: case ETextureFormat::BC3_UNORM: case ETextureFormat::BC5_UNORM:
        case ETextureFormat::BC5_SNORM: case ETextureFormat::BC6H_UF16: case ETextureFormat::BC6H_SF16: case ETextureFormat::BC7_UNORM: return {4,4,16};
        case ETextureFormat::RGB8: return {1,1,3};
        case ETextureFormat::RGB16F: return {1,1,6};
        case ETextureFormat::RGB32F: return {1,1,12};
        case ETextureFormat::RGBA8: case ETextureFormat::RGBA8SI: case ETextureFormat::RGBA8UI:
        case ETextureFormat::BGRA8: case ETextureFormat::R11G11B10F: case ETextureFormat::R10G10B10A2:
        case ETextureFormat::R32F: case ETextureFormat::R32UI: case ETextureFormat::RG16F: return {1,1,4};
        case ETextureFormat::RGBA16F: case ETextureFormat::RG32F: return {1,1,8};
        case ETextureFormat::RGBA32F: return {1,1,16};
        case ETextureFormat::R16F: case ETextureFormat::R16UI: case ETextureFormat::RG8: return {1,1,2};
        case ETextureFormat::R8: case ETextureFormat::R8UI: return {1,1,1};
        default: return {};
        }
    }

    struct TextureUpload
    {
        struct Footprint { jImageSubResourceData Source; uint64 RowBytes = 0, UploadPitch = 0; uint32 Rows = 0; };
        std::shared_ptr<jImageData> Image;
        std::shared_ptr<jTexture> Texture;
        std::vector<Footprint> Footprints;
        size_t FootprintIndex = 0;
        uint32 Row = 0, MipCount = 1, NextMip = 1;
        FormatBlock Texel;
        bool GeneratedMips = false, Finished = false, Cached = false;
    };

    bool AllocateTexture(TextureUpload& job, std::string& error)
    {
        auto& image = *job.Image;
        if (IsUseVulkan())
        {
            job.MipCount = image.MipLevel;
            if (image.MipLevel == 1 && image.CreateMipmapIfPossible && job.Texel.Height == 1)
            {
                VkFormatProperties properties{};
                vkGetPhysicalDeviceFormatProperties(g_rhi_vk->PhysicalDevice, GetVulkanTextureFormat(image.Format), &properties);
                const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
                    | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
                if ((properties.optimalTilingFeatures & required) == required)
                    job.MipCount = jTexture::GetMipLevels(image.Width, image.Height);
            }
            job.GeneratedMips = job.MipCount > static_cast<uint32>(image.MipLevel);
            const auto usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            std::shared_ptr<jTexture_Vulkan> texture;
            if (image.TextureType == ETextureType::TEXTURE_CUBE)
                texture = jBufferUtil_Vulkan::CreateCubeTexture(image.Width, image.Height, job.MipCount,
                    VK_SAMPLE_COUNT_1_BIT, GetVulkanTextureFormat(image.Format), VK_IMAGE_TILING_OPTIMAL,
                    usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, {}, VK_IMAGE_LAYOUT_UNDEFINED);
            else if (image.LayerCount > 1)
            {
                texture = jBufferUtil_Vulkan::CreateTexture2DArray(image.Width, image.Height, image.LayerCount, job.MipCount,
                    VK_SAMPLE_COUNT_1_BIT, GetVulkanTextureFormat(image.Format), VK_IMAGE_TILING_OPTIMAL,
                    usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, {}, VK_IMAGE_LAYOUT_UNDEFINED);
                if (texture && texture->Image)
                {
                    if (texture->View) vkDestroyImageView(g_rhi_vk->Device, texture->View, nullptr);
                    texture->View = jBufferUtil_Vulkan::CreateTexture2DArrayView(texture->Image,
                        image.LayerCount, GetVulkanTextureFormat(image.Format), VK_IMAGE_ASPECT_COLOR_BIT, job.MipCount);
                }
            }
            else
                texture = jBufferUtil_Vulkan::Create2DTexture(image.Width, image.Height, job.MipCount,
                    VK_SAMPLE_COUNT_1_BIT, GetVulkanTextureFormat(image.Format), VK_IMAGE_TILING_OPTIMAL,
                    usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, {}, VK_IMAGE_LAYOUT_UNDEFINED);
            if (!texture || !texture->Image || !texture->View) { error = "Vulkan texture allocation failed"; return false; }
            job.Texture = texture;
        }
#ifdef _WIN32
        else
        {
            job.MipCount = image.MipLevel;
            job.Texture = jBufferUtil_DX12::CreateTexture(image.Width, image.Height, image.LayerCount, image.MipLevel, 1,
                image.TextureType, image.Format, ETextureCreateFlag::NONE, EResourceLayout::GENERAL);
            if (!job.Texture || !job.Texture->GetHandle()) { error = "DX12 texture allocation failed"; return false; }
        }
#endif
        job.Texture->sRGB = image.sRGB;
        job.Texture->ResourceName = image.Filename;
        return true;
    }

    bool UploadRows(TextureUpload& job, const TextureUpload::Footprint& footprint, uint32 rows,
        const std::shared_ptr<UploadTicket>& ticket, std::string& error)
    {
        const uint64 bytes = footprint.UploadPitch * rows;
        ticket->Staging = AllocateStaging(bytes, error);
        if (!ticket->Staging) return false;
        auto* mapped = static_cast<uint8*>(ticket->Staging->Map());
        if (!mapped) { error = "Cannot map texture staging chunk"; return false; }
        memset(mapped, 0, static_cast<size_t>(bytes));
        const auto* source = job.Image->ImageBulkData.ImageData.data() + footprint.Source.Offset
            + uint64(job.Row) * footprint.Source.RowPitch;
        for (uint32 row = 0; row < rows; ++row)
            memcpy(mapped + uint64(row) * footprint.UploadPitch, source + uint64(row) * footprint.Source.RowPitch,
                static_cast<size_t>(footprint.RowBytes));
        ticket->Staging->Unmap();
        ticket->Texture = job.Texture; ticket->Bytes = bytes;
        const bool finalCopy = job.FootprintIndex + 1 == job.Footprints.size()
            && job.Row + rows == footprint.Rows && !job.GeneratedMips;
        g_rhi->TransitionLayout(ticket->Command, job.Texture.get(), EResourceLayout::TRANSFER_DST);
        ticket->Command->FlushBarrierBatch();
        const uint32 y = job.Row * job.Texel.Height;
        if (ticket->Vulkan)
        {
            const uint32 height = std::min(rows * job.Texel.Height, footprint.Source.Height - y);
            VkBufferImageCopy region{};
            region.bufferOffset = ticket->Staging->GetOffset();
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, footprint.Source.MipLevel, footprint.Source.Depth, 1};
            region.imageOffset = {0, static_cast<int32>(y), 0};
            region.imageExtent = {footprint.Source.Width, height, 1};
            vkCmdCopyBufferToImage(static_cast<jCommandBuffer_Vulkan*>(ticket->Command)->GetRef(),
                static_cast<jBuffer_Vulkan*>(ticket->Staging.get())->Buffer,
                static_cast<jTexture_Vulkan*>(job.Texture.get())->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
#ifdef _WIN32
        else
        {
            CopyTextureUploadChunk_DX12(static_cast<jCommandBuffer_DX12*>(ticket->Command)->Get(),
                static_cast<jTexture_DX12*>(job.Texture.get())->Texture->Get(),
                footprint.Source.MipLevel + footprint.Source.Depth * job.Image->MipLevel, y,
                static_cast<ID3D12Resource*>(ticket->Staging->GetHandle()), GetDX12TextureFormat(job.Image->Format),
                footprint.Source.Width, job.Texel.Width, job.Texel.Height, rows,
                static_cast<UINT>(footprint.UploadPitch));
        }
#endif
        if (finalCopy)
            g_rhi->TransitionLayout(ticket->Command, job.Texture.get(), EResourceLayout::SHADER_READ_ONLY);
        return true;
    }

    bool GenerateOneMip(TextureUpload& job, const std::shared_ptr<UploadTicket>& ticket)
    {
        if (!ticket->Vulkan) return false;
        auto* texture = static_cast<jTexture_Vulkan*>(job.Texture.get());
        const auto command = static_cast<jCommandBuffer_Vulkan*>(ticket->Command)->GetRef();
        ticket->Texture = job.Texture;
        const uint32 mip = job.NextMip;
        VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = texture->Image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, static_cast<uint32>(texture->LayerCount)};
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, static_cast<uint32>(texture->LayerCount)};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, static_cast<uint32>(texture->LayerCount)};
        region.srcOffsets[1] = {std::max(1, texture->Width >> (mip - 1)), std::max(1, texture->Height >> (mip - 1)), 1};
        region.dstOffsets[1] = {std::max(1, texture->Width >> mip), std::max(1, texture->Height >> mip), 1};
        vkCmdBlitImage(command, texture->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            texture->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        if (mip + 1 == job.MipCount)
        {
            barrier.subresourceRange.baseMipLevel = mip;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);
            texture->Layout = EResourceLayout::SHADER_READ_ONLY;
        }
        return true;
    }

    struct BufferUpload
    {
        const IStreamParam* Param = nullptr;
        std::shared_ptr<jBuffer> Buffer;
        uint64 Copied = 0, Size = 0;
        bool Index = false;
    };

    std::shared_ptr<jBuffer> AllocateBuffer(const BufferUpload& job)
    {
        const auto flags = EBufferCreateFlag::UAV | EBufferCreateFlag::AccelerationStructureBuildInput
            | (job.Index ? EBufferCreateFlag::IndexBuffer : EBufferCreateFlag::VertexBuffer);
        const auto layout = IsUseVulkan() ? EResourceLayout::UNDEFINED : EResourceLayout::GENERAL;
        if (job.Index)
        {
            ETextureFormat format = ETextureFormat::R32UI;
            if (job.Param->Attributes[0].UnderlyingType == EBufferElementType::UINT16) format = ETextureFormat::R16UI;
            if (job.Param->Attributes[0].UnderlyingType == EBufferElementType::BYTE) format = ETextureFormat::R8UI;
            // Vulkan shaders unpack indices from uint words. Reserve the final
            // word without extending the CPU payload or the copy-byte budget.
            const uint64 allocationSize = IsUseVulkan() ? Align(job.Size, uint64(4)) : job.Size;
            return g_rhi->CreateFormattedBuffer(allocationSize, 0, format, flags, layout);
        }
        return g_rhi->CreateRawBuffer(job.Size, 0, flags, layout);
    }

    bool UploadBuffer(BufferUpload& job, uint64 bytes, const std::shared_ptr<UploadTicket>& ticket, std::string& error)
    {
        ticket->Staging = AllocateStaging(bytes, error);
        if (!ticket->Staging) return false;
        auto* mapped = ticket->Staging->Map();
        if (!mapped) { error = "Cannot map geometry staging chunk"; return false; }
        memcpy(mapped, static_cast<const uint8*>(job.Param->GetBufferData()) + job.Copied, static_cast<size_t>(bytes));
        ticket->Staging->Unmap();
        ticket->Buffer = job.Buffer; ticket->Bytes = bytes;
        g_rhi->TransitionLayout(ticket->Command, job.Buffer.get(), EResourceLayout::TRANSFER_DST);
        ticket->Command->FlushBarrierBatch();
        if (ticket->Vulkan)
        {
            const auto command = static_cast<jCommandBuffer_Vulkan*>(ticket->Command)->GetRef();
            VkBufferCopy copy{ticket->Staging->GetOffset(), job.Buffer->GetOffset() + job.Copied, bytes};
            vkCmdCopyBuffer(command, static_cast<jBuffer_Vulkan*>(ticket->Staging.get())->Buffer,
                static_cast<jBuffer_Vulkan*>(job.Buffer.get())->Buffer, 1, &copy);
            if (job.Copied + bytes == job.Size)
            {
                VkBufferMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = static_cast<jBuffer_Vulkan*>(job.Buffer.get())->Buffer;
                barrier.offset = job.Buffer->GetOffset(); barrier.size = job.Size;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                    0, nullptr, 1, &barrier, 0, nullptr);
                static_cast<jBuffer_Vulkan*>(job.Buffer.get())->Layout = EResourceLayout::GENERAL;
            }
        }
#ifdef _WIN32
        else
        {
            static_cast<jCommandBuffer_DX12*>(ticket->Command)->Get()->CopyBufferRegion(
                static_cast<ID3D12Resource*>(job.Buffer->GetHandle()), job.Copied,
                static_cast<ID3D12Resource*>(ticket->Staging->GetHandle()), 0, bytes);
            if (job.Copied + bytes == job.Size)
                g_rhi->TransitionLayout(ticket->Command, job.Buffer.get(), EResourceLayout::READ_ONLY);
        }
#endif
        return true;
    }
}

struct jModelUploadSession::Impl
{
    std::unique_ptr<jPreparedModel> Prepared;
    const std::thread::id Owner = std::this_thread::get_id();
    std::vector<TextureUpload> Textures;
    std::vector<BufferUpload> Buffers;
    std::vector<std::shared_ptr<UploadTicket>> Pending;
    std::unique_ptr<jMeshObject> Object;
    size_t TextureIndex = 0, BufferIndex = 0, VertexStreams = 0, PositionStreams = 0, RenderIndex = 0;
    Progress Status;

    explicit Impl(std::unique_ptr<jPreparedModel> prepared) : Prepared(std::move(prepared))
    {
        Pending.reserve(MaxGlobalTickets);
        auto& data = *Prepared->Payload;
        for (const auto& source : data.Images)
        {
            if (!source->Decoded) throw std::runtime_error("Prepared texture data is missing");
            TextureUpload job; job.Image = source->Decoded; job.Texel = Block(job.Image->Format);
            const auto& image = *job.Image;
            if (!job.Texel.Bytes || image.Width <= 0 || image.Height <= 0 || image.MipLevel <= 0 || image.LayerCount <= 0
                || image.MipLevel > jTexture::GetMipLevels(image.Width, image.Height)
                || (image.TextureType == ETextureType::TEXTURE_CUBE && image.LayerCount != 6)
                || (image.TextureType != ETextureType::TEXTURE_CUBE && image.TextureType != ETextureType::TEXTURE_2D
                    && image.TextureType != ETextureType::TEXTURE_2D_ARRAY)
                || image.ImageBulkData.SubresourceFootprints.empty())
                throw std::runtime_error("Unsupported or invalid prepared texture: " + std::string(source->Filename.ToStr()));
            std::set<std::pair<uint32, uint32>> supplied;
            for (const auto& fp : image.ImageBulkData.SubresourceFootprints)
            {
                if (fp.MipLevel >= static_cast<uint32>(image.MipLevel) || fp.Depth >= static_cast<uint32>(image.LayerCount)
                    || fp.Width != std::max(1, image.Width >> fp.MipLevel)
                    || fp.Height != std::max(1, image.Height >> fp.MipLevel)
                    || !supplied.emplace(fp.MipLevel, fp.Depth).second)
                    throw std::runtime_error("Invalid or duplicate texture subresource");
                TextureUpload::Footprint footprint;
                footprint.Source = fp;
                footprint.Rows = (fp.Height + job.Texel.Height - 1) / job.Texel.Height;
                footprint.RowBytes = uint64((fp.Width + job.Texel.Width - 1) / job.Texel.Width) * job.Texel.Bytes;
                footprint.UploadPitch = IsUseVulkan() ? footprint.RowBytes : Align(footprint.RowBytes, uint64(256));
                if (fp.RowPitch < footprint.RowBytes || fp.Offset > image.ImageBulkData.ImageData.size()
                    || uint64(footprint.Rows - 1) * fp.RowPitch + footprint.RowBytes > image.ImageBulkData.ImageData.size() - fp.Offset)
                    throw std::runtime_error("Texture footprint exceeds its prepared CPU pixels");
                job.Footprints.push_back(footprint);
            }
            if (supplied.size() != size_t(image.LayerCount) * image.MipLevel)
                throw std::runtime_error("Prepared texture does not contain every mip/layer");
            job.Texture = jImageFileLoader::GetInstance().FindTextureForImage(job.Image);
            job.Cached = !!job.Texture; job.Finished = job.Cached;
            if (!job.Cached)
                for (const auto& fp : job.Footprints) Status.TotalBytes += fp.UploadPitch * fp.Rows;
            Textures.push_back(std::move(job));
        }
        const auto append = [&](const std::shared_ptr<jVertexStreamData>& stream) {
            if (!stream || stream->Params.empty()) throw std::runtime_error("Prepared vertex stream is empty");
            for (const auto& param : stream->Params)
            {
                if (!param || param->Stride <= 0 || !param->GetBufferSize()) throw std::runtime_error("Invalid prepared vertex stream");
                Buffers.push_back({param.get(), {}, 0, param->GetBufferSize(), false});
                Status.TotalBytes += param->GetBufferSize();
            }
        };
        append(data.VertexStream); VertexStreams = Buffers.size();
        append(data.PositionStream); PositionStreams = Buffers.size() - VertexStreams;
        if (!data.IndexStream || !data.IndexStream->Param || !data.IndexStream->Param->GetBufferSize()
            || data.IndexStream->Param->Attributes.empty()) throw std::runtime_error("Prepared index stream is empty");
        Buffers.push_back({data.IndexStream->Param, {}, 0, data.IndexStream->Param->GetBufferSize(), true});
        Status.TotalBytes += data.IndexStream->Param->GetBufferSize();
    }

    bool IsOwner() const { return Owner == std::this_thread::get_id(); }
    void Fail(const std::string& error) { Status.Current = State::Failed; Status.Error = error; }
    void PollPending(bool wait = false)
    {
        for (auto it = Pending.begin(); it != Pending.end();)
            if ((*it)->Poll(wait))
            {
                if (!(*it)->Error.empty()) Fail((*it)->Error);
                Status.CompletedBytes += (*it)->Bytes;
                it = Pending.erase(it);
            }
            else ++it;
        Status.PendingChunks = Pending.size();
        CollectTickets(false);
    }
    bool Submit(const std::shared_ptr<UploadTicket>& ticket, std::string& error)
    {
        if (!SubmitTicket(ticket, error)) return false;
        Pending.push_back(ticket);
        Status.SubmittedBytes += ticket->Bytes;
        Status.LastStepBytes += ticket->Bytes;
        Status.PendingChunks = Pending.size();
        return true;
    }

    bool CreateObject(std::string& error)
    {
        auto& data = *Prepared->Payload;
        auto geometry = std::make_shared<jRenderObjectGeometryData>();
        geometry->VertexStreamPtr = data.VertexStream; geometry->VertexStream_PositionOnlyPtr = data.PositionStream;
        geometry->IndexStreamPtr = data.IndexStream; geometry->bHasVertexColor = false; geometry->bHasVertexBiTangent = true;
        if (IsUseVulkan())
        {
            std::vector<std::shared_ptr<jBuffer_Vulkan>> main, position;
            for (size_t i = 0; i < VertexStreams; ++i) main.push_back(std::static_pointer_cast<jBuffer_Vulkan>(Buffers[i].Buffer));
            for (size_t i = 0; i < PositionStreams; ++i) position.push_back(std::static_pointer_cast<jBuffer_Vulkan>(Buffers[VertexStreams + i].Buffer));
            auto vertex = std::make_shared<jVertexBuffer_Vulkan>(); auto positionVertex = std::make_shared<jVertexBuffer_Vulkan>();
            if (!vertex->InitializeWithBuffers(data.VertexStream, main) || !positionVertex->InitializeWithBuffers(data.PositionStream, position))
            { error = "Cannot attach completed Vulkan vertex uploads"; return false; }
            auto index = std::make_shared<jIndexBuffer_Vulkan>();
            index->IndexStreamData = data.IndexStream; index->BufferPtr = std::static_pointer_cast<jBuffer_Vulkan>(Buffers.back().Buffer);
            geometry->VertexBufferPtr = vertex; geometry->VertexBuffer_PositionOnlyPtr = positionVertex; geometry->IndexBufferPtr = index;
        }
#ifdef _WIN32
        else
        {
            std::vector<std::shared_ptr<jBuffer_DX12>> main, position;
            for (size_t i = 0; i < VertexStreams; ++i) main.push_back(std::static_pointer_cast<jBuffer_DX12>(Buffers[i].Buffer));
            for (size_t i = 0; i < PositionStreams; ++i) position.push_back(std::static_pointer_cast<jBuffer_DX12>(Buffers[VertexStreams + i].Buffer));
            auto vertex = std::make_shared<jVertexBuffer_DX12>(); auto positionVertex = std::make_shared<jVertexBuffer_DX12>();
            if (!vertex->InitializeWithBuffers(data.VertexStream, main) || !positionVertex->InitializeWithBuffers(data.PositionStream, position))
            { error = "Cannot attach completed DX12 vertex uploads"; return false; }
            auto index = std::make_shared<jIndexBuffer_DX12>();
            index->IndexStreamData = data.IndexStream; index->BufferPtr = std::static_pointer_cast<jBuffer_DX12>(Buffers.back().Buffer);
            index->IBView.BufferLocation = index->BufferPtr->GetGPUAddress();
            index->IBView.SizeInBytes = static_cast<uint32>(data.IndexStream->Param->GetBufferSize());
            const auto underlying = data.IndexStream->Param->Attributes[0].UnderlyingType;
            index->IBView.Format = underlying == EBufferElementType::UINT16 ? DXGI_FORMAT_R16_UINT
                : underlying == EBufferElementType::BYTE ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_R32_UINT;
            geometry->VertexBufferPtr = vertex; geometry->VertexBuffer_PositionOnlyPtr = positionVertex; geometry->IndexBufferPtr = index;
        }
#endif
        std::map<jName, std::shared_ptr<jTexture>> textures;
        for (const auto& image : Textures)
        {
            textures.emplace(image.Image->Filename, image.Texture);
            jImageFileLoader::GetInstance().StoreTextureForImage(image.Image, image.Texture);
        }
        for (auto& material : data.MeshData->Materials)
            for (auto& texture : material.second->TexData)
            {
                const auto found = textures.find(texture.FilePath);
                if (found == textures.end()) continue;
                texture.Texture = found->second.get();
                material.second->OwnedTextures.push_back(found->second);
            }
        Object = std::make_unique<jMeshObject>();
        Object->MeshData = data.MeshData.release(); Object->SubMeshes = std::move(data.SubMeshes);
        Object->RootNode = data.RootNode;
        for (auto& node : data.Nodes) node.release();
        Object->RenderObjectGeometryDataPtr = std::move(geometry);
        return true;
    }
};

jModelUploadSession::jModelUploadSession(std::unique_ptr<jPreparedModel> prepared)
    : Data(std::make_unique<Impl>(std::move(prepared)))
{
    if (UploadOwner() == std::thread::id()) UploadOwner() = std::this_thread::get_id();
    if (UploadOwner() != std::this_thread::get_id()) throw std::runtime_error("Upload owner thread mismatch");
}

jModelUploadSession::~jModelUploadSession() { Cancel(); }

jModelUploadSession::Progress jModelUploadSession::GetProgress() const { return Data->Status; }

jModelUploadSession::Progress jModelUploadSession::Step() { return Step(Budget{}); }

jModelUploadSession::Progress jModelUploadSession::Step(const Budget& budget)
{
    if (!Data->IsOwner())
    {
        auto error = Data->Status; error.Current = State::Failed; error.Error = "Model upload requires its owner thread"; return error;
    }
    if (Data->Status.Current == State::Cancelled || Data->Status.Current == State::Failed
        || Data->Status.Current == State::Ready || Data->Status.Current == State::Consumed) return Data->Status;
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
    Data->Status.LastStepBytes = 0;
    if (!jModelUploadPlanning::ValidBudget(budget.MaxBytes, budget.MaxAdmissionMilliseconds, budget.MaxPendingChunks))
    { Data->Fail("Invalid byte/time/pending upload budget"); return Data->Status; }
    try
    {
        Data->PollPending();
        std::string error;
        while (Data->Status.Current != State::Failed && elapsed() < budget.MaxAdmissionMilliseconds)
        {
            if (Data->TextureIndex < Data->Textures.size())
            {
                auto& image = Data->Textures[Data->TextureIndex];
                if (image.Finished) { ++Data->TextureIndex; continue; }
                if (Data->Pending.size() >= budget.MaxPendingChunks || PendingGlobal() >= MaxGlobalTickets) break;
                if (image.FootprintIndex < image.Footprints.size() && !Data->Status.LastStepBytes
                    && (budget.MaxBytes < image.Footprints[image.FootprintIndex].UploadPitch
                        || image.Footprints[image.FootprintIndex].UploadPitch > MaxChunkBytes))
                { Data->Fail("Upload byte budget is smaller than one texture block row"); break; }
                if (!image.Texture)
                {
                    if (!AllocateTexture(image, error)) { Data->Fail(error); break; }
                    if (elapsed() >= budget.MaxAdmissionMilliseconds) break;
                }
                if (image.FootprintIndex < image.Footprints.size())
                {
                    const auto& fp = image.Footprints[image.FootprintIndex];
                    const uint64 available = std::min(MaxChunkBytes, budget.MaxBytes - Data->Status.LastStepBytes);
                    const auto rows = jModelUploadPlanning::Rows(fp.UploadPitch, fp.Rows - image.Row, available);
                    if (!rows)
                    {
                        if (!Data->Status.LastStepBytes && (budget.MaxBytes < fp.UploadPitch || fp.UploadPitch > MaxChunkBytes))
                            Data->Fail("Upload byte budget is smaller than one texture block row");
                        break;
                    }
                    auto ticket = BeginTicket(error);
                    if (!ticket || !UploadRows(image, fp, rows, ticket, error) || !Data->Submit(ticket, error))
                    { Data->Fail(error.empty() ? "Cannot admit texture upload chunk" : error); break; }
                    image.Row += rows;
                    if (image.Row == fp.Rows) { image.Row = 0; ++image.FootprintIndex; }
                    if (image.FootprintIndex == image.Footprints.size() && !image.GeneratedMips) image.Finished = true;
                }
                else if (image.GeneratedMips && image.NextMip < image.MipCount)
                {
                    auto ticket = BeginTicket(error);
                    if (!ticket || !GenerateOneMip(image, ticket) || !Data->Submit(ticket, error))
                    { Data->Fail(error.empty() ? "Cannot admit mip generation" : error); break; }
                    if (++image.NextMip == image.MipCount) image.Finished = true;
                    break; // One GPU mip level per frame admission, not an entire pyramid burst.
                }
            }
            else if (Data->BufferIndex < Data->Buffers.size())
            {
                auto& buffer = Data->Buffers[Data->BufferIndex];
                if (buffer.Copied == buffer.Size) { ++Data->BufferIndex; continue; }
                if (Data->Pending.size() >= budget.MaxPendingChunks || PendingGlobal() >= MaxGlobalTickets) break;
                if (!buffer.Buffer)
                {
                    buffer.Buffer = AllocateBuffer(buffer);
                    if (!buffer.Buffer || !buffer.Buffer->GetHandle()) { Data->Fail("GPU geometry allocation failed"); break; }
                    if (elapsed() >= budget.MaxAdmissionMilliseconds) break;
                }
                const auto bytes = std::min({buffer.Size - buffer.Copied, MaxChunkBytes,
                    budget.MaxBytes - Data->Status.LastStepBytes});
                if (!bytes) break;
                auto ticket = BeginTicket(error);
                if (!ticket || !UploadBuffer(buffer, bytes, ticket, error) || !Data->Submit(ticket, error))
                { Data->Fail(error.empty() ? "Cannot admit geometry upload chunk" : error); break; }
                buffer.Copied += bytes;
            }
            else
            {
                if (!Data->Pending.empty()) { Data->Status.Current = State::AwaitingGpu; break; }
                if (!Data->Object)
                {
                    if (!Data->CreateObject(error)) { Data->Fail(error); break; }
                    if (elapsed() >= budget.MaxAdmissionMilliseconds) break;
                }
                if (Data->RenderIndex < Data->Object->SubMeshes.size())
                {
                    const auto& mesh = Data->Object->SubMeshes[Data->RenderIndex++];
                    auto render = std::make_unique<jRenderObjectElement>();
                    render->SubMesh = mesh;
                    const auto material = Data->Object->MeshData->Materials.find(mesh.MaterialIndex);
                    if (material != Data->Object->MeshData->Materials.end()) render->MaterialPtr = material->second;
                    render->CreateRenderObject(Data->Object->RenderObjectGeometryDataPtr);
                    Data->Object->AddRenderObject(render.get()); render.release();
                }
                else { Data->Status.Current = State::Ready; break; }
            }
            if (Data->Status.LastStepBytes >= budget.MaxBytes) break;
        }
    }
    catch (const std::exception& exception) { Data->Fail(exception.what()); }
    catch (...) { Data->Fail("Unknown model upload failure"); }
    Data->Status.LastStepMilliseconds = elapsed();
    Data->Status.WorstStepMilliseconds = std::max(Data->Status.WorstStepMilliseconds, Data->Status.LastStepMilliseconds);
    Data->Status.AdmissionOverrun = Data->Status.LastStepMilliseconds > budget.MaxAdmissionMilliseconds;
    return Data->Status;
}

bool jModelUploadSession::PollReady()
{
    if (!Data->IsOwner()) return false;
    Data->PollPending();
    return Data->Status.Current == State::Ready;
}

std::unique_ptr<jMeshObject> jModelUploadSession::TakeModel()
{
    if (!Data->IsOwner() || Data->Status.Current != State::Ready) return {};
    Data->Status.Current = State::Consumed;
    return std::move(Data->Object);
}

void jModelUploadSession::Cancel()
{
    if (!Data || !Data->IsOwner() || Data->Status.Current == State::Consumed) return;
    Data->Status.Current = State::Cancelled;
    // Global tickets own every submitted staging chunk/target until its fence.
    Data->Pending.clear(); Data->Status.PendingChunks = 0;
    Data->Object.reset(); Data->Prepared.reset(); Data->Textures.clear(); Data->Buffers.clear();
}

void jModelUploadSession::CollectRetired() { CollectTickets(false); }
void jModelUploadSession::DrainRetired() { CollectTickets(true); }
void jModelUploadSession::WaitSubmitted() { if (Data->IsOwner()) Data->PollPending(true); }
