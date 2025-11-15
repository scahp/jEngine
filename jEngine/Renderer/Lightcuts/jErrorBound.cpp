#include "pch.h"
#include "jErrorBound.h"
#include "Math/MathUtility.h"
#include <cmath>

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

static const float EPSILON = 1e-6f;

//////////////////////////////////////////////////////////////////////////
// Bound Geometric Term: Omnidirectional Lights
//
// For point lights: G = 1 / distance²
// Upper bound = 1 / min_distance²
//
// Strategy: Find closest point on bounding box to shading point
//////////////////////////////////////////////////////////////////////////
float jErrorBound::BoundGeometricTerm_Omni(
	const jBoundingBox& lightBBox,
	const Vector& shadingPoint)
{
	// Find closest point on bounding box to shading point
	Vector closestPoint = lightBBox.GetClosestPoint(shadingPoint);

	// Compute minimum distance squared
	float minDistSq = (closestPoint - shadingPoint).LengthSQ();

	// Avoid division by zero
	if (minDistSq < EPSILON)
		minDistSq = EPSILON;

	// Return upper bound: 1 / min_distance²
	return 1.0f / minDistSq;
}

//////////////////////////////////////////////////////////////////////////
// Bound Geometric Term: Oriented Lights
//
// For oriented lights: G = max(cosφ, 0) / distance²
// where φ is the angle between light direction and light-to-point vector
//
// Upper bound requires bounding both:
// - Distance term: Use minimum distance (same as omni)
// - Directional term: Use bounding cone to find maximum cosφ
//////////////////////////////////////////////////////////////////////////
float jErrorBound::BoundGeometricTerm_Oriented(
	const jBoundingBox& lightBBox,
	const jBoundingCone& lightCone,
	const Vector& shadingPoint)
{
	// Find closest point on bounding box to shading point
	Vector closestPoint = lightBBox.GetClosestPoint(shadingPoint);

	// Compute minimum distance squared
	float minDistSq = (closestPoint - shadingPoint).LengthSQ();

	// Avoid division by zero
	if (minDistSq < EPSILON)
		minDistSq = EPSILON;

	// Bound the directional falloff term
	// For a bounding cone, we need to find the maximum possible cosφ
	// This is the best-case alignment between light direction and light-to-point vector
	float maxCos = BoundMinAngleToBBox(lightBBox, shadingPoint, lightCone.Axis);

	// If cone is degenerate (single direction), use the exact cosine
	if (lightCone.IsDegenerate())
	{
		return maxCos / minDistSq;
	}

	// For non-degenerate cones, we need to account for the cone's half-angle
	// The maximum cosine is achieved when the light direction is optimally aligned
	// Conservative approach: assume best possible alignment within cone bounds
	float coneHalfAngle = lightCone.HalfAngle;
	float maxPossibleCos = fminf(1.0f, maxCos + sinf(coneHalfAngle));
	maxPossibleCos = fmaxf(0.0f, maxPossibleCos);  // Clamp to [0, 1]

	return maxPossibleCos / minDistSq;
}

//////////////////////////////////////////////////////////////////////////
// Bound Material Term: Diffuse BRDF
//
// For diffuse BRDF: M = (kd/π) × max(N·L, 0)
// Upper bound = (kd/π) × max_possible_cos(N·L)
//
// Strategy: Find the light direction that maximizes cos(N·L)
// within the bounding box
//////////////////////////////////////////////////////////////////////////
float jErrorBound::BoundMaterialTerm_Diffuse(
	const jBoundingBox& lightBBox,
	const Vector& shadingPoint,
	const Vector& normal,
	float kd)
{
	// Bound the cosine of angle between normal and light direction
	float maxCos = BoundCosNormalLight(lightBBox, shadingPoint, normal);

	// Diffuse BRDF: kd/π × cos(θ)
	return (kd / PI) * maxCos;
}

//////////////////////////////////////////////////////////////////////////
// Bound Material Term: Phong Specular BRDF
//
// For Phong BRDF: M = (ks/π) × max(R·V, 0)^n
// where R is the reflection direction, V is view direction, n is exponent
//
// This is more complex to bound accurately. For now, use conservative approach.
//////////////////////////////////////////////////////////////////////////
float jErrorBound::BoundMaterialTerm_Phong(
	const jBoundingBox& lightBBox,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir,
	float ks,
	float exponent)
{
	// TODO: Implement proper Phong specular bounds
	// For now, return conservative upper bound
	// Maximum specular term is (ks/π) when R·V = 1
	return ks / PI;
}

//////////////////////////////////////////////////////////////////////////
// Helper: Bound Minimum Angle to Bounding Box
//
// Implements Equation 4 from the Lightcuts paper
// Computes the maximum cosine (tightest angle) between an axis
// and any direction from point to bounding box
//
// Strategy:
// - Find the point on bbox that minimizes angle to axis
// - This maximizes the cosine value
//////////////////////////////////////////////////////////////////////////
float jErrorBound::BoundMinAngleToBBox(
	const jBoundingBox& bbox,
	const Vector& point,
	const Vector& axis)
{
	// Normalize the axis
	Vector normalizedAxis = axis.GetNormalize();

	// Find the point on the bounding box that is most aligned with the axis
	// This is the point that maximizes the dot product with the axis direction
	Vector targetPoint = bbox.GetCenter() + normalizedAxis * bbox.GetDiagonal() * 0.5f;

	// Clamp to bounding box
	targetPoint = bbox.GetClosestPoint(targetPoint);

	// Compute direction from point to target
	Vector direction = (targetPoint - point);
	float dirLength = direction.Length();

	if (dirLength < EPSILON)
		return 1.0f;  // Point is inside bbox, return maximum

	direction = direction * (1.0f / dirLength);  // Normalize

	// Compute cosine
	float cosAngle = direction.DotProduct(normalizedAxis);

	// Clamp to valid range [0, 1]
	return fmaxf(0.0f, fminf(1.0f, cosAngle));
}

//////////////////////////////////////////////////////////////////////////
// Helper: Bound Cosine of Normal-Light Angle
//
// Finds the maximum possible cos(N·L) where L is the light direction
// for any light position within the bounding box
//
// Strategy:
// - Find the point on bbox that maximizes alignment with normal
// - Compute the corresponding light direction
// - Return the cosine
//////////////////////////////////////////////////////////////////////////
float jErrorBound::BoundCosNormalLight(
	const jBoundingBox& lightBBox,
	const Vector& shadingPoint,
	const Vector& normal)
{
	// Find the point on the bounding box that is most aligned with the normal
	// This is in the direction of the normal from the shading point
	Vector targetPoint = shadingPoint + normal * 1000.0f;  // Far point in normal direction
	Vector closestToTarget = lightBBox.GetClosestPoint(targetPoint);

	// Compute light direction
	Vector lightDir = (closestToTarget - shadingPoint);
	float lightDirLength = lightDir.Length();

	if (lightDirLength < EPSILON)
		return 0.0f;  // Degenerate case

	lightDir = lightDir * (1.0f / lightDirLength);  // Normalize

	// Compute cosine
	float cosAngle = normal.DotProduct(lightDir);

	// Clamp to [0, 1] (only positive hemisphere contributes)
	return fmaxf(0.0f, fminf(1.0f, cosAngle));
}
