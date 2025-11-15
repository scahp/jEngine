#pragma once

#include "jLightcut.h"
#include "Math/Vector.h"

//////////////////////////////////////////////////////////////////////////
// Lightcut Evaluator
//
// Evaluates the final lighting contribution from a computed lightcut.
// Takes a lightcut (set of cluster nodes) and computes the total
// illumination at a shading point.
//
// For each node in the cut:
// - Evaluates the cluster using its representative light
// - Sums the contributions: L = Σ [ M × G × V × I_cluster ]
//
// The evaluator can optionally trace shadow rays for visibility (V term).
// In Phase 3, we use V=1.0 (no shadows) for simplicity.
// In Phase 4, we integrate ray tracing for accurate visibility.
//
// References:
// - Lightcuts Paper Section 4: Evaluation
// - Equation 2: Cluster Approximation Formula
//////////////////////////////////////////////////////////////////////////

class jLightcutEvaluator
{
public:
	//////////////////////////////////////////////////////////////////////////
	// Constructor / Destructor
	//////////////////////////////////////////////////////////////////////////

	jLightcutEvaluator();
	~jLightcutEvaluator();

	//////////////////////////////////////////////////////////////////////////
	// Main Interface
	//////////////////////////////////////////////////////////////////////////

	// Evaluate lightcut and compute total illumination
	//
	// Parameters:
	//   cut          - The lightcut to evaluate (set of cluster nodes)
	//   shadingPoint - 3D position of the shading point
	//   normal       - Surface normal at shading point
	//   viewDir      - View direction (towards camera)
	//   traceShadows - Whether to trace shadow rays for visibility (Phase 4)
	//
	// Returns:
	//   Total illumination color (RGB)
	//
	Vector4 EvaluateLightcut(
		const jLightcut& cut,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir,
		bool traceShadows = false);

	//////////////////////////////////////////////////////////////////////////
	// Statistics
	//////////////////////////////////////////////////////////////////////////

	// Get number of shadow rays traced during last evaluation
	int32 GetShadowRayCount() const { return ShadowRayCount; }

	// Reset statistics
	void ResetStats() { ShadowRayCount = 0; }

private:
	//////////////////////////////////////////////////////////////////////////
	// Cluster Evaluation
	//////////////////////////////////////////////////////////////////////////

	// Evaluate a single cluster node
	// Returns illumination contribution from this cluster
	Vector4 EvaluateCluster(
		jLightTreeNode* node,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir,
		bool traceShadows);

	//////////////////////////////////////////////////////////////////////////
	// Lighting Components
	//////////////////////////////////////////////////////////////////////////

	// Evaluate material term (BRDF × cos)
	// Returns BRDF value for representative light
	float EvaluateMaterialTerm(
		jLight* light,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir);

	// Evaluate geometric term (distance attenuation)
	// Returns geometric attenuation factor
	float EvaluateGeometricTerm(
		jLight* light,
		const Vector& shadingPoint);

	// Evaluate visibility term (shadow test)
	// Returns 0.0 (occluded) or 1.0 (visible)
	// For Phase 3: always returns 1.0
	float EvaluateVisibility(
		jLight* light,
		const Vector& shadingPoint,
		bool traceShadows);

	//////////////////////////////////////////////////////////////////////////
	// Helper Functions
	//////////////////////////////////////////////////////////////////////////

	// Get light color (intensity as RGB)
	Vector4 GetLightColor(jLight* light);

	// Get light direction from shading point
	Vector GetLightDirection(
		jLight* light,
		const Vector& shadingPoint);

	//////////////////////////////////////////////////////////////////////////
	// Member Variables
	//////////////////////////////////////////////////////////////////////////

	// Statistics
	int32 ShadowRayCount = 0;
};
