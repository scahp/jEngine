#pragma once

#include "jLightTreeNode.h"
#include <vector>
#include <memory>

//////////////////////////////////////////////////////////////////////////
// Light Tree
//
// Manages the hierarchical light clustering structure used by Lightcuts.
// Builds a binary tree where:
// - Leaves: Individual lights
// - Internal nodes: Light clusters
//
// Build algorithm:
// 1. Separate lights by type (Omni, Oriented, Directional)
// 2. For each type, build subtree using bottom-up greedy clustering
// 3. Merge subtrees into single root
//
// The similarity metric for clustering is:
//   IC × (αC² + c² × (1 - cosβC)²)
// where:
//   IC = cluster intensity
//   αC = cluster spatial extent (bounding box diagonal)
//   βC = cluster directional extent (bounding cone angle)
//   c = spatial/directional scale factor
//////////////////////////////////////////////////////////////////////////

class jLightTree
{
public:
	jLightTree();
	~jLightTree();

	//////////////////////////////////////////////////////////////////////////
	// Build Tree
	//////////////////////////////////////////////////////////////////////////

	// Build tree from a list of lights
	void Build(const std::vector<jLight*>& lights, const jLightTreeBuildOptions& options);

	// Clear the tree and free all nodes
	void Clear();

	//////////////////////////////////////////////////////////////////////////
	// Access
	//////////////////////////////////////////////////////////////////////////

	// Get root node of the tree
	FORCEINLINE jLightTreeNode* GetRoot() const { return Root; }

	// Get total number of lights in the tree
	FORCEINLINE int32 GetNumLights() const { return TotalLights; }

	// Get memory usage in bytes
	size_t GetMemoryUsage() const;

	// Check if tree is built
	FORCEINLINE bool IsBuilt() const { return Root != nullptr; }

private:
	//////////////////////////////////////////////////////////////////////////
	// Build Helpers
	//////////////////////////////////////////////////////////////////////////

	// Build a subtree for a specific light type using bottom-up greedy clustering
	void BuildForLightType(std::vector<jLightTreeNode*>& nodes, ELightcutLightType type, const jLightTreeBuildOptions& options);

	// Compute similarity between two nodes using the clustering metric
	// Metric: IC × (αC² + c² × (1 - cosβC)²)
	float ComputeSimilarity(jLightTreeNode* a, jLightTreeNode* b, float spatialScale) const;

	// Find the pair of nodes with maximum similarity
	void FindMostSimilarPair(const std::vector<jLightTreeNode*>& nodes, int32& outIndexA, int32& outIndexB, float spatialScale) const;

	// Merge two nodes into a parent node
	jLightTreeNode* MergeNodes(jLightTreeNode* a, jLightTreeNode* b, bool randomRep);

	// Create a dummy parent to combine different light types
	jLightTreeNode* CreateDummyParent(jLightTreeNode* left, jLightTreeNode* right);

	//////////////////////////////////////////////////////////////////////////
	// Member Variables
	//////////////////////////////////////////////////////////////////////////

	// Root of the tree
	jLightTreeNode* Root = nullptr;

	// Total number of lights
	int32 TotalLights = 0;

	// All nodes (for memory management)
	std::vector<std::unique_ptr<jLightTreeNode>> AllNodes;

	// Build options used
	jLightTreeBuildOptions BuildOptions;
};
