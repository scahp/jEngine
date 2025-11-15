#include "pch.h"
#include "jLightcutSelector.h"
#include "Math/MathUtility.h"
#include <cmath>

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

static const float EPSILON = 1e-6f;

//////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
//////////////////////////////////////////////////////////////////////////

jLightcutSelector::jLightcutSelector()
{
}

jLightcutSelector::~jLightcutSelector()
{
}

//////////////////////////////////////////////////////////////////////////
// Compute Lightcut - Main Algorithm
//
// Implements Algorithm 1 from the Lightcuts paper:
// Greedy lightcut refinement with adaptive error threshold
//////////////////////////////////////////////////////////////////////////
jLightcut jLightcutSelector::ComputeLightcut(
	jLightTreeNode* root,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir,
	float errorRatio,
	int32 maxCutSize)
{
	JASSERT(root != nullptr);

	jLightcut cut;
	cut.Clear();
	cut.Reserve(256);  // Pre-allocate reasonable size

	// Cache shading information
	CachedShadingPoint = shadingPoint;
	CachedNormal = normal;
	CachedViewDir = viewDir;

	// Clear the priority queue (create new empty one)
	ErrorHeap = std::priority_queue<jLightcutNodeInfo>();

	// Start with root node
	float rootRadiance = ComputeRadianceEstimate(root, shadingPoint, normal, viewDir);
	float rootError = ComputeErrorBound(root, shadingPoint, normal, viewDir);

	ErrorHeap.push(jLightcutNodeInfo(root, rootRadiance, rootError));
	cut.TotalRadianceEstimate = rootRadiance;

	//////////////////////////////////////////////////////////////////////////
	// Greedy Refinement Loop
	//
	// At each iteration:
	// 1. Pop node with highest error from priority queue
	// 2. Check stopping conditions (error threshold, max cut size, leaf node)
	// 3. If stopping, add to cut; otherwise refine into children
	// 4. Update total radiance estimate
	//////////////////////////////////////////////////////////////////////////

	while (!ErrorHeap.empty())
	{
		// Get node with highest error bound
		jLightcutNodeInfo nodeInfo = ErrorHeap.top();
		ErrorHeap.pop();

		// Compute adaptive error threshold
		// Threshold = errorRatio × totalIllumination
		// This makes the error relative to total light contribution
		float errorThreshold = errorRatio * cut.TotalRadianceEstimate;

		//////////////////////////////////////////////////////////////////////////
		// Stopping Condition 1: Error is acceptable
		//////////////////////////////////////////////////////////////////////////
		if (nodeInfo.ErrorBound < errorThreshold)
		{
			// Error is below threshold - accept this node into the cut
			cut.Nodes.push_back(nodeInfo.Node);
			continue;
		}

		//////////////////////////////////////////////////////////////////////////
		// Stopping Condition 2: Maximum cut size reached
		//////////////////////////////////////////////////////////////////////////
		// Current cut size = nodes already added + nodes still in heap + this node
		int32 currentCutSize = cut.GetCutSize() + static_cast<int32>(ErrorHeap.size()) + 1;
		if (currentCutSize >= maxCutSize)
		{
			// Reached maximum cut size limit - stop refining
			cut.Nodes.push_back(nodeInfo.Node);
			continue;
		}

		//////////////////////////////////////////////////////////////////////////
		// Stopping Condition 3: Leaf node (cannot refine further)
		//////////////////////////////////////////////////////////////////////////
		if (nodeInfo.Node->IsLeaf())
		{
			// Leaf nodes represent exact lights - zero error
			cut.Nodes.push_back(nodeInfo.Node);
			continue;
		}

		//////////////////////////////////////////////////////////////////////////
		// Refine: Replace node with its two children
		//////////////////////////////////////////////////////////////////////////

		jLightTreeNode* leftChild = nodeInfo.Node->GetLeftChild();
		jLightTreeNode* rightChild = nodeInfo.Node->GetRightChild();

		JASSERT(leftChild != nullptr && rightChild != nullptr);

		// Compute radiance estimates for children
		float leftRadiance = ComputeRadianceEstimate(leftChild, shadingPoint, normal, viewDir);
		float rightRadiance = ComputeRadianceEstimate(rightChild, shadingPoint, normal, viewDir);

		// Compute error bounds for children
		float leftError = ComputeErrorBound(leftChild, shadingPoint, normal, viewDir);
		float rightError = ComputeErrorBound(rightChild, shadingPoint, normal, viewDir);

		// Add children to priority queue
		ErrorHeap.push(jLightcutNodeInfo(leftChild, leftRadiance, leftError));
		ErrorHeap.push(jLightcutNodeInfo(rightChild, rightRadiance, rightError));

		// Update total radiance estimate
		// Remove parent's contribution, add children's contributions
		cut.TotalRadianceEstimate -= nodeInfo.RadianceEstimate;
		cut.TotalRadianceEstimate += leftRadiance + rightRadiance;
	}

	// Add any remaining nodes in heap to cut
	// (This happens when all nodes have acceptable error)
	while (!ErrorHeap.empty())
	{
		cut.Nodes.push_back(ErrorHeap.top().Node);
		ErrorHeap.pop();
	}

	return cut;
}

//////////////////////////////////////////////////////////////////////////
// Compute Radiance Estimate
//
// Approximates the lighting contribution of a cluster using its
// representative light.
//
// Formula: L̃ₙ = M(x,ω) × G(x) × V(x) × I_cluster
// where:
//   M = Material term (BRDF × cos)
//   G = Geometric term (distance attenuation)
//   V = Visibility term (shadows)
//   I = Cluster intensity (sum of all lights in cluster)
//////////////////////////////////////////////////////////////////////////
float jLightcutSelector::ComputeRadianceEstimate(
	jLightTreeNode* node,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir)
{
	JASSERT(node != nullptr);

	// Get material term (BRDF × cos)
	float M = EvaluateMaterialTerm(node, shadingPoint, normal, viewDir);

	// Get geometric term (distance attenuation)
	float G = EvaluateGeometricTerm(node, shadingPoint);

	// Visibility term (simplified for Phase 3)
	// TODO Phase 4: Integrate shadow rays
	float V = 1.0f;  // Assume all lights visible for now

	// Cluster intensity
	float I = node->GetClusterIntensity();

	// Total radiance estimate
	float radiance = M * G * V * I;

	return fmaxf(0.0f, radiance);  // Clamp to positive
}

//////////////////////////////////////////////////////////////////////////
// Evaluate Material Term
//
// Simplified material evaluation for Phase 3
// Uses diffuse BRDF with representative light
//
// Formula: M = (kd/π) × max(N·L, 0)
// For now, assume kd = 1.0 (white diffuse surface)
//////////////////////////////////////////////////////////////////////////
float jLightcutSelector::EvaluateMaterialTerm(
	jLightTreeNode* node,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir)
{
	jLight* repLight = node->GetRepresentativeLight();
	JASSERT(repLight != nullptr);

	// Get light direction based on light type
	Vector lightDir;
	ELightType lightType = repLight->GetLightType();

	switch (lightType)
	{
	case ELightType::DIRECTIONAL:
	{
		jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(repLight);
		lightDir = dirLight->GetLightData().Direction * -1.0f;  // Negate to get direction TO light
		break;
	}

	case ELightType::POINT:
	{
		jPointLight* pointLight = static_cast<jPointLight*>(repLight);
		Vector lightPos = pointLight->GetLightData().Position;
		lightDir = (lightPos - shadingPoint);
		float len = lightDir.Length();
		if (len > EPSILON)
			lightDir = lightDir * (1.0f / len);  // Normalize
		break;
	}

	case ELightType::SPOT:
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(repLight);
		Vector lightPos = spotLight->GetLightData().Position;
		lightDir = (lightPos - shadingPoint);
		float len = lightDir.Length();
		if (len > EPSILON)
			lightDir = lightDir * (1.0f / len);  // Normalize
		break;
	}

	default:
		return 0.0f;
	}

	// Compute N·L
	float NdotL = normal.DotProduct(lightDir);
	NdotL = fmaxf(0.0f, NdotL);  // Clamp to positive

	// Simplified diffuse BRDF: 1/π × max(N·L, 0)
	// Assuming diffuse coefficient kd = 1.0
	float brdf = (1.0f / PI) * NdotL;

	return brdf;
}

//////////////////////////////////////////////////////////////////////////
// Evaluate Geometric Term
//
// Computes distance attenuation based on light type
//
// For point/spot lights: G = 1 / distance²
// For directional lights: G = 1 (no attenuation)
//////////////////////////////////////////////////////////////////////////
float jLightcutSelector::EvaluateGeometricTerm(
	jLightTreeNode* node,
	const Vector& shadingPoint)
{
	jLight* repLight = node->GetRepresentativeLight();
	JASSERT(repLight != nullptr);

	ELightType lightType = repLight->GetLightType();

	// Directional lights have no distance attenuation
	if (lightType == ELightType::DIRECTIONAL)
	{
		return 1.0f;
	}

	// Point and spot lights use inverse square falloff
	Vector lightPos;

	if (lightType == ELightType::POINT)
	{
		jPointLight* pointLight = static_cast<jPointLight*>(repLight);
		lightPos = pointLight->GetLightData().Position;
	}
	else if (lightType == ELightType::SPOT)
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(repLight);
		lightPos = spotLight->GetLightData().Position;
	}
	else
	{
		return 0.0f;
	}

	// Compute distance squared
	float distSq = (lightPos - shadingPoint).LengthSQ();

	// Avoid division by zero
	if (distSq < EPSILON)
		distSq = EPSILON;

	// Inverse square falloff
	return 1.0f / distSq;
}

//////////////////////////////////////////////////////////////////////////
// Compute Error Bound
//
// Computes conservative upper bound on approximation error
// Uses jErrorBound class for geometric and material bounds
//
// Formula: Error ≤ max(M) × max(G) × max(V) × I_cluster
//////////////////////////////////////////////////////////////////////////
float jLightcutSelector::ComputeErrorBound(
	jLightTreeNode* node,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir)
{
	JASSERT(node != nullptr);

	// For leaf nodes, error is zero (exact evaluation)
	if (node->IsLeaf())
		return 0.0f;

	// Get cluster intensity
	float I = node->GetClusterIntensity();

	// Bound material term
	// For Phase 3, use simplified constant bound
	float maxM = jErrorBound::BoundMaterialTerm_Simple();

	// Bound geometric term based on light type
	float maxG = 0.0f;
	ELightcutLightType lightType = node->GetLightType();

	switch (lightType)
	{
	case ELightcutLightType::Omni:
		maxG = jErrorBound::BoundGeometricTerm_Omni(
			node->GetBoundingBox(),
			shadingPoint);
		break;

	case ELightcutLightType::Oriented:
		maxG = jErrorBound::BoundGeometricTerm_Oriented(
			node->GetBoundingBox(),
			node->GetBoundingCone(),
			shadingPoint);
		break;

	case ELightcutLightType::Directional:
		maxG = jErrorBound::BoundGeometricTerm_Directional();
		break;

	default:
		maxG = 1.0f;  // Conservative
		break;
	}

	// Bound visibility term
	float maxV = jErrorBound::BoundVisibilityTerm();

	// Total error bound
	float errorBound = maxM * maxG * maxV * I;

	return fmaxf(0.0f, errorBound);  // Clamp to positive
}
