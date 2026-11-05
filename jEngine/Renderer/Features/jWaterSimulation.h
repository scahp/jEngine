#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace jWaterSimulation
{
using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
inline constexpr std::uint32_t MaximumWaves = 16;

// Distances are caller world units, Speed is world units/second, time is seconds.
// Direction is normalized internally. phase=k*(dot(direction,xz)-Speed*t)+Phase.
// Steepness is Gerstner Q; Validate bounds sum(Q*k*Amplitude) to avoid folding.
struct Wave
{
    Vec2 Direction{1.f, 0.f};
    float Amplitude = 0.f, Wavelength = 10.f, Speed = 1.f;
    float Steepness = 0.f, PhaseRadians = 0.f;
    bool operator==(const Wave&) const = default;
};
struct Settings
{
    float BaseHeight = 0.f;
    std::vector<Wave> Waves;
    bool operator==(const Settings&) const = default;
};
struct SurfaceSample
{
    Vec3 Position{}, Normal{0.f, 1.f, 0.f};
    Vec3 TangentX{1.f, 0.f, 0.f}, TangentZ{0.f, 0.f, 1.f}, Velocity{};
};
struct Bounds { Vec3 Min{}, Max{}; };

bool Validate(const Settings& settings, std::string& error);
// Pure evaluation: no clock/integration state. A paused caller reuses its time.
// Returns analytic displaced position, parameter tangents, unit normal, velocity.
// All coordinates share the caller's surface frame. For an entity transformed
// after vertex deformation, query its mesh-local XZ and transform the result
// externally; this module does not silently apply an entity/world matrix.
// Invalid input leaves out unchanged.
bool Evaluate(const Settings& settings, const Vec2& worldXZ, double timeSeconds,
    SurfaceSample& out, std::string& error);
// Conservative for every time, over the supplied undeformed parameter rectangle.
bool ConservativeBounds(const Settings& settings, const Vec2& minXZ, const Vec2& maxXZ,
    Bounds& out, std::string& error);
}
