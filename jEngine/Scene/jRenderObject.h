﻿#pragma once
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
	jRenderObjectGeometryData(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jVertexStreamData>& positionOnlyVertexStream, const std::shared_ptr<jIndexStreamData>& indexStream);
	~jRenderObjectGeometryData();

	void Create(const std::shared_ptr<jVertexStreamData>& InVertexStream, const std::shared_ptr<jIndexStreamData>& InIndexStream, bool InHasVertexColor = true, bool InHasVertexBiTangent = false);
    void CreateNew_ForRaytracing(const std::shared_ptr<jVertexStreamData>& InVertexStream, const std::shared_ptr<jVertexStreamData>& InVertexStream_PositionOnly
        , const std::shared_ptr<jIndexStreamData>& InIndexStream, bool InHasVertexColor = false, bool InHasVertexBiTangent = true);

    // Vertex buffers
    void UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream);

	EPrimitiveType GetPrimitiveType() const { return VertexStreamPtr ? VertexStreamPtr->PrimitiveType : EPrimitiveType::MAX; }
	FORCEINLINE bool HasInstancing() const { return !!VertexBuffer_InstanceDataPtr; }
	FORCEINLINE bool HasVertexColor() const { return bHasVertexColor; }
	FORCEINLINE bool HasVertexBiTangent() const { return bHasVertexBiTangent; }

    std::shared_ptr<jVertexStreamData> VertexStreamPtr;
    std::shared_ptr<jVertexStreamData> VertexStream_InstanceDataPtr;
    std::shared_ptr<jVertexStreamData> VertexStream_PositionOnlyPtr;

    // Index buffer
    std::shared_ptr<jIndexStreamData> IndexStreamPtr;

    std::shared_ptr<jVertexBuffer> VertexBufferPtr;
    std::shared_ptr<jVertexBuffer> VertexBuffer_PositionOnlyPtr;
    std::shared_ptr<jVertexBuffer> VertexBuffer_InstanceDataPtr;
    std::shared_ptr<jIndexBuffer> IndexBufferPtr;

    // IndirectCommand buffer
    std::shared_ptr<jBuffer> IndirectCommandBufferPtr;

	bool bHasVertexColor = true;
	bool bHasVertexBiTangent = false;
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
	virtual void BindBuffers(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, bool InPositionOnly, const jVertexBuffer* InOverrideInstanceData = nullptr) const;
	const std::vector<float>& GetVertices() const;
	FORCEINLINE bool HasInstancing() const { return GeometryDataPtr->HasInstancing(); }
	virtual bool IsSupportRaytracing() const;

    void UpdateWorldMatrix();
    Matrix World;

	std::shared_ptr<jRenderObjectGeometryData> GeometryDataPtr;

    std::shared_ptr<jBuffer> BottomLevelASBuffer;
	std::shared_ptr<jBuffer> ScratchASBuffer;
	std::shared_ptr<jBuffer> VertexAndIndexOffsetBuffer;
	uint32 RayTracingHitGroupIndex = 0;

	template <typename T> T* GetBottomLevelASBuffer() const { return (T*)BottomLevelASBuffer.get(); }
	template <typename T> T* GetScratchASBuffer() const { return (T*)ScratchASBuffer.get(); }
	template <typename T> T* GetVertexAndIndexOffsetBuffer() const { return (T*)VertexAndIndexOffsetBuffer.get(); }

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
		float Metallic;
		float Roughness;
		float Padding0;
		float Padding1;
	};

	//////////////////////////////////////////////////////////////////////////
	// RenderObjectUniformBuffer
	virtual const std::shared_ptr<jShaderBindingInstance>& CreateShaderBindingInstance();
	//////////////////////////////////////////////////////////////////////////

    std::shared_ptr<jMaterial> MaterialPtr;
	std::shared_ptr<jBuffer> TestUniformBuffer;

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

	bool NeedToUpdateRenderObjectUniformParameters = false;
	std::shared_ptr<IUniformBufferBlock> RenderObjectUniformParametersPtr;
	std::shared_ptr<jShaderBindingInstance> RenderObjectShaderBindingInstance;

	// Special code for PBR test
    float LastMetallic = 0.0f;
    float LastRoughness = 0.0f;
};

