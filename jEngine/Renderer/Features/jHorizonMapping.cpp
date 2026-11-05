#include "jHorizonMapping.h"

#include <algorithm>
#include <cmath>
#include <utility>

// Reference: scahp/Shadows (MIT), HorizonMapping@cfac2ac67b29b29e92dd1d091304f1e6ec94b4c8.
// No Windows PPL or power-of-two wrapping assumptions. The caller owns workers.
namespace jHorizonMapping
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double HalfPi = Pi*.5;

bool Cancelled(const BakeSettings& settings, std::string& error)
{
    if (settings.Cancelled && settings.Cancelled())
    { error = "Horizon bake cancelled."; return true; }
    return false;
}

double Sample(const HeightFieldView& field, Boundary edges, double x, double y)
{
    const auto coordinate = [edges](double value, std::uint32_t size)
    {
        if (edges == Boundary::Clamp) return std::clamp(value, 0., double(size-1));
        value = std::fmod(value, double(size));
        return value < 0. ? value+size : value;
    };
    x = coordinate(x, field.Width); y = coordinate(y, field.Height);
    const auto x0 = std::min(static_cast<std::uint32_t>(std::floor(x)), field.Width-1);
    const auto y0 = std::min(static_cast<std::uint32_t>(std::floor(y)), field.Height-1);
    const auto x1 = edges == Boundary::Wrap ? (x0+1)%field.Width : std::min(x0+1, field.Width-1);
    const auto y1 = edges == Boundary::Wrap ? (y0+1)%field.Height : std::min(y0+1, field.Height-1);
    const double fx = x-x0, fy = y-y0;
    const double top = field.Samples[std::size_t(y0)*field.Width+x0]*(1.-fx)
        + field.Samples[std::size_t(y0)*field.Width+x1]*fx;
    const double bottom = field.Samples[std::size_t(y1)*field.Width+x0]*(1.-fx)
        + field.Samples[std::size_t(y1)*field.Width+x1]*fx;
    return top*(1.-fy)+bottom*fy;
}

bool ValidateMap(const HorizonMap& map, std::string& error)
{
    const std::uint64_t count = std::uint64_t(map.Width)*map.Height;
    if (!map.Width || !map.Height || count > MaximumBakeTexels
        || map.Angles.size() != count || map.AmbientVisibility.size() != count)
    { error = "Invalid horizon map dimensions or payload lengths."; return false; }
    for (std::size_t i = 0; i < map.Angles.size(); ++i)
    {
        for (const float angle : map.Angles[i])
            if (!std::isfinite(angle) || angle < 0.f || angle > HalfPi+1e-6)
            { error = "Horizon map angle is nonfinite or outside [0,pi/2]."; return false; }
        if (!std::isfinite(map.AmbientVisibility[i]) || map.AmbientVisibility[i] < 0.f
            || map.AmbientVisibility[i] > 1.f)
        { error = "Horizon ambient visibility must be finite in [0,1]."; return false; }
    }
    return true;
}
}

bool Bake(const HeightFieldView& field, const BakeSettings& settings, HorizonMap& out, std::string& error)
{
    error.clear();
    try
    {
        const std::uint64_t count = std::uint64_t(field.Width)*field.Height;
        if (!field.Width || !field.Height || field.Width > 16384 || field.Height > 16384 || !field.Samples
            || field.SampleCount != count || !settings.MaxTexels || settings.MaxTexels > MaximumBakeTexels
            || count > settings.MaxTexels || !settings.RadiusTexels || settings.RadiusTexels > MaximumRadiusTexels
            || !settings.MaxSampleSteps || settings.MaxSampleSteps > MaximumBakeSampleSteps
            || (settings.Edges != Boundary::Clamp && settings.Edges != Boundary::Wrap)
            || !std::isfinite(field.HeightScale) || field.HeightScale < 0.f || field.HeightScale > 1e6f
            || !std::isfinite(field.TexelSizeU) || field.TexelSizeU <= 0.f || field.TexelSizeU > 1e6f
            || !std::isfinite(field.TexelSizeV) || field.TexelSizeV <= 0.f || field.TexelSizeV > 1e6f)
        { error = "Invalid or over-budget horizon height field/settings."; return false; }
        const std::uint64_t work = count*DirectionCount*settings.RadiusTexels;
        if (work > settings.MaxSampleSteps)
        { error = "Horizon bake exceeds the requested sample-step budget."; return false; }
        if (Cancelled(settings, error)) return false;
        for (std::uint32_t y = 0; y < field.Height; ++y)
        {
            if (Cancelled(settings, error)) return false;
            for (std::uint32_t x = 0; x < field.Width; ++x)
            {
                const float value = field.Samples[std::size_t(y)*field.Width+x];
                if (!std::isfinite(value) || value < 0.f || value > 1.f)
                { error = "Horizon source heights must be finite and normalized to [0,1]."; return false; }
            }
        }
        HorizonMap candidate;
        candidate.Width = field.Width; candidate.Height = field.Height;
        candidate.Angles.resize(static_cast<std::size_t>(count));
        candidate.AmbientVisibility.resize(static_cast<std::size_t>(count));
        std::array<double, DirectionCount> du{}, dv{}, unitDistance{};
        for (std::uint32_t k = 0; k < DirectionCount; ++k)
        {
            du[k] = std::cos(2.*Pi*k/DirectionCount); dv[k] = std::sin(2.*Pi*k/DirectionCount);
            // Make the cardinal rays exact, preventing a seam from floating
            // epsilon crossing into a wrapped neighbor in a 1D/flat texture.
            if (std::abs(du[k]) < 1e-12) du[k] = 0.;
            if (std::abs(dv[k]) < 1e-12) dv[k] = 0.;
            unitDistance[k] = std::hypot(du[k]*field.TexelSizeU, dv[k]*field.TexelSizeV);
        }
        for (std::uint32_t y = 0; y < field.Height; ++y)
        {
            if (Cancelled(settings, error)) return false;
            for (std::uint32_t x = 0; x < field.Width; ++x)
            {
                const auto index = std::size_t(y)*field.Width+x;
                const double startHeight = field.Samples[index];
                double sky = 0.;
                for (std::uint32_t k = 0; k < DirectionCount; ++k)
                {
                    double horizon = 0.;
                    for (std::uint32_t step = 1; step <= settings.RadiusTexels; ++step)
                    {
                        if ((step & 15u) == 1 && Cancelled(settings, error)) return false;
                        const double height = Sample(field, settings.Edges, x+du[k]*step, y+dv[k]*step);
                        const double rise = (height-startHeight)*field.HeightScale;
                        if (rise > 0.) horizon = std::max(horizon, std::atan2(rise, unitDistance[k]*step));
                    }
                    candidate.Angles[index][k] = static_cast<float>(horizon);
                    const double cosine = std::cos(horizon); sky += cosine*cosine;
                }
                candidate.AmbientVisibility[index] = static_cast<float>(std::clamp(sky/DirectionCount, 0., 1.));
            }
        }
        if (Cancelled(settings, error)) return false;
        out = std::move(candidate); return true;
    }
    catch (const std::exception& exception)
    { error = std::string("Horizon bake failed: ") + exception.what(); return false; }
}

bool EncodeLayers(const HorizonMap& map, EncodedLayers& out, std::string& error)
{
    error.clear();
    try
    {
        if (!ValidateMap(map, error)) return false;
        EncodedLayers candidate;
        candidate.Width = map.Width; candidate.Height = map.Height;
        candidate.Layer0.resize(map.Angles.size()); candidate.Layer1.resize(map.Angles.size());
        candidate.AmbientVisibility = map.AmbientVisibility;
        for (std::size_t i = 0; i < map.Angles.size(); ++i)
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                candidate.Layer0[i][channel] = static_cast<float>(std::clamp(map.Angles[i][channel]/HalfPi, 0., 1.));
                candidate.Layer1[i][channel] = static_cast<float>(std::clamp(map.Angles[i][channel+4]/HalfPi, 0., 1.));
            }
        out = std::move(candidate); return true;
    }
    catch (const std::exception& exception)
    { error = std::string("Horizon encoding failed: ") + exception.what(); return false; }
}
}
