#pragma once

#include "Math/Vector.h"
#include "Math/Matrix.h"

//////////////////////////////////////////////////////////////////////////
// Lightcuts Common Types and Constants
//////////////////////////////////////////////////////////////////////////

// Light types used in light tree
enum class ELightcutLightType : uint8
{
	Omni,           // Point light radiating equally in all directions
	Oriented,       // Directional emission with cosine-weighted hemisphere
	Directional,    // Infinitely far source (parallel rays)
};

// Axis-aligned bounding box
struct jBoundingBox
{
	Vector Min = Vector::ZeroVector;
	Vector Max = Vector::ZeroVector;

	jBoundingBox() = default;
	jBoundingBox(const Vector& InMin, const Vector& InMax)
		: Min(InMin), Max(InMax)
	{}

	// Create from a single point
	static jBoundingBox FromPoint(const Vector& Point)
	{
		return jBoundingBox(Point, Point);
	}

	// Merge two bounding boxes
	static jBoundingBox Merge(const jBoundingBox& A, const jBoundingBox& B)
	{
		Vector NewMin(
			fminf(A.Min.x, B.Min.x),
			fminf(A.Min.y, B.Min.y),
			fminf(A.Min.z, B.Min.z)
		);
		Vector NewMax(
			fmaxf(A.Max.x, B.Max.x),
			fmaxf(A.Max.y, B.Max.y),
			fmaxf(A.Max.z, B.Max.z)
		);
		return jBoundingBox(NewMin, NewMax);
	}

	// Get center point
	Vector GetCenter() const
	{
		return (Min + Max) * 0.5f;
	}

	// Get diagonal length
	float GetDiagonal() const
	{
		return (Max - Min).Length();
	}

	// Get the closest point on the box to a given point
	Vector GetClosestPoint(const Vector& Point) const
	{
		return Vector(
			fmaxf(Min.x, fminf(Point.x, Max.x)),
			fmaxf(Min.y, fminf(Point.y, Max.y)),
			fmaxf(Min.z, fminf(Point.z, Max.z))
		);
	}

	// Expand to include a point
	void ExpandToInclude(const Vector& Point)
	{
		Min.x = fminf(Min.x, Point.x);
		Min.y = fminf(Min.y, Point.y);
		Min.z = fminf(Min.z, Point.z);
		Max.x = fmaxf(Max.x, Point.x);
		Max.y = fmaxf(Max.y, Point.y);
		Max.z = fmaxf(Max.z, Point.z);
	}
};

// Bounding cone for oriented lights
struct jBoundingCone
{
	Vector Axis = Vector::ZeroVector;  // Central axis of the cone
	float HalfAngle = 0.0f;            // Cone half-angle in radians (0 = collapsed to axis, PI = hemisphere)

	jBoundingCone() = default;
	jBoundingCone(const Vector& InAxis, float InHalfAngle)
		: Axis(InAxis), HalfAngle(InHalfAngle)
	{}

	// Check if cone is degenerate (collapsed to a single direction)
	bool IsDegenerate() const { return HalfAngle < 1e-6f; }

	// Expand cone to include another cone
	void Expand(const jBoundingCone& Other);

	// Expand cone to include a direction
	void ExpandToInclude(const Vector& Direction);
};

// Light tree build configuration
struct jLightTreeBuildOptions
{
	// Spatial vs directional scaling factor 'c' in similarity metric
	// Metric = IC * (αC² + c² * (1 - cosβC)²)
	// Set to scene diagonal for oriented lights, 0 for omni/directional
	float SpatialDirectionalScale = 1.0f;

	// Randomize representative light selection (for decorrelation)
	bool RandomizeRepresentative = true;

	// Debug: Force specific tree build method
	bool DebugForceBalancedTree = false;

	jLightTreeBuildOptions() = default;
};

// Statistics for lightcut computation
struct jLightcutStats
{
	// Per-frame totals
	int32 TotalLights = 0;
	int32 TotalPixelsShaded = 0;

	// Cut size statistics
	float AvgCutSize = 0.0f;
	int32 MinCutSize = INT_MAX;
	int32 MaxCutSize = 0;
	float CutSizeStdDev = 0.0f;

	// Shadow ray statistics
	int64 TotalShadowRays = 0;
	float AvgShadowRaysPerPixel = 0.0f;

	// Timing
	float TreeBuildTime_ms = 0.0f;
	float LightcutComputeTime_ms = 0.0f;
	float NaiveRenderTime_ms = 0.0f;  // For comparison
	float Speedup = 0.0f;

	// Memory
	size_t LightTreeMemoryBytes = 0;

	void Reset();
	void UpdateCutSize(int32 cutSize);
	void UpdateShadowRays(int32 shadowRays);
	void Finalize();
	void Print() const;
};

// Debug visualization modes
enum class ELightcutDebugMode : uint8
{
	None,           // No debug visualization
	CutSize,        // Visualize number of lights per pixel (heatmap)
	ErrorBound,     // Visualize error bounds
	ShadowRays,     // Visualize shadow ray count
	TreeDepth,      // Visualize tree depth accessed
	ClusterBounds,  // Render cluster bounding boxes
};

// Constants
namespace LightcutConstants
{
	// Default error ratio (2% from paper)
	constexpr float DefaultErrorRatio = 0.02f;

	// Maximum cut size (to prevent runaway in dark regions)
	constexpr int32 DefaultMaxCutSize = 1000;

	// Minimum intensity to consider a light (avoid division by zero)
	constexpr float MinLightIntensity = 1e-6f;

	// Maximum tree depth for debugging
	constexpr int32 MaxTreeDepth = 100;
}
