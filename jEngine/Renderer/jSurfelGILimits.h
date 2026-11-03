#pragma once
#include <cstdint>

namespace jSurfelGILimits
{
    constexpr int32_t MaxSurfels = 2097152;
    constexpr int64_t MaxLinearDispatchThreads = int64_t(65535) * 64;

    // The cell map needs at least one physical slot per cell. Maintenance
    // shaders use a one-dimensional dispatch with 64 threads per workgroup.
    constexpr bool CanAllocate(int64_t cells, int64_t candidates)
    {
        return cells > 0 && cells <= MaxSurfels
            && candidates > 0 && candidates <= MaxLinearDispatchThreads;
    }
}
