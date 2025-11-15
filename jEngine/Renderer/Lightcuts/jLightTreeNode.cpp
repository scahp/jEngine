#include "pch.h"
#include "jLightTreeNode.h"
#include <cmath>
#include <cstdlib>

//////////////////////////////////////////////////////////////////////////
// Constructor: Leaf Node
//////////////////////////////////////////////////////////////////////////
jLightTreeNode::jLightTreeNode(jLight* light)
	: RepresentativeLight(light)
	, LeftChild(nullptr)
	, RightChild(nullptr)
{
	JASSERT(light != nullptr);

	// Leaf node has 1 light
	NumLights = 1;

	// Detect light type
	LightType = DetectLightType(light);

	// Get light intensity
	ClusterIntensity = GetLightIntensity(light);

	// Initialize bounding box from light position
	Vector lightPos = GetLightPosition(light);
	BoundingBox = jBoundingBox::FromPoint(lightPos);
	BoundingBoxDiagonal = 0.0f;

	// Initialize bounding cone for oriented lights
	if (LightType == ELightcutLightType::Oriented || LightType == ELightcutLightType::Directional)
	{
		Vector lightDir = GetLightDirection(light);
		BoundingCone = jBoundingCone(lightDir, 0.0f);  // Degenerate cone for single light
	}
	else
	{
		BoundingCone = jBoundingCone(Vector::ZeroVector, 0.0f);
	}
}

//////////////////////////////////////////////////////////////////////////
// Constructor: Internal Node
//////////////////////////////////////////////////////////////////////////
jLightTreeNode::jLightTreeNode(jLightTreeNode* left, jLightTreeNode* right, bool randomRep, bool allowDifferentTypes)
	: LeftChild(left)
	, RightChild(right)
{
	JASSERT(left != nullptr && right != nullptr);

	// Only check type consistency if not explicitly allowing different types
	if (!allowDifferentTypes)
	{
		JASSERT(left->GetLightType() == right->GetLightType());  // Children must be same type
	}

	// Compute cluster properties
	ComputeClusterProperties();

	// Choose representative light
	ChooseRepresentative(left, right, randomRep);
}

//////////////////////////////////////////////////////////////////////////
// Destructor
//////////////////////////////////////////////////////////////////////////
jLightTreeNode::~jLightTreeNode()
{
	// Note: We don't delete children here - tree owns all nodes
	// We also don't delete RepresentativeLight - it's owned by the scene
}

//////////////////////////////////////////////////////////////////////////
// Compute Cluster Properties
//////////////////////////////////////////////////////////////////////////
void jLightTreeNode::ComputeClusterProperties()
{
	// Sum intensities
	ClusterIntensity = LeftChild->GetClusterIntensity() + RightChild->GetClusterIntensity();

	// Sum light counts
	NumLights = LeftChild->GetNumLights() + RightChild->GetNumLights();

	// Use light type from left child (for mixed-type nodes, this is arbitrary)
	LightType = LeftChild->GetLightType();

	// Merge bounding boxes
	BoundingBox = jBoundingBox::Merge(LeftChild->GetBoundingBox(), RightChild->GetBoundingBox());
	BoundingBoxDiagonal = BoundingBox.GetDiagonal();

	// Merge bounding cones for oriented lights
	if (LightType == ELightcutLightType::Oriented || LightType == ELightcutLightType::Directional)
	{
		// For now, use a simple cone merge strategy
		// TODO: Implement proper cone merging algorithm
		const jBoundingCone& leftCone = LeftChild->GetBoundingCone();
		const jBoundingCone& rightCone = RightChild->GetBoundingCone();

		// If one cone is degenerate, use the other
		if (leftCone.IsDegenerate())
		{
			BoundingCone = rightCone;
		}
		else if (rightCone.IsDegenerate())
		{
			BoundingCone = leftCone;
		}
		else
		{
			// Compute average axis (normalized)
			Vector avgAxis = (leftCone.Axis + rightCone.Axis);
			float avgAxisLen = avgAxis.Length();

			if (avgAxisLen > 1e-6f)
			{
				avgAxis = avgAxis / avgAxisLen;

				// Compute maximum angle to cover both cones
				float maxAngle = 0.0f;

				// Angle from average axis to left cone boundary
				float leftDot = leftCone.Axis.DotProduct(avgAxis);
				float leftAngle = acosf(fmaxf(-1.0f, fminf(1.0f, leftDot))) + leftCone.HalfAngle;
				maxAngle = fmaxf(maxAngle, leftAngle);

				// Angle from average axis to right cone boundary
				float rightDot = rightCone.Axis.DotProduct(avgAxis);
				float rightAngle = acosf(fmaxf(-1.0f, fminf(1.0f, rightDot))) + rightCone.HalfAngle;
				maxAngle = fmaxf(maxAngle, rightAngle);

				BoundingCone = jBoundingCone(avgAxis, maxAngle);
			}
			else
			{
				// Cones are opposite - use hemisphere
				BoundingCone = jBoundingCone(leftCone.Axis, 3.14159265f);  // PI radians = hemisphere
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Choose Representative Light
//////////////////////////////////////////////////////////////////////////
void jLightTreeNode::ChooseRepresentative(jLightTreeNode* left, jLightTreeNode* right, bool randomize)
{
	if (randomize)
	{
		// Intensity-weighted random selection
		float leftIntensity = left->GetClusterIntensity();
		float rightIntensity = right->GetClusterIntensity();
		float totalIntensity = leftIntensity + rightIntensity;

		if (totalIntensity > 1e-6f)
		{
			float randomValue = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
			float leftProbability = leftIntensity / totalIntensity;

			RepresentativeLight = (randomValue < leftProbability)
				? left->GetRepresentativeLight()
				: right->GetRepresentativeLight();
		}
		else
		{
			// If both have zero intensity, just pick left
			RepresentativeLight = left->GetRepresentativeLight();
		}
	}
	else
	{
		// Deterministic: always pick the brighter one
		RepresentativeLight = (left->GetClusterIntensity() > right->GetClusterIntensity())
			? left->GetRepresentativeLight()
			: right->GetRepresentativeLight();
	}
}

//////////////////////////////////////////////////////////////////////////
// Detect Light Type
//////////////////////////////////////////////////////////////////////////
ELightcutLightType jLightTreeNode::DetectLightType(jLight* light)
{
	JASSERT(light != nullptr);

	ELightType type = light->GetLightType();

	switch (type)
	{
	case ELightType::DIRECTIONAL:
		return ELightcutLightType::Directional;

	case ELightType::SPOT:
		return ELightcutLightType::Oriented;

	case ELightType::POINT:
		return ELightcutLightType::Omni;

	case ELightType::AMBIENT:
		// Ambient lights are not used in lightcuts
		JASSERT(false);
		return ELightcutLightType::Omni;

	default:
		// Default to omni for unknown types
		return ELightcutLightType::Omni;
	}
}

//////////////////////////////////////////////////////////////////////////
// Get Light Position
//////////////////////////////////////////////////////////////////////////
Vector jLightTreeNode::GetLightPosition(jLight* light)
{
	JASSERT(light != nullptr);

	ELightType type = light->GetLightType();

	switch (type)
	{
	case ELightType::POINT:
	{
		jPointLight* pointLight = static_cast<jPointLight*>(light);
		return pointLight->GetLightData().Position;
	}

	case ELightType::SPOT:
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(light);
		return spotLight->GetLightData().Position;
	}

	case ELightType::DIRECTIONAL:
	{
		// Directional lights don't have a position, use light position from data
		jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
		return dirLight->GetLightData().LightPos;
	}

	default:
		return Vector::ZeroVector;
	}
}

//////////////////////////////////////////////////////////////////////////
// Get Light Direction
//////////////////////////////////////////////////////////////////////////
Vector jLightTreeNode::GetLightDirection(jLight* light)
{
	JASSERT(light != nullptr);

	ELightType type = light->GetLightType();

	switch (type)
	{
	case ELightType::DIRECTIONAL:
	{
		jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
		return dirLight->GetLightData().Direction;
	}

	case ELightType::SPOT:
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(light);
		return spotLight->GetLightData().Direction;
	}

	default:
		// Point lights don't have a direction
		return Vector::ZeroVector;
	}
}

//////////////////////////////////////////////////////////////////////////
// Get Light Intensity
//////////////////////////////////////////////////////////////////////////
float jLightTreeNode::GetLightIntensity(jLight* light)
{
	JASSERT(light != nullptr);

	ELightType type = light->GetLightType();

	switch (type)
	{
	case ELightType::POINT:
	{
		jPointLight* pointLight = static_cast<jPointLight*>(light);
		const Vector& intensity = pointLight->GetLightData().DiffuseIntensity;
		return intensity.x + intensity.y + intensity.z;
	}

	case ELightType::SPOT:
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(light);
		const Vector& intensity = spotLight->GetLightData().DiffuseIntensity;
		return intensity.x + intensity.y + intensity.z;
	}

	case ELightType::DIRECTIONAL:
	{
		jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
		const Vector& intensity = dirLight->GetLightData().DiffuseIntensity;
		return intensity.x + intensity.y + intensity.z;
	}

	default:
		return 0.0f;
	}
}

//////////////////////////////////////////////////////////////////////////
// Compute Max Geometric Term
//
// This computes an upper bound on the geometric attenuation term
// G(x,y) / ||x - y||^2 for all lights in the cluster.
//
// For now, we use a simple conservative estimate based on the
// closest point on the bounding box to the shading point.
//////////////////////////////////////////////////////////////////////////
float jLightTreeNode::ComputeMaxGeometricTerm(const Vector& x) const
{
	// Find closest point on bounding box to shading point
	Vector closestPoint = BoundingBox.GetClosestPoint(x);
	float distSq = (x - closestPoint).LengthSQ();

	// Avoid division by zero
	if (distSq < 1e-6f)
		distSq = 1e-6f;

	// For point lights: G(x,y) = 1 / ||x - y||^2
	// Maximum value is when light is at closest point
	if (LightType == ELightcutLightType::Omni)
	{
		return 1.0f / distSq;
	}

	// For oriented lights, we need to account for directional falloff
	// For now, use conservative upper bound
	// TODO: Implement proper oriented light geometric term
	if (LightType == ELightcutLightType::Oriented)
	{
		// Conservative: assume maximum cosine term
		return 1.0f / distSq;
	}

	// For directional lights, there's no distance falloff
	if (LightType == ELightcutLightType::Directional)
	{
		return 1.0f;
	}

	return 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Compute Max Material Term
//
// This computes an upper bound on the BRDF * cos(theta) term
// for all lights in the cluster.
//
// Simplified version: just returns 1.0 (Lambert BRDF upper bound)
// TODO: Implement proper material-aware bounds
//////////////////////////////////////////////////////////////////////////
float jLightTreeNode::ComputeMaxMaterialTerm(const Vector& x, const Vector& normal, const Vector& viewDir) const
{
	// For now, return conservative upper bound
	// Lambert BRDF: rho/pi * max(0, n dot l)
	// Maximum value is 1 when perfectly aligned
	return 1.0f;
}
