#include "jSubsurfaceDiffusion.h"
#include <algorithm>

namespace jSubsurface
{
bool BuildKernel(const Profile& profile, Kernel& output, std::string& error)
{
    if (!std::isfinite(profile.RadiusWorld) || profile.RadiusWorld <= 0.0f ||
        profile.RadiusWorld > 10000.0f)
    {
        error = "Subsurface profile support must be a positive finite world distance.";
        return false;
    }
    for (float channel : profile.Falloff)
    {
        if (!std::isfinite(channel) || channel < 0.05f || channel > 1.0f)
        {
            error = "Subsurface profile channel falloff must be finite and between 0.05 and 1.";
            return false;
        }
    }

    Kernel result{};
    constexpr int half = int(KernelSampleCount / 2);
    for (std::uint32_t i = 0; i < KernelSampleCount; ++i)
    {
        const float uniformOffset = float(int(i) - half) / float(half);
        result[i].Offset = uniformOffset * std::abs(uniformOffset);
    }

    // Integrate each lobe over the tap's actual bin instead of using point
    // density with a uniform area at these deliberately nonuniform offsets.
    constexpr std::array<double, 3> lobeSigma = {0.075, 0.20, 0.45};
    constexpr std::array<double, 3> lobeEnergy = {0.45, 0.35, 0.20};
    constexpr double invSqrtTwo = 0.7071067811865475244;
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        double total = 0.0;
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const double left = i == 0 ? -1.0 :
                0.5 * (result[i - 1].Offset + result[i].Offset);
            const double right = i + 1 == result.size() ? 1.0 :
                0.5 * (result[i].Offset + result[i + 1].Offset);
            double weight = 0.0;
            for (std::size_t lobe = 0; lobe < lobeSigma.size(); ++lobe)
            {
                const double scale = invSqrtTwo /
                    (lobeSigma[lobe] * double(profile.Falloff[channel]));
                weight += lobeEnergy[lobe] * 0.5 *
                    (std::erf(right * scale) - std::erf(left * scale));
            }
            result[i].Weight[channel] = float(weight);
            total += weight;
        }
        if (!std::isfinite(total) || total <= 0.0)
        {
            error = "Subsurface profile integration did not produce finite energy.";
            return false;
        }
        for (auto& sample : result) sample.Weight[channel] /= float(total);
    }
    output = result;
    error.clear();
    return true;
}

bool ValidateExtent(std::uint32_t width, std::uint32_t height,
    std::size_t& scratchBytes, std::string& error)
{
    if (!width || !height || width > 8192 || height > 8192)
    {
        error = "Subsurface diffusion needs a nonempty viewport no larger than 8192 per axis.";
        return false;
    }
    const std::uint64_t bytes = std::uint64_t(width) * height * ScratchBytesPerPixel;
    if (bytes > ScratchTargetBudgetBytes)
    {
        error = "Subsurface diffusion exceeds its 256 MiB scratch-image budget.";
        return false;
    }
    scratchBytes = std::size_t(bytes);
    error.clear();
    return true;
}

float EffectiveRadiusWorld(const Profile& profile, const Settings& settings, float thicknessWorld)
{
    if (!HasEffect(settings) || !std::isfinite(thicknessWorld) || thicknessWorld <= 0.0f)
        return 0.0f;
    return std::min(profile.RadiusWorld * settings.RadiusScale, thicknessWorld);
}

float ProjectedRadiusPixels(float radiusWorld, float worldUnitsPerPixel, float maximumPixels)
{
    if (!std::isfinite(radiusWorld) || radiusWorld <= 0.0f ||
        !std::isfinite(worldUnitsPerPixel) || worldUnitsPerPixel <= 1.0e-12f ||
        !std::isfinite(maximumPixels) || maximumPixels <= 0.0f)
        return 0.0f;
    return std::min(radiusWorld / worldUnitsPerPixel,
        std::min(maximumPixels, MaximumRadiusPixels));
}

RGB ComposeDiffuse(const RGB& scene, const RGB& originalDiffuse,
    const RGB& filteredDiffuse, const RGB& mask, float strength)
{
    RGB result = scene;
    if (!std::isfinite(strength) || strength <= 0.0f) return result;
    for (std::size_t c = 0; c < 3; ++c)
    {
        if (!std::isfinite(mask[c]) || !std::isfinite(originalDiffuse[c]) ||
            !std::isfinite(filteredDiffuse[c])) continue;
        const float blend = std::clamp(mask[c], 0.0f, 1.0f) * std::min(strength, 1.0f);
        result[c] += blend * (filteredDiffuse[c] - originalDiffuse[c]);
    }
    return result;
}
}
