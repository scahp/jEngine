#include "pch.h"
#include "jLightcutTypes.h"
#include "Math/MathUtility.h"
#include <cmath>

//////////////////////////////////////////////////////////////////////////
// jBoundingCone Implementation
//////////////////////////////////////////////////////////////////////////

static const float EPSILON = 1e-6f;

void jBoundingCone::Expand(const jBoundingCone& Other)
{
	// If this cone is degenerate, just copy the other
	if (IsDegenerate())
	{
		Axis = Other.Axis;
		HalfAngle = Other.HalfAngle;
		return;
	}

	// If other cone is degenerate, just include its axis direction
	if (Other.IsDegenerate())
	{
		ExpandToInclude(Other.Axis);
		return;
	}

	// Compute angle between the two axes
	float cosAngle = Axis.DotProduct(Other.Axis);
	cosAngle = fmaxf(-1.0f, fminf(1.0f, cosAngle));  // Clamp to [-1, 1]
	float angle = acosf(cosAngle);

	// Check if one cone contains the other
	if (angle + Other.HalfAngle <= HalfAngle + EPSILON)
	{
		// This cone contains the other
		return;
	}
	if (angle + HalfAngle <= Other.HalfAngle + EPSILON)
	{
		// Other cone contains this one
		Axis = Other.Axis;
		HalfAngle = Other.HalfAngle;
		return;
	}

	// Compute new bounding cone
	// New half-angle is half the sum of angles and half-angles
	float newHalfAngle = (angle + HalfAngle + Other.HalfAngle) * 0.5f;

	// New axis is weighted average based on half-angles
	float t = (newHalfAngle - HalfAngle) / (angle + EPSILON);
	t = fmaxf(0.0f, fminf(1.0f, t));  // Clamp to [0, 1]

	// Spherical interpolation (slerp) between axes
	if (angle > EPSILON)
	{
		float sinAngle = sinf(angle);
		float w0 = sinf((1.0f - t) * angle) / sinAngle;
		float w1 = sinf(t * angle) / sinAngle;

		Axis = Axis * w0 + Other.Axis * w1;
		Axis = Axis.GetNormalize();
	}

	HalfAngle = newHalfAngle;

	// Clamp half-angle to valid range [0, PI]
	HalfAngle = fmaxf(0.0f, fminf(PI, HalfAngle));
}

void jBoundingCone::ExpandToInclude(const Vector& Direction)
{
	// Normalize the direction
	Vector dir = Direction.GetNormalize();

	// If cone is degenerate, just set it to this direction
	if (IsDegenerate())
	{
		Axis = dir;
		HalfAngle = 0.0f;
		return;
	}

	// Compute angle between cone axis and direction
	float cosAngle = Axis.DotProduct(dir);
	cosAngle = fmaxf(-1.0f, fminf(1.0f, cosAngle));  // Clamp to [-1, 1]
	float angle = acosf(cosAngle);

	// If direction is already inside cone, do nothing
	if (angle <= HalfAngle + EPSILON)
	{
		return;
	}

	// Expand cone to include direction
	// New half-angle is half the sum of the angle and current half-angle
	float newHalfAngle = (angle + HalfAngle) * 0.5f;

	// New axis is rotated halfway between current axis and direction
	float t = (newHalfAngle - HalfAngle) / (angle + EPSILON);
	t = fmaxf(0.0f, fminf(1.0f, t));  // Clamp to [0, 1]

	// Spherical interpolation (slerp)
	if (angle > EPSILON)
	{
		float sinAngle = sinf(angle);
		float w0 = sinf((1.0f - t) * angle) / sinAngle;
		float w1 = sinf(t * angle) / sinAngle;

		Axis = Axis * w0 + dir * w1;
		Axis = Axis.GetNormalize();
	}

	HalfAngle = newHalfAngle;

	// Clamp half-angle to valid range [0, PI]
	HalfAngle = fmaxf(0.0f, fminf(PI, HalfAngle));
}

//////////////////////////////////////////////////////////////////////////
// jLightcutStats Implementation
//////////////////////////////////////////////////////////////////////////

void jLightcutStats::Reset()
{
	TotalLights = 0;
	TotalPixelsShaded = 0;
	AvgCutSize = 0.0f;
	MinCutSize = INT_MAX;
	MaxCutSize = 0;
	CutSizeStdDev = 0.0f;
	TotalShadowRays = 0;
	AvgShadowRaysPerPixel = 0.0f;
	TreeBuildTime_ms = 0.0f;
	LightcutComputeTime_ms = 0.0f;
	NaiveRenderTime_ms = 0.0f;
	Speedup = 0.0f;
	LightTreeMemoryBytes = 0;
}

void jLightcutStats::UpdateCutSize(int32 cutSize)
{
	if (cutSize < MinCutSize)
		MinCutSize = cutSize;
	if (cutSize > MaxCutSize)
		MaxCutSize = cutSize;

	// Running average
	float prevAvg = AvgCutSize;
	int32 n = TotalPixelsShaded;
	AvgCutSize = (prevAvg * n + cutSize) / (n + 1);

	TotalPixelsShaded++;
}

void jLightcutStats::UpdateShadowRays(int32 shadowRays)
{
	TotalShadowRays += shadowRays;
}

void jLightcutStats::Finalize()
{
	if (TotalPixelsShaded > 0)
	{
		AvgShadowRaysPerPixel = (float)TotalShadowRays / (float)TotalPixelsShaded;
	}

	if (NaiveRenderTime_ms > 0.0f && LightcutComputeTime_ms > 0.0f)
	{
		Speedup = NaiveRenderTime_ms / LightcutComputeTime_ms;
	}
}

void jLightcutStats::Print() const
{
	// TODO: Implement stats printing
	// For now, this is a placeholder
}
