#pragma once

#include "jLightTreeNode.h"
#include <vector>

//////////////////////////////////////////////////////////////////////////
// Lightcut Data Structures
//
// A lightcut is an adaptive subset of light tree nodes that provides
// an approximate lighting solution for a shading point.
//
// Key concepts:
// - A cut is a set of nodes that partitions all lights in the scene
// - Each node is either accepted (part of the cut) or refined (replaced by children)
// - The refinement is guided by error bounds vs. error threshold
// - Final cut size is typically 100-500 nodes for thousands of lights
//
// References:
// - Lightcuts Paper Section 4: The Lightcuts Algorithm
// - Algorithm 1: Greedy Lightcut Refinement
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Lightcut
//
// Represents a computed lightcut for a single shading point
//////////////////////////////////////////////////////////////////////////
struct jLightcut
{
	// The nodes on the cut (partitioning the light tree)
	std::vector<jLightTreeNode*> Nodes;

	// Sum of radiance estimates from all nodes
	float TotalRadianceEstimate = 0.0f;

	// Number of shadow rays cast during evaluation (for statistics)
	int32 NumShadowRays = 0;

	// Clear the lightcut (reset to empty state)
	void Clear()
	{
		Nodes.clear();
		TotalRadianceEstimate = 0.0f;
		NumShadowRays = 0;
	}

	// Get the number of nodes in the cut
	int32 GetCutSize() const
	{
		return static_cast<int32>(Nodes.size());
	}

	// Reserve space for expected cut size (optimization)
	void Reserve(int32 expectedSize)
	{
		Nodes.reserve(expectedSize);
	}
};

//////////////////////////////////////////////////////////////////////////
// Lightcut Node Info
//
// Per-node information maintained during lightcut refinement
// Used in the priority queue to track error bounds
//////////////////////////////////////////////////////////////////////////
struct jLightcutNodeInfo
{
	// The tree node being considered
	jLightTreeNode* Node;

	// Radiance estimate for this cluster: L̃ₙ = M × G × V × I
	// This is the approximation using the representative light
	float RadianceEstimate;

	// Conservative upper bound on approximation error
	// If error < threshold, node is accepted into the cut
	float ErrorBound;

	// Constructor
	jLightcutNodeInfo(jLightTreeNode* node, float radiance, float error)
		: Node(node)
		, RadianceEstimate(radiance)
		, ErrorBound(error)
	{}

	// Default constructor (for STL containers)
	jLightcutNodeInfo()
		: Node(nullptr)
		, RadianceEstimate(0.0f)
		, ErrorBound(0.0f)
	{}

	// Comparison operator for priority queue
	// We want a max heap based on error bound (highest error = highest priority)
	// Note: std::priority_queue is a max heap, but operator< gives min heap
	// So we reverse the comparison to get max heap behavior
	bool operator<(const jLightcutNodeInfo& other) const
	{
		return ErrorBound < other.ErrorBound;  // Reverse for max heap
	}
};
