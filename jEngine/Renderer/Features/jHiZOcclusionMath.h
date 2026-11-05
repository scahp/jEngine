#pragma once

#include "jHiZOcclusionSettings.h"
#include <array>
#include <span>
#include <vector>

namespace jHiZOcclusion
{
using Matrix4 = std::array<double, 16>; // Column-major, clip=M*world, Z [0,1].
using BoxCorners = std::array<std::array<double, 3>, 8>;

struct IndexedArguments
{
    std::uint32_t IndexCount = 0, InstanceCount = 1, FirstIndex = 0;
    std::int32_t BaseVertex = 0;
    std::uint32_t FirstInstance = 0;
    bool operator==(const IndexedArguments&) const = default;
};
static_assert(sizeof(IndexedArguments) == 20);
struct IndexedRange
{
    std::int64_t FirstIndex = 0, EndIndex = 0, BaseVertex = 0, EndVertex = 0;
};
// Preserve the real submesh index/base-vertex range. Invalid ranges retain output.
bool BuildIndexedArguments(std::uint32_t indexCapacity, std::uint32_t vertexCapacity,
    const IndexedRange& range, IndexedArguments& output, std::string& error);
// Legacy raster coverage multiplies vertex alpha even for a white texture.
// Validate retained FLOAT alpha values instead of equating an RT alpha flag
// with raster transparency. Invalid/unknown layouts conservatively return false.
bool VertexAlphaIsOpaque(std::span<const unsigned char> bytes, std::size_t count,
    std::size_t stride, std::size_t alphaOffset) noexcept;

struct PyramidLevel
{
    std::uint32_t Width = 0, Height = 0, Offset = 0;
    bool operator==(const PyramidLevel&) const = default;
};
struct PyramidLayout
{
    std::uint32_t ViewWidth = 0, ViewHeight = 0, LevelCount = 0, ElementCount = 0;
    std::array<PyramidLevel, MaximumLevels> Levels{};
    std::size_t ResourceBytes = 0; // Float pyramid plus full-resolution D32.
    bool operator==(const PyramidLayout&) const = default;
};
// Power-of-two padding is far depth (1), preserving odd edge pixels at all levels.
bool BuildLayout(std::uint32_t width, std::uint32_t height, PyramidLayout& output, std::string& error);
bool BuildPyramid(std::span<const float> depth, const PyramidLayout& layout,
    std::vector<float>& output, std::string& error);

struct ScreenBounds
{
    // Inclusive integer rectangle, expanded by one pixel. Empty/uncertain
    // projections are not testable and must retain their normal draw.
    std::uint32_t MinimumX = 0, MinimumY = 0, MaximumX = 0, MaximumY = 0;
    double NearestDepth = 0.0;
    bool Testable = false;
    bool operator==(const ScreenBounds&) const = default;
};
// Valid near-plane/w<=epsilon/partially off-screen bounds return Testable=false.
// Invalid data fails transactionally. ndcYUp means textureY=(1-ndcY)*0.5.
bool ProjectBounds(const BoxCorners& corners, const Matrix4& viewProjection,
    std::uint32_t width, std::uint32_t height, bool ndcYUp,
    ScreenBounds& output, std::string& error);
// Never reports occlusion for invalid bounds/layout/depth data. The selected
// maximum-reduction level covers the complete rectangle with at most four taps.
bool IsOccluded(const ScreenBounds& bounds, const PyramidLayout& layout,
    std::span<const float> pyramid, float depthBias) noexcept;
}
