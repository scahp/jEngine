#include "Renderer/Features/jSceneFeatureSettings.h"
#include <cmath>
#include <iostream>
#include <limits>

int RunSceneFeatureTests()
{
    using namespace jSceneFeatures;
    int failures = 0;
    auto expect = [&](bool passed, const char* label)
    {
        std::cout << (passed ? "PASS " : "FAIL ") << label << '\n';
        failures += !passed;
    };
    Settings raw, validated;
    std::string error;
    expect(ValidateSettings(raw, validated, error) && error.empty(),
        "scene feature defaults form a valid CPU snapshot");
    expect(!raw.Environment.Enabled && !raw.Reflection.Enabled && !raw.Atmosphere.Enabled,
        "new scene features do not implicitly change an existing project's lighting");
    auto environment = raw.Environment;
    environment.Enabled = true; environment.Intensity = 2.0f;
    expect(EffectiveEnvironmentIntensity(environment, false) == 2.0f &&
        EffectiveEnvironmentIntensity(environment, true) == 0.0f,
        "SH ambient falls back only when actual diffuse GI is absent");
    environment.CombineWithDiffuseGI = true;
    expect(EffectiveEnvironmentIntensity(environment, true) == 2.0f,
        "explicit additive policy permits SH together with diffuse GI");
    environment.Enabled = false;
    expect(EffectiveEnvironmentIntensity(environment, false) == 0.0f,
        "disabled SH contributes no diffuse radiance");
    raw.Reflection.Plane = {0.0f, 2.0f, 0.0f, 6.0f};
    raw.Atmosphere.SunDirection = {0.0f, 4.0f, 3.0f};
    expect(ValidateSettings(raw, validated, error) && validated.Reflection.Plane[1] == 1.0f &&
        validated.Reflection.Plane[3] == 3.0f &&
        std::abs(validated.Atmosphere.SunDirection[1] - 0.8f) < 1.0e-6f &&
        std::abs(validated.Atmosphere.SunDirection[2] - 0.6f) < 1.0e-6f,
        "normalization preserves reflector geometry and the toward-sun direction");
    Settings secondValidation;
    expect(ValidateSettings(validated, secondValidation, error) &&
        secondValidation.Reflection.Plane == validated.Reflection.Plane,
        "a validated frame snapshot can be reused without changing its plane");
    const auto previous = validated;
    raw.Reflection.Plane = {0.0f, 0.0f, 0.0f, 1.0f};
    expect(!ValidateSettings(raw, validated, error) && !error.empty() &&
        validated.Reflection.Plane == previous.Reflection.Plane,
        "a degenerate reflector is rejected without replacing the last valid snapshot");
    raw = Settings{};
    raw.Environment.Radiance[7][2] = std::numeric_limits<float>::quiet_NaN();
    expect(!ValidateSettings(raw, validated, error) &&
        validated.Environment.Radiance == previous.Environment.Radiance,
        "nonfinite SH coefficient data is rejected before GPU upload");
    raw = Settings{}; raw.Atmosphere.MieAnisotropy = 1.0f;
    expect(!ValidateSettings(raw, validated, error),
        "a singular atmospheric phase setting is rejected");
    raw = Settings{}; raw.Atmosphere.LightSamples = 0;
    expect(!ValidateSettings(raw, validated, error),
        "zero optical-depth samples cannot reach shader division");
    raw = Settings{}; raw.Atmosphere.ViewSamples = 65;
    expect(!ValidateSettings(raw, validated, error),
        "atmosphere integration work remains within the declared sample budget");
    raw = Settings{}; raw.Atmosphere.MetersPerWorldUnit = std::numeric_limits<float>::infinity();
    expect(!ValidateSettings(raw, validated, error),
        "nonfinite unit conversion is rejected before reconstructing distances");
    raw = Settings{}; raw.Reflection.Plane = {0.0f, 0.00001f, 0.0f, 1000.0f};
    expect(!ValidateSettings(raw, validated, error),
        "normalization cannot publish a plane outside the declared world range");
    expect(ReflectionUnavailableReason(true, false) && ReflectionUnavailableReason(false, true) &&
        ReflectionUnavailableReason(false, false, 4) && !ReflectionUnavailableReason(false, false),
        "PPR capability reasons distinguish Forward, memoryless and multisample inputs");
    return failures ? 1 : 0;
}

#ifdef JENGINE_SCENE_FEATURE_TEST_STANDALONE
int main() { return RunSceneFeatureTests(); }
#endif
