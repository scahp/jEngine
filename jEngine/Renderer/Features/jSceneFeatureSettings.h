#pragma once

#include "jEnvironmentLighting.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>

// Plain CPU snapshots. Asset paths and authoring state belong to the project,
// while command recording and temporary GPU resources belong to the renderer.
namespace jSceneFeatures
{
struct EnvironmentSettings
{
    bool Enabled = false;
    bool CombineWithDiffuseGI = false;
    float Intensity = 1.0f;
    // Orthonormal radiance SH, before cosine convolution. The default is a
    // uniform linear radiance of 0.1, so enabling it also works without a bake.
    jEnvironmentLighting::Coefficients Radiance = {
        jEnvironmentLighting::RGB{0.3544907702f, 0.3544907702f, 0.3544907702f}};
};

struct ReflectionSettings
{
    bool Enabled = false;
    bool FillHoles = true;
    // Reflector plane: dot(normal, worldPosition) - distance = 0.
    std::array<float, 4> Plane = {0.0f, 1.0f, 0.0f, 0.0f};
    float PlaneThickness = 5.0f;       // World units, not linear-depth units.
    float NormalThreshold = 0.75f;
    float RoughnessThreshold = 0.6f;
    float Intensity = 1.0f;
    float EdgeFade = 0.05f;            // Fraction of the viewport edge.
    float FallbackRoughnessLod = 5.0f;
    std::array<float, 3> FallbackRadiance = {0.03f, 0.04f, 0.06f};
};

struct AtmosphereSettings
{
    bool Enabled = false;
    bool UseSceneDirectionalLight = true;
    float Intensity = 1.0f;
    float MetersPerWorldUnit = 1.0f;
    float GroundHeight = 0.0f;         // World-space Y.
    float AtmosphereHeightMeters = 60000.0f;
    float MaxDistanceMeters = 100000.0f;
    float RayleighScaleHeightMeters = 8000.0f;
    float MieScaleHeightMeters = 1200.0f;
    std::array<float, 3> RayleighScattering = {5.8e-6f, 13.5e-6f, 33.1e-6f};
    std::array<float, 3> MieScattering = {2.1e-5f, 2.1e-5f, 2.1e-5f};
    float MieAnisotropy = 0.8f;
    // Direction points toward the sun. Used when no directional light exists,
    // or when following the scene's first directional light is disabled.
    std::array<float, 3> SunDirection = {0.2f, 0.8f, 0.4f};
    std::array<float, 3> SolarIrradiance = {20.0f, 20.0f, 20.0f};
    std::uint32_t ViewSamples = 16;
    std::uint32_t LightSamples = 4;
};

struct Settings
{
    EnvironmentSettings Environment;
    ReflectionSettings Reflection;
    AtmosphereSettings Atmosphere;
};

static_assert(std::is_trivially_copyable_v<Settings>);

// Diffuse GI validity is supplied by the renderer. Requested/enabled GI is not
// necessarily usable on the current renderer or current frame.
inline float EffectiveEnvironmentIntensity(const EnvironmentSettings& settings, bool diffuseGIActive)
{
    if (!settings.Enabled || (diffuseGIActive && !settings.CombineWithDiffuseGI))
        return 0.0f;
    return settings.Intensity;
}

inline const char* ReflectionUnavailableReason(bool forwardRenderer, bool memorylessGBuffer,
    std::uint32_t samples = 1)
{
    if (forwardRenderer)
        return "Pixel projected reflection requires Deferred normal, albedo and material buffers.";
    if (memorylessGBuffer)
        return "Pixel projected reflection requires stored GBuffer attachments.";
    if (samples != 1)
        return "Pixel projected reflection requires single-sample resolved scene inputs.";
    return nullptr;
}

// Transactional validation: a rejected snapshot leaves output unchanged.
// Normalize plane distance together with its normal, and normalize sun direction.
inline bool ValidateSettings(const Settings& input, Settings& output, std::string& error)
{
    auto bounded = [](float value, float minimum, float maximum)
    { return std::isfinite(value) && value >= minimum && value <= maximum; };
    auto reject = [&](const char* reason) { error = reason; return false; };
    if (!bounded(input.Environment.Intensity, 0.0f, 100.0f))
        return reject("Environment intensity must be finite and between 0 and 100.");
    for (const auto& coefficient : input.Environment.Radiance)
        for (float channel : coefficient)
            if (!bounded(channel, -1000000.0f, 1000000.0f))
                return reject("Environment SH coefficients must be finite and bounded.");
    const auto& reflection = input.Reflection;
    float planeLengthSquared = 0.0f;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (!bounded(reflection.Plane[i], -10000000.0f, 10000000.0f))
            return reject("Reflection plane components must be finite and bounded.");
        if (i < 3) planeLengthSquared += reflection.Plane[i] * reflection.Plane[i];
    }
    if (planeLengthSquared < 1.0e-12f ||
        !bounded(reflection.PlaneThickness, 0.0001f, 100000.0f) ||
        !bounded(reflection.NormalThreshold, 0.0f, 1.0f) ||
        !bounded(reflection.RoughnessThreshold, 0.0f, 1.0f) ||
        !bounded(reflection.Intensity, 0.0f, 10.0f) ||
        !bounded(reflection.EdgeFade, 0.0f, 0.5f) ||
        !bounded(reflection.FallbackRoughnessLod, 0.0f, 16.0f))
        return reject("Reflection plane or filter settings are outside their supported range.");
    for (float channel : reflection.FallbackRadiance)
        if (!bounded(channel, 0.0f, 1000000.0f))
            return reject("Reflection fallback radiance must be finite and nonnegative.");
    const auto& atmosphere = input.Atmosphere;
    if (!bounded(atmosphere.Intensity, 0.0f, 100.0f) ||
        !bounded(atmosphere.MetersPerWorldUnit, 0.000001f, 100000.0f) ||
        !bounded(atmosphere.GroundHeight, -10000000.0f, 10000000.0f) ||
        !bounded(atmosphere.AtmosphereHeightMeters, 100.0f, 100000.0f) ||
        !bounded(atmosphere.MaxDistanceMeters, 1.0f, 1000000.0f) ||
        !bounded(atmosphere.RayleighScaleHeightMeters, 1.0f, 100000.0f) ||
        !bounded(atmosphere.MieScaleHeightMeters, 1.0f, 100000.0f) ||
        !bounded(atmosphere.MieAnisotropy, -0.95f, 0.95f) ||
        atmosphere.ViewSamples < 4 || atmosphere.ViewSamples > 64 ||
        atmosphere.LightSamples < 1 || atmosphere.LightSamples > 16)
        return reject("Atmosphere distances, density, phase or sample count are outside their supported range.");
    float sunLengthSquared = 0.0f;
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (!bounded(atmosphere.RayleighScattering[i], 0.0f, 0.01f) ||
            !bounded(atmosphere.MieScattering[i], 0.0f, 0.01f) ||
            !bounded(atmosphere.SolarIrradiance[i], 0.0f, 1000000.0f) ||
            !bounded(atmosphere.SunDirection[i], -1000000.0f, 1000000.0f))
            return reject("Atmosphere scattering, sun direction and radiance must be finite and bounded.");
        sunLengthSquared += atmosphere.SunDirection[i] * atmosphere.SunDirection[i];
    }
    if (sunLengthSquared < 1.0e-12f)
        return reject("Atmosphere sun direction must be nonzero.");
    Settings validated = input;
    const float planeLength = std::sqrt(planeLengthSquared);
    for (float& component : validated.Reflection.Plane) component /= planeLength;
    if (!bounded(validated.Reflection.Plane[3], -10000000.0f, 10000000.0f))
        return reject("Normalized reflection plane distance is outside the supported world range.");
    const float sunLength = std::sqrt(sunLengthSquared);
    for (float& component : validated.Atmosphere.SunDirection) component /= sunLength;
    output = validated;
    error.clear();
    return true;
}
}
