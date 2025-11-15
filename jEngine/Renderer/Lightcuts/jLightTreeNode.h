#pragma once

#include "jLightcutTypes.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jSpotLight.h"

//////////////////////////////////////////////////////////////////////////
// Light Tree Node
//
// Represents a node in the light tree hierarchy. Each node is either:
// - Leaf: Contains a single light source
// - Internal: Aggregates two child nodes into a cluster
//
// Internal nodes store:
// - Representative light (for shading)
// - Bounding volumes (for error estimation)
// - Cluster intensity (sum of children)
//////////////////////////////////////////////////////////////////////////

class jLightTreeNode
{
public:
	//////////////////////////////////////////////////////////////////////////
	// Constructors
	//////////////////////////////////////////////////////////////////////////

	// Create a leaf node from a single light
	jLightTreeNode(jLight* light);

	// Create an internal node by merging two children
	// allowDifferentTypes: Set to true when merging different light types (for root node)
	jLightTreeNode(jLightTreeNode* left, jLightTreeNode* right, bool randomRep, bool allowDifferentTypes = false);

	~jLightTreeNode();

	//////////////////////////////////////////////////////////////////////////
	// Tree Structure
	//////////////////////////////////////////////////////////////////////////

	FORCEINLINE bool IsLeaf() const { return LeftChild == nullptr && RightChild == nullptr; }
	FORCEINLINE jLightTreeNode* GetLeftChild() const { return LeftChild; }
	FORCEINLINE jLightTreeNode* GetRightChild() const { return RightChild; }

	//////////////////////////////////////////////////////////////////////////
	// Light Information
	//////////////////////////////////////////////////////////////////////////

	// Get the representative light for this cluster
	FORCEINLINE jLight* GetRepresentativeLight() const { return RepresentativeLight; }

	// Get total intensity of this cluster (sum of all lights)
	FORCEINLINE float GetClusterIntensity() const { return ClusterIntensity; }

	// Get number of lights in this cluster
	FORCEINLINE int32 GetNumLights() const { return NumLights; }

	// Get the light type for this cluster
	FORCEINLINE ELightcutLightType GetLightType() const { return LightType; }

	//////////////////////////////////////////////////////////////////////////
	// Bounding Volumes
	//////////////////////////////////////////////////////////////////////////

	// Get spatial bounding box
	FORCEINLINE const jBoundingBox& GetBoundingBox() const { return BoundingBox; }

	// Get directional bounding cone (for oriented lights)
	FORCEINLINE const jBoundingCone& GetBoundingCone() const { return BoundingCone; }

	// Get precomputed bounding box diagonal
	FORCEINLINE float GetBoundingBoxDiagonal() const { return BoundingBoxDiagonal; }

	//////////////////////////////////////////////////////////////////////////
	// Error Bounds Computation
	//
	// These functions compute upper bounds on the lighting error
	// when approximating this cluster with its representative light.
	// Used in the lightcut refinement algorithm.
	//////////////////////////////////////////////////////////////////////////

	// Compute maximum geometric term: max(G(x, y) / ||x - y||^2)
	// For a cluster, this is the maximum geometric attenuation factor
	float ComputeMaxGeometricTerm(const Vector& x) const;

	// Compute maximum material term: max(BRDF * cos(theta))
	// Simplified version for now (can be extended with full BRDF)
	float ComputeMaxMaterialTerm(const Vector& x, const Vector& normal, const Vector& viewDir) const;

private:
	//////////////////////////////////////////////////////////////////////////
	// Internal Helper Methods
	//////////////////////////////////////////////////////////////////////////

	// Compute cluster properties by merging children
	void ComputeClusterProperties();

	// Choose representative light from children (intensity-weighted random)
	void ChooseRepresentative(jLightTreeNode* left, jLightTreeNode* right, bool randomize);

	// Detect light type from jLight object
	static ELightcutLightType DetectLightType(jLight* light);

	// Extract position from light (for bounding box)
	static Vector GetLightPosition(jLight* light);

	// Extract direction from light (for bounding cone)
	static Vector GetLightDirection(jLight* light);

	// Extract intensity from light (sum of RGB components)
	static float GetLightIntensity(jLight* light);

	//////////////////////////////////////////////////////////////////////////
	// Member Variables
	//////////////////////////////////////////////////////////////////////////

	// Tree structure
	jLightTreeNode* LeftChild = nullptr;
	jLightTreeNode* RightChild = nullptr;

	// Light information
	jLight* RepresentativeLight = nullptr;
	float ClusterIntensity = 0.0f;
	int32 NumLights = 0;
	ELightcutLightType LightType = ELightcutLightType::Omni;

	// Bounding volumes
	jBoundingBox BoundingBox;
	jBoundingCone BoundingCone;
	float BoundingBoxDiagonal = 0.0f;
};
