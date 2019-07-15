#pragma once
#include "Math/Vector.h"

class jVertexAdjacency;
class jObject;
struct jVertexStreamData;

class jShadowVolume
{
public:
	jShadowVolume() = default;
	jShadowVolume(jVertexAdjacency* vertexAdjacency) : VertexAdjacency(vertexAdjacency) {}

	void Update(const Vector& lightPosOrDirection, bool isOmniDirectional, jObject* ownerObject);

	jObject* EdgeObject = nullptr;
	jObject* QuadObject = nullptr;

private:
	void UpdateEdge(size_t edgeKey);		// to do the edge detection
	void CreateShadowVolumeObject();
	void UpdateShadowVolumeObject();

private:
	bool IsInitialized = false;
	bool CreateEdgeObject = true;
	bool CreateQuadObject = true;
	jVertexAdjacency* VertexAdjacency = nullptr;
	std::set<size_t> Edges;
	std::vector<float> EdgeVertices;
	std::vector<float> QuadVertices;

	std::shared_ptr<jVertexStreamData> EdgeVertexStreamData;

	std::shared_ptr<jVertexStreamData> QuadVertexStreamData;
};
