#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct jRenderFrameContext;
struct jTexture;

namespace jEditorCapture
{
enum class Format { Png, Jpeg, Bmp, Mp4 };
enum class Phase { Idle, Screenshot, Recording, Saving };

struct Options
{
    std::filesystem::path File;
    int Width = 1920, Height = 1080, FramesPerSecond = 30;
    Format FileFormat = Format::Png;
};

struct Status
{
    Phase State = Phase::Idle;
    std::filesystem::path File;
    std::string Error;
    uint64_t Frames = 0;
    double Seconds = 0;
};

bool Supports(Format format);
const char* Extension(Format format);
// The UI and render owner call this API. The encoder only receives CPU pixels.
bool Start(Options options, std::string& error);
void Stop();
const Status& GetStatus();
void ReadFrame(const std::shared_ptr<jRenderFrameContext>& frame, jTexture& source, float displayAspect);
void EndFrame();
void Shutdown();
}
