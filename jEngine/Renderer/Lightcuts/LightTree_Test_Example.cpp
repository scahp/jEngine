// Example test code for Light Tree
// This file demonstrates how to use the Light Tree structure
// NOT COMPILED - Just for reference

#include "jLightTree.h"
#include "Scene/Light/jLight.h"

void TestLightTree()
{
	// Example 1: Create test lights
	std::vector<jLight*> testLights;

	// Create 4 point lights in a square pattern
	testLights.push_back(jLight::CreatePointLight(
		Vector(0.0f, 0.0f, 0.0f),           // Position
		Vector(1.0f, 1.0f, 1.0f),           // Color (white)
		100.0f,                              // Max distance
		Vector(1.0f, 1.0f, 1.0f),           // Diffuse intensity
		Vector(0.5f, 0.5f, 0.5f),           // Specular intensity
		32.0f                                // Specular power
	));

	testLights.push_back(jLight::CreatePointLight(
		Vector(10.0f, 0.0f, 0.0f),
		Vector(1.0f, 0.0f, 0.0f),           // Red
		100.0f,
		Vector(1.0f, 1.0f, 1.0f),
		Vector(0.5f, 0.5f, 0.5f),
		32.0f
	));

	testLights.push_back(jLight::CreatePointLight(
		Vector(0.0f, 0.0f, 10.0f),
		Vector(0.0f, 1.0f, 0.0f),           // Green
		100.0f,
		Vector(1.0f, 1.0f, 1.0f),
		Vector(0.5f, 0.5f, 0.5f),
		32.0f
	));

	testLights.push_back(jLight::CreatePointLight(
		Vector(10.0f, 0.0f, 10.0f),
		Vector(0.0f, 0.0f, 1.0f),           // Blue
		100.0f,
		Vector(1.0f, 1.0f, 1.0f),
		Vector(0.5f, 0.5f, 0.5f),
		32.0f
	));

	// Example 2: Build light tree
	jLightTree tree;
	jLightTreeBuildOptions options;
	options.SpatialDirectionalScale = 1.0f;
	options.RandomizeRepresentative = true;

	tree.Build(testLights, options);

	// Example 3: Verify tree structure
	JASSERT(tree.IsBuilt());
	JASSERT(tree.GetNumLights() == 4);
	JASSERT(tree.GetRoot() != nullptr);

	jLightTreeNode* root = tree.GetRoot();
	JASSERT(!root->IsLeaf());  // Root should be internal node
	JASSERT(root->GetNumLights() == 4);
	JASSERT(root->GetClusterIntensity() > 0.0f);

	// Example 4: Verify bounding box
	const jBoundingBox& bbox = root->GetBoundingBox();
	Vector center = bbox.GetCenter();
	float diagonal = bbox.GetDiagonal();

	// Center should be around (5, 0, 5)
	JASSERT(fabs(center.x - 5.0f) < 0.1f);
	JASSERT(fabs(center.z - 5.0f) < 0.1f);

	// Diagonal should be around sqrt(10^2 + 10^2) = 14.14
	JASSERT(diagonal > 14.0f && diagonal < 15.0f);

	// Example 5: Check error bounds
	Vector shadingPoint(5.0f, 5.0f, 5.0f);
	float geometricTerm = root->ComputeMaxGeometricTerm(shadingPoint);
	JASSERT(geometricTerm > 0.0f);

	// Example 6: Memory usage
	size_t memoryBytes = tree.GetMemoryUsage();
	// Should be approximately: 4 leaves + 3 internal nodes = 7 nodes
	// Each node is ~200 bytes, so around 1400 bytes
	JASSERT(memoryBytes > 0);

	// Clean up
	tree.Clear();
}

void TestMultiTypeLights()
{
	// Example: Mix of different light types
	std::vector<jLight*> mixedLights;

	// Add point lights
	for (int i = 0; i < 5; ++i)
	{
		mixedLights.push_back(jLight::CreatePointLight(
			Vector(i * 10.0f, 0.0f, 0.0f),
			Vector(1.0f, 1.0f, 1.0f),
			100.0f,
			Vector(1.0f, 1.0f, 1.0f),
			Vector(0.5f, 0.5f, 0.5f),
			32.0f
		));
	}

	// Add directional lights
	for (int i = 0; i < 2; ++i)
	{
		mixedLights.push_back(jLight::CreateDirectionalLight(
			Vector(0.0f, -1.0f, 0.0f),      // Direction (down)
			Vector(1.0f, 1.0f, 0.8f),       // Color (warm white)
			Vector(0.8f, 0.8f, 0.8f),       // Diffuse intensity
			Vector(0.3f, 0.3f, 0.3f),       // Specular intensity
			64.0f                            // Specular power
		));
	}

	// Add spot lights
	for (int i = 0; i < 3; ++i)
	{
		mixedLights.push_back(jLight::CreateSpotLight(
			Vector(i * 5.0f, 10.0f, 0.0f),  // Position
			Vector(0.0f, -1.0f, 0.0f),      // Direction (down)
			Vector(1.0f, 1.0f, 1.0f),       // Color
			50.0f,                           // Max distance
			0.3f,                            // Penumbra angle
			0.5f,                            // Umbra angle
			Vector(1.0f, 1.0f, 1.0f),       // Diffuse intensity
			Vector(0.5f, 0.5f, 0.5f),       // Specular intensity
			32.0f                            // Specular power
		));
	}

	// Build tree
	jLightTree tree;
	jLightTreeBuildOptions options;
	options.SpatialDirectionalScale = 100.0f;  // Scene diagonal estimate
	options.RandomizeRepresentative = true;

	tree.Build(mixedLights, options);

	// Verify
	JASSERT(tree.IsBuilt());
	JASSERT(tree.GetNumLights() == 10);  // 5 point + 2 directional + 3 spot

	// The tree should have 3 subtrees (one per type) merged at root
	jLightTreeNode* root = tree.GetRoot();
	JASSERT(root != nullptr);
	JASSERT(!root->IsLeaf());

	// Clean up
	tree.Clear();
}

void TestSingleLight()
{
	// Edge case: single light
	std::vector<jLight*> singleLight;
	singleLight.push_back(jLight::CreatePointLight(
		Vector(0.0f, 0.0f, 0.0f),
		Vector(1.0f, 1.0f, 1.0f),
		100.0f,
		Vector(1.0f, 1.0f, 1.0f),
		Vector(0.5f, 0.5f, 0.5f),
		32.0f
	));

	jLightTree tree;
	jLightTreeBuildOptions options;
	tree.Build(singleLight, options);

	// Single light should be a leaf node as root
	JASSERT(tree.IsBuilt());
	JASSERT(tree.GetNumLights() == 1);
	JASSERT(tree.GetRoot()->IsLeaf());

	tree.Clear();
}

void TestEmptyLights()
{
	// Edge case: no lights
	std::vector<jLight*> noLights;

	jLightTree tree;
	jLightTreeBuildOptions options;
	tree.Build(noLights, options);

	// Should result in empty tree
	JASSERT(!tree.IsBuilt());
	JASSERT(tree.GetRoot() == nullptr);
	JASSERT(tree.GetNumLights() == 0);
}
