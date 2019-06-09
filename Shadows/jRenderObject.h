#pragma once
#include "jRHIType.h"
#include "Math\Vector.h"
#include "Math\Matrix.h"

class jCamera;
class jLight;
struct jVertexBuffer;
struct jIndexBuffer;
struct jShader;
struct jMaterialData;
struct jTexture;

class jRenderObject
{
public:
	jRenderObject();
	~jRenderObject();

	void CreateRenderObject(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream);

	void Draw(jCamera* camera, jShader* shader);
	void Draw(jCamera* camera, jShader* shader, jLight* light);
	
	void SetRenderProperty(jShader* shader);
	void SetCameraProperty(jShader* shader, jCamera* camera);
	void SetMaterialProperty(jShader* shader, jMaterialData* materialData);
	void SetLightProperty(jShader* shader, jCamera* camera, jMaterialData* materialData);
	void SetLightProperty(jShader* shader, jLight* light, jMaterialData* materialData);
	void SetTextureProperty(jShader* shader, jMaterialData* materialData);

	std::shared_ptr<jVertexStreamData> VertexStream;
	jVertexBuffer* VertexBuffer = nullptr;

	std::shared_ptr<jIndexStreamData> IndexStream;
	jIndexBuffer* IndexBuffer = nullptr;

	jTexture* tex_object = nullptr;
	jTexture* tex_object2 = nullptr;
	jTexture* tex_object_array = nullptr;

	Vector Pos = Vector::ZeroVector;
	Vector Rot = Vector::ZeroVector;
	Vector Scale = Vector::OneVector;

	Matrix World;

	bool Collided = false;
	bool IsTwoSided = false;
};

