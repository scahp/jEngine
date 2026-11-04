#pragma once

#include <cmath>
#include <cstdint>

// CPU-only configuration shared by renderer, console and editor callers.
// Projection, depth conventions, bias and resource ownership remain with the
// shadow producer; filtering only averages depth-comparison visibility.
namespace jShadowFiltering
{
enum class Mode : std::uint32_t
{
    Single = 0,
    Poisson16 = 1,
};

inline constexpr std::uint32_t PoissonSampleCount = 16;
inline constexpr float MaximumRadiusTexels = 4.f;

struct Settings
{
    Mode Method = Mode::Single;
    float RadiusTexels = 1.f;
};

// Invalid input leaves output unchanged. Rendering code can retain its last
// valid settings while UI/console callers explain why the edit was rejected.
inline bool TryMakeSettings(int method, float radiusTexels, Settings& output) noexcept
{
    if ((method != static_cast<int>(Mode::Single) && method != static_cast<int>(Mode::Poisson16))
        || !std::isfinite(radiusTexels) || radiusTexels < 0.f || radiusTexels > MaximumRadiusTexels)
        return false;
    output = {static_cast<Mode>(method), radiusTexels};
    return true;
}
}
