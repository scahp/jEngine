#include "pch.h"
#include "jLightcutEvaluator.h"
#include "jLightTreeNode.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Math/MathUtility.h"
#include <cmath>

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

static const float EPSILON = 1e-6f;

//////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
//////////////////////////////////////////////////////////////////////////

jLightcutEvaluator::jLightcutEvaluator()
	: ShadowRayCount(0)
{
}

jLightcutEvaluator::~jLightcutEvaluator()
{
}

//////////////////////////////////////////////////////////////////////////
// Evaluate Lightcut
//
// Main evaluation function: sums contributions from all nodes in the cut
//
// Formula: L(x,ω) = Σ [ Mⱼ(x,ω) × Gⱼ(x) × Vⱼ(x) × Iᶜ ]
// where j = representative light of each cluster C
//////////////////////////////////////////////////////////////////////////
Vector4 jLightcutEvaluator::EvaluateLightcut(
	const jLightcut& cut,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir,
	bool traceShadows)
{
	// Reset statistics
	ShadowRayCount = 0;

	// Accumulate illumination from all clusters in the cut
	Vector4 totalIllumination = Vector4::ZeroVector;

	for (jLightTreeNode* node : cut.Nodes)
	{
		JASSERT(node != nullptr);

		// Evaluate this cluster's contribution
		Vector4 contribution = EvaluateCluster(
			node,
			shadingPoint,
			normal,
			viewDir,
			traceShadows);

		totalIllumination = totalIllumination + contribution;
	}

	// Clamp to positive values
	totalIllumination.x = fmaxf(0.0f, totalIllumination.x);
	totalIllumination.y = fmaxf(0.0f, totalIllumination.y);
	totalIllumination.z = fmaxf(0.0f, totalIllumination.z);
	totalIllumination.w = 1.0f;

	return totalIllumination;
}

//////////////////////////////////////////////////////////////////////////
// Evaluate Cluster
//
// Evaluates a single cluster node using its representative light
//
// Formula: Lᶜ = M × G × V × Iᶜ
// where:
//   M = Material term (BRDF × cos)
//   G = Geometric term (distance attenuation)
//   V = Visibility term (shadows)
//   Iᶜ = Cluster intensity (sum of all lights in cluster)
//////////////////////////////////////////////////////////////////////////
Vector4 jLightcutEvaluator::EvaluateCluster(
	jLightTreeNode* node,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir,
	bool traceShadows)
{
	JASSERT(node != nullptr);

	// Get representative light
	jLight* repLight = node->GetRepresentativeLight();
	if (repLight == nullptr)
		return Vector4::ZeroVector;

	// Evaluate material term (BRDF × cos)
	float M = EvaluateMaterialTerm(repLight, shadingPoint, normal, viewDir);
	if (M < EPSILON)
		return Vector4::ZeroVector;  // Early out if no material contribution

	// Evaluate geometric term (distance attenuation)
	float G = EvaluateGeometricTerm(repLight, shadingPoint);
	if (G < EPSILON)
		return Vector4::ZeroVector;  // Early out if too far

	// Evaluate visibility term (shadow test)
	float V = EvaluateVisibility(repLight, shadingPoint, traceShadows);
	if (V < EPSILON)
		return Vector4::ZeroVector;  // Early out if occluded

	// Get cluster intensity (sum of all lights in cluster)
	float I = node->GetClusterIntensity();

	// Get light color
	Vector4 lightColor = GetLightColor(repLight);

	// Combine terms: L = M × G × V × I × color
	Vector4 contribution = lightColor * (M * G * V * I);

	return contribution;
}

//////////////////////////////////////////////////////////////////////////
// Evaluate Material Term
//
// Simplified diffuse BRDF for Phase 3
//
// Formula: M = (kd/π) × max(N·L, 0)
// Assumes diffuse coefficient kd = 1.0
//////////////////////////////////////////////////////////////////////////
float jLightcutEvaluator::EvaluateMaterialTerm(
	jLight* light,
	const Vector& shadingPoint,
	const Vector& normal,
	const Vector& viewDir)
{
	JASSERT(light != nullptr);

	// Get light direction
	Vector lightDir = GetLightDirection(light, shadingPoint);

	// Compute N·L (Lambert's cosine law)
	float NdotL = normal.DotProduct(lightDir);
	NdotL = fmaxf(0.0f, NdotL);  // Clamp to positive (only front-facing)

	// Simplified diffuse BRDF: (1/π) × max(N·L, 0)
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
float jLightcutEvaluator::EvaluateGeometricTerm(
	jLight* light,
	const Vector& shadingPoint)
{
	JASSERT(light != nullptr);

	ELightType lightType = light->GetLightType();

	// Directional lights have no distance attenuation
	if (lightType == ELightType::DIRECTIONAL)
	{
		return 1.0f;
	}

	// Get light position
	Vector lightPos;

	if (lightType == ELightType::POINT)
	{
		jPointLight* pointLight = static_cast<jPointLight*>(light);
		lightPos = pointLight->GetLightData().Position;
	}
	else if (lightType == ELightType::SPOT)
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(light);
		lightPos = spotLight->GetLightData().Position;
	}
	else
	{
		return 0.0f;  // Unknown light type
	}

	// Compute distance squared
	float distSq = (lightPos - shadingPoint).LengthSQ();

	// Avoid division by zero
	if (distSq < EPSILON)
		distSq = EPSILON;

	// Inverse square falloff: G = 1 / r²
	return 1.0f / distSq;
}

//////////////////////////////////////////////////////////////////////////
// Evaluate Visibility
//
// For Phase 3: Always returns 1.0 (no shadows)
// For Phase 4: Trace shadow rays for accurate visibility
//
// Returns:
//   1.0 = Fully visible
//   0.0 = Fully occluded
//////////////////////////////////////////////////////////////////////////
float jLightcutEvaluator::EvaluateVisibility(
	jLight* light,
	const Vector& shadingPoint,
	bool traceShadows)
{
	// Phase 3: Simplified - assume all lights are visible
	if (!traceShadows)
	{
		return 1.0f;
	}

	// Phase 4: TODO - Implement shadow ray tracing
	// For now, still return 1.0
	ShadowRayCount++;  // Count the shadow ray we would trace
	return 1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Get Light Color
//
// Extracts the light's color/intensity as a Vector4
// The color represents the light's radiant intensity
//////////////////////////////////////////////////////////////////////////
Vector4 jLightcutEvaluator::GetLightColor(jLight* light)
{
	JASSERT(light != nullptr);

	ELightType lightType = light->GetLightType();

	if (lightType == ELightType::POINT)
	{
		jPointLight* pointLight = static_cast<jPointLight*>(light);
		const auto& lightData = pointLight->GetLightData();
		return Vector4(lightData.Color.x, lightData.Color.y, lightData.Color.z, 1.0f);
	}
	else if (lightType == ELightType::DIRECTIONAL)
	{
		jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
		const auto& lightData = dirLight->GetLightData();
		return Vector4(lightData.Color.x, lightData.Color.y, lightData.Color.z, 1.0f);
	}
	else if (lightType == ELightType::SPOT)
	{
		jSpotLight* spotLight = static_cast<jSpotLight*>(light);
		const auto& lightData = spotLight->GetLightData();
		return Vector4(lightData.Color.x, lightData.Color.y, lightData.Color.z, 1.0f);
	}

	// Default: white light
	return Vector4(1.0f, 1.0f, 1.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Get Light Direction
//
// Returns the normalized direction from shading point to light
// For directional lights, returns the light's direction
//////////////////////////////////////////////////////////////////////////
Vector jLightcutEvaluator::GetLightDirection(
	jLight* light,
	const Vector& shadingPoint)
{
	JASSERT(light != nullptr);

	ELightType lightType = light->GetLightType();

	if (lightType == ELightType::DIRECTIONAL)
	{
		// Directional lights have a constant direction
		jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
		Vector dir = dirLight->GetLightData().Direction * -1.0f;  // Negate to get direction TO light
		return dir.GetNormalize();
	}
	else if (lightType == ELightType::POINT)
	{
		// Point lights: direction from shading point to light position
		jPointLight* pointLight = static_cast<jPointLight*>(light);
		Vector lightPos = pointLight->GetLightData().Position;
		Vector dir = lightPos - shadingPoint;
		float len = dir.Length();
		if (len > EPSILON)
			return dir * (1.0f / len);  // Normalize
		return Vector::ZeroVector;
	}
	else if (lightType == ELightType::SPOT)
	{
		// Spot lights: direction from shading point to light position
		jSpotLight* spotLight = static_cast<jSpotLight*>(light);
		Vector lightPos = spotLight->GetLightData().Position;
		Vector dir = lightPos - shadingPoint;
		float len = dir.Length();
		if (len > EPSILON)
			return dir * (1.0f / len);  // Normalize
		return Vector::ZeroVector;
	}

	return Vector::ZeroVector;
}
