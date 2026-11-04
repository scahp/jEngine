#pragma once

// Private CPU payload shared by preparation and owner-thread upload implementation.
#include "jModelLoader.h"
#include "jImageFileLoader.h"
#include "Scene/jMeshObject.h"

struct jPreparedModel::Data
{
    struct Image { jName Filename; std::shared_ptr<jImageData> Decoded; };
    std::unique_ptr<jMeshData> MeshData = std::make_unique<jMeshData>();
    std::vector<jSubMesh> SubMeshes;
    std::vector<std::unique_ptr<jMeshNode>> Nodes;
    jMeshNode* RootNode = nullptr;
    std::shared_ptr<jVertexStreamData> VertexStream;
    std::shared_ptr<jVertexStreamData> PositionStream;
    std::shared_ptr<jIndexStreamData> IndexStream;
    std::vector<std::shared_ptr<Image>> Images;
    jModelPreparationStats Stats;
};
