#pragma once

#include "jTextureSubsurfaceSettings.h"
#include <array>

namespace jTextureSubsurface
{
using RGB = std::array<float, 3>;
// Six Gaussian lobes of the d'Eon skin approximation. Variance is in mm^2;
// each channel is independently normalized before upload. The five stored
// convolutions use sqrt(delta variance), not a variance as a sample distance.
inline constexpr std::array<float, 6> VarianceMm2{0.f, .0484f, .187f, .567f, 1.99f, 7.41f};
inline constexpr std::array<RGB, 6> RawLobeWeights{{
    {.233f,.455f,.649f}, {.100f,.336f,.344f}, {.118f,.198f,0.f},
    {.133f,.007f,.007f}, {.358f,.004f,0.f}, {.078f,0.f,0.f}}};
std::array<RGB, 6> NormalizedLobeWeights();
float IncrementalSigmaMm(std::uint32_t level);
float RangeAttenuation(float distanceSquared, float maximumDistance);
// The sampled front-entry normal matters: substituting the exit normal makes
// this correction identically zero, as in the original demo's overwritten Ni.
float CorrectedThicknessMm(float geometricWorldThickness, float worldUnitsPerMm,
    float exitNoL, float entryExitNormalDot);
float EncodeTransmission(float thicknessMm, float extinctionPerMm, float scale);
float DecodeTransmission(float transmission, float extinctionPerMm);
// Unit ray, actual sampled triangle plane. Invalid/grazing/range failures
// preserve output so callers can choose an explicit local screen fallback.
bool ReceiverPlaneDistance(const RGB& origin,const RGB& ray,const RGB& entryPoint,
    const RGB& entryNormal,float maximumDistance,float& output);
bool ValidateWorkExtent(std::uint32_t surfaces, std::uint32_t tileResolution,
    std::uint32_t captureResolution, std::uint32_t width, std::uint32_t height,
    std::size_t& bytes, std::string& error);
}
