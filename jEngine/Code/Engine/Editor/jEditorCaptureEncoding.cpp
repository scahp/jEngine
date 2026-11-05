#include "pch.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "jEditorCaptureEncoding.h"
#include "lodepng.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <DirectXTex.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "mfuuid.lib")
#endif

namespace jEditorCapture::Detail
{
namespace
{
namespace fs = std::filesystem;

class OutputFile
{
public:
    explicit OutputFile(const fs::path& destination) : Destination(destination)
    {
        fs::create_directories(Destination.parent_path());
        if (fs::exists(Destination)) throw std::runtime_error("Capture destination already exists.");
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        static std::atomic_uint64_t sequence = 0;
        const auto path = Destination.parent_path() / (".capture-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
        if (!fs::create_directory(path)) throw std::runtime_error("Could not reserve a temporary capture folder.");
        Directory = path;
        Temporary = Directory / Destination.filename();
    }
    ~OutputFile()
    {
        std::error_code error;
        fs::remove(Temporary, error);
        fs::remove(Directory, error);
    }
    void Publish() const
    {
#ifdef _WIN32
        if (!MoveFileExW(Temporary.c_str(), Destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not publish the capture. Check the destination and choose an unused filename.");
#else
        // Creating the link fails if another file appeared at the destination.
        fs::create_hard_link(Temporary, Destination);
#endif
    }
    fs::path Temporary;
private:
    fs::path Destination, Directory;
};

void WriteBytes(const fs::path& path, const void* data, size_t bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    file.close();
}

void WriteBitmap(const fs::path& path, const Options& options, const Pixels& frame)
{
    // 24-bit BMP, top-down rows. All fields in the file are little-endian.
    constexpr size_t headerSize = 54;
    const size_t stride = (size_t(options.Width) * 3 + 3) & ~size_t(3);
    std::vector<uint8_t> bytes(headerSize + stride * options.Height);
    const auto field = [&](size_t offset, uint32_t value) {
        for (size_t i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (i * 8));
    };
    bytes[0] = 'B'; bytes[1] = 'M';
    field(2, static_cast<uint32_t>(bytes.size()));
    field(10, headerSize); field(14, 40);
    field(18, options.Width); field(22, static_cast<uint32_t>(-options.Height));
    bytes[26] = 1; bytes[28] = 24;
    field(34, static_cast<uint32_t>(stride * options.Height));
    for (int y = 0; y < options.Height; ++y)
    {
        const auto* source = frame.Rgba.data() + size_t(y) * options.Width * 4;
        auto* dest = bytes.data() + headerSize + size_t(y) * stride;
        for (int x = 0; x < options.Width; ++x, source += 4, dest += 3)
        {
            dest[0] = source[2]; dest[1] = source[1]; dest[2] = source[0];
        }
    }
    WriteBytes(path, bytes.data(), bytes.size());
}

#ifdef _WIN32
void Check(HRESULT result, const char* operation)
{
    if (SUCCEEDED(result)) return;
    std::ostringstream message;
    message << operation << " failed (0x" << std::hex << static_cast<uint32_t>(result) << ")";
    throw std::runtime_error(message.str());
}

struct ComApartment
{
    ComApartment() { Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Capture COM initialization"); }
    ~ComApartment() { CoUninitialize(); }
};

class SystemLibrary
{
public:
    explicit SystemLibrary(const wchar_t* name) : Handle(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
    {
        if (!Handle) throw std::runtime_error("Windows Media Foundation is unavailable on this installation.");
    }
    ~SystemLibrary() { FreeLibrary(Handle); }
    template<class Function> Function Load(const char* name) const
    {
        const auto address = GetProcAddress(Handle, name);
        if (!address) throw std::runtime_error(std::string("Missing Media Foundation entry point ") + name);
        return reinterpret_cast<Function>(address);
    }
private:
    HMODULE Handle;
};

struct MediaFoundation
{
    SystemLibrary Platform{L"mfplat.dll"}, ReadWrite{L"mfreadwrite.dll"};
    decltype(&::MFCreateMediaType) CreateMediaType = Platform.Load<decltype(&::MFCreateMediaType)>("MFCreateMediaType");
    decltype(&::MFCreateMemoryBuffer) CreateBuffer = Platform.Load<decltype(&::MFCreateMemoryBuffer)>("MFCreateMemoryBuffer");
    decltype(&::MFCreateSample) CreateSample = Platform.Load<decltype(&::MFCreateSample)>("MFCreateSample");
    decltype(&::MFCreateSinkWriterFromURL) CreateWriter = ReadWrite.Load<decltype(&::MFCreateSinkWriterFromURL)>("MFCreateSinkWriterFromURL");
    decltype(&::MFShutdown) Shutdown = Platform.Load<decltype(&::MFShutdown)>("MFShutdown");

    MediaFoundation()
    {
        const auto start = Platform.Load<decltype(&::MFStartup)>("MFStartup");
        Check(start(MF_VERSION, MFSTARTUP_FULL), "Media Foundation startup");
    }
    ~MediaFoundation() { Shutdown(); }
};

class VideoWriter
{
public:
    VideoWriter(const fs::path& path, const Options& options)
        : Width(options.Width), Height(options.Height), Fps(options.FramesPerSecond)
    {
        Check(Media.CreateWriter(path.c_str(), nullptr, nullptr, Writer.GetAddressOf()), "MP4 writer creation");
        auto output = Type(MFVideoFormat_H264);
        const auto bitrate = std::clamp(uint64_t(Width) * Height * Fps / 6, uint64_t(1000000), uint64_t(60000000));
        Check(output->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate)), "H.264 bitrate");
        Check(Writer->AddStream(output.Get(), &Stream), "H.264 stream creation");
        auto input = Type(MFVideoFormat_NV12);
        Check(input->SetUINT32(MF_MT_DEFAULT_STRIDE, Width), "NV12 row pitch");
        Check(input->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709), "NV12 color matrix");
        Check(input->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235), "NV12 color range");
        Check(Writer->SetInputMediaType(Stream, input.Get(), nullptr), "H.264 encoder setup");
        Check(Writer->BeginWriting(), "MP4 recording start");
    }
    void Write(const Pixels& frame, uint64_t nextSlot)
    {
        const DWORD size = Width * Height * 3 / 2;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        Check(Media.CreateBuffer(size, buffer.GetAddressOf()), "Video frame allocation");
        BYTE* data = nullptr;
        Check(buffer->Lock(&data, nullptr, nullptr), "Video frame lock");
        ConvertNv12(frame.Rgba, data);
        Check(buffer->Unlock(), "Video frame unlock");
        Check(buffer->SetCurrentLength(size), "Video frame size");
        Microsoft::WRL::ComPtr<IMFSample> sample;
        Check(Media.CreateSample(sample.GetAddressOf()), "Video sample allocation");
        Check(sample->AddBuffer(buffer.Get()), "Video sample buffer");
        const auto time = Time(frame.Slot);
        Check(sample->SetSampleTime(time), "Video sample time");
        Check(sample->SetSampleDuration(Time(nextSlot) - time), "Video sample duration");
        Check(Writer->WriteSample(Stream, sample.Get()), "H.264 encoding");
    }
    void Finish()
    {
        Check(Writer->Finalize(), "MP4 finalization");
        Writer.Reset();
    }
private:
    Microsoft::WRL::ComPtr<IMFMediaType> Type(const GUID& subtype)
    {
        Microsoft::WRL::ComPtr<IMFMediaType> type;
        Check(Media.CreateMediaType(type.GetAddressOf()), "Video media type creation");
        Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Video type");
        Check(type->SetGUID(MF_MT_SUBTYPE, subtype), "Video codec");
        Check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Progressive video");
        Check(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, Width, Height), "Video dimensions");
        Check(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, Fps, 1), "Video frame rate");
        Check(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Video pixel aspect ratio");
        return type;
    }
    LONGLONG Time(uint64_t slot) const { return static_cast<LONGLONG>(slot * 10000000ull / Fps); }
    void ConvertNv12(const std::vector<uint8_t>& rgba, uint8_t* output) const
    {
        auto* chroma = output + size_t(Width) * Height;
        for (UINT32 y = 0; y < Height; y += 2)
        {
            const auto* top = rgba.data() + size_t(y) * Width * 4;
            const auto* bottom = top + size_t(Width) * 4;
            auto* luma = output + size_t(y) * Width;
            for (UINT32 x = 0; x < Width; x += 2, top += 8, bottom += 8)
            {
                const auto luminance = [](const uint8_t* p) { return uint8_t(16 + ((47 * p[0] + 157 * p[1] + 16 * p[2] + 128) >> 8)); };
                luma[x] = luminance(top); luma[x + 1] = luminance(top + 4);
                luma[x + Width] = luminance(bottom); luma[x + Width + 1] = luminance(bottom + 4);
                const int r = (top[0] + top[4] + bottom[0] + bottom[4] + 2) / 4;
                const int g = (top[1] + top[5] + bottom[1] + bottom[5] + 2) / 4;
                const int b = (top[2] + top[6] + bottom[2] + bottom[6] + 2) / 4;
                *chroma++ = uint8_t(128 + ((-26 * r - 86 * g + 112 * b + 128) >> 8));
                *chroma++ = uint8_t(128 + ((112 * r - 102 * g - 10 * b + 128) >> 8));
            }
        }
    }
    ComApartment Apartment;
    MediaFoundation Media;
    Microsoft::WRL::ComPtr<IMFSinkWriter> Writer;
    UINT32 Width, Height, Fps;
    DWORD Stream = 0;
};
#endif

void WriteScreenshot(const fs::path& path, const Options& options, Pixels& frame)
{
    switch (options.FileFormat)
    {
    case Format::Png:
    {
        unsigned char* encoded = nullptr;
        size_t size = 0;
        const auto result = LodePNG_encode32(&encoded, &size, frame.Rgba.data(), options.Width, options.Height);
        std::unique_ptr<unsigned char, decltype(&std::free)> data(encoded, std::free);
        if (result) throw std::runtime_error("PNG encoding failed (" + std::to_string(result) + ").");
        WriteBytes(path, data.get(), size);
        return;
    }
    case Format::Bmp: WriteBitmap(path, options, frame); return;
#ifdef _WIN32
    case Format::Jpeg:
    {
        ComApartment apartment;
        DirectX::Image image{};
        image.width = options.Width; image.height = options.Height;
        image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        image.rowPitch = size_t(options.Width) * 4; image.slicePitch = frame.Rgba.size();
        image.pixels = frame.Rgba.data();
        DirectX::Blob jpeg;
        Check(DirectX::SaveToWICMemory(image, DirectX::WIC_FLAGS_FORCE_RGB, GUID_ContainerFormatJpeg, jpeg), "JPEG encoding");
        WriteBytes(path, jpeg.GetBufferPointer(), jpeg.GetBufferSize());
        return;
    }
#endif
    default: throw std::runtime_error("Unsupported screenshot format.");
    }
}
}

Result Encode(Job& job) try
{
    OutputFile file(job.Output.File);
    uint64_t endSlot = 0;
    const auto read = [&](Pixels& frame) {
        std::unique_lock lock(job.Mutex);
        job.Wake.wait(lock, [&] { return job.Closed || !job.Frames.empty(); });
        if (!job.Error.empty()) throw std::runtime_error(job.Error);
        if (job.Frames.empty()) { endSlot = job.EndSlot; return false; }
        frame = std::move(job.Frames.front());
        job.Frames.pop_front();
        return true;
    };
    Pixels frame;
    uint64_t count = 0;
    if (job.Output.FileFormat == Format::Mp4)
    {
#ifdef _WIN32
        VideoWriter writer(file.Temporary, job.Output);
        if (!read(frame)) throw std::runtime_error("Recording ended before the first rendered frame.");
        Pixels next;
        // A skipped capture extends the previous frame's duration instead of
        // accelerating playback or feeding duplicates to a busy encoder.
        while (read(next))
        {
            writer.Write(frame, next.Slot);
            ++count;
            frame = std::move(next);
        }
        writer.Write(frame, std::max(endSlot, frame.Slot + 1));
        ++count;
        writer.Finish();
#else
        throw std::runtime_error("MP4 recording is available on Windows.");
#endif
    }
    else
    {
        if (!read(frame)) throw std::runtime_error("No rendered frame was available for the screenshot.");
        WriteScreenshot(file.Temporary, job.Output, frame);
        count = 1;
    }
    file.Publish();
    return {count, {}};
}
catch (const std::exception& error) { return {0, error.what()}; }
}
#endif
