// This is moidifed version of the https://github.com/knightcrawler25/GLSL-PathTracer repository.

#include "pch.h"
#include "jPathTracingData.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include "FileLoader/jImageFileLoader.h"

namespace GLSLPT
{

void jPathTracingLoadData::CreateSceneFor_jEngine()
{

}

bool Mesh::LoadFromFile(const std::string& filename)
{
    name = filename;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filename.c_str(), 0, true);

    if (!ret)
    {
        printf("Unable to load model\n");
        return false;
    }

    // Loop over shapes
    for (size_t s = 0; s < shapes.size(); s++)
    {
        // Loop over faces(polygon)
        size_t index_offset = 0;

        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
        {
            // Loop over vertices in the face.
            for (size_t v = 0; v < 3; v++)
            {
                // access to vertex
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
                tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
                tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
                tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

                tinyobj::real_t tx, ty;

                if (!attrib.texcoords.empty())
                {
                    tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                    ty = (tinyobj::real_t)(1.0 - attrib.texcoords[2 * idx.texcoord_index + 1]);
                }
                else
                {
                    if (v == 0)
                        tx = ty = 0;
                    else if (v == 1)
                        tx = 0, ty = 1;
                    else
                        tx = ty = 1;
                }

                verticesUVX.push_back(Vector4(vx, vy, vz, tx));
                normalsUVY.push_back(Vector4(nx, ny, nz, ty));
            }

            index_offset += 3;
        }
    }

    return true;
}

int32 jPathTracingLoadData::AddMesh(const std::string& filename)
{
    int32 id = -1;
    // Check if mesh was already loaded
    for (int32 i = 0; i < meshes.size(); i++)
        if (meshes[i]->name == filename)
            return i;

    id = (int32)meshes.size();
    Mesh* mesh = new Mesh;

    printf("Loading model %s\n", filename.c_str());
    if (mesh->LoadFromFile(filename))
        meshes.push_back(mesh);
    else
    {
        printf("Unable to load model %s\n", filename.c_str());
        delete mesh;
        id = -1;
    }
    return id;
}

int32 jPathTracingLoadData::AddTexture(const std::string& filename)
{
    int32 id = -1;
    // Check if texture was already loaded
    for (int32 i = 0; i < textures.size(); i++)
        if (textures[i]->ResourceName.ToStr() == filename)
            return i;

    id = (int32)textures.size();

    printf("Loading texture %s\n", filename.c_str());
    jTexture* texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName(filename.c_str())).lock().get();

    if (texture)
    {
        textures.push_back(texture);
    }
    else
    {
        printf("Unable to load texture %s\n", filename.c_str());
        id = -1;
    }

    return id;
}

int32 jPathTracingLoadData::AddMaterial(const Material& material)
{
    int32 id = (int32)materials.size();
    materials.push_back(material);
    return id;
}

int32 jPathTracingLoadData::AddMeshInstance(const MeshInstance& meshInstance)
{
    int32 id = (int32)meshInstances.size();
    meshInstances.push_back(meshInstance);
    return id;
}

int32 jPathTracingLoadData::AddLight(const Light& light)
{
    int32 id = (int32)lights.size();
    lights.push_back(light);
    return id;
}

void jPathTracingLoadData::AddCamera(const Vector& InPos, const Vector& InTarget, const Vector& InUp, float InFov, float InNear, float InFar)
{
    jCamera::SetCamera(&Camera, InPos, InTarget, InUp, InFov, InNear, InFar, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
}

void jPathTracingLoadData::AddEnvMap(const std::string& filename)
{
    //if (envMap)
    //    delete envMap;

    //envMap = new EnvironmentMap;
    //if (envMap->LoadMap(filename.c_str()))
    //    printf("HDR %s loaded\n", filename.c_str());
    //else
    //{
    //    printf("Unable to load HDR\n");
    //    delete envMap;
    //    envMap = nullptr;
    //}
    //envMapModified = true;
    //dirty = true;
}

}