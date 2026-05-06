#include "pch.h"
#include "jModelLoader.h"

#include "assimp/Importer.hpp"
#include "assimp/cimport.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp/Exporter.hpp"
#include "assimp/GltfMaterial.h"
#include "Scene/jMeshObject.h"
#include "Math/Vector.h"
#include "Scene/jRenderObject.h"
#include "jImageFileLoader.h"
#include <filesystem>

#if defined _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

jModelLoader* jModelLoader::_instance = nullptr;

jModelLoader::jModelLoader()
	: ThreadPool(MINIMUM_WORKER_THREAD_COUNT)
{	
}


jModelLoader::~jModelLoader()
{
}

//bool jModelLoader::ConvertToFBX(const char* destFilename, const char* filename) const
//{
//	Assimp::Exporter exporter;
//	//for (int32 i = 0; i < exporter.GetExportFormatCount(); ++i)
//	//	const aiExportFormatDesc* format = exporter.GetExportFormatDescription(i);
//
//	Assimp::Importer importer;
//	const aiScene* scene = importer.ReadFile(filename, 0);
//	aiReturn Ret = exporter.Export(scene, "fbx", destFilename, 0, nullptr);
//	//aiReleaseImport(scene);
//
//	return (Ret == aiReturn_SUCCESS);
//}

jMeshObject* jModelLoader::LoadFromFile(const char* filename, const char* materialRootDir)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace
		| aiProcess_MakeLeftHanded
	);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		JASSERT(!importer.GetErrorString());
		return nullptr;
	}

	jMeshObject* object = new jMeshObject();

	jMeshData* meshData = new jMeshData();
	object->MeshData = meshData;

	for (uint32 i = 0; i < scene->mNumMeshes; ++i)
	{
		const aiMesh* assimpMesh = scene->mMeshes[i];

		jSubMesh subMesh;
		if (assimpMesh->mName.length > 0)
			subMesh.Name = assimpMesh->mName.C_Str();

		subMesh.StartVertex = static_cast<int32>(meshData->Vertices.size());
		meshData->Vertices.resize(meshData->Vertices.size() + assimpMesh->mNumVertices);
		subMesh.EndVertex = static_cast<int32>(meshData->Vertices.size());

		memcpy(&meshData->Vertices[subMesh.StartVertex], &assimpMesh->mVertices[0], assimpMesh->mNumVertices * sizeof(Vector));

		const bool HasTangentBitangent = assimpMesh->HasTangentsAndBitangents();
		if (HasTangentBitangent)
		{
			meshData->Tangents.resize(meshData->Tangents.size() + assimpMesh->mNumVertices);
			memcpy(&meshData->Tangents[subMesh.StartVertex], &assimpMesh->mTangents[0], assimpMesh->mNumVertices * sizeof(Vector));

			meshData->Bitangents.resize(meshData->Bitangents.size() + assimpMesh->mNumVertices);
			memcpy(&meshData->Bitangents[subMesh.StartVertex], &assimpMesh->mBitangents[0], assimpMesh->mNumVertices * sizeof(Vector));
		}

		if (assimpMesh->HasNormals())
		{
			meshData->Normals.resize(meshData->Normals.size() + assimpMesh->mNumVertices);
			memcpy(&meshData->Normals[subMesh.StartVertex], &assimpMesh->mNormals[0], assimpMesh->mNumVertices * sizeof(Vector));
		}

		if (assimpMesh->HasTextureCoords(0))
		{
			meshData->TexCoord.resize(meshData->TexCoord.size() + assimpMesh->mNumVertices);
			for (uint32 k = 0; k < assimpMesh->mNumVertices; ++k)
			{
				auto& texCoord = meshData->TexCoord[subMesh.StartVertex + k];
				auto& assimpTexCoord = assimpMesh->mTextureCoords[0][k];
				texCoord.x = assimpTexCoord.x;
				texCoord.y = assimpTexCoord.y;
			}
		}

		if (assimpMesh->HasFaces())
		{
			subMesh.StartFace = static_cast<int32>(meshData->Faces.size());
			meshData->Faces.resize(meshData->Faces.size() + assimpMesh->mNumFaces * 3);
			subMesh.EndFace = static_cast<int32>(meshData->Faces.size());
			for (unsigned int k = 0; k < assimpMesh->mNumFaces; ++k)
			{
				aiFace& face = assimpMesh->mFaces[k];
				memcpy(&meshData->Faces[subMesh.StartFace + k * 3], &face.mIndices[0], sizeof(uint32) * 3);
			}
		}
		subMesh.MaterialIndex = assimpMesh->mMaterialIndex;
		object->SubMeshes.emplace_back(subMesh);
	}

	std::vector<jMaterial*> Materials;
	for (uint32 i = 0; i < scene->mNumMaterials; ++i)
	{
		aiMaterial* material = scene->mMaterials[i];

		std::string name = material->GetName().C_Str();

		auto newMeshMaterial = new jMeshMaterial();
#if USE_SPONZA_PBR
		for (uint32 k = aiTextureType_NONE; k <= aiTextureType_UNKNOWN; ++k)
#else
		for (uint32 k = aiTextureType_DIFFUSE; k <= aiTextureType_REFLECTION; ++k)
#endif
		{
			auto curTexType = static_cast<aiTextureType>(k);
			if (0 >= material->GetTextureCount(curTexType))
				continue;

			int32 Index = 0;
			if (k == aiTextureType_DIFFUSE)
				Index = (int32)jMaterial::EMaterialTextureType::Albedo;
			else if (k == aiTextureType_NORMALS)
				Index = (int32)jMaterial::EMaterialTextureType::Normal;
			//else if (k == aiTextureType_OPACITY)
			//	Index = (int32)jMaterial::EMaterialTextureType::Opacity;
#if USE_SPONZA_PBR
			//else if (k == aiTextureType_BASE_COLOR)
			//	Index = (int32)jMaterial::EMaterialTextureType::BaseColor;
			else if (k == aiTextureType_METALNESS)
				Index = (int32)jMaterial::EMaterialTextureType::Metallic;
			//else if (k == aiTextureType_DIFFUSE_ROUGHNESS)
			//	Index = (int32)jMaterial::EMaterialTextureType::Roughness;
#endif
			else
				continue;

			jMeshMaterial::TextureData& curTexData = newMeshMaterial->TexData[Index];

			aiString str;
			aiTextureMapping mapping;
			aiTextureMapMode mode[2];
			aiTextureOp op;
			aiReturn ret = material->GetTexture(curTexType, 0, &str, &mapping, nullptr, nullptr, &op, &mode[0]);
			if (ret != aiReturn_SUCCESS)
				continue;

			auto FuncTextureAddressMode = [](aiTextureMapMode InMode)
			{
				switch (InMode)
				{
				case aiTextureMapMode_Wrap:		return ETextureAddressMode::REPEAT;
				case aiTextureMapMode_Clamp:	return ETextureAddressMode::CLAMP_TO_EDGE;
				case aiTextureMapMode_Decal:	return ETextureAddressMode::CLAMP_TO_BORDER;
				case aiTextureMapMode_Mirror:	return ETextureAddressMode::MIRRORED_REPEAT;
				default:
					break;
				}
				return ETextureAddressMode::REPEAT;
			};
            auto FuncTextureFilter = [](uint32 InFilter, bool InIsMinFilter)
            {
                switch (InFilter)
                {
                case 9728u: return ETextureFilter::NEAREST;                 // GL_NEAREST
                case 9729u: return ETextureFilter::LINEAR;                  // GL_LINEAR
                case 9984u: return ETextureFilter::NEAREST_MIPMAP_NEAREST;  // GL_NEAREST_MIPMAP_NEAREST
                case 9985u: return ETextureFilter::LINEAR_MIPMAP_NEAREST;   // GL_LINEAR_MIPMAP_NEAREST
                case 9986u: return ETextureFilter::NEAREST_MIPMAP_LINEAR;   // GL_NEAREST_MIPMAP_LINEAR
                case 9987u: return ETextureFilter::LINEAR_MIPMAP_LINEAR;    // GL_LINEAR_MIPMAP_LINEAR
                default:
                    break;
                }
                return InIsMinFilter ? ETextureFilter::LINEAR_MIPMAP_LINEAR : ETextureFilter::LINEAR;
            };
			curTexData.TextureAddressModeU = FuncTextureAddressMode(mode[0]);
			curTexData.TextureAddressModeV = FuncTextureAddressMode(mode[1]);

            uint32 MinFilter = 0;
            uint32 MagFilter = 0;
#if defined(AI_MATKEY_GLTF_MAPPINGFILTER_MIN) && defined(AI_MATKEY_GLTF_MAPPINGFILTER_MAG)
            if (material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(curTexType, 0), MinFilter) == aiReturn_SUCCESS)
            {
                curTexData.MinificationFilter = FuncTextureFilter(MinFilter, true);
            }
            if (material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(curTexType, 0), MagFilter) == aiReturn_SUCCESS)
            {
                curTexData.MagnificationFilter = FuncTextureFilter(MagFilter, false);
            }
#endif

            ai_real Anisotropy = 1.0f;
            if (material->Get(AI_MATKEY_ANISOTROPY_FACTOR, Anisotropy) == aiReturn_SUCCESS)
            {
                curTexData.MaxAnisotropy = (Anisotropy > 1.0f) ? (float)Anisotropy : 1.0f;
            }

			curTexData.Name = jName(str.C_Str());

			const std::string texturePath = str.C_Str();
			const bool isEmbeddedTexture = (!texturePath.empty() && texturePath[0] == '*');
			const bool isDataUri = (texturePath.rfind("data:", 0) == 0);
			if (!isEmbeddedTexture && !isDataUri && !texturePath.empty())
			{
				std::filesystem::path resolvedPath(texturePath);
				if (!resolvedPath.is_absolute() && materialRootDir)
					resolvedPath = std::filesystem::path(materialRootDir) / resolvedPath;

				resolvedPath = resolvedPath.lexically_normal();
				std::error_code errorCode;
				if (std::filesystem::exists(resolvedPath, errorCode) && !errorCode)
				{
					const std::string filePath = resolvedPath.generic_string();
					jName FilePathName = jName(filePath);
					curTexData.FilePath = FilePathName;

					Materials.push_back(newMeshMaterial);
					jTask T;
					T.pHashFunc = [&FilePathName]()
					{
						return (size_t)FilePathName.GetNameHash();
					};
					T.pFunc = [filePath]() {
						jImageFileLoader::GetInstance().LoadImageDataFromFile(jName(filePath));
					};
					ThreadPool.Enqueue(T);
				}
			}
		}

		ai_real Opacity = 1.0f;
		material->Get(AI_MATKEY_OPACITY, Opacity);
        newMeshMaterial->bRaytracingAlphaTest = (material->GetTextureCount(aiTextureType_OPACITY) > 0);
        newMeshMaterial->RaytracingAlphaCutoff = 0.5f;

        // Legacy fallback: if only scalar opacity exists, treat as alpha-cutout candidate.
        if (Opacity < 0.999f)
        {
            newMeshMaterial->bRaytracingAlphaTest = true;
            newMeshMaterial->RaytracingAlphaCutoff = (float)Opacity;
        }

        aiString AlphaMode;
        if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, AlphaMode) == aiReturn_SUCCESS)
        {
            const char* AlphaModeStr = AlphaMode.C_Str();
            if (AlphaModeStr && _stricmp(AlphaModeStr, "OPAQUE") == 0)
            {
                newMeshMaterial->bRaytracingAlphaTest = false;
            }
            else if (AlphaModeStr && _stricmp(AlphaModeStr, "MASK") == 0)
            {
                newMeshMaterial->bRaytracingAlphaTest = true;

                ai_real AlphaCutoff = 0.5f;
                if (material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, AlphaCutoff) == aiReturn_SUCCESS)
                {
                    newMeshMaterial->RaytracingAlphaCutoff = (float)AlphaCutoff;
                }
            }
            else if (AlphaModeStr && _stricmp(AlphaModeStr, "BLEND") == 0)
            {
                // Current HWRT DI path does not support translucent blending.
                // Keep alpha-test disabled for BLEND mode.
                newMeshMaterial->bRaytracingAlphaTest = false;
            }
        }

		ai_real Reflectivity = 0.0f;
		material->Get(AI_MATKEY_REFLECTIVITY, Reflectivity);

		ai_real IndexOfRefraction = 1.0f;		// 1.0 means lights will not refract
		material->Get(AI_MATKEY_REFRACTI, IndexOfRefraction);

		aiColor3D Ambient(1.0f);
		material->Get(AI_MATKEY_COLOR_AMBIENT, Ambient);

		aiColor3D Diffuse(1.0f);
		material->Get(AI_MATKEY_COLOR_DIFFUSE, Diffuse);

		aiColor3D Specular(1.0f);
		material->Get(AI_MATKEY_COLOR_SPECULAR, Specular);

		aiColor3D Emissive(1.0f);
		material->Get(AI_MATKEY_COLOR_EMISSIVE, Emissive);

		ai_real SpecularShiness = 0.0f;
		material->Get(AI_MATKEY_SHININESS, SpecularShiness);

		newMeshMaterial->Data.Ambient = Vector4(Ambient.r, Ambient.g, Ambient.b, 1.0f);
		newMeshMaterial->Data.Diffuse = Vector4(Diffuse.r, Diffuse.g, Diffuse.b, 1.0f);
		newMeshMaterial->Data.Specular = Vector4(Specular.r, Specular.g, Specular.b, 1.0f);
		newMeshMaterial->Data.Emissive = Vector4(Emissive.r, Emissive.g, Emissive.b, 1.0f);
		newMeshMaterial->Data.SpecularShiness = SpecularShiness;
		newMeshMaterial->Data.Opacity = Opacity;
		newMeshMaterial->Data.Reflectivity = Reflectivity;
		newMeshMaterial->Data.IndexOfRefraction = IndexOfRefraction;

		meshData->Materials.emplace(std::make_pair(i, std::shared_ptr<jMaterial>(newMeshMaterial)));
	}

    ThreadPool.WaitAll();
    
    for (jMaterial* mat : Materials)
    {
		for (int32 i = 0; i < _countof(mat->TexData); ++i)
		{
			if (mat->TexData[i].FilePath.IsValid())
				mat->TexData[i].Texture = jImageFileLoader::GetInstance().LoadTextureFromFile(mat->TexData[i].FilePath).lock().get();
		}
    }

	JASSERT(scene->mRootNode);
	if (scene->mRootNode)
	{
		jMeshNode* rootNode = new jMeshNode();

		rootNode->MeshIndex.resize(scene->mRootNode->mNumMeshes);
		memcpy(&rootNode->MeshIndex[0], scene->mRootNode->mMeshes, scene->mRootNode->mNumMeshes * sizeof(int32));

		auto fillUpMeshNode = [](jMeshNode* node, aiNode* assimpNode)
		{
			node->MeshIndex.resize(assimpNode->mNumMeshes);
			memcpy(&node->MeshIndex[0], assimpNode->mMeshes, assimpNode->mNumMeshes * sizeof(int32));
		};

		std::function<void(jMeshNode*, aiNode*)> fillupChildNode = [&](jMeshNode* node, aiNode* assimpNode)
		{
			for (uint32 i = 0; i < assimpNode->mNumChildren; ++i)
			{
				auto newNode = new jMeshNode();
				fillUpMeshNode(newNode, assimpNode->mChildren[i]);
				fillupChildNode(newNode, assimpNode->mChildren[i]);
				node->childNode.push_back(newNode);
			}
		};

		fillUpMeshNode(rootNode, scene->mRootNode);
		fillupChildNode(rootNode, scene->mRootNode);

		object->RootNode = rootNode;
	}

	const int32 elementCount = static_cast<int32>(meshData->Vertices.size());

    // Position-only vertex stream 생성
    std::vector<jPositionOnlyVertex> verticesPositionOnly(meshData->Vertices.size());
    for (int32 i = 0; i < (int32)meshData->Vertices.size(); ++i)
    {
        verticesPositionOnly[i].Pos = meshData->Vertices[i];
    }

    const int32 NumOfVertices = (int32)meshData->Vertices.size();
    auto positionOnlyVertexStreamData = std::make_shared<jVertexStreamData>();
    {
        auto streamParam = std::make_shared<jStreamParam<jPositionOnlyVertex>>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back(IStreamParam::jAttribute(jNameStatic("POSITION"), EBufferElementType::FLOAT, sizeof(float) * 3));
        streamParam->Name = jName("jPositionOnlyVertex");
        streamParam->Stride = sizeof(jPositionOnlyVertex);
        streamParam->Data.swap(verticesPositionOnly);
        positionOnlyVertexStreamData->Params.push_back(streamParam);

        positionOnlyVertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
        positionOnlyVertexStreamData->ElementCount = NumOfVertices;
    }

	// Base vertex stream 생성
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
			VerticesData[i].Pos = meshData->Vertices[i];
			VerticesData[i].Normal = meshData->Normals[i];
			VerticesData[i].Tangent = meshData->Tangents[i];
			VerticesData[i].Bitangent = meshData->Bitangents[i];
			VerticesData[i].TexCoord = meshData->TexCoord[i];
		}
		vertexStreamData->Params.push_back(streamParam);

		vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
		vertexStreamData->ElementCount = elementCount;
	}

	auto indexStreamData = std::make_shared<jIndexStreamData>();
	indexStreamData->ElementCount = static_cast<int32>(meshData->Faces.size());
	if (indexStreamData->ElementCount > 65535)
	{
		auto streamParam = new jStreamParam<uint32>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back({.UnderlyingType=EBufferElementType::UINT32, .Stride=sizeof(int32) * 3});
		streamParam->Name = jName("Index");
		streamParam->Data.resize(meshData->Faces.size());
		streamParam->Stride = sizeof(uint32) * 3;
		memcpy(&streamParam->Data[0], &meshData->Faces[0], meshData->Faces.size() * sizeof(uint32));
		indexStreamData->Param = streamParam;
	}
	else
	{
        auto streamParam = new jStreamParam<uint16>();
        streamParam->BufferType = EBufferType::STATIC;
        streamParam->Attributes.push_back({.UnderlyingType=EBufferElementType::UINT16, .Stride=sizeof(uint16) * 3});
        streamParam->Name = jName("Index");
        streamParam->Data.resize(meshData->Faces.size());
        streamParam->Stride = sizeof(uint16) * 3;
        for (size_t i = 0; i < meshData->Faces.size(); ++i)
        {
            const uint32 indexValue = meshData->Faces[i];
            JASSERT(indexValue <= 0xFFFFu);
            streamParam->Data[i] = static_cast<uint16>(indexValue);
        }
        indexStreamData->Param = streamParam;
	}

    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>();
	object->RenderObjectGeometryDataPtr->CreateNew_ForRaytracing(vertexStreamData, positionOnlyVertexStreamData, indexStreamData, false, true);

	for (int32 i = 0; i < (int32)object->SubMeshes.size(); ++i)
	{
		const jSubMesh& subMesh = object->SubMeshes[i];

		auto StaticMeshRenderObject = new jRenderObjectElement();
		StaticMeshRenderObject->SubMesh = subMesh;
		StaticMeshRenderObject->MaterialPtr = meshData->Materials[subMesh.MaterialIndex];
		StaticMeshRenderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
		object->AddRenderObject(StaticMeshRenderObject);  // Use AddRenderObject to automatically set Owner
	}

	return object;
}

jMeshObject* jModelLoader::LoadFromFile(const char* filename)
{
	return LoadFromFile(filename, "Image/");
}

jThreadPool::jThreadPool(size_t num_threads) : IsStop(false)
{
    for (size_t i = 0; i < num_threads; ++i) 
	{
        WorkerThreads.emplace_back([this] 
		{
			const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const bool shouldUninitializeCOM = SUCCEEDED(comInitResult);

            while (true) 
			{
				jTask task;
                {
                    std::unique_lock<std::mutex> lock(QueueLock);

                    // 작업이 없고 스레드 풀 종료 요청도 없다면 대기
                    ConditionVariable.wait(lock, [this] { return IsStop || !TaskQueue.empty(); });

                    if (IsStop && TaskQueue.empty()) 
					{
                        break;

                    }

					if (TaskQueue.size() > 0)
					{
						task = TaskQueue.front();
						TaskQueue.pop();
					}
                }

				if (task.IsValid())
				{
					task.Do();
					--RemainTask;
				}
            }

			if (shouldUninitializeCOM)
				CoUninitialize();
        });

    }
}

jThreadPool::~jThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(QueueLock);
        IsStop = true;
    }

    ConditionVariable.notify_all();

    for (std::thread& worker : WorkerThreads) 
	{
        worker.join();
    }
}

void jThreadPool::Enqueue(const jTask& file_path)
{
    std::unique_lock<std::mutex> lock(QueueLock);

    // 중복 작업인지 확인
    auto hash = file_path.GetHash();
    if (ProcessingTaskHashes.find(hash) == ProcessingTaskHashes.end())
	{
        // 중복이 아닐 때만 작업 큐에 추가
        TaskQueue.push(file_path);
        RemainTask++;
		ProcessingTaskHashes.insert(hash);
        ConditionVariable.notify_one();
    }
}
