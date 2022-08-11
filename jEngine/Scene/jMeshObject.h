#pragma once
#include "jObject.h"
#include "Math\Vector.h"

struct jTexture;

struct jMeshMaterial
{
	enum class EMaterialTextureType : int8
	{
		DiffuseSampler = 0,
		SpecularSampler,
		AmbientSampler,
		EmissiveSampler,
		HeightSampler,				// Height or Bump map
		NormalSampler,
		ShininessSampler,
		OpacitySampler,
		DisplacementSampler,
		LightmapSampler,
		ReflectionSampler,
		Max
	};

	static jName MaterialTextureTypeString[(int32)EMaterialTextureType::Max + 1];

	struct Material
	{
		Vector4 Ambient = Vector4::OneVector;
		Vector4 Diffuse = Vector4::OneVector;
		Vector4 Specular = Vector4::OneVector;
		Vector4 Emissive = Vector4::OneVector;
		float SpecularShiness = 0.0f;
		float Opacity = 1.0f;
		float Reflectivity = 0.0f;
		float IndexOfRefraction = 1.0f;		// 1.0 means lights will not refract

		void BindMaterialData(const jShader* shader) const;
	};

	struct TextureData
	{
		std::weak_ptr<jTexture> TextureWeakPtr;
		ETextureAddressMode TextureAddressModeU = ETextureAddressMode::REPEAT;
		ETextureAddressMode TextureAddressModeV = ETextureAddressMode::REPEAT;
		std::string TextureName;

		const jTexture* GetTexture() const
		{ 
			if (TextureWeakPtr.expired())
				return nullptr;
			return TextureWeakPtr.lock().get(); 
		}
	};

	Material Data;
	TextureData TexData[static_cast<int32>(EMaterialTextureType::Max)];
};
struct jMeshData
{
	std::vector<Vector> Vertices;
	std::vector<Vector> Normals;
	std::vector<Vector> Tangents;
	std::vector<Vector> Bitangents;
	std::vector<Vector2> TexCoord;
	std::map<int32, jMeshMaterial*> Materials;
	std::vector<uint32> Faces;
};

struct jSubMesh
{
	int32 StartVertex = -1;
	int32 EndVertex = -1;
	int32 StartFace = -1;
	int32 EndFace = -1;
	int32 MaterialIndex = -1;

	jMaterialData MaterialData;
	std::string Name;
};

struct jMeshNode
{
	int32 SubMeshIndex = -1;
	jMeshNode* Parent = nullptr;

	std::vector<int32> MeshIndex;
	std::vector<jMeshNode*> childNode;
};

class jMeshObject : public jObject
{
public:
	static jMeshMaterial NullMeshMateral;

	jMeshObject();
	
	jMeshData* MeshData = nullptr;
	mutable std::vector<jSubMesh> SubMeshes;
	jMeshNode* RootNode = nullptr;

	virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount = 0 ) const override;

	void SetMaterialUniform(const jShader* shader, const jMeshMaterial* material) const;

	void DrawNode(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jMeshNode* node, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights) const;
	void DrawSubMesh(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, int32 meshIndex, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights) const;
};

class jHairObject : public jObject
{
public:

};
