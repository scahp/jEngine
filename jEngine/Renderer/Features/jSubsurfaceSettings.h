#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>

// These are scene-world distances, with no implicit centimetre/mm conversion.
// Presets approximate surface diffusion; they are not measured BSSRDF data or
// a model of light transmitted through hidden/back-facing geometry.
namespace jSubsurface
{
inline constexpr std::uint32_t MaximumProfiles = 8;
inline constexpr std::uint32_t KernelSampleCount = 17;
inline constexpr float MaximumRadiusPixels = 64.0f;

struct Profile
{
    const char* Name;
    float RadiusWorld;
    std::array<float, 3> Falloff;
};

// IDs are persistent material data. Append/change a versioned preset rather
// than reorder this table. RadiusWorld is the maximum support, not a sigma.
inline constexpr std::array<Profile, MaximumProfiles> Profiles = {{
    {"Skin",    1.2f, {1.0f, 0.37f, 0.30f}},
    {"Wax",     4.0f, {1.0f, 0.80f, 0.60f}},
    {"Marble",  8.0f, {1.0f, 0.85f, 0.70f}},
    {"Milk",    2.5f, {0.85f, 0.95f, 1.0f}},
    {"Jade",    5.0f, {0.35f, 1.0f, 0.45f}},
    {"Leaf",    1.0f, {0.35f, 1.0f, 0.20f}},
    {"Clay",    2.0f, {1.0f, 0.50f, 0.30f}},
    {"Neutral", 2.0f, {1.0f, 1.0f, 1.0f}}
}};

struct Settings
{
    bool Enabled = false;
    float Strength = 1.0f;
    float RadiusScale = 1.0f;
    float DepthRejectWorld = 1.0f;
    float NormalThreshold = 0.6f;
    float MaxRadiusPixels = 32.0f;
};

static_assert(std::is_trivially_copyable_v<Settings>);

// Transactional validation, also used when the feature is disabled so invalid
// persisted data cannot hide until it is enabled.
inline bool ValidateSettings(const Settings& input, Settings& output, std::string& error)
{
    auto bounded = [](float value, float minimum, float maximum)
    { return std::isfinite(value) && value >= minimum && value <= maximum; };
    if (!bounded(input.Strength, 0.0f, 1.0f) ||
        !bounded(input.RadiusScale, 0.0f, 100.0f) ||
        !bounded(input.DepthRejectWorld, 0.00001f, 10000.0f) ||
        !bounded(input.NormalThreshold, -1.0f, 1.0f) ||
        !bounded(input.MaxRadiusPixels, 0.0f, MaximumRadiusPixels))
    {
        error = "Subsurface strength, radius or surface rejection settings are outside their finite supported ranges.";
        return false;
    }
    output = input;
    error.clear();
    return true;
}

inline bool HasEffect(const Settings& settings)
{
    return settings.Enabled && settings.Strength > 0.0f &&
        settings.RadiusScale > 0.0f && settings.MaxRadiusPixels > 0.0f;
}
}
