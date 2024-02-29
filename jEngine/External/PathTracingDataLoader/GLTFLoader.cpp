/*
 * MIT License
 *
 * Copyright(c) 2019 Asif Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

 // Much of this is from accompanying code for Ray Tracing Gems II, Chapter 14: The Reference Path Tracer
 // and was adapted for this project. See https://github.com/boksajak/referencePT for the original


#include "pch.h"
#include <map>
#include <cstdint>
#include "GLTFLoader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
#include "Math/Quaternion.h"
#include "FileLoader/jImageFileLoader.h"

namespace GLSLPT
{
    struct Primitive
    {
        int32 primitiveId;
        int32 materialId;
    };

    // Note: A GLTF mesh can contain multiple primitives and each primitive can potentially have a different material applied.
    // The two level BVH in this repo holds material ids per mesh and not per primitive, so this function loads each primitive from the gltf mesh as a new mesh
    void LoadMeshes(jPathTracingLoadData* scene, tinygltf::Model& gltfModel, std::map<int, std::vector<Primitive>>& meshPrimMap)
    {
        for (int32 gltfMeshIdx = 0; gltfMeshIdx < gltfModel.meshes.size(); gltfMeshIdx++)
        {
            tinygltf::Mesh gltfMesh = gltfModel.meshes[gltfMeshIdx];

            for (int32 gltfPrimIdx = 0; gltfPrimIdx < gltfMesh.primitives.size(); gltfPrimIdx++)
            {
                tinygltf::Primitive prim = gltfMesh.primitives[gltfPrimIdx];

                // Skip points and lines
                if (prim.mode != TINYGLTF_MODE_TRIANGLES)
                    continue;

                int32 indicesIndex = prim.indices;
                int32 positionIndex = -1;
                int32 normalIndex = -1;
                int32 uv0Index = -1;

                if (prim.attributes.count("POSITION") > 0)
                {
                    positionIndex = prim.attributes["POSITION"];
                }

                if (prim.attributes.count("NORMAL") > 0)
                {
                    normalIndex = prim.attributes["NORMAL"];
                }

                if (prim.attributes.count("TEXCOORD_0") > 0)
                {
                    uv0Index = prim.attributes["TEXCOORD_0"];
                }

                // Vertex positions
                tinygltf::Accessor positionAccessor = gltfModel.accessors[positionIndex];
                tinygltf::BufferView positionBufferView = gltfModel.bufferViews[positionAccessor.bufferView];
                const tinygltf::Buffer& positionBuffer = gltfModel.buffers[positionBufferView.buffer];
                const uint8_t* positionBufferAddress = positionBuffer.data.data();
                int32 positionStride = tinygltf::GetComponentSizeInBytes(positionAccessor.componentType) * tinygltf::GetNumComponentsInType(positionAccessor.type);
                // TODO: Recheck
                if (positionBufferView.byteStride > 0)
                    positionStride = (int32)positionBufferView.byteStride;

                // FIXME: Some GLTF files like TriangleWithoutIndices.gltf have no indices
                // Vertex indices
                tinygltf::Accessor indexAccessor = gltfModel.accessors[indicesIndex];
                tinygltf::BufferView indexBufferView = gltfModel.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = gltfModel.buffers[indexBufferView.buffer];
                const uint8_t* indexBufferAddress = indexBuffer.data.data();
                int32 indexStride = tinygltf::GetComponentSizeInBytes(indexAccessor.componentType) * tinygltf::GetNumComponentsInType(indexAccessor.type);

                // Normals
                tinygltf::Accessor normalAccessor;
                tinygltf::BufferView normalBufferView;
                const uint8_t* normalBufferAddress = nullptr;
                int32 normalStride = -1;
                if (normalIndex > -1)
                {
                    normalAccessor = gltfModel.accessors[normalIndex];
                    normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
                    const tinygltf::Buffer& normalBuffer = gltfModel.buffers[normalBufferView.buffer];
                    normalBufferAddress = normalBuffer.data.data();
                    normalStride = tinygltf::GetComponentSizeInBytes(normalAccessor.componentType) * tinygltf::GetNumComponentsInType(normalAccessor.type);
                    if (normalBufferView.byteStride > 0)
                        normalStride = (int32)normalBufferView.byteStride;
                }

                // Texture coordinates
                tinygltf::Accessor uv0Accessor;
                tinygltf::BufferView uv0BufferView;
                const uint8_t* uv0BufferAddress = nullptr;
                int32 uv0Stride = -1;
                if (uv0Index > -1)
                {
                    uv0Accessor = gltfModel.accessors[uv0Index];
                    uv0BufferView = gltfModel.bufferViews[uv0Accessor.bufferView];
                    const tinygltf::Buffer& uv0Buffer = gltfModel.buffers[uv0BufferView.buffer];
                    uv0BufferAddress = uv0Buffer.data.data();
                    uv0Stride = tinygltf::GetComponentSizeInBytes(uv0Accessor.componentType) * tinygltf::GetNumComponentsInType(uv0Accessor.type);
                    if (uv0BufferView.byteStride > 0)
                        uv0Stride = (int32)uv0BufferView.byteStride;
                }

                std::vector<Vector> vertices;
                std::vector<Vector> normals;
                std::vector<Vector2> uvs;

                // Get vertex data
                for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count; vertexIndex++)
                {
                    Vector vertex, normal;
                    Vector2 uv;

                    {
                        const uint8_t* address = positionBufferAddress + positionBufferView.byteOffset + positionAccessor.byteOffset + (vertexIndex * positionStride);
                        memcpy(&vertex, address, 12);
                    }

                    if (normalIndex > -1)
                    {
                        const uint8_t* address = normalBufferAddress + normalBufferView.byteOffset + normalAccessor.byteOffset + (vertexIndex * normalStride);
                        memcpy(&normal, address, 12);
                    }

                    if (uv0Index > -1)
                    {
                        const uint8_t* address = uv0BufferAddress + uv0BufferView.byteOffset + uv0Accessor.byteOffset + (vertexIndex * uv0Stride);
                        memcpy(&uv, address, 8);
                    }

                    vertices.push_back(vertex);
                    normals.push_back(normal);
                    uvs.push_back(uv);
                }

                // Get index data
                std::vector<int> indices(indexAccessor.count);
                const uint8_t* baseAddress = indexBufferAddress + indexBufferView.byteOffset + indexAccessor.byteOffset;
                if (indexStride == 1)
                {
                    std::vector<uint8_t> quarter;
                    quarter.resize(indexAccessor.count);

                    memcpy(quarter.data(), baseAddress, (indexAccessor.count * indexStride));

                    // Convert quarter precision indices to full precision
                    for (size_t i = 0; i < indexAccessor.count; i++)
                    {
                        indices[i] = quarter[i];
                    }
                }
                else if (indexStride == 2)
                {
                    std::vector<uint16_t> half;
                    half.resize(indexAccessor.count);

                    memcpy(half.data(), baseAddress, (indexAccessor.count * indexStride));

                    // Convert half precision indices to full precision
                    for (size_t i = 0; i < indexAccessor.count; i++)
                    {
                        indices[i] = half[i];
                    }
                }
                else
                {
                    memcpy(indices.data(), baseAddress, (indexAccessor.count * indexStride));
                }

                Mesh* mesh = new Mesh();

                // Get triangles from vertex indices
                for (int32 v = 0; v < indices.size(); v++)
                {
                    Vector pos = vertices[indices[v]];
                    Vector nrm = normals[indices[v]];
                    Vector2 uv = uvs[indices[v]];

                    #if PATH_TRACING_DATA_LEFT_HAND
					mesh->verticesUVX.push_back(Vector4(-pos.x, pos.y, pos.z, uv.x));
					mesh->normalsUVY.push_back(Vector4(-nrm.x, nrm.y, nrm.z, uv.y));
                    #else
                    mesh->verticesUVX.push_back(Vector4(pos.x, pos.y, pos.z, uv.x));
                    mesh->normalsUVY.push_back(Vector4(nrm.x, nrm.y, nrm.z, uv.y));
                    #endif // PATH_TRACING_DATA_LEFT_HAND
                }

                mesh->name = gltfMesh.name;
                int32 sceneMeshId = (int32)scene->meshes.size();
                scene->meshes.push_back(mesh);
                // Store a mapping for a gltf mesh and the loaded primitive data
                // This is used for creating instances based on the primitive
                int32 sceneMatIdx = (int32)(prim.material + scene->materials.size());
                meshPrimMap[gltfMeshIdx].push_back(Primitive{ sceneMeshId, sceneMatIdx });
            }
        }
    }

    void LoadTextures(jPathTracingLoadData* scene, tinygltf::Model& gltfModel)
    {
        for (size_t i = 0; i < gltfModel.textures.size(); ++i)
        {
            tinygltf::Texture& gltfTex = gltfModel.textures[i];
            tinygltf::Image& image = gltfModel.images[gltfTex.source];
            std::string texName = gltfTex.name;
            if (strcmp(gltfTex.name.c_str(), "") == 0)
                texName = image.uri;

            jTexture* texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName(texName.c_str())).lock().get();
            check(texture);
            scene->textures.push_back(texture);
        }
    }

    void LoadMaterials(jPathTracingLoadData* scene, tinygltf::Model& gltfModel)
    {
        int32 sceneTexIdx = (int32)scene->textures.size();
        // TODO: Support for KHR extensions
        for (size_t i = 0; i < gltfModel.materials.size(); i++)
        {
            const tinygltf::Material gltfMaterial = gltfModel.materials[i];
            const tinygltf::PbrMetallicRoughness pbr = gltfMaterial.pbrMetallicRoughness;

            // Convert glTF material
            Material material;

            // Albedo
            material.baseColor = Vector((float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1], (float)pbr.baseColorFactor[2]);
            if (pbr.baseColorTexture.index > -1)
                material.baseColorTexId = (pbr.baseColorTexture.index + sceneTexIdx);

            // Opacity
            material.opacity = (float)pbr.baseColorFactor[3];

            // Alpha
            material.alphaCutoff = static_cast<float>(gltfMaterial.alphaCutoff);
            if (strcmp(gltfMaterial.alphaMode.c_str(), "OPAQUE") == 0) material.alphaMode = AlphaMode::Opaque;
            else if (strcmp(gltfMaterial.alphaMode.c_str(), "BLEND") == 0) material.alphaMode = AlphaMode::Blend;
            else if (strcmp(gltfMaterial.alphaMode.c_str(), "MASK") == 0) material.alphaMode = AlphaMode::Mask;

            // Roughness and Metallic
            material.roughness = sqrtf((float)pbr.roughnessFactor); // Repo's disney material doesn't use squared roughness
            material.metallic = (float)pbr.metallicFactor;
            if (pbr.metallicRoughnessTexture.index > -1)
                material.metallicRoughnessTexID = (pbr.metallicRoughnessTexture.index + sceneTexIdx);

            // Normal Map
            material.normalmapTexID = (gltfMaterial.normalTexture.index + sceneTexIdx);

            // Emission
            material.emission = Vector((float)gltfMaterial.emissiveFactor[0], (float)gltfMaterial.emissiveFactor[1], (float)gltfMaterial.emissiveFactor[2]);
            if (gltfMaterial.emissiveTexture.index > -1)
                material.emissionmapTexID = (gltfMaterial.emissiveTexture.index + sceneTexIdx);

            // KHR_materials_transmission
            if (gltfMaterial.extensions.find("KHR_materials_transmission") != gltfMaterial.extensions.end())
            {
                const auto& ext = gltfMaterial.extensions.find("KHR_materials_transmission")->second;
                if (ext.Has("transmissionFactor"))
                    material.specTrans = (float)(ext.Get("transmissionFactor").Get<double>());
            }

            scene->AddMaterial(material);
        }

        // Default material
        if (scene->materials.size() == 0)
        {
            Material defaultMat;
            scene->materials.push_back(defaultMat);
        }
    }

    void TraverseNodes(jPathTracingLoadData* scene, tinygltf::Model& gltfModel, int32 nodeIdx, Matrix& parentMat, std::map<int, std::vector<Primitive>>& meshPrimMap)
    {
        tinygltf::Node gltfNode = gltfModel.nodes[nodeIdx];

        Matrix localMat;

        if (gltfNode.matrix.size() > 0)
        {
            localMat.m[0][0] = (float)gltfNode.matrix[0];
            localMat.m[0][1] = (float)gltfNode.matrix[1];
            localMat.m[0][2] = (float)gltfNode.matrix[2];
            localMat.m[0][3] = (float)gltfNode.matrix[3];

            localMat.m[1][0] = (float)gltfNode.matrix[4];
            localMat.m[1][1] = (float)gltfNode.matrix[5];
            localMat.m[1][2] = (float)gltfNode.matrix[6];
            localMat.m[1][3] = (float)gltfNode.matrix[7];

            localMat.m[2][0] = (float)gltfNode.matrix[8];
            localMat.m[2][1] = (float)gltfNode.matrix[9];
            localMat.m[2][2] = (float)gltfNode.matrix[10];
            localMat.m[2][3] = (float)gltfNode.matrix[11];

            localMat.m[3][0] = (float)gltfNode.matrix[12];
            localMat.m[3][1] = (float)gltfNode.matrix[13];
            localMat.m[3][2] = (float)gltfNode.matrix[14];
            localMat.m[3][3] = (float)gltfNode.matrix[15];
        }
        else
        {
            Matrix translate, rot, scale;

            if (gltfNode.translation.size() > 0)
            {
                translate.m[3][0] = (float)gltfNode.translation[0];
                translate.m[3][1] = (float)gltfNode.translation[1];
                translate.m[3][2] = (float)gltfNode.translation[2];
                #if PATH_TRACING_DATA_LEFT_HAND
                translate.m[3][0] *= -1.0f;
                #endif // PATH_TRACING_DATA_LEFT_HAND
            }

            if (gltfNode.rotation.size() > 0)
            {
                rot = Quaternion((float)gltfNode.rotation[0], (float)gltfNode.rotation[1]
                    , (float)gltfNode.rotation[2], (float)gltfNode.rotation[3]).GetMatrix();
            }

            if (gltfNode.scale.size() > 0)
            {
                scale.m[0][0] = (float)gltfNode.scale[0];
                scale.m[1][1] = (float)gltfNode.scale[1];
                scale.m[2][2] = (float)gltfNode.scale[2];
            }

            localMat = translate * rot * scale;
        }

        Matrix xform = localMat * parentMat;

        // When at a leaf node, add an instance to the scene (if a mesh exists for it)
        if (gltfNode.children.size() == 0 && gltfNode.mesh != -1)
        {
            std::vector<Primitive> prims = meshPrimMap[gltfNode.mesh];

            // Write the instance data
            for (int32 i = 0; i < prims.size(); i++)
            {
                std::string name = gltfNode.name;
                // TODO: Better naming
                if (strcmp(name.c_str(), "") == 0)
                    name = "Mesh " + std::to_string(gltfNode.mesh) + " Prim" + std::to_string(prims[i].primitiveId);

                MeshInstance instance(name, prims[i].primitiveId, xform, prims[i].materialId < 0 ? 0 : prims[i].materialId);
                scene->AddMeshInstance(instance);
            }
        }

        for (size_t i = 0; i < gltfNode.children.size(); i++)
        {
            TraverseNodes(scene, gltfModel, gltfNode.children[i], xform, meshPrimMap);
        }
    }

    void LoadInstances(jPathTracingLoadData* scene, tinygltf::Model& gltfModel, Matrix xform, std::map<int, std::vector<Primitive>>& meshPrimMap)
    {
        const tinygltf::Scene gltfScene = gltfModel.scenes[gltfModel.defaultScene];

        for (int32 rootIdx = 0; rootIdx < gltfScene.nodes.size(); rootIdx++)
        {
            TraverseNodes(scene, gltfModel, gltfScene.nodes[rootIdx], xform, meshPrimMap);
        }
    }

    bool LoadGLTF(const std::string& filename, jPathTracingLoadData* scene, jRenderOptions& renderOptions, Matrix xform, bool binary)
    {
        tinygltf::Model gltfModel;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        printf("Loading GLTF %s\n", filename.c_str());

        bool ret;

        if (binary)
            ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
        else
            ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);

        if (!ret)
        {
            printf("Unable to load file %s. Error: %s\n", filename.c_str(), err.c_str());
            return false;
        }

        std::map<int, std::vector<Primitive>> meshPrimMap;
        LoadMeshes(scene, gltfModel, meshPrimMap);
        LoadMaterials(scene, gltfModel);
        LoadTextures(scene, gltfModel);
        LoadInstances(scene, gltfModel, xform, meshPrimMap);

        return true;
    }
}
