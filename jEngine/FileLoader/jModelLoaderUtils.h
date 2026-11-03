#pragma once

#include "Scene/jMeshObject.h"
#include "assimp/scene.h"
#include <algorithm>
#include <filesystem>
#include <limits>

namespace jModelLoaderUtils
{
    // Validate before writing: Triangulate still leaves points and lines in
    // imported files, whereas our index stream always draws triangles.
    inline bool AppendMesh(const aiMesh& mesh, jMeshData& data, jSubMesh& subMesh)
    {
        if (!mesh.HasPositions() || !mesh.HasFaces())
            return false;
        const size_t maxCount = static_cast<size_t>(std::numeric_limits<int32>::max());
        if (data.Vertices.size() > maxCount || data.Faces.size() > maxCount
            || mesh.mNumVertices > maxCount - data.Vertices.size()
            || mesh.mNumFaces > (maxCount - data.Faces.size()) / 3)
            return false;
        for (uint32 faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
        {
            const auto& face = mesh.mFaces[faceIndex];
            if (face.mNumIndices != 3 || !face.mIndices)
                return false;
            for (uint32 i = 0; i < 3; ++i)
                if (face.mIndices[i] >= mesh.mNumVertices)
                    return false;
        }

        subMesh.Name = mesh.mName.C_Str();
        subMesh.StartVertex = static_cast<int32>(data.Vertices.size());
        subMesh.EndVertex = subMesh.StartVertex + static_cast<int32>(mesh.mNumVertices);
        const size_t vertexCount = static_cast<size_t>(subMesh.EndVertex);
        data.Vertices.resize(vertexCount);
        // All streams share the same vertex offset, even when an earlier mesh
        // lacks UVs, normals, or tangents. Defaults also handle a whole file
        // without those optional attributes.
        data.Normals.resize(vertexCount, Vector(0.0f, 1.0f, 0.0f));
        data.Tangents.resize(vertexCount, Vector(1.0f, 0.0f, 0.0f));
        data.Bitangents.resize(vertexCount, Vector(0.0f, 0.0f, 1.0f));
        data.TexCoord.resize(vertexCount, Vector2(0.0f, 0.0f));
        const auto toVector = [](const aiVector3D& value) { return Vector(value.x, value.y, value.z); };
        for (uint32 i = 0; i < mesh.mNumVertices; ++i)
        {
            const size_t index = static_cast<size_t>(subMesh.StartVertex) + i;
            data.Vertices[index] = toVector(mesh.mVertices[i]);
            if (mesh.HasNormals())
                data.Normals[index] = toVector(mesh.mNormals[i]);
            if (mesh.HasTangentsAndBitangents())
            {
                data.Tangents[index] = toVector(mesh.mTangents[i]);
                data.Bitangents[index] = toVector(mesh.mBitangents[i]);
            }
            if (mesh.HasTextureCoords(0))
                data.TexCoord[index] = Vector2(mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y);
        }

        subMesh.StartFace = static_cast<int32>(data.Faces.size());
        for (uint32 i = 0; i < mesh.mNumFaces; ++i)
            data.Faces.insert(data.Faces.end(), mesh.mFaces[i].mIndices, mesh.mFaces[i].mIndices + 3);
        subMesh.EndFace = static_cast<int32>(data.Faces.size());
        subMesh.MaterialIndex = mesh.mMaterialIndex;
        return true;
    }

    inline bool Requires32BitIndices(const std::vector<uint32>& indices)
    {
        return std::any_of(indices.begin(), indices.end(), [](uint32 index) { return index > 0xFFFFu; });
    }

    inline std::filesystem::path ResolveTexturePath(std::string texturePath, const char* materialRootDir)
    {
        // Assimp preserves Windows separators from authoring files on macOS.
        std::replace(texturePath.begin(), texturePath.end(), '\\', '/');
        std::filesystem::path result(texturePath);
        if (!result.is_absolute() && materialRootDir)
            result = std::filesystem::path(materialRootDir) / result;
        return result.lexically_normal();
    }
}
