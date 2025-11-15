#pragma once

#include "jLightcutTypes.h"
#include "jLightTreeNode.h"

//////////////////////////////////////////////////////////////////////////
// Error Bound Computation
//
// Computes conservative upper bounds for cluster error terms.
// Used by the lightcut refinement algorithm to determine when to
// replace a cluster node with its children.
//
// Error bounds are computed for three terms:
// - Geometric term G: Distance attenuation (1/r² or cosφ/r²)
// - Material term M: BRDF × cos(θ)
// - Visibility term V: Shadow occlusion
//
// The total error bound for a cluster is:
//   Error ≤ max(M) × max(G) × max(V) × I_cluster
//
// References:
// - Lightcuts Paper Section 5.1: Error Bounds
// - Equation 4: Bounding minimum angle to bounding volume
//////////////////////////////////////////////////////////////////////////

class jErrorBound
{
public:
	//////////////////////////////////////////////////////////////////////////
	// Geometric Term Bounds
	//////////////////////////////////////////////////////////////////////////

	// Bound geometric term for omnidirectional lights (point lights)
	// G = 1 / distance²
	// Returns upper bound: 1 / min_distance²
	static float BoundGeometricTerm_Omni(
		const jBoundingBox& lightBBox,
		const Vector& shadingPoint);

	// Bound geometric term for oriented lights (spotlights)
	// G = max(cosφ, 0) / distance²
	// Returns upper bound considering both distance and directional falloff
	static float BoundGeometricTerm_Oriented(
		const jBoundingBox& lightBBox,
		const jBoundingCone& lightCone,
		const Vector& shadingPoint);

	// Bound geometric term for directional lights
	// G = 1 (no distance attenuation)
	static float BoundGeometricTerm_Directional()
	{
		return 1.0f;  // Directional lights have constant G=1
	}

	//////////////////////////////////////////////////////////////////////////
	// Material Term Bounds
	//////////////////////////////////////////////////////////////////////////

	// Bound material term for diffuse BRDF
	// M = (kd/π) × max(N·L, 0)
	// Returns conservative upper bound
	static float BoundMaterialTerm_Diffuse(
		const jBoundingBox& lightBBox,
		const Vector& shadingPoint,
		const Vector& normal,
		float kd);  // Diffuse coefficient

	// Bound material term for Phong specular BRDF
	// M = (ks/π) × max(R·V, 0)^n
	// Returns conservative upper bound
	static float BoundMaterialTerm_Phong(
		const jBoundingBox& lightBBox,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir,
		float ks,       // Specular coefficient
		float exponent); // Phong exponent

	// Simplified material term bound (conservative)
	// Returns 1.0 (maximum possible BRDF value)
	// Used when material properties are unknown
	static float BoundMaterialTerm_Simple()
	{
		return 1.0f;  // Conservative: max possible BRDF value
	}

	//////////////////////////////////////////////////////////////////////////
	// Visibility Term Bound
	//////////////////////////////////////////////////////////////////////////

	// Bound visibility term
	// V ∈ [0, 1] where 0 = fully occluded, 1 = fully visible
	// Returns 1.0 (conservative: assume all lights potentially visible)
	static float BoundVisibilityTerm()
	{
		return 1.0f;  // All lights potentially visible
	}

private:
	//////////////////////////////////////////////////////////////////////////
	// Helper Functions
	//////////////////////////////////////////////////////////////////////////

	// Bound minimum angle between a point and a bounding volume
	// Used in Equation 4 of the Lightcuts paper
	// Returns maximum cosine value (tightest angle)
	static float BoundMinAngleToBBox(
		const jBoundingBox& bbox,
		const Vector& point,
		const Vector& axis);

	// Bound cosine of angle between surface normal and light direction
	// Used for material term computation
	// Returns maximum possible cos(N·L) value
	static float BoundCosNormalLight(
		const jBoundingBox& lightBBox,
		const Vector& shadingPoint,
		const Vector& normal);
};
