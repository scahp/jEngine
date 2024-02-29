// This is moidifed version of the https://github.com/knightcrawler25/GLSL-PathTracer repository.

#include "pch.h"
#include "jPathTracingData.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "FileLoader/jImageFileLoader.h"
#include "PathTracingDataLoader.h"
#include "GLTFLoader.h"
#include "Scene/jRenderObject.h"
#include "Scene/jObject.h"
#include "jGame.h"
#include "Material/jMaterial.h"
#include "Scene/Light/jPathTracingLight.h"
#include "jPrimitiveUtil.h"

jPathTracingLoadData* gPathTracingScene = nullptr;

jPathTracingLoadData* jPathTracingLoadData::LoadPathTracingData(std::string InFilename)
{
    jPathTracingLoadData* NewData = new jPathTracingLoadData();

	std::string ext = InFilename.substr(InFilename.find_last_of(".") + 1);

	bool success = false;
	Matrix xform;

	jRenderOptions renderOptions;

	if (ext == "scene")
		success = GLSLPT::LoadSceneFromFile(InFilename, NewData, renderOptions);
	else if (ext == "gltf")
		success = GLSLPT::LoadGLTF(InFilename, NewData, renderOptions, xform, false);
	else if (ext == "glb")
		success = GLSLPT::LoadGLTF(InFilename, NewData, renderOptions, xform, true);

	if (!success)
	{
		check(0);
	}

	//selectedInstance = 0;

	//// Add a default HDR if there are no lights in the scene
	//if (!scene->envMap && !envMaps.empty())
	//{
	//    scene->AddEnvMap(envMaps[envMapIdx]);
	//    renderOptions.enableEnvMap = scene->lights.empty() ? true : false;
	//    renderOptions.envMapIntensity = 1.5f;
	//}

    NewData->renderOptions = renderOptions;

    return NewData;
}

void jPathTracingLoadData::CreateSceneFor_jEngine(jGame* InGame)
{
#define OBJECT_HIT_GROUP_INDEX 0
#define LIGHT_HIT_GROUP_INDEX 1

    // Create mesh for jObject
    for (const MeshInstance& MeshInst : meshInstances)
    {
		auto object = new jObject();
		auto renderObject = new jRenderObject();
		object->RenderObjects.push_back(renderObject);
		jObject::AddObject(object);

        // Create Gemoetry
        Mesh* mesh = meshes[MeshInst.meshID];

        const int32 elementCount = (int32)mesh->verticesUVX.size();
		auto positionOnlyVertexStreamData = std::make_shared<jVertexStreamData>();
		{
			auto streamParam = std::make_shared<jStreamParam<jPositionOnlyVertex>>();
			streamParam->BufferType = EBufferType::STATIC;
			streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("POSITION"), EBufferElementType::FLOAT, sizeof(float) * 3));
			streamParam->Name = jName("jPositionOnlyVertex");
			streamParam->Stride = sizeof(jPositionOnlyVertex);
            streamParam->Data.resize(elementCount);
            jPositionOnlyVertex* VerticesData = streamParam->Data.data();
            for (int32 i = 0; i < elementCount; ++i)
            {
                Vector4 Pos_U = mesh->verticesUVX[i];
                VerticesData[i].Pos = Vector(Pos_U);
            }
            positionOnlyVertexStreamData->Params.push_back(streamParam);
            positionOnlyVertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
            positionOnlyVertexStreamData->ElementCount = elementCount;
        }

        // Create Base VertexStream
        auto vertexStreamData = std::make_shared<jVertexStreamData>();
        {
            auto streamParam = std::make_shared<jStreamParam<jBaseVertex>>();
            streamParam->BufferType = EBufferType::STATIC;
            streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("POSITION"), EBufferElementType::FLOAT, sizeof(float) * 3));
            streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("NORMAL"), EBufferElementType::FLOAT, sizeof(float) * 3));
            streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("TANGENT"), EBufferElementType::FLOAT, sizeof(float) * 3));
            streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("BITANGENT"), EBufferElementType::FLOAT, sizeof(float) * 3));
            streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("TEXCOORD"), EBufferElementType::FLOAT, sizeof(float) * 2));
            streamParam->Name = jName("jBaseVertex");
            streamParam->Data.resize(elementCount);
            streamParam->Stride = sizeof(jBaseVertex);
            jBaseVertex* VerticesData = streamParam->Data.data();
            for (int32 i = 0; i < elementCount; ++i)
            {
                Vector4 Pos_U = mesh->verticesUVX[i];
                Vector4 Normal_V = mesh->normalsUVY[i];

                VerticesData[i].Pos = Vector(Pos_U);
                VerticesData[i].Normal = Vector(Normal_V);
                //VerticesData[i].Tangent = meshData->Tangents[i];
                //VerticesData[i].Bitangent = meshData->Bitangents[i];
                VerticesData[i].TexCoord = Vector2(Pos_U.w, Normal_V.w);
            }
            vertexStreamData->Params.push_back(streamParam);

            vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
            vertexStreamData->ElementCount = elementCount;
        }

        auto indexStreamData = std::make_shared<jIndexStreamData>();
        indexStreamData->ElementCount = static_cast<int32>(elementCount);
        //if (indexStreamData->ElementCount > 65535)
        if (1)  // todo support load 16 bit index for shader
        {
            std::vector<uint32> indices(elementCount);
            for (int32 i = 0; i < elementCount; ++i)
            {
                indices[i] = i;
            }

            auto streamParam = new jStreamParam<uint32>();
            streamParam->BufferType = EBufferType::STATIC;
            streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(int32) * 3));
            streamParam->Name = jName("Index");
            streamParam->Data.resize(indices.size());
            streamParam->Stride = sizeof(uint32) * 3;
			for (int32 i = 0; i < indices.size(); i += 3)
			{
				streamParam->Data[i + 0] = indices[i + 0];
				streamParam->Data[i + 1] = indices[i + 1];
				streamParam->Data[i + 2] = indices[i + 2];
			}
            indexStreamData->Param = streamParam;
        }
        else
        {
            std::vector<uint16> indices(elementCount);
            for (int32 i = 0; i < elementCount; ++i)
            {
                indices[i] = i;
            }

            auto streamParam = new jStreamParam<uint16>();
            streamParam->BufferType = EBufferType::STATIC;
            streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT16, sizeof(uint16) * 3));
            streamParam->Name = jName("Index");
            streamParam->Data.resize(indices.size());
            streamParam->Stride = sizeof(uint16) * 3;
            for (int32 i = 0; i < indices.size(); i += 3)
            {
                streamParam->Data[i + 0] = indices[i + 0];

                // Convert from CW to CCW
                streamParam->Data[i + 1] = indices[i + 2];
                streamParam->Data[i + 2] = indices[i + 1];
            }
            indexStreamData->Param = streamParam;
        }

        object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>();
        object->RenderObjectGeometryDataPtr->CreateNew_ForRaytracing(vertexStreamData, positionOnlyVertexStreamData, indexStreamData, false, true);
        renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);

        auto transform = MeshInst.transform;

        renderObject->SetPos(transform.GetTranslateVector());
        renderObject->SetScale(transform.GetScaleVector());
        renderObject->SetRot(transform.GetRotateVector());

        // Create Material
        const Material& material = materials[MeshInst.materialID];

        std::shared_ptr<jMaterial> materialPtr = std::make_shared<jMaterial>();
        materialPtr->MaterialDataPtr = std::make_shared<jMaterialData>();
        materialPtr->MaterialDataPtr->Data.resize(sizeof(material));
        memcpy(materialPtr->MaterialDataPtr->Data.data(), &material, sizeof(material));

        renderObject->MaterialPtr = materialPtr;
        renderObject->RayTracingHitGroupIndex = OBJECT_HIT_GROUP_INDEX;

        SpawnObjects.push_back(object);
    }

    // Create Light
    for(int32 i=0;i<(int32)lights.size();++i)
	{
        const Light& L = lights[i];

        jPathTracingLightUniformBufferData Data;
        Data.position = L.position;
        Data.emission = L.emission;
        Data.u = L.u;
        Data.v = L.v;
        Data.radius = L.radius;
        Data.area = L.area;
        Data.type = L.type;

        jLight* NewLight = jLight::CreatePathTracingLight(Data);
        jLight::AddLights(NewLight);
        SpawnLights.push_back(NewLight);

        switch (L.type)
        {
		case RectLight:
		{
            Vector Size = Abs(L.u) + Abs(L.v);
            Size.y = Max(0.001f, Size.y);
            jQuadPrimitive* quad = jPrimitiveUtil::CreateQuad(Data.position + (L.u + L.v) * 0.5f, Size, Vector(1.0f), Vector4::ColorWhite);
            jObject::AddObject(quad);
            SpawnObjects.push_back(quad);

            check(quad->RenderObjects.size() > 0);
            const Vector Normal = L.v.CrossProduct(L.u).GetNormalize();
            Vector EulerAngle = Vector::GetEulerAngleFrom(Normal);
			quad->RenderObjects[0]->SetRot(EulerAngle);
            quad->RenderObjects[0]->RayTracingHitGroupIndex = 1;

            // todo : need to replace it as a light data
            // todo : I need additional data structure for lights. ex). hit group and so on.
            Material material{};
            material.lightId = i;

			std::shared_ptr<jMaterial> materialPtr = std::make_shared<jMaterial>();
			materialPtr->MaterialDataPtr = std::make_shared<jMaterialData>();
			materialPtr->MaterialDataPtr->Data.resize(sizeof(material));
			memcpy(materialPtr->MaterialDataPtr->Data.data(), &material, sizeof(material));
            quad->RenderObjects[0]->MaterialPtr = materialPtr;
            quad->RenderObjects[0]->RayTracingHitGroupIndex = LIGHT_HIT_GROUP_INDEX;

			break;
		}
		case SphereLight:
		{
            jObject* sphere = jPrimitiveUtil::CreateSphere(Data.position, Data.radius, 60, 60, Vector(1.0f), Vector4::ColorWhite);
            jObject::AddObject(sphere);
            SpawnObjects.push_back(sphere);

			// todo : need to replace it as a light data
            // todo : I need additional data structure for lights. ex). hit group and so on.
			Material material{};
            material.lightId = i;

			std::shared_ptr<jMaterial> materialPtr = std::make_shared<jMaterial>();
			materialPtr->MaterialDataPtr = std::make_shared<jMaterialData>();
			materialPtr->MaterialDataPtr->Data.resize(sizeof(material));
			memcpy(materialPtr->MaterialDataPtr->Data.data(), &material, sizeof(material));
            sphere->RenderObjects[0]->MaterialPtr = materialPtr;
            sphere->RenderObjects[0]->RayTracingHitGroupIndex = LIGHT_HIT_GROUP_INDEX;

			break;
		}
		case DistantLight:
		{
            check(0);       // todo
			break;
		}
        }

    }

    // Set MainCamera
    *InGame->MainCamera = Camera;
}

void jPathTracingLoadData::ClearSceneFor_jEngine(class jGame* InGame)
{
    for (jObject* obj : SpawnObjects)
    {
        jObject::RemoveObject(obj);
    }
    SpawnObjects.clear();

	for (jLight* light : SpawnLights)
	{
		jLight::RemoveLights(light);
	}
    SpawnLights.clear();
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

#if PATH_TRACING_DATA_LEFT_HAND
                vx *= -1.0f;
                nx *= -1.0f;
#endif // PATH_TRACING_DATA_LEFT_HAND

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
