#pragma once

#include "jSubsurfaceSettings.h"
#include <cstddef>

namespace jSubsurface
{
using RGB = std::array<float, 3>;

struct KernelSample
{
    RGB Weight{};
    float Offset = 0.0f;
};
using Kernel = std::array<KernelSample, KernelSampleCount>;

// Original three-Gaussian separable approximation. Every colour channel is
// normalized independently over finite support, with denser near-field taps.
// The result preserves a constant diffuse field; it does not tint its energy.
bool BuildKernel(const Profile& profile, Kernel& output, std::string& error);

inline constexpr std::size_t ScratchTargetBudgetBytes = 256u * 1024u * 1024u;
inline constexpr std::size_t ScratchBytesPerPixel = 3u * 8u; // 3 RGBA16F targets.

// Includes only this module's three scratch images. Upstream material/diffuse
// MRTs, and their MSAA storage, remain accounted for by the renderer.
bool ValidateExtent(std::uint32_t width, std::uint32_t height,
    std::size_t& scratchBytes, std::string& error);

float EffectiveRadiusWorld(const Profile& profile, const Settings& settings,
    float thicknessWorld);
float ProjectedRadiusPixels(float radiusWorld, float worldUnitsPerPixel,
    float maximumPixels);
RGB ComposeDiffuse(const RGB& scene, const RGB& originalDiffuse,
    const RGB& filteredDiffuse, const RGB& mask, float strength);
}
