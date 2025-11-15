#include "pch.h"
#include "jLightTree.h"
#include <algorithm>
#include <cmath>

//////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
//////////////////////////////////////////////////////////////////////////

jLightTree::jLightTree()
	: Root(nullptr)
	, TotalLights(0)
{
}

jLightTree::~jLightTree()
{
	Clear();
}

//////////////////////////////////////////////////////////////////////////
// Clear
//////////////////////////////////////////////////////////////////////////

void jLightTree::Clear()
{
	// unique_ptr will automatically delete all nodes
	AllNodes.clear();
	Root = nullptr;
	TotalLights = 0;
}

//////////////////////////////////////////////////////////////////////////
// Build
//
// Main entry point for building the light tree.
// Algorithm:
// 1. Separate lights by type
// 2. Build subtree for each type
// 3. Merge subtrees into single root
//////////////////////////////////////////////////////////////////////////

void jLightTree::Build(const std::vector<jLight*>& lights, const jLightTreeBuildOptions& options)
{
	// Clear existing tree
	Clear();

	if (lights.empty())
		return;

	// Store build options
	BuildOptions = options;
	TotalLights = static_cast<int32>(lights.size());

	// Separate lights by type
	std::vector<jLight*> omniLights;
	std::vector<jLight*> orientedLights;
	std::vector<jLight*> directionalLights;

	for (jLight* light : lights)
	{
		// Skip ambient lights
		if (light->GetLightType() == ELightType::AMBIENT)
			continue;

		// Classify by type
		ELightType type = light->GetLightType();
		switch (type)
		{
		case ELightType::POINT:
			omniLights.push_back(light);
			break;

		case ELightType::SPOT:
			orientedLights.push_back(light);
			break;

		case ELightType::DIRECTIONAL:
			directionalLights.push_back(light);
			break;

		default:
			break;
		}
	}

	// Build subtrees for each type
	std::vector<jLightTreeNode*> subtreeRoots;

	// Build omni light subtree
	if (!omniLights.empty())
	{
		std::vector<jLightTreeNode*> omniNodes;
		for (jLight* light : omniLights)
		{
			AllNodes.push_back(std::make_unique<jLightTreeNode>(light));
			omniNodes.push_back(AllNodes.back().get());
		}
		BuildForLightType(omniNodes, ELightcutLightType::Omni, options);
		if (!omniNodes.empty())
			subtreeRoots.push_back(omniNodes[0]);
	}

	// Build oriented light subtree
	if (!orientedLights.empty())
	{
		std::vector<jLightTreeNode*> orientedNodes;
		for (jLight* light : orientedLights)
		{
			AllNodes.push_back(std::make_unique<jLightTreeNode>(light));
			orientedNodes.push_back(AllNodes.back().get());
		}
		BuildForLightType(orientedNodes, ELightcutLightType::Oriented, options);
		if (!orientedNodes.empty())
			subtreeRoots.push_back(orientedNodes[0]);
	}

	// Build directional light subtree
	if (!directionalLights.empty())
	{
		std::vector<jLightTreeNode*> directionalNodes;
		for (jLight* light : directionalLights)
		{
			AllNodes.push_back(std::make_unique<jLightTreeNode>(light));
			directionalNodes.push_back(AllNodes.back().get());
		}
		BuildForLightType(directionalNodes, ELightcutLightType::Directional, options);
		if (!directionalNodes.empty())
			subtreeRoots.push_back(directionalNodes[0]);
	}

	// Merge subtrees into single root
	if (subtreeRoots.empty())
	{
		Root = nullptr;
	}
	else if (subtreeRoots.size() == 1)
	{
		Root = subtreeRoots[0];
	}
	else
	{
		// Create dummy parents to combine different light types
		jLightTreeNode* current = subtreeRoots[0];
		for (size_t i = 1; i < subtreeRoots.size(); ++i)
		{
			current = CreateDummyParent(current, subtreeRoots[i]);
		}
		Root = current;
	}
}

//////////////////////////////////////////////////////////////////////////
// Build For Light Type
//
// Bottom-up greedy clustering algorithm:
// 1. Start with leaf nodes (one per light)
// 2. Find most similar pair
// 3. Merge into parent node
// 4. Repeat until one node remains
//////////////////////////////////////////////////////////////////////////

void jLightTree::BuildForLightType(std::vector<jLightTreeNode*>& nodes, ELightcutLightType type, const jLightTreeBuildOptions& options)
{
	if (nodes.empty())
		return;

	// Single light - already done
	if (nodes.size() == 1)
		return;

	// Determine spatial scale 'c' for similarity metric
	// For oriented lights, use scene diagonal; for others, use 0 (spatial only)
	float spatialScale = (type == ELightcutLightType::Oriented) ? options.SpatialDirectionalScale : 0.0f;

	// Bottom-up greedy clustering
	while (nodes.size() > 1)
	{
		// Find most similar pair
		int32 indexA = -1;
		int32 indexB = -1;
		FindMostSimilarPair(nodes, indexA, indexB, spatialScale);

		JASSERT(indexA >= 0 && indexB >= 0);
		JASSERT(indexA < static_cast<int32>(nodes.size()) && indexB < static_cast<int32>(nodes.size()));
		JASSERT(indexA != indexB);

		// Merge the pair
		jLightTreeNode* nodeA = nodes[indexA];
		jLightTreeNode* nodeB = nodes[indexB];
		jLightTreeNode* parent = MergeNodes(nodeA, nodeB, options.RandomizeRepresentative);

		// Remove merged nodes and add parent
		// Remove larger index first to avoid invalidating smaller index
		int32 removeFirst = (indexA > indexB) ? indexA : indexB;
		int32 removeSecond = (indexA > indexB) ? indexB : indexA;

		nodes.erase(nodes.begin() + removeFirst);
		nodes.erase(nodes.begin() + removeSecond);
		nodes.push_back(parent);
	}

	// Now nodes contains single root for this type
}

//////////////////////////////////////////////////////////////////////////
// Compute Similarity
//
// Similarity metric from paper:
//   IC × (αC² + c² × (1 - cosβC)²)
//
// where:
//   IC = cluster intensity (sum of a and b)
//   αC = spatial extent (bounding box diagonal)
//   βC = directional extent (bounding cone angle)
//   c = spatial/directional scale
//////////////////////////////////////////////////////////////////////////

float jLightTree::ComputeSimilarity(jLightTreeNode* a, jLightTreeNode* b, float spatialScale) const
{
	JASSERT(a != nullptr && b != nullptr);

	// Compute merged cluster properties
	float mergedIntensity = a->GetClusterIntensity() + b->GetClusterIntensity();

	// Compute merged bounding box diagonal
	jBoundingBox mergedBox = jBoundingBox::Merge(a->GetBoundingBox(), b->GetBoundingBox());
	float mergedDiagonal = mergedBox.GetDiagonal();
	float spatialTerm = mergedDiagonal * mergedDiagonal;

	// Compute merged bounding cone angle
	float directionalTerm = 0.0f;
	if (spatialScale > 0.0f)
	{
		const jBoundingCone& coneA = a->GetBoundingCone();
		const jBoundingCone& coneB = b->GetBoundingCone();

		// Simplified: compute angle between cone axes
		float dotProduct = coneA.Axis.DotProduct(coneB.Axis);
		dotProduct = fmaxf(-1.0f, fminf(1.0f, dotProduct));  // Clamp to [-1, 1]

		float cosAngle = dotProduct;
		float oneMinusCos = 1.0f - cosAngle;

		directionalTerm = spatialScale * spatialScale * oneMinusCos * oneMinusCos;
	}

	// Similarity = IC × (αC² + c² × (1 - cosβC)²)
	float similarity = mergedIntensity * (spatialTerm + directionalTerm);

	return similarity;
}

//////////////////////////////////////////////////////////////////////////
// Find Most Similar Pair
//
// Brute force search for pair with maximum similarity.
// Time complexity: O(n²) per iteration
// TODO: Optimize with spatial data structure (k-d tree, BVH)
//////////////////////////////////////////////////////////////////////////

void jLightTree::FindMostSimilarPair(const std::vector<jLightTreeNode*>& nodes, int32& outIndexA, int32& outIndexB, float spatialScale) const
{
	JASSERT(nodes.size() >= 2);

	float maxSimilarity = -1.0f;
	outIndexA = -1;
	outIndexB = -1;

	// Brute force: check all pairs
	for (size_t i = 0; i < nodes.size(); ++i)
	{
		for (size_t j = i + 1; j < nodes.size(); ++j)
		{
			float similarity = ComputeSimilarity(nodes[i], nodes[j], spatialScale);

			if (similarity > maxSimilarity)
			{
				maxSimilarity = similarity;
				outIndexA = static_cast<int32>(i);
				outIndexB = static_cast<int32>(j);
			}
		}
	}

	JASSERT(outIndexA >= 0 && outIndexB >= 0);
}

//////////////////////////////////////////////////////////////////////////
// Merge Nodes
//////////////////////////////////////////////////////////////////////////

jLightTreeNode* jLightTree::MergeNodes(jLightTreeNode* a, jLightTreeNode* b, bool randomRep)
{
	JASSERT(a != nullptr && b != nullptr);

	// Create parent node
	AllNodes.push_back(std::make_unique<jLightTreeNode>(a, b, randomRep));
	return AllNodes.back().get();
}

//////////////////////////////////////////////////////////////////////////
// Create Dummy Parent
//
// Creates a parent node to combine subtrees of different light types.
// Uses the first child's representative (arbitrary choice).
//////////////////////////////////////////////////////////////////////////

jLightTreeNode* jLightTree::CreateDummyParent(jLightTreeNode* left, jLightTreeNode* right)
{
	JASSERT(left != nullptr && right != nullptr);

	// For dummy parents combining different types, allow different types and use non-randomized merge
	// This is a special case and the representative doesn't matter much
	const bool randomRep = false;
	const bool allowDifferentTypes = true;
	AllNodes.push_back(std::make_unique<jLightTreeNode>(left, right, randomRep, allowDifferentTypes));
	return AllNodes.back().get();
}

//////////////////////////////////////////////////////////////////////////
// Get Memory Usage
//////////////////////////////////////////////////////////////////////////

size_t jLightTree::GetMemoryUsage() const
{
	size_t totalSize = sizeof(jLightTree);

	// Size of all nodes
	totalSize += AllNodes.size() * sizeof(jLightTreeNode);

	// Size of node vector
	totalSize += AllNodes.capacity() * sizeof(std::unique_ptr<jLightTreeNode>);

	return totalSize;
}
