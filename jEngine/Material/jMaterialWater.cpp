#include "pch.h"
#include "jMaterialWater.h"
#include "Shader/jCommonShaderParameters.h"

void FillMaterialWaterShaderParameters(const jMaterialWaterState* water,
    jMaterialGraphUniformBuffer& parameters)
{
    if (!water || !water->Enabled || !std::isfinite(water->TimeSeconds)
        || water->Settings.Waves.size() > 4) return;
    std::string error;
    if (!jWaterSimulation::Validate(water->Settings, error)) return;
    // Reduce the temporal phase in double precision on the owner thread. The
    // GPU evaluates only the spatial term, avoiding long-session float drift.
    parameters.WaterControl = Vector4(1.0f, water->Settings.BaseHeight,
        0.0f, static_cast<float>(water->Settings.Waves.size()));
    for (size_t i = 0; i < water->Settings.Waves.size(); ++i)
    {
        const auto& wave = water->Settings.Waves[i];
        const double length = std::hypot(double(wave.Direction[0]), double(wave.Direction[1]));
        parameters.WaterWavesA[i] = Vector4(float(wave.Direction[0] / length),
            float(wave.Direction[1] / length), wave.Amplitude, wave.Wavelength);
        constexpr double twoPi = 6.2831853071795864769;
        const double periodTime = wave.Speed == 0.0f ? 0.0 : std::remainder(water->TimeSeconds,
            double(wave.Wavelength) / std::abs(double(wave.Speed)));
        const double phase = std::remainder(double(wave.PhaseRadians)
            - twoPi / double(wave.Wavelength) * double(wave.Speed) * periodTime, twoPi);
        parameters.WaterWavesB[i] = Vector4(wave.Speed, wave.Steepness, float(phase), 0.0f);
    }
}
