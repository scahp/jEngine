#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace jHorizonMapping
{
enum class Boundary : std::uint32_t { Clamp = 0, Wrap = 1 };
inline constexpr std::uint32_t DirectionCount = 8;
inline constexpr std::uint32_t MaximumRadiusTexels = 256;
inline constexpr std::uint64_t MaximumBakeTexels = 4194304;
inline constexpr std::uint64_t MaximumBakeSampleSteps = 536870912;

// Borrowed row-major normalized [0,1] height samples. HeightScale and texel sizes
// use the same caller world units. +height follows the tangent-space normal.
struct HeightFieldView
{
    std::uint32_t Width = 0, Height = 0;
    const float* Samples = nullptr;
    std::size_t SampleCount = 0;
    float HeightScale = 1.f, TexelSizeU = 1.f, TexelSizeV = 1.f;
};
struct BakeSettings
{
    Boundary Edges = Boundary::Clamp;
    std::uint32_t RadiusTexels = 16;
    std::uint64_t MaxTexels = 1048576, MaxSampleSteps = 134217728;
    std::function<bool()> Cancelled;
};
struct HorizonMap
{
    std::uint32_t Width = 0, Height = 0;
    // Angles in radians [0,pi/2]. Direction k has phi=2*pi*k/8, starting at +U
    // and turning toward +V. The result is independent of renderer world axes.
    std::vector<std::array<float, DirectionCount>> Angles;
    // Cosine-weighted sky visibility approximation mean(cos(horizon)^2).
    std::vector<float> AmbientVisibility;
};
struct EncodedLayers
{
    std::uint32_t Width = 0, Height = 0;
    std::vector<std::array<float, 4>> Layer0, Layer1;
    std::vector<float> AmbientVisibility;
};

// O(width*height*8*radius), including preflight work/memory budgets. Cancellation
// is checked before work and at every row/short tap block. No threads are spawned.
// Clamp replicates edge texels; Wrap supports arbitrary positive resolutions.
// Both operations are transactional: out remains unchanged on failure/cancel.
bool Bake(const HeightFieldView& field, const BakeSettings& settings,
    HorizonMap& out, std::string& error);
// Two RGBA float layers encode directions0..3/4..7 as angle/(pi/2), no gamma.
bool EncodeLayers(const HorizonMap& map, EncodedLayers& out, std::string& error);
}
