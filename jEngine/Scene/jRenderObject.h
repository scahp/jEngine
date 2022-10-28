#pragma once
#include "Math\Vector.h"
#include "Math\Matrix.h"
#include "jBoundPrimitiveType.h"

struct jVertexBuffer;
struct jIndexBuffer;
struct jTexture;
struct jSamplerStateInfo;

class jRenderObject
{
public:
	jRenderObject();
	~jRenderObject();

	void CreateRenderObject(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream);

	//void Draw(const jCamera* camera, const jShader* shader, int32 startIndex = -1, int32 count = -1);
	void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
		, int32 startIndex = 0, int32 count = -1, int32 instanceCount = 1);

	// todo 함수를 줄일까? 아니면 이렇게 쓸까? 고민
	//void Draw(const jCamera* camera, const jShader* shader, int32 startIndex, int32 count, int32 baseVertexIndex);
	//void DrawBaseVertexIndex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, const jMaterialData& materialData, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount = 1);
	
	EPrimitiveType GetPrimitiveType() const { return VertexStream ? VertexStream->PrimitiveType : EPrimitiveType::MAX; }
	void BindBuffers(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, bool InPositionOnly) const;

	const std::vector<float>& GetVertices() const;

	void UpdateWorldMatrix();
	Matrix GetWorld() const;

	FORCEINLINE bool HasInstancing() const { return !!VertexBuffer_InstanceData; }

	// Vertex buffers
    void UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream);
    void UpdateVertexStream();

	std::shared_ptr<jVertexStreamData> VertexStream;
	std::shared_ptr<jVertexStreamData> VertexStream_InstanceData;
	std::shared_ptr<jVertexStreamData> VertexStream_PositionOnly;
	jVertexBuffer* VertexBuffer = nullptr;
	jVertexBuffer* VertexBuffer_PositionOnly = nullptr;
	jVertexBuffer* VertexBuffer_InstanceData = nullptr;

	// Index buffer
	std::shared_ptr<jIndexStreamData> IndexStream;
	jIndexBuffer* IndexBuffer = nullptr;

	// IndirectCommand buffer
    jBuffer* IndirectCommandBuffer = nullptr;

	Matrix World;

	FORCEINLINE void SetPos(const Vector& InPos) { Pos = InPos; SetDirtyFlags(EDirty::POS); }
	FORCEINLINE void SetRot(const Vector& InRot) { Rot = InRot; SetDirtyFlags(EDirty::ROT); }
	FORCEINLINE void SetScale(const Vector& InScale) { Scale = InScale; SetDirtyFlags(EDirty::SCALE); }
	FORCEINLINE const Vector& GetPos() const { return Pos; }
	FORCEINLINE const Vector& GetRot() const { return Rot; }
	FORCEINLINE const Vector& GetScale() const { return Scale; }

    bool IsTwoSided = false;
    bool IsHiddenBoundBox = false;
	//////////////////////////////////////////////////////////////////////////

	struct jRenderObjectUniformBuffer
	{
		Matrix M;
		Matrix MV;
		Matrix MVP;
		Matrix InvM;
	};

	//////////////////////////////////////////////////////////////////////////
	// RenderObjectUniformBuffer
	jShaderBindingInstance* CreateShaderBindingInstance(const jView* view);
	//////////////////////////////////////////////////////////////////////////

private:
	enum EDirty : int8
	{
		NONE	= 0,
		POS		= 1,
		ROT		= 1 << 1,
		SCALE	= 1 << 2,
		POS_ROT_SCALE = POS | ROT | SCALE,
	};
	EDirty DirtyFlags = EDirty::POS_ROT_SCALE;
	void SetDirtyFlags(EDirty InEnum)
	{
		using T = std::underlying_type<EDirty>::type;
		DirtyFlags = static_cast<EDirty>(static_cast<T>(InEnum) | static_cast<T>(DirtyFlags));
	}
	void ClearDirtyFlags(EDirty InEnum)
	{
		using T = std::underlying_type<EDirty>::type;
		DirtyFlags = static_cast<EDirty>(static_cast<T>(InEnum) & (!static_cast<T>(DirtyFlags)));
	}
	FORCEINLINE void ClearDirtyFlags() { DirtyFlags = EDirty::NONE; }

	Vector Pos = Vector::ZeroVector;
	Vector Rot = Vector::ZeroVector;
	Vector Scale = Vector::OneVector;
};

