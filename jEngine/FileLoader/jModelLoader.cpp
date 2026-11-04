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
#include "jModelLoaderUtils.h"
#include "jPreparedModelData.h"
#include "jModelUploadSession.h"
#include "jEditorAssetPath.h"
#include "jAssimpUtf8IO.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "Core/jLog.h"
#include <filesystem>
#include <utility>

#if defined _DEBUG
#pragma comment(lib, "assimp-vc143-mtd.lib")
#else
#pragma comment(lib, "assimp-vc143-mt.lib")
#endif

namespace
{
    using jEditorAssetPath::FromUtf8;
    using jEditorAssetPath::ToUtf8;
    struct ModelImportSettings
    {
        float Scale = 1.0f;
        bool FlipUVs = true;
        bool GenerateNormals = true;
    };

    bool ReadImportSettings(const char* filename, ModelImportSettings& settings, std::string& error)
    {
        namespace fs = std::filesystem;
        const auto metadata = FromUtf8(std::string(filename) + ".jasset");
        std::error_code ec;
        const auto status = fs::symlink_status(metadata, ec);
        if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) return true;
        if (ec || fs::is_symlink(status) || !fs::is_regular_file(status))
        { error = "Cannot read regular model metadata: " + ToUtf8(metadata); return false; }
        const auto size = fs::file_size(metadata, ec);
        if (ec || size > 16 * 1024 * 1024) { error = "Model metadata missing or exceeds 16 MiB: " + ToUtf8(metadata); return false; }
        std::ifstream file(metadata, std::ios::binary);
        std::string text(static_cast<size_t>(size), '\0');
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file || static_cast<size_t>(file.gcount()) != text.size())
        { error = "Cannot read model metadata: " + ToUtf8(metadata); return false; }
        using Json = nlohmann::json;
        try
        {
            const auto json = Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
                if (depth > 128) throw std::runtime_error("Model metadata nesting exceeds 128");
                return true;
            }, false);
            if (json.is_discarded() || !json.is_object() || json.value("schemaVersion", 0) != 1)
                throw std::runtime_error("Unsupported or invalid model metadata schema");
            const auto options = json.value("importSettings", Json::object());
            if (!options.is_object()) throw std::runtime_error("Model importSettings must be an object");
            for (auto it = options.begin(); it != options.end(); ++it)
                if (it.key() != "scale" && it.key() != "flipUVs" && it.key() != "generateNormals")
                    throw std::runtime_error("Unknown model import setting: " + it.key());
            if (options.contains("scale"))
            {
                if (!options["scale"].is_number()) throw std::runtime_error("Model import scale must be a finite positive number");
                const double scale = options["scale"].get<double>();
                if (!std::isfinite(scale) || scale <= 0 || scale > std::numeric_limits<float>::max()
                    || static_cast<float>(scale) <= 0) throw std::runtime_error("Model import scale must be a finite positive float");
                settings.Scale = static_cast<float>(scale);
            }
            for (const char* key : {"flipUVs", "generateNormals"})
                if (options.contains(key) && !options[key].is_boolean())
                    throw std::runtime_error(std::string("Model import ") + key + " must be boolean");
            settings.FlipUVs = options.value("flipUVs", true);
            settings.GenerateNormals = options.value("generateNormals", true);
            return true;
        }
        catch (const std::exception& exception) { error = ToUtf8(metadata) + ": " + exception.what(); return false; }
    }

    bool IsCancelled(const jAssetCancellation& cancellation)
    {
        return cancellation && cancellation->load();
    }

    struct jPreparationFutures
    {
        std::vector<std::future<void>> Tasks;
        ~jPreparationFutures()
        {
            // An exception/early cancellation must not leave a task referring to
            // preparation-local data. These are this request's futures only.
            for (auto& task : Tasks)
                if (task.valid())
                    task.wait();
        }
    };
}

jPreparedModel::jPreparedModel() : Payload(std::make_unique<Data>()) {}
jPreparedModel::~jPreparedModel() = default;
jPreparedModel::jPreparedModel(jPreparedModel&&) noexcept = default;
jPreparedModel& jPreparedModel::operator=(jPreparedModel&&) noexcept = default;
const jModelPreparationStats& jPreparedModel::GetStats() const { return Payload->Stats; }
bool jPreparedModel::GetVertexPosition(size_t index, float position[3]) const
{
    if (!position || !Payload || !Payload->MeshData || index >= Payload->MeshData->Vertices.size()) return false;
    const auto& value = Payload->MeshData->Vertices[index];
    position[0] = value.x; position[1] = value.y; position[2] = value.z; return true;
}
bool jPreparedModel::GetVertexTexCoord(size_t index, float uv[2]) const
{
    if (!uv || !Payload || !Payload->MeshData || index >= Payload->MeshData->TexCoord.size()) return false;
    const auto& value = Payload->MeshData->TexCoord[index]; uv[0] = value.x; uv[1] = value.y; return true;
}
bool jPreparedModel::GetVertexNormal(size_t index, float normal[3]) const
{
    if (!normal || !Payload || !Payload->MeshData || index >= Payload->MeshData->Normals.size()) return false;
    const auto& value = Payload->MeshData->Normals[index];
    normal[0] = value.x; normal[1] = value.y; normal[2] = value.z; return true;
}

jModelLoader& jModelLoader::GetInstance()
{
    static jModelLoader instance;
    return instance;
}

jModelLoader::jModelLoader()
    : ThreadPool(jThreadPool::DefaultWorkerCount())
    , OwnerThread(std::this_thread::get_id())
{
}

jModelLoader::~jModelLoader() = default;

bool jModelLoader::IsOwnerThread() const
{
    return OwnerThread == std::this_thread::get_id();
}

void jModelLoader::Shutdown()
{
    ThreadPool.Shutdown();
    jModelUploadSession::DrainRetired();
}

std::unique_ptr<jPreparedModel> jModelLoader::PrepareModel(const char* filename,
    const char* materialRootDir, const jAssetCancellation& cancellation, std::string* error)
{
    if (error)
        error->clear();
    const auto fail = [&](const std::string& message) -> std::unique_ptr<jPreparedModel> {
        if (error)
            *error = message;
        return nullptr;
    };
    if (!filename || !filename[0])
        return fail("Empty model filename");
    if (IsCancelled(cancellation))
        return fail("Cancelled");
    const auto started = std::chrono::steady_clock::now();
    try
    {
    jPreparationFutures pending;
	ModelImportSettings settings;
    std::string settingsError;
    if (!ReadImportSettings(filename, settings, settingsError)) return fail(settingsError);
    if (IsCancelled(cancellation)) return fail("Cancelled");
	Assimp::Importer importer;
    importer.SetIOHandler(new jAssimpUtf8IO());
    unsigned flags = aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_MakeLeftHanded;
    if (settings.FlipUVs) flags |= aiProcess_FlipUVs;
    if (settings.GenerateNormals) flags |= aiProcess_GenSmoothNormals;
	const aiScene* scene = importer.ReadFile(filename, flags);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
        return fail(std::string("Model load failed: ") + filename + ": " + importer.GetErrorString());
	}

    if (IsCancelled(cancellation))
        return fail("Cancelled");
    auto prepared = std::make_unique<jPreparedModel>();
    auto& data = *prepared->Payload;
    jMeshData* meshData = data.MeshData.get();
    std::unordered_set<std::string> imagePaths;
    std::unordered_set<std::string> warnedPaths;

	for (uint32 i = 0; i < scene->mNumMeshes; ++i)
	{
        if (IsCancelled(cancellation))
            return fail("Cancelled");
		const aiMesh* assimpMesh = scene->mMeshes[i];

		jSubMesh subMesh;
		if (!assimpMesh || !jModelLoaderUtils::AppendMesh(*assimpMesh, *meshData, subMesh))
		{
            return fail(std::string("Unsupported or invalid triangle mesh: ") + filename);
		}
		data.SubMeshes.emplace_back(subMesh);
	}

    for (auto& position : meshData->Vertices)
    {
        position = position * settings.Scale;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return fail("Model import scale produces nonfinite vertex positions");
    }

	for (uint32 i = 0; i < scene->mNumMaterials; ++i)
	{
        if (IsCancelled(cancellation))
            return fail("Cancelled");
		aiMaterial* material = scene->mMaterials[i];
        auto materialOwner = std::make_shared<jMeshMaterial>();
        auto* newMeshMaterial = materialOwner.get();
        aiString materialName;
        if (material->Get(AI_MATKEY_NAME, materialName) == aiReturn_SUCCESS)
            newMeshMaterial->SourceMaterialName = materialName.C_Str();
        meshData->Materials.emplace(i, std::move(materialOwner));
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

			std::string texturePath = str.C_Str();
			const bool isEmbeddedTexture = (!texturePath.empty() && texturePath[0] == '*');
			const bool isDataUri = (texturePath.rfind("data:", 0) == 0);
			if (!isEmbeddedTexture && !isDataUri && !texturePath.empty())
			{
                std::replace(texturePath.begin(), texturePath.end(), '\\', '/');
                auto resolvedPath = FromUtf8(texturePath);
                if (!resolvedPath.is_absolute() && materialRootDir) resolvedPath = FromUtf8(materialRootDir) / resolvedPath;
                resolvedPath = resolvedPath.lexically_normal();
				std::error_code errorCode;
				if (std::filesystem::exists(resolvedPath, errorCode) && !errorCode)
				{
					const std::string filePath = ToUtf8(resolvedPath);
					jName FilePathName = jName(filePath);
					curTexData.FilePath = FilePathName;

                    if (imagePaths.insert(filePath).second)
                    {
                        auto image = std::make_shared<jPreparedModel::Data::Image>();
                        image->Filename = FilePathName;
                        data.Images.push_back(image);
                        pending.Tasks.emplace_back(ThreadPool.Submit([image, cancellation] {
                            if (!IsCancelled(cancellation))
                            {
                                image->Decoded = jImageFileLoader::GetInstance().LoadImageDataFromFile(image->Filename).lock();
                                if (!image->Decoded && !IsCancelled(cancellation))
                                    throw std::runtime_error(std::string("Image decode failed: ") + image->Filename.ToStr());
                            }
                        }));
                    }
				}
                else if (!IsCancelled(cancellation) && warnedPaths.insert(ToUtf8(resolvedPath)).second)
                {
                    jLog::Get().Write(EjLogLevel::Warning, "Assets",
                        std::string("Optional texture fallback: ") + ToUtf8(resolvedPath) + ": "
                        + (errorCode ? errorCode.message() : "file not found"));
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

	}

    if (meshData->Vertices.empty() || meshData->Faces.empty())
        return fail("Model contains no supported triangle geometry");
    if (IsCancelled(cancellation))
        return fail("Cancelled");

	JASSERT(scene->mRootNode);
	if (scene->mRootNode)
	{
        auto rootOwner = std::make_unique<jMeshNode>();
        jMeshNode* rootNode = rootOwner.get();
        data.Nodes.push_back(std::move(rootOwner));

		auto fillUpMeshNode = [](jMeshNode* node, aiNode* assimpNode)
		{
			node->MeshIndex.resize(assimpNode->mNumMeshes);
			if (assimpNode->mNumMeshes)
				memcpy(node->MeshIndex.data(), assimpNode->mMeshes, assimpNode->mNumMeshes * sizeof(int32));
		};

		std::function<void(jMeshNode*, aiNode*)> fillupChildNode = [&](jMeshNode* node, aiNode* assimpNode)
		{
			for (uint32 i = 0; i < assimpNode->mNumChildren; ++i)
			{
                auto nodeOwner = std::make_unique<jMeshNode>();
                auto* newNode = nodeOwner.get();
                data.Nodes.push_back(std::move(nodeOwner));
				newNode->Parent = node;
				fillUpMeshNode(newNode, assimpNode->mChildren[i]);
				fillupChildNode(newNode, assimpNode->mChildren[i]);
				node->childNode.push_back(newNode);
			}
		};

		fillUpMeshNode(rootNode, scene->mRootNode);
		fillupChildNode(rootNode, scene->mRootNode);

		data.RootNode = rootNode;
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
	if (jModelLoaderUtils::Requires32BitIndices(meshData->Faces))
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

    data.VertexStream = std::move(vertexStreamData);
    data.PositionStream = std::move(positionOnlyVertexStreamData);
    data.IndexStream = std::move(indexStreamData);
    for (auto& task : pending.Tasks)
        task.get(); // Never waits for another request's textures.
    if (IsCancelled(cancellation))
        return fail("Cancelled");
    data.Stats.VertexCount = meshData->Vertices.size();
    data.Stats.IndexCount = meshData->Faces.size();
    data.Stats.SubMeshCount = data.SubMeshes.size();
    data.Stats.TextureCount = data.Images.size();
    data.Stats.CpuMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return prepared;
    }
    catch (const std::exception& exception)
    {
        return fail(std::string("Model preparation failed: ") + exception.what());
    }
    catch (...)
    {
        return fail("Unknown model preparation failure");
    }
}

std::unique_ptr<jModelUploadSession> jModelLoader::BeginFinalize(std::unique_ptr<jPreparedModel> prepared, std::string* error)
{
    if (error) error->clear();
    const auto fail = [&](const char* message) -> std::unique_ptr<jModelUploadSession> {
        if (error) *error = message;
        return {};
    };
    if (!IsOwnerThread()) return fail("Model finalization requires the model loader owner thread");
    if (!prepared || !prepared->Payload || !prepared->Payload->VertexStream)
        return fail("No prepared model data");
    if (!g_rhi) return fail("Model finalization requires an initialized RHI");
    try
    {
        return std::unique_ptr<jModelUploadSession>(new jModelUploadSession(std::move(prepared)));
    }
    catch (const std::exception& exception) { return fail(exception.what()); }
    catch (...) { return fail("Unknown model upload initialization failure"); }
}

jMeshObject* jModelLoader::FinalizeModel(std::unique_ptr<jPreparedModel> prepared, std::string* error)
{
    auto upload = BeginFinalize(std::move(prepared), error);
    if (!upload) return nullptr;
    jModelUploadSession::Budget budget;
    budget.MaxBytes = 128 * 1024 * 1024;
    budget.MaxAdmissionMilliseconds = 60000;
    budget.MaxPendingChunks = 4;
    for (;;)
    {
        const auto progress = upload->Step(budget);
        if (progress.Current == jModelUploadSession::State::Ready) return upload->TakeModel().release();
        if (progress.Current == jModelUploadSession::State::Failed || progress.Current == jModelUploadSession::State::Cancelled)
        {
            if (error) *error = progress.Error;
            return nullptr;
        }
        upload->WaitSubmitted();
    }
}

jMeshObject* jModelLoader::LoadFromFile(const char* filename, const char* materialRootDir)
{
    if (!IsOwnerThread())
        return nullptr;
    std::string error;
    auto prepared = PrepareModel(filename, materialRootDir, {}, &error);
    auto* object = prepared ? FinalizeModel(std::move(prepared), &error) : nullptr;
    if (!object && !error.empty())
        std::cerr << error << std::endl;
    return object;
}

jMeshObject* jModelLoader::LoadFromFile(const char* filename)
{
    return LoadFromFile(filename, "Image/");
}

size_t jThreadPool::DefaultWorkerCount()
{
    const size_t hardware = std::thread::hardware_concurrency();
    return std::clamp(hardware > 2 ? hardware - 2 : size_t(1), size_t(1), size_t(4));
}

jThreadPool::jThreadPool(size_t numThreads, size_t queueCapacity)
    : QueueCapacity(queueCapacity ? queueCapacity : std::max(size_t(1), numThreads) * 2)
{
    try
    {
        for (size_t i = 0; i < std::max(size_t(1), numThreads); ++i)
        {
            WorkerThreads.emplace_back([this] {
#ifdef _WIN32
                const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                const bool shouldUninitializeCOM = SUCCEEDED(comInitResult);
#endif
                for (;;)
                {
                    jTask task;
                    {
                        std::unique_lock<std::mutex> lock(QueueLock);
                        ConditionVariable.wait(lock, [this] { return IsStop || !TaskQueue.empty(); });
                        if (IsStop && TaskQueue.empty())
                            break;
                        task = std::move(TaskQueue.front());
                        TaskQueue.pop();
                    }
                    QueueSpaceConditionVariable.notify_all();
                    try
                    {
                        task.Do();
                    }
                    catch (...)
                    {
                        std::lock_guard<std::mutex> lock(QueueLock);
                        if (!TaskException)
                            TaskException = std::current_exception();
                    }
                    {
                        std::lock_guard<std::mutex> lock(QueueLock);
                        --RemainTask;
                    }
                    CompletionConditionVariable.notify_all();
                }
#ifdef _WIN32
                if (shouldUninitializeCOM)
                    CoUninitialize();
#endif
            });
        }
    }
    catch (...)
    {
        Shutdown();
        throw;
    }
}

jThreadPool::~jThreadPool()
{
    Shutdown();
}

void jThreadPool::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(QueueLock);
        IsStop = true;
    }
    ConditionVariable.notify_all();
    QueueSpaceConditionVariable.notify_all();
    for (auto& worker : WorkerThreads)
        if (worker.joinable())
            worker.join();
    WorkerThreads.clear();
}

void jThreadPool::Enqueue(const jTask& task)
{
    if (!task.IsValid())
        return;
    std::unique_lock<std::mutex> lock(QueueLock);
    const auto hash = task.GetHash();
    if (task.pHashFunc && ProcessingTaskHashes.count(hash))
        return;
    QueueSpaceConditionVariable.wait(lock, [this] { return IsStop || TaskQueue.size() < QueueCapacity; });
    if (IsStop)
        throw std::runtime_error("Cannot enqueue on a stopped asset thread pool");
    // Recheck after the wait because another producer may have queued this hash.
    if (task.pHashFunc && !ProcessingTaskHashes.insert(hash).second)
        return;
    try
    {
        TaskQueue.push(task);
    }
    catch (...)
    {
        if (task.pHashFunc)
            ProcessingTaskHashes.erase(hash);
        throw;
    }
    ++RemainTask;
    ConditionVariable.notify_one();
}

std::future<void> jThreadPool::Submit(std::function<void()> function)
{
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(function));
    auto future = task->get_future();
    jTask queued;
    queued.pFunc = [task] { (*task)(); };
    Enqueue(queued);
    return future;
}

void jThreadPool::WaitAll()
{
    std::unique_lock<std::mutex> lock(QueueLock);
    CompletionConditionVariable.wait(lock, [this] { return RemainTask == 0; });
    ProcessingTaskHashes.clear();
    const auto error = std::exchange(TaskException, nullptr);
    lock.unlock();
    if (error)
        std::rethrow_exception(error);
}
