#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// CPU planning contract shared by real GPU admission and headless regressions.
namespace jModelUploadPlanning
{
    inline constexpr size_t MaxTickets = 4;
    inline constexpr std::uint64_t MaxStagingBytes = 2 * 1024 * 1024;
    inline bool ValidBudget(std::uint64_t bytes, double milliseconds, size_t pending)
    {
        return bytes && std::isfinite(milliseconds) && milliseconds > 0 && pending && pending <= MaxTickets;
    }
    inline std::uint32_t Rows(std::uint64_t uploadPitch, std::uint32_t remainingRows, std::uint64_t availableBytes)
    {
        if (!uploadPitch) return 0;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(remainingRows,
            std::min(availableBytes, MaxStagingBytes) / uploadPitch));
    }
}
