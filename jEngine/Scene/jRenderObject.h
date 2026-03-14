#pragma once
#include "Math\Vector.h"
#include "Math\Matrix.h"
#include "jBoundPrimitiveType.h"
#include "jSceneObject.h"
#include "jObjectTypes.h"
#include "Shader/jCommonShaderParameters.h"

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

class jRenderObject : public jSceneObject
{
public:
	jRenderObject();
	virtual ~jRenderObject();

	// RenderObject ID for picking
	jRenderObjectID RenderObjectID = 0;
	static jRenderObject* FindRenderObjectByID(jRenderObjectID id);

    virtual void CreateRenderObject(const std::shared_ptr<jRenderObjectGeometryData>& InRenderObjectGeometryData);

	virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
		, int32 startIndex, int32 indexCount, int32 startVertex, int32 vertexCount, int32 instanceCount);
	virtual void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, int32 instanceCount = 1);

	EPrimitiveType GetPrimitiveType() const { return GeometryDataPtr->GetPrimitiveType(); }
    virtual void BindBuffers(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, bool InPositionOnly, const jVertexBuffer* InOverrideInstanceData = nullptr) const;
	const std::vector<float>& GetVertices() const;
	FORCEINLINE bool HasInstancing() const { return GeometryDataPtr->HasInstancing(); }
	virtual bool IsSupportRaytracing() const;

    virtual void UpdateWorldMatrix() override;
    virtual void OnTransformDirty() override;

	std::shared_ptr<jRenderObjectGeometryData> GeometryDataPtr;

    std::shared_ptr<jBuffer> BottomLevelASBuffer;
	std::shared_ptr<jBuffer> ScratchASBuffer;
	std::shared_ptr<jBuffer> VertexAndIndexOffsetBuffer;
	uint32 RayTracingHitGroupIndex = 0;

	template <typename T> T* GetBottomLevelASBuffer() const { return (T*)BottomLevelASBuffer.get(); }
	template <typename T> T* GetScratchASBuffer() const { return (T*)ScratchASBuffer.get(); }
	template <typename T> T* GetVertexAndIndexOffsetBuffer() const { return (T*)VertexAndIndexOffsetBuffer.get(); }

    bool IsTwoSided = false;
    bool IsHiddenBoundBox = false;
	//////////////////////////////////////////////////////////////////////////

    using jRenderObjectUniformBuffer = RenderObjectUniformBuffer;

	//////////////////////////////////////////////////////////////////////////
	// RenderObjectUniformBuffer
	virtual const std::shared_ptr<jShaderBindingInstance>& CreateShaderBindingInstance();
	//////////////////////////////////////////////////////////////////////////

    std::shared_ptr<jMaterial> MaterialPtr;
	std::shared_ptr<jBuffer> TestUniformBuffer;

private:
	// RenderObjectID management
	static jRenderObjectID s_NextRenderObjectID;
	static std::unordered_map<jRenderObjectID, jRenderObject*> s_RenderObjectIDMap;

	bool NeedToUpdateRenderObjectUniformParameters = false;
	std::shared_ptr<IUniformBufferBlock> RenderObjectUniformParametersPtr;
	std::shared_ptr<jShaderBindingInstance> RenderObjectShaderBindingInstance;

	// Special code for PBR test
    float LastMetallic = 0.0f;
    float LastRoughness = 0.0f;
};
