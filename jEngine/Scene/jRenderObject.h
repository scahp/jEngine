#pragma once
#include "Math\Vector.h"
#include "Math\Matrix.h"
#include "jBoundPrimitiveType.h"

struct jVertexBuffer;
struct jIndexBuffer;
struct jTexture;
struct jSamplerStateInfo;
class jMaterial;

class jRenderObjectGeometryData
{
public:
	jRenderObjectGeometryData() = default;
	jRenderObjectGeometryData(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream);
	~jRenderObjectGeometryData();

	void Create(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream);

    // Vertex buffers
    void UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream);
    void UpdateVertexStream();

	EPrimitiveType GetPrimitiveType() const { return VertexStream ? VertexStream->PrimitiveType : EPrimitiveType::MAX; }
	FORCEINLINE bool HasInstancing() const { return !!VertexBuffer_InstanceData; }

    std::shared_ptr<jVertexStreamData> VertexStream;
    std::shared_ptr<jVertexStreamData> VertexStream_InstanceData;
    std::shared_ptr<jVertexStreamData> VertexStream_PositionOnly;

    // Index buffer
    std::shared_ptr<jIndexStreamData> IndexStream;

    jVertexBuffer* VertexBuffer = nullptr;
    jVertexBuffer* VertexBuffer_PositionOnly = nullptr;
    jVertexBuffer* VertexBuffer_InstanceData = nullptr;
    jIndexBuffer* IndexBuffer = nullptr;

    // IndirectCommand buffer
    jBuffer* IndirectCommandBuffer = nullptr;
};

class jRenderObject
{
public:
	jRenderObject();
	virtual ~jRenderObject();

    virtual void CreateRenderObject(const std::shared_ptr<jRenderObjectGeometryData>& InRenderObjectGeometryData);

	virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
		, int32 startIndex, int32 indexCount, int32 startVertex, int32 vertexCount, int32 instanceCount);
	virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, int32 instanceCount = 1);

	EPrimitiveType GetPrimitiveType() const { return GeometryDataPtr->GetPrimitiveType(); }
	virtual void BindBuffers(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, bool InPositionOnly) const;
	const std::vector<float>& GetVertices() const;
	FORCEINLINE bool HasInstancing() const { return GeometryDataPtr->HasInstancing(); }

    void UpdateWorldMatrix();
    Matrix World;

	std::shared_ptr<jRenderObjectGeometryData> GeometryDataPtr;

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
		Matrix InvM;
	};

	//////////////////////////////////////////////////////////////////////////
	// RenderObjectUniformBuffer
	virtual jShaderBindingInstance* CreateShaderBindingInstance();
	//////////////////////////////////////////////////////////////////////////

    std::shared_ptr<jMaterial> MaterialPtr;

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

