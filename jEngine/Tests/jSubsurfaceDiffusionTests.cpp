#include "jSubsurfaceDiffusionTests.h"
#include "Renderer/Features/jSubsurfaceDiffusion.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>

int RunSubsurfaceDiffusionTests()
{
    using namespace jSubsurface;
    int checks = 0, failures = 0;
    auto require = [&](bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL subsurface: " << message << '\n';
        }
    };
    auto near = [](float a, float b) { return std::abs(a - b) <= 2.0e-6f; };
    std::string error;
    Settings settings, validated;
    require(!HasEffect(settings), "default is off");
    require(ValidateSettings(settings, validated, error), "defaults validate");
    settings.Enabled = true;
    require(HasEffect(settings), "explicit enable has an effect");
    for (int zero = 0; zero < 3; ++zero)
    {
        auto off = settings;
        if (zero == 0) off.Strength = 0.0f;
        if (zero == 1) off.RadiusScale = 0.0f;
        if (zero == 2) off.MaxRadiusPixels = 0.0f;
        require(!HasEffect(off), "zero controls disable all pass work");
    }
    for (int invalid = 0; invalid < 7; ++invalid)
    {
        auto bad = settings;
        if (invalid == 0) bad.Strength = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 1) bad.Strength = 1.01f;
        if (invalid == 2) bad.RadiusScale = -1.0f;
        if (invalid == 3) bad.DepthRejectWorld = 0.0f;
        if (invalid == 4) bad.NormalThreshold = 1.01f;
        if (invalid == 5) bad.MaxRadiusPixels = MaximumRadiusPixels + 1.0f;
        if (invalid == 6) bad.MaxRadiusPixels = std::numeric_limits<float>::infinity();
        validated.Strength = 0.125f;
        require(!ValidateSettings(bad, validated, error) && validated.Strength == 0.125f,
            "invalid snapshots reject transactionally");
    }

    std::array<Kernel, MaximumProfiles> kernels;
    for (std::size_t profile = 0; profile < Profiles.size(); ++profile)
    {
        require(BuildKernel(Profiles[profile], kernels[profile], error), "all stable presets build");
        const auto& kernel = kernels[profile];
        bool finite = true, symmetric = true;
        RGB total{}, firstMoment{}, secondMoment{};
        for (std::size_t i = 0; i < kernel.size(); ++i)
        {
            const auto& sample = kernel[i];
            symmetric = symmetric && near(sample.Offset, -kernel[kernel.size() - 1 - i].Offset);
            for (std::size_t c = 0; c < 3; ++c)
            {
                finite = finite && std::isfinite(sample.Weight[c]) && sample.Weight[c] >= 0.0f;
                symmetric = symmetric && near(sample.Weight[c], kernel[kernel.size() - 1 - i].Weight[c]);
                total[c] += sample.Weight[c];
                firstMoment[c] += sample.Weight[c] * sample.Offset;
                secondMoment[c] += sample.Weight[c] * sample.Offset * sample.Offset;
            }
        }
        require(finite && symmetric, "finite positive mirror-symmetric profile");
        require(near(total[0], 1.0f) && near(total[1], 1.0f) && near(total[2], 1.0f),
            "unit channel energy preserves constant diffuse fields");
        require(near(firstMoment[0], 0.0f) && near(firstMoment[1], 0.0f) && near(firstMoment[2], 0.0f),
            "symmetric impulse has no spatial shift");
        require(secondMoment[0] > 0.0f && secondMoment[1] > 0.0f && secondMoment[2] > 0.0f,
            "each channel spreads an impulse");
        require(kernel.front().Offset == -1.0f && kernel.back().Offset == 1.0f &&
            kernel[KernelSampleCount / 2].Offset == 0.0f, "bounded support includes exact center");

        // An impulse transported by the two actual separable axis kernels has
        // unit total energy in each channel, independently of profile colour.
        RGB transported{};
        for (const auto& x : kernel)
            for (const auto& y : kernel)
                for (std::size_t c = 0; c < 3; ++c) transported[c] += x.Weight[c] * y.Weight[c];
        require(near(transported[0], 1.0f) && near(transported[1], 1.0f) && near(transported[2], 1.0f),
            "two-dimensional impulse energy is conserved");

        // With every neighboring sample rejected by a boundary, its weight is
        // returned to the center rather than normalized against unrelated data.
        const RGB center = {0.125f, 0.75f, 2.0f};
        RGB edge{};
        for (const auto& sample : kernel)
            for (std::size_t c = 0; c < 3; ++c) edge[c] += sample.Weight[c] * center[c];
        require(near(edge[0], center[0]) && near(edge[1], center[1]) && near(edge[2], center[2]),
            "fully rejected boundary preserves its own diffuse energy");
    }
    auto variance = [](const Kernel& kernel, std::size_t channel)
    {
        float value = 0.0f;
        for (const auto& sample : kernel) value += sample.Weight[channel] * sample.Offset * sample.Offset;
        return value;
    };
    require(variance(kernels[0], 0) > variance(kernels[0], 1) &&
        variance(kernels[0], 1) > variance(kernels[0], 2), "skin spreads red farther than green and blue");
    require(near(variance(kernels[7], 0), variance(kernels[7], 1)) &&
        near(variance(kernels[7], 1), variance(kernels[7], 2)), "neutral profile has no chromatic spread");
    require(variance(kernels[1], 1) > variance(kernels[0], 1), "non-skin profile changes the diffusion distribution");
    Kernel unchanged = kernels[0];
    const auto before = unchanged;
    Profile invalidProfile = Profiles[0];
    invalidProfile.Falloff[1] = 0.0f;
    require(!BuildKernel(invalidProfile, unchanged, error) &&
        std::memcmp(&unchanged, &before, sizeof(Kernel)) == 0, "invalid profile leaves last-good kernel intact");

    require(near(EffectiveRadiusWorld(Profiles[0], settings, 0.5f), 0.5f), "authored thickness bounds diffusion support");
    require(near(EffectiveRadiusWorld(Profiles[0], settings, 100.0f), 1.2f), "profile bounds thick-material support");
    require(EffectiveRadiusWorld(Profiles[0], settings, 0.0f) == 0.0f &&
        EffectiveRadiusWorld(Profiles[0], settings, std::numeric_limits<float>::quiet_NaN()) == 0.0f,
        "zero and invalid thickness cannot produce a radius");
    require(ProjectedRadiusPixels(2.0f, 0.25f, 32.0f) == 8.0f &&
        ProjectedRadiusPixels(2.0f, 0.5f, 32.0f) == 4.0f,
        "world-to-pixel radius follows projected scale");
    require(ProjectedRadiusPixels(1000.0f, 0.01f, 32.0f) == 32.0f &&
        ProjectedRadiusPixels(1000.0f, 0.01f, 1000.0f) == MaximumRadiusPixels,
        "near-camera radii obey both finite pixel budgets");
    require(ProjectedRadiusPixels(1.0f, 0.0f, 32.0f) == 0.0f,
        "degenerate inverse projection cannot create an unbounded filter");

    const RGB diffuse = {0.3f, 0.1f, 0.5f}, filtered = {0.1f, 0.25f, 0.2f};
    const RGB specularAndEmission = {5.0f, 0.7f, 2.0f};
    RGB scene;
    for (std::size_t c = 0; c < 3; ++c) scene[c] = diffuse[c] + specularAndEmission[c];
    const auto composed = ComposeDiffuse(scene, diffuse, filtered, RGB{1.0f, 1.0f, 1.0f}, 1.0f);
    require(near(composed[0] - filtered[0], specularAndEmission[0]) &&
        near(composed[1] - filtered[1], specularAndEmission[1]) &&
        near(composed[2] - filtered[2], specularAndEmission[2]),
        "diffuse replacement preserves the unfiltered specular/emissive remainder");
    require(ComposeDiffuse(scene, diffuse, filtered, RGB{1.0f, 1.0f, 1.0f}, 0.0f) == scene,
        "zero global strength is exact identity");
    require(ComposeDiffuse(scene, diffuse, filtered, RGB{}, 1.0f) == scene,
        "zero material strength is exact identity");
    const auto redOnly = ComposeDiffuse(scene, diffuse, filtered, RGB{0.5f, 0.0f, 0.0f}, 1.0f);
    require(near(redOnly[0], scene[0] + 0.5f * (filtered[0] - diffuse[0])) &&
        redOnly[1] == scene[1] && redOnly[2] == scene[2], "material colour controls redistribution per channel");

    std::size_t bytes = 123;
    require(ValidateExtent(3840, 2160, bytes, error) && bytes == std::size_t(3840) * 2160 * 24,
        "4K scratch accounting is explicit and within budget");
    bytes = 123;
    require(!ValidateExtent(8192, 8192, bytes, error) && bytes == 123,
        "large viewports fail before allocation with unchanged accounting");
    require(!ValidateExtent(std::numeric_limits<std::uint32_t>::max(), 2, bytes, error) &&
        !ValidateExtent(0, 1, bytes, error), "overflow/empty extents are rejected");

    std::cout << "Subsurface diffusion CPU: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

#if defined(JENGINE_SUBSURFACE_TEST_MAIN)
int main() { return RunSubsurfaceDiffusionTests(); }
#endif
