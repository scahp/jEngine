#include "jWaterSimulation.h"

#include <algorithm>
#include <cmath>

// Reference: scahp/Shadows (MIT),
// WaveWithPhysicalModel@6ae2d30a0e35dd02761127ef419246e338f5c414.
// This is bounded analytic Gerstner evaluation, not a fluid/boat simulation.
namespace jWaterSimulation
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double MaximumWorldCoordinate = 1e9;
constexpr double MaximumWaveDistance = 1e6;
constexpr double MaximumTimeSeconds = 1e12;

bool FinitePosition(const Vec2& value)
{
    return std::isfinite(value[0]) && std::isfinite(value[1])
        && std::abs(double(value[0])) <= MaximumWorldCoordinate
        && std::abs(double(value[1])) <= MaximumWorldCoordinate;
}

Vec3 ToFloat(const std::array<double, 3>& value)
{ return {static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2])}; }
}

bool Validate(const Settings& settings, std::string& error)
{
    error.clear();
    if (!std::isfinite(settings.BaseHeight) || std::abs(double(settings.BaseHeight)) > MaximumWorldCoordinate
        || settings.Waves.size() > MaximumWaves)
    { error = "Water requires a finite base height and at most 16 waves."; return false; }
    double folding = 0., verticalExtent = 0.;
    for (const auto& wave : settings.Waves)
    {
        const double directionLength = std::hypot(double(wave.Direction[0]), double(wave.Direction[1]));
        if (!std::isfinite(directionLength) || directionLength <= 0.
            || !std::isfinite(wave.Amplitude) || wave.Amplitude < 0.f || wave.Amplitude > MaximumWaveDistance
            || !std::isfinite(wave.Wavelength) || wave.Wavelength < 1e-4f || wave.Wavelength > MaximumWorldCoordinate
            || !std::isfinite(wave.Speed) || std::abs(double(wave.Speed)) > MaximumWaveDistance
            || !std::isfinite(wave.Steepness) || wave.Steepness < 0.f || wave.Steepness > 1.f
            || !std::isfinite(wave.PhaseRadians) || std::abs(double(wave.PhaseRadians)) > MaximumWorldCoordinate)
        { error = "Water wave direction/units are invalid or a wave parameter exceeds its bounds."; return false; }
        folding += double(wave.Steepness)*(2.*Pi/wave.Wavelength)*wave.Amplitude;
        verticalExtent += wave.Amplitude;
    }
    if (folding > .95 || verticalExtent > MaximumWaveDistance)
    { error = "Water wave aggregate exceeds non-folding steepness or displacement bounds."; return false; }
    return true;
}

bool Evaluate(const Settings& settings, const Vec2& worldXZ, double timeSeconds,
    SurfaceSample& out, std::string& error)
{
    if (!Validate(settings, error)) return false;
    if (!FinitePosition(worldXZ) || !std::isfinite(timeSeconds) || std::abs(timeSeconds) > MaximumTimeSeconds)
    { error = "Water sample position/time is nonfinite or out of bounds."; return false; }
    std::array<double, 3> position{worldXZ[0], settings.BaseHeight, worldXZ[1]};
    std::array<double, 3> dx{1., 0., 0.}, dz{0., 0., 1.}, velocity{};
    for (const auto& wave : settings.Waves)
    {
        if (wave.Amplitude == 0.f) continue;
        const double directionLength = std::hypot(double(wave.Direction[0]), double(wave.Direction[1]));
        const double x = wave.Direction[0]/directionLength, z = wave.Direction[1]/directionLength;
        const double k = 2.*Pi/wave.Wavelength, omega = k*wave.Speed;
        // Reduce time before multiplying, retaining a small periodic phase even
        // for long-running scenes. No accumulated timestep/integration error.
        const double periodicTime = wave.Speed == 0.f ? 0.
            : std::remainder(timeSeconds, double(wave.Wavelength)/std::abs(double(wave.Speed)));
        const double phase = std::remainder(k*(x*worldXZ[0]+z*worldXZ[1])-omega*periodicTime
            + wave.PhaseRadians, 2.*Pi);
        const double sine = std::sin(phase), cosine = std::cos(phase);
        const double amplitude = wave.Amplitude, horizontal = wave.Steepness*amplitude;
        position[0] += horizontal*x*cosine;
        position[1] += amplitude*sine;
        position[2] += horizontal*z*cosine;
        dx[0] -= horizontal*k*x*x*sine;
        dx[1] += amplitude*k*x*cosine;
        dx[2] -= horizontal*k*x*z*sine;
        dz[0] -= horizontal*k*x*z*sine;
        dz[1] += amplitude*k*z*cosine;
        dz[2] -= horizontal*k*z*z*sine;
        velocity[0] += horizontal*x*omega*sine;
        velocity[1] -= amplitude*omega*cosine;
        velocity[2] += horizontal*z*omega*sine;
    }
    // cross(dP/dz,dP/dx) yields +Y for a flat surface.
    std::array<double, 3> normal{dz[1]*dx[2]-dz[2]*dx[1],
        dz[2]*dx[0]-dz[0]*dx[2], dz[0]*dx[1]-dz[1]*dx[0]};
    const double length = std::hypot(std::hypot(normal[0], normal[1]), normal[2]);
    if (!std::isfinite(length) || length <= 0.)
    { error = "Water surface normal is degenerate."; return false; }
    for (auto& component : normal) component /= length;
    SurfaceSample candidate;
    candidate.Position = ToFloat(position); candidate.Normal = ToFloat(normal);
    candidate.TangentX = ToFloat(dx); candidate.TangentZ = ToFloat(dz);
    candidate.Velocity = ToFloat(velocity);
    for (const auto* value : {&candidate.Position, &candidate.Normal, &candidate.TangentX,
        &candidate.TangentZ, &candidate.Velocity})
        for (const float component : *value)
            if (!std::isfinite(component))
            { error = "Water evaluation overflowed."; return false; }
    out = candidate; return true;
}

bool ConservativeBounds(const Settings& settings, const Vec2& minXZ, const Vec2& maxXZ,
    Bounds& out, std::string& error)
{
    if (!Validate(settings, error)) return false;
    if (!FinitePosition(minXZ) || !FinitePosition(maxXZ) || minXZ[0] > maxXZ[0] || minXZ[1] > maxXZ[1])
    { error = "Water bounds require an ordered finite world rectangle."; return false; }
    double horizontalX = 0., horizontalZ = 0., vertical = 0.;
    for (const auto& wave : settings.Waves)
    {
        const double length = std::hypot(double(wave.Direction[0]), double(wave.Direction[1]));
        horizontalX += wave.Steepness*double(wave.Amplitude)*std::abs(wave.Direction[0]/length);
        horizontalZ += wave.Steepness*double(wave.Amplitude)*std::abs(wave.Direction[1]/length);
        vertical += wave.Amplitude;
    }
    Bounds candidate;
    // Round outwards when converting double extrema to GPU/gameplay float AABBs.
    const auto lower = [](double value) { return std::nextafter(static_cast<float>(value), -INFINITY); };
    const auto upper = [](double value) { return std::nextafter(static_cast<float>(value), INFINITY); };
    candidate.Min = {lower(minXZ[0]-horizontalX), lower(settings.BaseHeight-vertical), lower(minXZ[1]-horizontalZ)};
    candidate.Max = {upper(maxXZ[0]+horizontalX), upper(settings.BaseHeight+vertical), upper(maxXZ[1]+horizontalZ)};
    out = candidate; return true;
}
}
