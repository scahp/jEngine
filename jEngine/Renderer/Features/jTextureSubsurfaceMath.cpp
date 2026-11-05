#include "jTextureSubsurfaceMath.h"
#include <algorithm>

namespace jTextureSubsurface
{
std::array<RGB, 6> NormalizedLobeWeights()
{
    auto weights = RawLobeWeights;
    RGB sums{};
    for (const auto& w : weights) for (int c=0;c<3;++c) sums[c] += w[c];
    for (auto& w : weights) for (int c=0;c<3;++c) w[c] /= sums[c];
    return weights;
}
float IncrementalSigmaMm(std::uint32_t level)
{
    return level > 0 && level < VarianceMm2.size() ?
        std::sqrt(VarianceMm2[level] - VarianceMm2[level-1]) : 0.f;
}
float RangeAttenuation(float d2, float maximumDistance)
{
    if (!std::isfinite(d2) || d2 < 0.f || !std::isfinite(maximumDistance) || maximumDistance < 0.f) return 0.f;
    if (maximumDistance == 0.f) return 1.f;
    const float x = d2 / (maximumDistance * maximumDistance);
    const float window = std::clamp(1.f - x*x, 0.f, 1.f);
    return window*window;
}
float CorrectedThicknessMm(float world, float units, float noL, float normalDot)
{
    if (!std::isfinite(world) || !std::isfinite(units) || !std::isfinite(noL) ||
        !std::isfinite(normalDot) || world < 0.f || units <= 0.f) return 0.f;
    const float backFacing = std::clamp(-normalDot, 0.f, 1.f);
    return (world / units) * (1.f + backFacing * (std::clamp(-noL, 0.f, 1.f) - 1.f));
}
float EncodeTransmission(float thickness, float extinction, float scale)
{
    if (!std::isfinite(thickness) || !std::isfinite(extinction) || !std::isfinite(scale) ||
        thickness < 0.f || extinction <= 0.f || scale < 0.f) return 0.f;
    return std::exp(-std::min(80.f, thickness*extinction*scale));
}
float DecodeTransmission(float transmission, float extinction)
{
    if (!std::isfinite(transmission) || !std::isfinite(extinction) || extinction <= 0.f) return 0.f;
    return -std::log(std::clamp(transmission, .000001f, 1.f))/extinction;
}
bool ValidateWorkExtent(std::uint32_t count, std::uint32_t tile, std::uint32_t capture,
    std::uint32_t width, std::uint32_t height, std::size_t& bytes, std::string& error)
{
    if (!count || count > MaximumSurfaces || !tile || tile > 1024 || !capture || capture > 2048 ||
        !width || !height || width > 8192 || height > 8192)
    { error="Texture diffusion extent or surface count is outside the supported bounds."; return false; }
    const std::uint64_t columns=count>1?2:1, rows=count>2?2:1;
    // 6 irradiance + stretch + entryUV + finish + ping/finalUV + coverage,
    // all RGBA16F; capture RGBA32F + RGBA16F + D32; HDR + completed diffuse RGBA16F;
    // R32UI per-surface rejection flags (four fixed entries).
    const std::uint64_t candidate=columns*rows*tile*tile*11u*8u+
        std::uint64_t(capture)*capture*28u+std::uint64_t(width)*height*16u+16u;
    if (candidate > MaximumWorkBytes)
    { error="Texture diffusion exceeds its 512MiB per-view scratch budget; screen diffusion retained."; return false; }
    bytes=std::size_t(candidate); error.clear(); return true;
}
bool ReceiverPlaneDistance(const RGB& origin,const RGB& ray,const RGB& point,
    const RGB& normal,float maximumDistance,float& output)
{
    if(!std::isfinite(maximumDistance)||maximumDistance<=0.f)return false;
    float numerator=0.f,denominator=0.f;
    for(std::size_t i=0;i<3;++i)
    {
        if(!std::isfinite(origin[i])||!std::isfinite(ray[i])||!std::isfinite(point[i])||!std::isfinite(normal[i]))return false;
        numerator+=normal[i]*(point[i]-origin[i]);denominator+=normal[i]*ray[i];
    }
    if(!std::isfinite(denominator)||std::abs(denominator)<1.e-4f)return false;
    const float candidate=numerator/denominator;
    if(!std::isfinite(candidate)||candidate<=0.f||candidate>maximumDistance)return false;
    output=candidate;return true;
}
}
