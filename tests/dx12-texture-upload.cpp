// Standalone WARP regression: exercise the exact production chunk copy helper.
// No engine/PCH, shaders, ray tracing, swapchain, or window initialization.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "RHI/DX12/jTextureUploadChunk_DX12.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Match jEngine/pch.cpp. CMake deploys this SDK's Core and SDKLayers beside us.
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 613; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

namespace
{
using Microsoft::WRL::ComPtr;
constexpr UINT WaitMilliseconds = 30000;
constexpr UINT64 MaximumReadbackBytes = 2 * 1024 * 1024;
constexpr size_t MaximumChunks = 128;

std::string Hex(HRESULT value)
{
    std::ostringstream text;
    text << "0x" << std::hex << static_cast<unsigned long>(value);
    return text.str();
}
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed: " + Hex(result));
}
[[noreturn]] void FatalSubmittedWork(const std::string& message)
{
    // Never free in-flight staging after a failed signal/wait. Kill this test
    // process instead of entering device/queue teardown with unknown GPU work.
    std::cerr << "FAIL " << message << "; submitted resources remain retained until process termination" << std::endl;
    TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
    std::_Exit(EXIT_FAILURE);
}

D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES result{};
    result.Type = type;
    result.CreationNodeMask = result.VisibleNodeMask = 1;
    return result;
}
D3D12_RESOURCE_DESC BufferDescription(UINT64 bytes)
{
    D3D12_RESOURCE_DESC result{};
    result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    result.Width = bytes;
    result.Height = 1;
    result.DepthOrArraySize = result.MipLevels = 1;
    result.SampleDesc.Count = 1;
    result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return result;
}
UINT MipCount(UINT width, UINT height)
{
    UINT count = 0;
    for (UINT dimension = std::max(width, height); dimension; dimension >>= 1) ++count;
    return count;
}
struct DebugMessage
{
    D3D12_MESSAGE_ID Id;
    D3D12_MESSAGE_SEVERITY Severity;
    std::string Text;
};
struct Context
{
    ComPtr<ID3D12Debug> Debug;
    ComPtr<IDXGIFactory4> Factory;
    ComPtr<IDXGIAdapter1> Adapter;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12InfoQueue> Info;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12Fence> Fence;
    HANDLE Event = nullptr;
    UINT64 FenceValue = 0;
    size_t Submissions = 0;

    Context()
    {
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&Debug)), "D3D12GetDebugInterface (debug layer is required, never skipped)");
        Debug->EnableDebugLayer();
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)), "CreateDXGIFactory2");
        Check(Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)), "EnumWarpAdapter");
        DXGI_ADAPTER_DESC1 description{};
        Check(Adapter->GetDesc1(&description), "GetDesc1");
        Require((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0, "EnumWarpAdapter did not select a software adapter");
        Check(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)), "D3D12CreateDevice(WARP)");
        Check(Device.As(&Info), "ID3D12InfoQueue (debug layer is required, never skipped)");
        Check(Info->SetMessageCountLimit(4096), "SetMessageCountLimit");
        Check(Info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE), "SetBreakOnSeverity(CORRUPTION)");
        Check(Info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE), "SetBreakOnSeverity(ERROR)");
        Info->SetMuteDebugOutput(TRUE);
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(Device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&Queue)), "CreateCommandQueue");
        Check(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)), "CreateFence");
        Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(Event != nullptr, "CreateEvent failed");
        NoErrors("WARP initialization");
    }
    ~Context() { if (Event) CloseHandle(Event); }

    std::vector<DebugMessage> Messages()
    {
        Require(Info->GetNumMessagesDiscardedByMessageCountLimit() == 0, "Debug InfoQueue overflowed");
        std::vector<DebugMessage> messages;
        const UINT64 count = Info->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i)
        {
            SIZE_T bytes = 0;
            Check(Info->GetMessage(i, nullptr, &bytes), "GetMessage size");
            Require(bytes && bytes <= 1024 * 1024, "Unexpected debug message size");
            std::vector<unsigned char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(Info->GetMessage(i, message, &bytes), "GetMessage");
            messages.push_back({message->ID, message->Severity, message->pDescription ? message->pDescription : ""});
        }
        Info->ClearStoredMessages();
        return messages;
    }
    void NoErrors(const std::string& label)
    {
        bool failed = false;
        for (const auto& message : Messages())
            if (message.Severity == D3D12_MESSAGE_SEVERITY_ERROR || message.Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            {
                failed = true;
                std::cerr << "DEBUG " << label << " id=" << unsigned(message.Id) << " " << message.Text << std::endl;
            }
        Require(!failed, label + " reported D3D12 ERROR/CORRUPTION");
        Check(Device->GetDeviceRemovedReason(), "GetDeviceRemovedReason");
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes, D3D12_HEAP_TYPE type)
    {
        Require(bytes && bytes <= MaximumReadbackBytes, "Fixture buffer is not bounded");
        const auto heap = Heap(type);
        const auto description = BufferDescription(bytes);
        ComPtr<ID3D12Resource> result;
        Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&result)), "CreateCommittedResource(buffer)");
        return result;
    }
    void Commands(ComPtr<ID3D12CommandAllocator>& allocator, ComPtr<ID3D12GraphicsCommandList>& list)
    {
        Check(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
        Check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
            IID_PPV_ARGS(&list)), "CreateCommandList");
    }
    void SubmitAndWait(ID3D12GraphicsCommandList* list)
    {
        Check(list->Close(), "Close(valid upload/readback list)");
        NoErrors("Before submission");
        ID3D12CommandList* lists[]{list};
        Queue->ExecuteCommandLists(1, lists); ++Submissions;
        const UINT64 value = ++FenceValue;
        const HRESULT signal = Queue->Signal(Fence.Get(), value);
        if (FAILED(signal)) FatalSubmittedWork("Queue Signal failed: " + Hex(signal));
        if (Fence->GetCompletedValue() < value)
        {
            const HRESULT event = Fence->SetEventOnCompletion(value, Event);
            if (FAILED(event)) FatalSubmittedWork("SetEventOnCompletion failed: " + Hex(event));
            const DWORD waited = WaitForSingleObject(Event, WaitMilliseconds);
            if (waited != WAIT_OBJECT_0) FatalSubmittedWork("GPU fence did not complete within 30 seconds, wait=" + std::to_string(waited));
        }
        NoErrors("Completed upload/readback");
    }
};

struct Format
{
    const char* Name;
    DXGI_FORMAT Resource, Upload;
    UINT BlockWidth, BlockHeight, BlockBytes;
};
struct Case
{
    Format Pixel;
    UINT Width, Height, Layers, RowsPerChunk;
    bool Cube = false;
    std::string Name() const
    {
        return std::string(Pixel.Name) + " " + std::to_string(Width) + "x" + std::to_string(Height)
            + " layers=" + std::to_string(Layers) + (Cube ? " cube" : " array")
            + " block_rows_per_chunk=" + std::to_string(RowsPerChunk);
    }
};
D3D12_RESOURCE_DESC TextureDescription(const Case& fixture)
{
    D3D12_RESOURCE_DESC result{};
    result.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    result.Width = fixture.Width;
    result.Height = fixture.Height;
    result.DepthOrArraySize = static_cast<UINT16>(fixture.Layers);
    result.MipLevels = static_cast<UINT16>(MipCount(fixture.Width, fixture.Height));
    result.Format = fixture.Pixel.Resource;
    result.SampleDesc.Count = 1;
    result.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return result;
}
ComPtr<ID3D12Resource> Texture(Context& context, const D3D12_RESOURCE_DESC& description)
{
    const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> result;
    Check(context.Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result)), "CreateCommittedResource(texture)");
    return result;
}
unsigned char Pattern(UINT seed, UINT layer, UINT mip, UINT row, UINT byte)
{
    return static_cast<unsigned char>((seed * 23 + layer * 61 + mip * 37 + row * 13 + byte * 7) ^ (byte >> 3));
}
DXGI_FORMAT Typed(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_BC1_TYPELESS) return DXGI_FORMAT_BC1_UNORM;
    if (format == DXGI_FORMAT_BC3_TYPELESS) return DXGI_FORMAT_BC3_UNORM;
    return format;
}

void CheckCase(Context& context, const Case& fixture, UINT seed)
{
    context.NoErrors("Previous case teardown");
    const auto description = TextureDescription(fixture);
    auto texture = Texture(context, description);
    ComPtr<ID3D12DescriptorHeap> cubeView;
    if (fixture.Cube)
    {
        Require(fixture.Layers == 6 && fixture.Width == fixture.Height, "Cube fixture requires six square slices");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = 1;
        Check(context.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&cubeView)), "Create cube SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = Typed(fixture.Pixel.Resource);
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.TextureCube.MipLevels = description.MipLevels;
        context.Device->CreateShaderResourceView(texture.Get(), &view, cubeView->GetCPUDescriptorHandleForHeapStart());
    }
    const UINT count = UINT(description.MipLevels) * fixture.Layers;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(count);
    std::vector<UINT> rowCounts(count);
    std::vector<UINT64> rowSizes(count);
    UINT64 required = 0;
    // A nonzero base exercises real 512-byte placement offsets, not only zero.
    context.Device->GetCopyableFootprints(&description, 0, count, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
        footprints.data(), rowCounts.data(), rowSizes.data(), &required);
    Require(required && required != std::numeric_limits<UINT64>::max(), "GetCopyableFootprints returned an invalid size");
    UINT64 capacity = 0;
    for (UINT i = 0; i < count; ++i)
    {
        Require(footprints[i].Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT == 0, "Readback footprint offset is not 512 aligned");
        Require(footprints[i].Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0, "Readback row pitch is not 256 aligned");
        capacity = std::max(capacity, footprints[i].Offset + UINT64(footprints[i].Footprint.RowPitch) * rowCounts[i]);
    }
    auto readback = context.Buffer(capacity, D3D12_HEAP_TYPE_READBACK);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    context.Commands(allocator, commands);
    std::vector<ComPtr<ID3D12Resource>> staging;
    UINT64 totalSourceBytes = 0;
    for (UINT layer = 0; layer < fixture.Layers; ++layer)
        for (UINT mip = 0; mip < description.MipLevels; ++mip)
        {
            const UINT width = std::max(1u, fixture.Width >> mip), height = std::max(1u, fixture.Height >> mip);
            const UINT rows = (height + fixture.Pixel.BlockHeight - 1) / fixture.Pixel.BlockHeight;
            const UINT rowBytes = ((width + fixture.Pixel.BlockWidth - 1) / fixture.Pixel.BlockWidth) * fixture.Pixel.BlockBytes;
            const UINT pitch = ((rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)
                * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
            const UINT subresource = mip + layer * UINT(description.MipLevels);
            Require(rowCounts[subresource] == rows && rowSizes[subresource] == rowBytes, "Device footprint does not match tight block-row data");
            for (UINT row = 0; row < rows;)
            {
                const UINT chunkRows = std::min(fixture.RowsPerChunk, rows - row);
                Require(staging.size() < MaximumChunks, "Too many staging chunks in small fixture");
                auto chunk = context.Buffer(UINT64(pitch) * chunkRows, D3D12_HEAP_TYPE_UPLOAD);
                unsigned char* mapped = nullptr;
                const D3D12_RANGE noRead{0, 0};
                Check(chunk->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map upload chunk");
                std::memset(mapped, 0xcd, size_t(pitch) * chunkRows);
                for (UINT y = 0; y < chunkRows; ++y)
                    for (UINT x = 0; x < rowBytes; ++x) mapped[size_t(y) * pitch + x] = Pattern(seed, layer, mip, row + y, x);
                const D3D12_RANGE written{0, SIZE_T(pitch) * chunkRows};
                chunk->Unmap(0, &written);
                // This is the production function, not a duplicate of its fix.
                CopyTextureUploadChunk_DX12(commands.Get(), texture.Get(), subresource, row * fixture.Pixel.BlockHeight,
                    chunk.Get(), fixture.Pixel.Upload, width, fixture.Pixel.BlockWidth, fixture.Pixel.BlockHeight, chunkRows, pitch);
                totalSourceBytes += UINT64(rowBytes) * chunkRows;
                staging.push_back(std::move(chunk)); row += chunkRows;
            }
        }
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition.pResource = texture.Get();
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &transition);
    for (UINT subresource = 0; subresource < count; ++subresource)
    {
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprints[subresource];
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = texture.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = subresource;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    context.SubmitAndWait(commands.Get()); // Staging, texture and command allocator remain alive through this fence.
    unsigned char* mapped = nullptr;
    const D3D12_RANGE read{0, static_cast<SIZE_T>(capacity)};
    Check(readback->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Map completed readback");
    bool matches = true;
    std::string mismatch;
    for (UINT layer = 0; layer < fixture.Layers && matches; ++layer)
        for (UINT mip = 0; mip < description.MipLevels && matches; ++mip)
        {
            const UINT subresource = mip + layer * UINT(description.MipLevels);
            for (UINT row = 0; row < rowCounts[subresource] && matches; ++row)
                for (UINT byte = 0; byte < rowSizes[subresource]; ++byte)
                    if (mapped[footprints[subresource].Offset + UINT64(row) * footprints[subresource].Footprint.RowPitch + byte]
                        != Pattern(seed, layer, mip, row, byte))
                    {
                        matches = false;
                        mismatch = "layer=" + std::to_string(layer) + " mip=" + std::to_string(mip)
                            + " row=" + std::to_string(row) + " byte=" + std::to_string(byte);
                        break;
                    }
        }
    const D3D12_RANGE noWrite{0, 0}; readback->Unmap(0, &noWrite);
    Require(matches, "Actual GPU readback bytes differ: " + mismatch);
    context.NoErrors(fixture.Name());
    std::cout << "PASS " << fixture.Name() << " mips=" << description.MipLevels << " subresources=" << count
        << " chunks=" << staging.size() << " tight_bytes=" << totalSourceBytes << std::endl;
}

void NegativeControl(Context& context, UINT logicalSize)
{
    context.NoErrors("Before negative control");
    const size_t submissionsBefore = context.Submissions;
    {
        const Case fixture{{"BC3 negative", DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, 4, 4, 16}, 4, 4, 1, 1};
        const auto description = TextureDescription(fixture);
        auto texture = Texture(context, description);
        auto staging = context.Buffer(256, D3D12_HEAP_TYPE_UPLOAD);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        context.Commands(allocator, commands);
        context.NoErrors("Negative-control setup");
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = logicalSize == 2 ? 1 : 2;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = staging.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint = {DXGI_FORMAT_BC3_UNORM, 4, 4, 1, 256};
        const D3D12_BOX originalInvalidBox{0, 0, 0, logicalSize, logicalSize, 1};
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, &originalInvalidBox);
        bool found = false;
        for (const auto& message : context.Messages())
        {
            Require(message.Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, "Negative control caused CORRUPTION");
            found |= message.Id == D3D12_MESSAGE_ID_COPYTEXTUREREGION_INVALIDSRCBOX
                && message.Severity == D3D12_MESSAGE_SEVERITY_ERROR;
        }
        Require(found, "Debug-layer oracle did not detect original INVALIDSRCBOX(873) for " + std::to_string(logicalSize) + "x" + std::to_string(logicalSize));
        // Invalid recordings may fail Close. They are deliberately discarded and
        // never passed to ExecuteCommandLists, so no invalid GPU work is sent.
        (void)commands->Close();
    }
    context.Messages(); // Discard only this intentional negative recording's Close diagnostics.
    Require(context.Submissions == submissionsBefore, "Negative control submitted an invalid list");
    Check(context.Device->GetDeviceRemovedReason(), "Negative-control device health");
    std::cout << "PASS negative original " << logicalSize << "x" << logicalSize << " source box reports873; list never submitted" << std::endl;
}
}

int main()
{
    unsigned passed = 0, failed = 0;
    try
    {
        Context context;
        ++passed; std::cout << "PASS headless WARP device and required D3D12 debug InfoQueue, Agility SDK613" << std::endl;
        for (UINT size : {2u, 1u}) { NegativeControl(context, size); ++passed; }
        const std::vector<Format> formats = {
            {"BC1_UNORM", DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, 4, 4, 8},
            {"BC3_UNORM", DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, 4, 4, 16},
            {"BC1_TYPELESS_typed_upload", DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM, 4, 4, 8},
            {"BC3_TYPELESS_typed_upload", DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM, 4, 4, 16},
            {"BC1_TYPELESS_upload", DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_TYPELESS, 4, 4, 8},
            {"BC3_TYPELESS_upload", DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_TYPELESS, 4, 4, 16},
            {"RGBA8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 4}};
        std::vector<Case> cases;
        for (const auto& format : formats)
        {
            cases.push_back({format, 4, 4, 1, 1});
            cases.push_back({format, 20, 12, 3, 1});
            cases.push_back({format, 20, 12, 3, 2});
        }
        for (size_t index : {size_t(0), size_t(1), size_t(2), size_t(3)}) cases.push_back({formats[index], 20, 20, 6, 2, true});
        cases.push_back({formats[1], 68, 12, 2, 2}); // A real 512-byte row pitch, in addition to the 256-byte cases.
        cases.push_back({formats.back(), 68, 12, 2, 2});
        for (size_t i = 0; i < cases.size(); ++i)
            try { CheckCase(context, cases[i], static_cast<UINT>(i + 1)); ++passed; }
            catch (const std::exception& exception)
            {
                ++failed; std::cerr << "FAIL " << cases[i].Name() << ": " << exception.what() << std::endl;
                // Close/Create/Map can fail before the normal validation drain.
                // Attribute their InfoQueue details to this case, not the next.
                try { context.NoErrors("Failed case " + cases[i].Name()); }
                catch (const std::exception& diagnostic) { std::cerr << "DETAIL " << diagnostic.what() << std::endl; }
                Check(context.Device->GetDeviceRemovedReason(), "Device health after failed case");
            }
        context.NoErrors("Final case teardown");
    }
    catch (const std::exception& exception) { ++failed; std::cerr << "FAIL WARP texture regression: " << exception.what() << std::endl; }
    std::cout << "DX12 texture upload checks: " << passed << " PASS, " << failed << " FAIL" << std::endl;
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
