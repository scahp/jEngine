#include "pch.h"
#include "jRenderObject.h"
#include "jGame.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "Scene/jObject.h"
#include "RHI/jFrameBufferPool.h"
#include "RHI/Vulkan/jUniformBufferBlock_Vulkan.h"
#include "Material/jMaterial.h"
#include "jOptions.h"
#include "RHI/DX12/jVertexBuffer_DX12.h"
#include "RHI/DX12/jIndexBuffer_DX12.h"
#include "RHI/DX12/jBufferUtil_DX12.h"

// jRenderObjectGeometryData
jRenderObjectGeometryData::jRenderObjectGeometryData(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream)
{
    Create(vertexStream, indexStream);
}

jRenderObjectGeometryData::jRenderObjectGeometryData(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jVertexStreamData>& positionOnlyVertexStream, const std::shared_ptr<jIndexStreamData>& indexStream)
{
    CreateNew_ForRaytracing(vertexStream, positionOnlyVertexStream, indexStream);
}

jRenderObjectGeometryData::~jRenderObjectGeometryData()
{
    VertexStreamPtr.reset();
    VertexStream_PositionOnlyPtr.reset();
}

void jRenderObjectGeometryData::Create(const std::shared_ptr<jVertexStreamData>& InVertexStream, const std::shared_ptr<jIndexStreamData>& InIndexStream, bool InHasVertexColor, bool InHasVertexBiTangent)
{
    VertexStreamPtr = InVertexStream;
    IndexStreamPtr = InIndexStream;

    if (VertexStreamPtr && ensure(VertexStreamPtr->Params.size()))
    {
        VertexStream_PositionOnlyPtr = std::make_shared<jVertexStreamData>();
        VertexStream_PositionOnlyPtr->Params.push_back(VertexStreamPtr->Params[0]);
        VertexStream_PositionOnlyPtr->PrimitiveType = VertexStreamPtr->PrimitiveType;
        VertexStream_PositionOnlyPtr->ElementCount = VertexStreamPtr->ElementCount;
    }

    VertexBufferPtr = g_rhi->CreateVertexBuffer(VertexStreamPtr);
    VertexBuffer_PositionOnlyPtr = g_rhi->CreateVertexBuffer(VertexStream_PositionOnlyPtr);
    IndexBufferPtr = g_rhi->CreateIndexBuffer(IndexStreamPtr);

    bHasVertexColor = InHasVertexColor;
    bHasVertexBiTangent = InHasVertexBiTangent;
}

void jRenderObjectGeometryData::CreateNew_ForRaytracing(const std::shared_ptr<jVertexStreamData>& InVertexStream, const std::shared_ptr<jVertexStreamData>& InVertexStream_PositionOnly
    , const std::shared_ptr<jIndexStreamData>& InIndexStream, bool InHasVertexColor, bool InHasVertexBiTangent)
{
    VertexStreamPtr = InVertexStream;
    VertexStream_PositionOnlyPtr = InVertexStream_PositionOnly;
    IndexStreamPtr = InIndexStream;

    VertexBufferPtr = g_rhi->CreateVertexBuffer(VertexStreamPtr);
    VertexBuffer_PositionOnlyPtr = g_rhi->CreateVertexBuffer(VertexStream_PositionOnlyPtr);
    IndexBufferPtr = g_rhi->CreateIndexBuffer(IndexStreamPtr);

    bHasVertexColor = InHasVertexColor;
    bHasVertexBiTangent = InHasVertexBiTangent;
}

void jRenderObjectGeometryData::UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream)
{
    VertexStreamPtr = vertexStream;

    if (VertexStreamPtr && ensure(VertexStreamPtr->Params.size()))
    {
        VertexStream_PositionOnlyPtr = std::make_shared<jVertexStreamData>();
        VertexStream_PositionOnlyPtr->Params.push_back(VertexStreamPtr->Params[0]);
        VertexStream_PositionOnlyPtr->PrimitiveType = VertexStreamPtr->PrimitiveType;
        VertexStream_PositionOnlyPtr->ElementCount = VertexStreamPtr->ElementCount;
    }
}

// jRenderObject
jRenderObject::jRenderObject()
{
}

jRenderObject::~jRenderObject()
{
}

void jRenderObject::CreateRenderObject(const std::shared_ptr<jRenderObjectGeometryData>& InRenderObjectGeometryData)
{
    GeometryDataPtr = InRenderObjectGeometryData;
    UpdateWorldMatrix();
}

void jRenderObject::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
	, int32 startIndex, int32 indexCount, int32 startVertex, int32 vertexCount, int32 instanceCount)
{
	if (!GeometryDataPtr)
		return;
	
	startIndex = startIndex != -1 ? startIndex : 0;

	const EPrimitiveType primitiveType = GetPrimitiveType();
	if (GeometryDataPtr->IndexBufferPtr)
	{
        if (GeometryDataPtr->IndirectCommandBufferPtr)
        {
            g_rhi->DrawElementsIndirect(InRenderFrameContext, primitiveType, GeometryDataPtr->IndirectCommandBufferPtr.get(), startIndex, instanceCount);
        }
		else
		{
			auto& indexStreamData = GeometryDataPtr->IndexBufferPtr->IndexStreamData;
			indexCount = indexCount != -1 ? indexCount : indexStreamData->ElementCount;
			if (instanceCount <= 0)
				g_rhi->DrawElementsBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, indexCount, startVertex);
			else
				g_rhi->DrawElementsInstancedBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, indexCount, startVertex, instanceCount);
		}
	}
	else
	{
		if (GeometryDataPtr->IndirectCommandBufferPtr)
		{
			g_rhi->DrawIndirect(InRenderFrameContext, primitiveType, GeometryDataPtr->IndirectCommandBufferPtr.get(), startVertex, instanceCount);
		}
		else
		{
			vertexCount = vertexCount != -1 ? vertexCount : GeometryDataPtr->VertexStreamPtr->ElementCount;
			if (instanceCount <= 0)
				g_rhi->DrawArrays(InRenderFrameContext, primitiveType, startVertex, vertexCount);
			else
				g_rhi->DrawArraysInstanced(InRenderFrameContext, primitiveType, startVertex, vertexCount, instanceCount);
		}
	}
}

void jRenderObject::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, int32 instanceCount)
{
	Draw(InRenderFrameContext, 0, -1, 0, -1, instanceCount);
}

void jRenderObject::BindBuffers(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, bool InPositionOnly, const jVertexBuffer* InOverrideInstanceData) const
{
    if (InPositionOnly)
    {
        if (GeometryDataPtr->VertexBuffer_PositionOnlyPtr)
            GeometryDataPtr->VertexBuffer_PositionOnlyPtr->Bind(InRenderFrameContext);
    }
    else
    {
        if (GeometryDataPtr->VertexBufferPtr)
            GeometryDataPtr->VertexBufferPtr->Bind(InRenderFrameContext);
    }
    
    if (InOverrideInstanceData)
        InOverrideInstanceData->Bind(InRenderFrameContext);
    else if (GeometryDataPtr->VertexBuffer_InstanceDataPtr)
        GeometryDataPtr->VertexBuffer_InstanceDataPtr->Bind(InRenderFrameContext);

    if (GeometryDataPtr->IndexBufferPtr)
        GeometryDataPtr->IndexBufferPtr->Bind(InRenderFrameContext);
}

bool jRenderObject::IsSupportRaytracing() const
{
    if (!GeometryDataPtr)
        return false;

    if (!GeometryDataPtr->VertexBuffer_PositionOnlyPtr)
        return false;

    return GeometryDataPtr->VertexBuffer_PositionOnlyPtr->IsSupportRaytracing();
}

void jRenderObject::UpdateWorldMatrix()
{
    if (static_cast<int32>(DirtyFlags) & static_cast<int32>(EDirty::POS_ROT_SCALE))
    {
        auto posMatrix = Matrix::MakeTranslate(Pos);
        auto rotMatrix = Matrix::MakeRotate(Rot);
        auto scaleMatrix = Matrix::MakeScale(Scale);
        World = posMatrix * rotMatrix * scaleMatrix;

        NeedToUpdateRenderObjectUniformParameters = true;

        ClearDirtyFlags(EDirty::POS_ROT_SCALE);
    }
}

const std::vector<float>& jRenderObject::GetVertices() const
{
    if (GeometryDataPtr->VertexStreamPtr && !GeometryDataPtr->VertexStreamPtr->Params.empty())
		return static_cast<jStreamParam<float>*>(GeometryDataPtr->VertexStreamPtr->Params[0].get())->Data;

	static const std::vector<float> s_emtpy;
	return s_emtpy;
}

const std::shared_ptr<jShaderBindingInstance>& jRenderObject::CreateShaderBindingInstance()
{
    // Special code for PBR test
    if (LastMetallic != gOptions.Metallic || LastRoughness != gOptions.Roughness)
        NeedToUpdateRenderObjectUniformParameters = true;

    // Update uniform buffer if it need to.
    if (NeedToUpdateRenderObjectUniformParameters)
    {
        NeedToUpdateRenderObjectUniformParameters = false;

        jRenderObjectUniformBuffer ubo;
        ubo.M = World;
        ubo.InvM = ubo.M.GetInverse();
        ubo.Metallic = gOptions.Metallic;
        ubo.Roughness = gOptions.Roughness;

        RenderObjectUniformParametersPtr = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("RenderObjectUniformParameters"), jLifeTimeType::MultiFrame, sizeof(jRenderObjectUniformBuffer)));
        RenderObjectUniformParametersPtr->UpdateBufferData(&ubo, sizeof(ubo));

        TestUniformBuffer = g_rhi->CreateStructuredBuffer(sizeof(ubo), 0, sizeof(ubo), EBufferCreateFlag::UAV | EBufferCreateFlag::ShaderBindingTable
            , EImageLayout::GENERAL, &ubo, sizeof(ubo));

        LastMetallic = gOptions.Metallic;
        LastRoughness = gOptions.Roughness;

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jUniformBufferResource>(RenderObjectUniformParametersPtr.get())));

        if (RenderObjectShaderBindingInstance)
            RenderObjectShaderBindingInstance->Free();

        RenderObjectShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
        check(RenderObjectShaderBindingInstance.get());
    }
	
	//ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
	//	, ResourceInlineAllactor.Alloc<jUniformBufferResource>(&RenderObjectUniformParameters));

 //   return g_rhi->CreateShaderBindingInstance(ShaderBindingArray);

    return RenderObjectShaderBindingInstance;
}

