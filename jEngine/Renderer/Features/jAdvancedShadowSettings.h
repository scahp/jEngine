#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>

// Plain CPU configuration. No editor, resource, camera or GPU ownership lives
// here. The renderer validates a snapshot before admitting bounded shadow work.
namespace jAdvancedShadows
{
enum class Projection : std::uint32_t { Single = 0, Cascaded = 1, Perspective = 2 };
enum class PointProjection : std::uint32_t { Cube = 0, DualParaboloid = 1 };
enum class Representation : std::uint32_t
{
    Depth = 0, Variance = 1, Exponential = 2, ExponentialVariance = 3, Deep = 4, ShadowVolume = 5
};
enum class LightKind : std::uint32_t { Directional = 0, Point = 1, Spot = 2 };

inline constexpr std::uint32_t DeepBinCount = 8;
inline constexpr std::uint32_t MaximumCascades = 4;
inline constexpr std::uint32_t MaximumVolumeTriangles = 65536;
inline constexpr std::uint32_t MinimumMapResolution = 64;
inline constexpr std::uint32_t MaximumMapResolution = 1024;

struct Settings
{
    Projection DirectionalProjection = Projection::Single;
    jAdvancedShadows::PointProjection PointProjection = jAdvancedShadows::PointProjection::Cube;
    Representation Method = Representation::Depth;
    std::uint32_t MapResolution = 512;
    std::uint32_t CascadeCount = 4;
    float SplitLambda = .5f;
    float MaxDistance = 20000.f;
    bool CascadeStabilize = true;
    float CasterPadding = 1000.f;
    float PositiveExponent = 32.f;
    float NegativeExponent = 8.f;
    float MinimumVariance = 1e-5f;
    float BleedReduction = .2f;
    std::uint32_t BlurRadius = 2;
    float DeepOpacityMultiplier = 1.f;
    float DeepTauMax = 16.f;
    float DeepMaxDistance = 20000.f;
    std::uint32_t VolumeMaxTriangles = 20000;
    float VolumeRange = 20000.f;
    bool operator==(const Settings&) const = default;
};
static_assert(std::is_trivially_copyable_v<Settings>);

// Validate all fields, including inactive ones. Invalid input preserves output.
// Projection settings are independent defaults for different light types;
// ValidateForLight checks the effective combination for one actual light.
inline bool ValidateSettings(const Settings& input, Settings& output, std::string& error)
{
    const auto bounded = [](float value, float minimum, float maximum)
    { return std::isfinite(value) && value >= minimum && value <= maximum; };
    const auto reject = [&](const char* reason) { error = reason; return false; };
    if (std::uint32_t(input.DirectionalProjection) > std::uint32_t(Projection::Perspective))
        return reject("Directional shadow projection must be Single (0), Cascaded (1) or Perspective (2).");
    if (std::uint32_t(input.PointProjection) > std::uint32_t(PointProjection::DualParaboloid))
        return reject("Point shadow projection must be Cube (0) or Dual Paraboloid (1).");
    if (std::uint32_t(input.Method) > std::uint32_t(Representation::ShadowVolume))
        return reject("Shadow representation must be Depth, Variance, Exponential, Exponential Variance, Deep or Shadow Volume (0..5).");
    if (input.MapResolution < MinimumMapResolution || input.MapResolution > MaximumMapResolution
        || (input.MapResolution & (input.MapResolution - 1)) != 0)
        return reject("Shadow map resolution must be a power of two from 64 to 1024.");
    if (!input.CascadeCount || input.CascadeCount > MaximumCascades)
        return reject("Shadow cascade count must be an integer from 1 to 4.");
    if (!bounded(input.SplitLambda, 0, 1))
        return reject("Shadow cascade split lambda must be finite and between 0 and 1.");
    if (!bounded(input.MaxDistance, .01f, 1e6f))
        return reject("Shadow maximum distance must be finite and between 0.01 and 1000000 world units.");
    if (!bounded(input.CasterPadding, 0, 1e5f))
        return reject("Shadow caster padding must be finite and between 0 and 100000 world units.");
    if (!bounded(input.PositiveExponent, .1f, 40) || !bounded(input.NegativeExponent, .1f, 40))
        return reject("Shadow positive and negative exponents must be finite and between 0.1 and 40 (FP32 moments).");
    if (!bounded(input.MinimumVariance, 1e-8f, 1))
        return reject("Shadow minimum variance must be finite and between 0.00000001 and 1.");
    if (!bounded(input.BleedReduction, 0, .999f))
        return reject("Shadow light-bleed reduction must be finite and between 0 and 0.999.");
    if (input.BlurRadius > 8)
        return reject("Shadow blur radius must be an integer from 0 to 8 texels.");
    if (!bounded(input.DeepOpacityMultiplier, 0, 16))
        return reject("Deep shadow opacity multiplier must be finite and between 0 and 16.");
    if (!bounded(input.DeepTauMax, .01f, 32))
        return reject("Deep shadow maximum optical depth must be finite and between 0.01 and 32.");
    if (!bounded(input.DeepMaxDistance, .01f, 1e6f))
        return reject("Deep shadow maximum distance must be finite and between 0.01 and 1000000 world units.");
    if (!input.VolumeMaxTriangles || input.VolumeMaxTriangles > MaximumVolumeTriangles)
        return reject("Shadow volume triangle budget must be an integer from 1 to 65536.");
    if (!bounded(input.VolumeRange, 32, 1e6f))
        return reject("Shadow volume range must be finite and between 32 and 1000000 world units.");
    output = input; error.clear(); return true;
}

inline bool ValidateForLight(const Settings& settings, LightKind kind, std::string& error)
{
    Settings checked;
    if (!ValidateSettings(settings, checked, error)) return false;
    if (std::uint32_t(kind) > std::uint32_t(LightKind::Spot))
    { error = "Advanced shadows require a directional, point or spot light."; return false; }
    if (settings.Method == Representation::ShadowVolume)
    {
        if (kind == LightKind::Directional && settings.DirectionalProjection != Projection::Single)
        { error = "Directional shadow volumes require Single projection; Cascaded and Perspective cannot be combined with volumes."; return false; }
        if (kind == LightKind::Point && settings.PointProjection != PointProjection::Cube)
        { error = "Point shadow volumes require Cube projection; Dual Paraboloid cannot be combined with volumes."; return false; }
    }
    error.clear(); return true;
}
} // namespace jAdvancedShadows
