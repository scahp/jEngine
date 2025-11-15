#pragma once

#include "jLightcut.h"
#include "jErrorBound.h"
#include <queue>

//////////////////////////////////////////////////////////////////////////
// Lightcut Selector
//
// Computes an adaptive lightcut for a shading point using the
// greedy refinement algorithm from the Lightcuts paper.
//
// Algorithm Overview:
// 1. Start with root node of light tree
// 2. Maintain priority queue of nodes ordered by error bound
// 3. Pop node with highest error from queue
// 4. If error < threshold, add to cut (accept)
// 5. Otherwise, refine: replace with two children
// 6. Repeat until all nodes are accepted or max cut size reached
//
// The error threshold is adaptive based on total illumination:
//   threshold = errorRatio × totalRadianceEstimate
//
// This ensures error is relative to the total lighting contribution.
//
// References:
// - Lightcuts Paper Section 4: The Lightcuts Algorithm
// - Algorithm 1: Greedy Lightcut Refinement
// - Section 5.1: Computing Error Bounds
//////////////////////////////////////////////////////////////////////////

class jLightcutSelector
{
public:
	//////////////////////////////////////////////////////////////////////////
	// Constructor / Destructor
	//////////////////////////////////////////////////////////////////////////

	jLightcutSelector();
	~jLightcutSelector();

	//////////////////////////////////////////////////////////////////////////
	// Main Interface
	//////////////////////////////////////////////////////////////////////////

	// Compute lightcut for a shading point
	//
	// Parameters:
	//   root         - Root of the light tree
	//   shadingPoint - 3D position of the shading point
	//   normal       - Surface normal at shading point
	//   viewDir      - View direction (towards camera)
	//   errorRatio   - Relative error threshold (default 0.02 = 2%)
	//   maxCutSize   - Maximum allowed cut size (safety limit)
	//
	// Returns:
	//   Lightcut containing the selected nodes
	//
	// Note: The returned lightcut is valid until the next call to ComputeLightcut
	jLightcut ComputeLightcut(
		jLightTreeNode* root,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir,
		float errorRatio = LightcutConstants::DefaultErrorRatio,
		int32 maxCutSize = LightcutConstants::DefaultMaxCutSize);

private:
	//////////////////////////////////////////////////////////////////////////
	// Radiance Estimation
	//////////////////////////////////////////////////////////////////////////

	// Compute radiance estimate for a cluster node
	// Uses representative light to approximate cluster contribution
	// Formula: L̃ₙ = M × G × V × I_cluster
	// where M = material term, G = geometric term, V = visibility, I = intensity
	float ComputeRadianceEstimate(
		jLightTreeNode* node,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir);

	// Evaluate material term (BRDF × cos)
	// Simplified version for Phase 3
	float EvaluateMaterialTerm(
		jLightTreeNode* node,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir);

	// Evaluate geometric term (distance attenuation)
	float EvaluateGeometricTerm(
		jLightTreeNode* node,
		const Vector& shadingPoint);

	//////////////////////////////////////////////////////////////////////////
	// Error Bound Computation
	//////////////////////////////////////////////////////////////////////////

	// Compute conservative error bound for a cluster node
	// Formula: Error ≤ max(M) × max(G) × max(V) × I_cluster
	// Uses jErrorBound class for conservative bounds
	float ComputeErrorBound(
		jLightTreeNode* node,
		const Vector& shadingPoint,
		const Vector& normal,
		const Vector& viewDir);

	//////////////////////////////////////////////////////////////////////////
	// Member Variables
	//////////////////////////////////////////////////////////////////////////

	// Priority queue for greedy refinement
	// Nodes with highest error bound are refined first
	std::priority_queue<jLightcutNodeInfo> ErrorHeap;

	// Cached shading information (for current point)
	Vector CachedShadingPoint;
	Vector CachedNormal;
	Vector CachedViewDir;
};
