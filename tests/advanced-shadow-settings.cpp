#include "../jEngine/Renderer/Features/jAdvancedShadowSettings.h"
#include <array>
#include <iostream>
#include <limits>

int RunAdvancedShadowSettingsTests()
{
    using namespace jAdvancedShadows;
    int checks = 0, failures = 0;
    const auto expect = [&](bool passed, const char* label)
    { ++checks; if (!passed) { ++failures; std::cerr << "FAIL advanced shadows: " << label << '\n'; } };
    Settings output; output.MapResolution = 128;
    std::string error;
    expect(ValidateSettings({}, output, error) && error.empty() && output == Settings{}
        && output.DirectionalProjection == Projection::Single && output.PointProjection == PointProjection::Cube
        && output.Method == Representation::Depth, "defaults preserve the existing shadow path");
    struct Bound { const char* Name; float Settings::* Field; float Low, High; };
    const Bound bounds[] = {
        {"SplitLambda", &Settings::SplitLambda, 0, 1}, {"MaxDistance", &Settings::MaxDistance, .01f, 1e6f},
        {"CasterPadding", &Settings::CasterPadding, 0, 1e5f}, {"PositiveExponent", &Settings::PositiveExponent, .1f, 40},
        {"NegativeExponent", &Settings::NegativeExponent, .1f, 40}, {"MinimumVariance", &Settings::MinimumVariance, 1e-8f, 1},
        {"BleedReduction", &Settings::BleedReduction, 0, .999f}, {"DeepOpacityMultiplier", &Settings::DeepOpacityMultiplier, 0, 16},
        {"DeepTauMax", &Settings::DeepTauMax, .01f, 32}, {"DeepMaxDistance", &Settings::DeepMaxDistance, .01f, 1e6f},
        {"VolumeRange", &Settings::VolumeRange, 32, 1e6f}};
    const auto reject = [&](const Settings& input, const char* label)
    {
        const auto previous = output;
        expect(!ValidateSettings(input, output, error) && !error.empty() && output == previous, label);
    };
    for (const auto& field : bounds)
    {
        for (const float value : {field.Low, field.High})
        { Settings input; input.*field.Field = value; expect(ValidateSettings(input, output, error) && output == input, field.Name); }
        for (const float value : {std::nextafter(field.Low, -std::numeric_limits<float>::infinity()),
            std::nextafter(field.High, std::numeric_limits<float>::infinity()),
            std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
        { Settings input; input.*field.Field = value; reject(input, field.Name); }
    }
    for (const auto invalid : {3u, UINT32_MAX})
    { Settings input; input.DirectionalProjection = Projection(invalid); reject(input, "invalid directional enum"); }
    for (const auto invalid : {2u, UINT32_MAX})
    { Settings input; input.PointProjection = PointProjection(invalid); reject(input, "invalid point enum"); }
    for (const auto invalid : {6u, UINT32_MAX})
    { Settings input; input.Method = Representation(invalid); reject(input, "invalid representation enum"); }
    for (std::uint32_t value : {64u, 128u, 256u, 512u, 1024u})
    { Settings input; input.MapResolution = value; expect(ValidateSettings(input, output, error), "supported map tile size"); }
    for (std::uint32_t value : {0u, 63u, 96u, 1025u, UINT32_MAX})
    { Settings input; input.MapResolution = value; reject(input, "tile size requires a bounded power of two"); }
    for (std::uint32_t value : {0u, 5u, UINT32_MAX})
    { Settings input; input.CascadeCount = value; reject(input, "bounded cascade count"); }
    for (std::uint32_t value : {9u, UINT32_MAX})
    { Settings input; input.BlurRadius = value; reject(input, "bounded blur radius"); }
    for (std::uint32_t value : {0u, 65537u, UINT32_MAX})
    { Settings input; input.VolumeMaxTriangles = value; reject(input, "bounded source triangle budget"); }
    for (std::uint32_t value : {1u, 4u})
    { Settings input; input.CascadeCount = value; expect(ValidateSettings(input, output, error), "inclusive cascade endpoints"); }
    for (std::uint32_t value : {0u, 8u})
    { Settings input; input.BlurRadius = value; expect(ValidateSettings(input, output, error), "inclusive blur endpoints"); }
    for (std::uint32_t value : {1u, 65536u})
    { Settings input; input.VolumeMaxTriangles = value; expect(ValidateSettings(input, output, error), "inclusive triangle endpoints"); }
    for (std::uint32_t method = 0; method < 6; ++method)
        for (std::uint32_t directional = 0; directional < 3; ++directional)
            for (std::uint32_t point = 0; point < 2; ++point)
                for (std::uint32_t kind = 0; kind < 3; ++kind)
                {
                    Settings input; input.Method = Representation(method); input.DirectionalProjection = Projection(directional);
                    input.PointProjection = PointProjection(point);
                    const bool allowed = method != 5 || (kind != 0 || directional == 0) && (kind != 1 || point == 0);
                    expect(ValidateForLight(input, LightKind(kind), error) == allowed && (allowed ? error.empty() : !error.empty()),
                        "full representation/projection/light-type compatibility matrix");
                }
    expect(!ValidateForLight({}, LightKind(3), error) && !error.empty(), "unsupported light kind rejects");
    Settings snapshot; snapshot.DeepTauMax = std::numeric_limits<float>::quiet_NaN();
    expect(!ValidateForLight(snapshot, LightKind::Directional, error), "inactive deep parameters are validated for a depth light");
    std::cout << "Advanced shadow settings: " << checks << " checks, " << failures << " failures\n";
    return failures;
}

#ifdef JENGINE_ADVANCED_SHADOW_SETTINGS_TEST_MAIN
int main() { return RunAdvancedShadowSettingsTests() ? 1 : 0; }
#endif
