#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace jEnvironmentLighting
{
using RGB = std::array<float, 3>;
using Direction = std::array<float, 3>;
using Coefficients = std::array<RGB, 9>;

inline constexpr std::uint32_t AssetVersion = 1;
inline constexpr std::size_t MaximumAssetBytes = 65536;
inline constexpr std::uint64_t MaximumProjectionTexels = 16777216;

// Synchronous borrowed linear HDR radiance; row 0 is the north (+Y) pole.
// theta = pi*v, phi = 2*pi*u: +X at u=0, +Z at u=.25. No exposure/gamma.
struct ImageView
{
    std::uint32_t Width = 0, Height = 0;
    const RGB* Pixels = nullptr;
    std::size_t PixelCount = 0;
};

struct ProjectionOptions
{
    std::uint64_t MaxTexels = 4194304;
    std::function<bool()> Cancelled;
};

// Unconvolved orthonormal real SH radiance coefficients, positive Sloan basis:
// [1, y, z, x, xy, yz, 3*z*z-1, xz, x*x-y*y]. World coordinates are Y-up.
// Projection/load are transactional; cancellation or invalid input retains out.
bool ProjectEquirectangular(const ImageView& image, Coefficients& out, std::string& error,
    const ProjectionOptions& options = {});
bool Validate(const Coefficients& coefficients, std::string& error);
RGB EvaluateRadiance(const Coefficients& coefficients, const Direction& direction) noexcept;
// Returns irradiance E, with band convolution pi,2pi/3,pi/4 and negative ringing
// clamped to zero. Renderer applies material diffuse albedo/pi and intensity.
RGB EvaluateDiffuseIrradiance(const Coefficients& coefficients, const Direction& normal) noexcept;

// .jsh v1 records basis/space/linear-radiance units and exactly 9 RGB values.
// Save uses an exclusive sibling staging file then atomic replace. A failure
// retains the previous destination. No textures, GPU resources or global state.
bool LoadAsset(const std::filesystem::path& path, Coefficients& out, std::string& error);
bool SaveAsset(const std::filesystem::path& path, const Coefficients& coefficients, std::string& error);
}
