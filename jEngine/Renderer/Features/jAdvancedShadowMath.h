#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// CPU-only shadow algorithms adapted from scahp/Shadows (MIT, Choi Jae Ho).
// No device, frame clock or mutable global state. Callers own all input/output
// storage. Rejected/cancelled calls leave outputs unchanged.
namespace jAdvancedShadowMath
{
using Vec2 = std::array<double, 2>;
using Vec3 = std::array<double, 3>;
using Vec4 = std::array<double, 4>;
// Column-major storage: m[column*4+row], column vectors, clip = M * world.
// Outputs use XY [-1,1], Z [0,1]; ReverseZ reverses only the output Z mapping.
using Mat4 = std::array<double, 16>;
using FrustumCorners = std::array<Vec3, 8>; // near 0..3, corresponding far 4..7.
inline constexpr std::uint32_t MaximumCascades = 4;
inline constexpr std::size_t MaximumVolumeTriangles = 65536;
inline constexpr std::size_t MaximumDeepLayers = 64;
inline constexpr double MaximumMomentExponent = 40.0; // Float4 second moments stay finite.

Mat4 Identity() noexcept;
Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept;
Vec4 Transform(const Mat4& matrix, const Vec4& value) noexcept;
bool ProjectPoint(const Mat4& matrix, const Vec3& point, Vec3& output) noexcept;

struct CascadeInput
{
    FrustumCorners Corners{};
    double NearDistance = 1.0, FarDistance = 1000.0;
    Vec3 LightDirection{0.0, -1.0, 0.0}; // Direction travelled by light rays.
    std::uint32_t Count = 4, Resolution = 2048;
    double SplitLambda = 0.5, CasterPadding = 100.0;
    bool Stabilize = true, ReverseZ = false;
};
struct Cascade
{
    Mat4 WorldToShadow = Identity();
    FrustumCorners Corners{};
    double NearDistance = 0.0, FarDistance = 0.0;
    double WorldUnitsPerTexel = 0.0;
};
struct CascadeResult
{
    std::uint32_t Count = 0;
    std::array<double, MaximumCascades + 1> Splits{};
    std::array<Cascade, MaximumCascades> Cascades{};
};
bool BuildCascades(const CascadeInput& input, CascadeResult& output, std::string& error);

struct PerspectiveInput
{
    Mat4 CameraViewProjection = Identity(); // Actual camera Z must already be [0,1].
    FrustumCorners Corners{};
    Vec3 LightDirection{0.0, -1.0, 0.0};
    double CasterPadding = 100.0; // World-space padding for the orthographic fallback.
    // Source PSM virtual-camera slide, in world units. The adapter can derive
    // this from the nearest positive caster view depth. Sliding both virtual
    // near/far distances with the eye preserves the real receiver clip planes;
    // neither the caller's camera matrix nor its world frustum is modified.
    double VirtualSlideBack = 0.0; // Finite [0,1e6]; zero preserves the raw warp.
    bool ReverseZ = false;
};
struct PerspectiveResult
{
    Mat4 WorldToShadow = Identity();
    bool OrthographicFallback = false;
    std::string FallbackReason;
};
// Post-perspective light transform: projectionPP * viewPP * cameraVP.
// Singular/infinite post-perspective lights or unsafe homogeneous clipping use
// an ordinary fitted directional projection, reported explicitly in the result.
bool BuildPerspectiveShadowMap(const PerspectiveInput& input, PerspectiveResult& output, std::string& error);

enum class Hemisphere : std::uint32_t { Front = 0, Back = 1 };
struct ParaboloidSample
{
    Hemisphere Face = Hemisphere::Front;
    Vec2 UV{0.5, 0.5}; // Per-face [0,1], +Y is not image-flipped here.
    double Depth = 0.0; // Linear radial (distance-near)/(far-near).
};
// Light-local XYZ; front is +Z, back is -Z. A point on the seam selects front.
bool ProjectDualParaboloid(const Vec3& lightLocal, double nearDistance, double farDistance,
    ParaboloidSample& output, std::string& error);
bool UnprojectDualParaboloid(const ParaboloidSample& sample, double nearDistance, double farDistance,
    Vec3& output, std::string& error);

struct TriangleMesh
{
    std::vector<Vec3> Positions;
    std::vector<std::uint32_t> Indices;
};
struct SubdivisionSettings
{
    double MaximumEdgeAngleDegrees = 15.0;
    std::uint32_t MaximumDepth = 5;
    std::size_t MaximumTriangles = 65536;
    std::function<bool()> Cancelled;
};
struct SubdivisionResult
{
    TriangleMesh Mesh;
    std::uint32_t Passes = 0;
};
// Light-local input. Shared marked edges receive the same midpoint, including
// neighboring triangles; this avoids projection cracks from T-junctions.
// Exceeding the angular/depth/triangle budget fails instead of silently drawing
// an under-tessellated projection. Hemisphere clipping remains with the caller.
bool SubdivideForDualParaboloid(std::span<const Vec3> positions,
    std::span<const std::uint32_t> indices, SubdivisionResult& output,
    std::string& error, const SubdivisionSettings& settings = {});

struct VolumeInput
{
    std::span<const Vec3> Positions;
    std::span<const std::uint32_t> Indices; // Outward CCW in the supplied coordinate frame.
    Vec3 Light{0.0, -1.0, 0.0}; // Point position or ray direction, according to PointLight.
    bool PointLight = false, WeldCoincidentPositions = true;
    double ExtrusionDistance = 1000.0; // Finite displacement from each original vertex.
};
struct VolumeLimits
{
    std::size_t MaxInputTriangles = 20000, MaxOutputTriangles = 160000;
    std::function<bool()> Cancelled;
};
struct VolumeMesh
{
    std::vector<Vec3> Positions;
    std::vector<std::uint32_t> Indices;
    std::vector<std::array<std::uint32_t, 2>> SilhouetteEdges;
    std::size_t NearCapIndexCount = 0, FarCapFirstIndex = 0, FarCapIndexCount = 0;
    std::size_t SideFirstIndex = 0, SideIndexCount = 0;
};
// Requires a closed consistently outward oriented triangle 2-manifold. Exact
// position welding supports normal/UV seams without fuzzy topology changes.
// Degenerate, open, pinched/nonmanifold meshes and point lights on/inside the
// mesh fail explicitly. This produces geometry only; stencil/depth-clamp and
// camera near/far clipping policy remain the renderer's responsibility.
bool BuildShadowVolume(const VolumeInput& input, VolumeMesh& output, std::string& error,
    const VolumeLimits& limits = {});

// Shared CPU references: canonical forward projected depth [0,1]. Producer and
// receiver must use the same depth mapping (including projective PSM depth).
// Exponents are [0.1,40]. Encoders return point moments (zero depth gradient).
// Readers accept linearly filtered/derivative-expanded second moments; EVSM's
// minimum depth variance is scaled by each receiver warp derivative squared.
bool VsmMoments(double depth, Vec2& output, std::string& error);
bool EsmMoment(double depth, double exponent, double& output, std::string& error);
bool EvsmMoments(double depth, double positiveExponent, double negativeExponent,
    Vec4& output, std::string& error);
bool VsmVisibility(const Vec2& moments, double receiverDepth, double minimumVariance,
    double bleedingReduction, double& output, std::string& error);
bool EsmVisibility(double moment, double receiverDepth, double exponent,
    double& output, std::string& error);
bool EvsmVisibility(const Vec4& moments, double receiverDepth, double positiveExponent, double negativeExponent,
    double minimumVariance, double bleedingReduction, double& output, std::string& error);

struct DeepLayer { double Depth = 0.0, Opacity = 0.0; };
struct DeepKnot { double Depth = 0.0, Transmittance = 1.0; };
// Sorted step curve, coalescing equal-depth fragments; T = product(1-opacity).
bool BuildDeepCurve(std::span<const DeepLayer> layers, std::vector<DeepKnot>& output,
    std::string& error, std::size_t maxLayers = MaximumDeepLayers);
bool DeepTransmittance(std::span<const DeepKnot> curve, double receiverDepth,
    double& output, std::string& error);
using DeepTauBins = std::array<double, 8>; // Per-bin optical depth, not a prefix sum.
// Per-fragment tau=min(-log(max(1-clamp(opacity*multiplier,0,1),1e-7)),16),
// accumulated per bin. Bins are not pre-clamped before fractional evaluation.
// Evaluation includes previous bins and the linear fraction of the current bin,
// then exp(-min(totalTau,maxTau)). Same bounded eight-bin reference as the GPU.
bool BuildDeepTauBins(std::span<const DeepLayer> layers, double opacityMultiplier,
    double maxTau, DeepTauBins& output, std::string& error,
    std::size_t maxLayers = MaximumDeepLayers);
bool EvaluateDeepTauBins(const DeepTauBins& bins, double receiverDepth, double maxTau,
    double& output, std::string& error);
}
