#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace jHiZOcclusion
{
inline constexpr std::uint32_t MaximumCandidates = 16384;
inline constexpr std::size_t MaximumScannedVertices = 4 * 1024 * 1024;
inline constexpr std::size_t MaximumResourceBytes = 128 * 1024 * 1024;
inline constexpr std::uint32_t MaximumDimension = 8192;
inline constexpr std::uint32_t MaximumLevels = 14;

struct Settings
{
    bool Enabled = false;
    float DepthBias = 1.0e-5f; // Forward normalized depth; positive is conservative.
    std::uint32_t MaxCandidates = 4096;
    bool operator==(const Settings&) const = default;
};

// Owner-thread settings snapshot. Rejected input leaves output unchanged.
inline bool ValidateSettings(const Settings& input, Settings& output, std::string& error)
{
    error.clear();
    if (!std::isfinite(input.DepthBias) || input.DepthBias < 0.0f || input.DepthBias > 0.01f ||
        input.MaxCandidates == 0 || input.MaxCandidates > MaximumCandidates)
    {
        error = "Hi-Z requires finite depth bias [0,0.01] and 1..16384 candidates.";
        return false;
    }
    output = input;
    return true;
}
}
