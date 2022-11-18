#include "pch.h"
#include "jModelLoader.h"

#include "assimp/Importer.hpp"
#include "assimp/cimport.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "assimp/Exporter.hpp"
#include "Scene/jMeshObject.h"
#include "Math/Vector.h"
#include "Scene/jRenderObject.h"
#include "jImageFileLoader.h"

#if defined _DEBUG
#pragma comment(lib, "assimp-vc142-mtd.lib")
#else
#pragma comment(lib, "assimp-vc142-mt.lib")
#endif

jModelLoader* jModelLoader::_instance = nullptr;

jModelLoader::jModelLoader()
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
	const aiScene* scene = importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

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

	for (uint32 i = 0; i < scene->mNumMaterials; ++i)
	{
		aiMaterial* material = scene->mMaterials[i];

		std::string name = material->GetName().C_Str();

		auto newMeshMaterial = new jMeshMaterial();
		for (uint32 k = aiTextureType_DIFFUSE; k <= aiTextureType_REFLECTION; ++k)
		{
			auto curTexType = static_cast<aiTextureType>(k);
			if (0 >= material->GetTextureCount(curTexType))
				continue;

			int32 Index = 0;
			if (k == aiTextureType_DIFFUSE)
				Index = (int32)jMaterial::EMaterialTextureType::DiffuseSampler;
			else if (k == aiTextureType_NORMALS)
				Index = (int32)jMaterial::EMaterialTextureType::NormalSampler;
			else if (k == aiTextureType_OPACITY)
				Index = (int32)jMaterial::EMaterialTextureType::OpacitySampler;
			else
				continue;

			jMeshMaterial::TextureData& curTexData = newMeshMaterial->TexData[Index];

			aiString str;
			aiTextureMapping mapping;
			aiTextureMapMode mode[2];
			aiTextureOp op;
			material->GetTexture(curTexType, 0, &str, &mapping, nullptr, nullptr, &op, &mode[0]);

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
			curTexData.TextureAddressModeU = FuncTextureAddressMode(mode[0]);
			curTexData.TextureAddressModeV = FuncTextureAddressMode(mode[1]);

			std::string FilePath;
			if (materialRootDir)
				FilePath = materialRootDir;

			if (FilePath.length() > 0 && str.length > 0)
			{
				const char last = FilePath[FilePath.length() - 1];
				const char first = str.data[0];
				const bool lastHasNoSlash = (last != '/' && last != '\\');
				const bool firstHasNoSlash = (first != '/' && first != '\\');
				if (lastHasNoSlash && firstHasNoSlash)
					FilePath += "\\";
			}
			FilePath += str.C_Str();

			curTexData.Texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName(FilePath), true).lock().get();
			curTexData.Name = jName(str.C_Str());
		}

		ai_real Opacity = 1.0f;
		material->Get(AI_MATKEY_OPACITY, Opacity);

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

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Name = jName("Pos");
		streamParam->Data.resize(elementCount * 3);
		streamParam->Stride = sizeof(float) * 3;
		memcpy(&streamParam->Data[0], &meshData->Vertices[0], meshData->Vertices.size() * sizeof(Vector));
		vertexStreamData->Params.push_back(streamParam);
	}

	if (!meshData->Normals.empty())
	{
		JASSERT(meshData->Vertices.size() == meshData->Normals.size());

		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Name = jName("Normal");
		streamParam->Data.resize(elementCount * 3);
		streamParam->Stride = sizeof(float) * 3;
		memcpy(&streamParam->Data[0], &meshData->Normals[0], meshData->Normals.size() * sizeof(Vector));
		vertexStreamData->Params.push_back(streamParam);
	}

	if (!meshData->Tangents.empty())
	{
		JASSERT(meshData->Vertices.size() == meshData->Tangents.size());

		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Name = jName("Tangent");
		streamParam->Data.resize(elementCount * 3);
		streamParam->Stride = sizeof(float) * 3;
		memcpy(&streamParam->Data[0], &meshData->Tangents[0], meshData->Tangents.size() * sizeof(Vector));
		vertexStreamData->Params.push_back(streamParam);
	}

	if (!meshData->Bitangents.empty())
	{
		JASSERT(meshData->Vertices.size() == meshData->Bitangents.size());

		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Name = jName("Bitangent");
		streamParam->Data.resize(elementCount * 3);
		streamParam->Stride = sizeof(float) * 3;
		memcpy(&streamParam->Data[0], &meshData->Bitangents[0], meshData->Bitangents.size() * sizeof(Vector));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Name = jName("TexCoord");
		streamParam->Data.resize(elementCount * 2);
		streamParam->Stride = sizeof(float) * 2;
		memcpy(&streamParam->Data[0], &meshData->TexCoord[0], meshData->TexCoord.size() * sizeof(Vector2));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	auto indexStreamData = std::make_shared<jIndexStreamData>();
	indexStreamData->ElementCount = static_cast<int32>(meshData->Faces.size());
	{
		auto streamParam = new jStreamParam<uint32>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(float) * 3));
		streamParam->Name = jName("Index");
		streamParam->Data.resize(meshData->Faces.size());
		streamParam->Stride = sizeof(uint32) * 3;
		memcpy(&streamParam->Data[0], &meshData->Faces[0], meshData->Faces.size() * sizeof(uint32));
		indexStreamData->Param = streamParam;
	}

	for (int32 i = 0; i < (int32)object->SubMeshes.size(); ++i)
	{
		const jSubMesh& subMesh = object->SubMeshes[i];

		auto StaticMeshRenderObject = new jStaticMeshRenderObject();
		StaticMeshRenderObject->SubMesh = subMesh;
		StaticMeshRenderObject->MaterialPtr = meshData->Materials[subMesh.MaterialIndex];
		StaticMeshRenderObject->CreateRenderObject(vertexStreamData, indexStreamData);
		object->RenderObjects.push_back(StaticMeshRenderObject);
	}

	return object;
}

jMeshObject* jModelLoader::LoadFromFile(const char* filename)
{
	return LoadFromFile(filename, "Image/");
}
