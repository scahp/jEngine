#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace jTextureSubsurface
{
inline constexpr std::uint32_t MaximumSurfaces = 4;
inline constexpr std::uint32_t MaximumSurfaceID = (1u << 20) - 1u;
inline constexpr std::size_t MaximumWorkBytes = 512u * 1024u * 1024u;

// Finite texture-space surface transport; the screen-space module remains an
// independent fallback. Distances below explicitly convert world units to mm.
struct Settings
{
    std::uint32_t TileResolution = 1024;
    std::uint32_t CaptureResolution = 1024;
    float GaussianStepScale = 1.0f;
    float WorldUnitsPerMillimeter = 1.0f;
    bool EnableTransmission = true;
    float TransmissionExtinctionPerMillimeter = 0.2f;
    float TransmissionThicknessScale = 1.0f;
    float CaptureBiasWorld = 0.05f;
};

inline bool ValidateSettings(const Settings& input, Settings& output, std::string& error)
{
    const auto finiteRange = [](float value, float lo, float hi)
    { return std::isfinite(value) && value >= lo && value <= hi; };
    const auto resolution = [](std::uint32_t value, std::uint32_t maximum)
    { return value >= 128 && value <= maximum && (value & (value - 1)) == 0; };
    if (!resolution(input.TileResolution, 1024) || !resolution(input.CaptureResolution, 2048) ||
        !finiteRange(input.GaussianStepScale, 0.0f, 10.0f) ||
        !finiteRange(input.WorldUnitsPerMillimeter, 0.00001f, 1000.0f) ||
        !finiteRange(input.TransmissionExtinctionPerMillimeter, 0.001f, 100.0f) ||
        !finiteRange(input.TransmissionThicknessScale, 0.0f, 10.0f) ||
        !finiteRange(input.CaptureBiasWorld, 0.0f, 100.0f))
    {
        error = "Texture diffusion requires bounded power-of-two tiles/capture and finite physical transport settings.";
        return false;
    }
    output = input; error.clear(); return true;
}
}
