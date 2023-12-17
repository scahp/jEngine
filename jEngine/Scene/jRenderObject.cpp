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

jRenderObjectGeometryData::~jRenderObjectGeometryData()
{
    delete VertexBuffer;
    delete VertexBuffer_PositionOnly;
    delete VertexBuffer_InstanceData;
    delete IndexBuffer;
    delete IndirectCommandBuffer;

    VertexStream.reset();
    VertexStream_PositionOnly.reset();
}

void jRenderObjectGeometryData::Create(const std::shared_ptr<jVertexStreamData>& InVertexStream, const std::shared_ptr<jIndexStreamData>& InIndexStream, bool InHasVertexColor, bool InHasVertexBiTangent)
{
    VertexStream = InVertexStream;
    IndexStream = InIndexStream;

    if (VertexStream && ensure(VertexStream->Params.size()))
    {
        VertexStream_PositionOnly = std::make_shared<jVertexStreamData>();
        VertexStream_PositionOnly->Params.push_back(VertexStream->Params[0]);
        VertexStream_PositionOnly->PrimitiveType = VertexStream->PrimitiveType;
        VertexStream_PositionOnly->ElementCount = VertexStream->ElementCount;
    }

    VertexBuffer = g_rhi->CreateVertexBuffer(VertexStream);
    VertexBuffer_PositionOnly = g_rhi->CreateVertexBuffer(VertexStream_PositionOnly);
    IndexBuffer = g_rhi->CreateIndexBuffer(IndexStream);

    bHasVertexColor = InHasVertexColor;
    bHasVertexBiTangent = InHasVertexBiTangent;
}

void jRenderObjectGeometryData::UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream)
{
    VertexStream = vertexStream;
    g_rhi->UpdateVertexBuffer(VertexBuffer, VertexStream);

    if (VertexStream && ensure(VertexStream->Params.size()))
    {
        VertexStream_PositionOnly = std::make_shared<jVertexStreamData>();
        VertexStream_PositionOnly->Params.push_back(VertexStream->Params[0]);
        VertexStream_PositionOnly->PrimitiveType = VertexStream->PrimitiveType;
        VertexStream_PositionOnly->ElementCount = VertexStream->ElementCount;
    }
}

void jRenderObjectGeometryData::UpdateVertexStream()
{
    g_rhi->UpdateVertexBuffer(VertexBuffer, VertexStream);
    g_rhi->UpdateVertexBuffer(VertexBuffer_PositionOnly, VertexStream_PositionOnly);
}

// jRenderObject
jRenderObject::jRenderObject()
{
}

jRenderObject::~jRenderObject()
{
    RenderObjectUniformParametersPtr.reset();
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
	if (GeometryDataPtr->IndexBuffer)
	{
        if (GeometryDataPtr->IndirectCommandBuffer)
        {
            g_rhi->DrawElementsIndirect(InRenderFrameContext, primitiveType, GeometryDataPtr->IndirectCommandBuffer, startIndex, instanceCount);
        }
		else
		{
			auto& indexStreamData = GeometryDataPtr->IndexBuffer->IndexStreamData;
			indexCount = indexCount != -1 ? indexCount : indexStreamData->ElementCount;
			if (instanceCount <= 0)
				g_rhi->DrawElementsBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, indexCount, startVertex);
			else
				g_rhi->DrawElementsInstancedBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, indexCount, startVertex, instanceCount);
		}
	}
	else
	{
		if (GeometryDataPtr->IndirectCommandBuffer)
		{
			g_rhi->DrawIndirect(InRenderFrameContext, primitiveType, GeometryDataPtr->IndirectCommandBuffer, startVertex, instanceCount);
		}
		else
		{
			vertexCount = vertexCount != -1 ? vertexCount : GeometryDataPtr->VertexStream->ElementCount;
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
        if (GeometryDataPtr->VertexBuffer_PositionOnly)
            GeometryDataPtr->VertexBuffer_PositionOnly->Bind(InRenderFrameContext);
    }
    else
    {
        if (GeometryDataPtr->VertexBuffer)
            GeometryDataPtr->VertexBuffer->Bind(InRenderFrameContext);
    }
    
    if (InOverrideInstanceData)
        InOverrideInstanceData->Bind(InRenderFrameContext);
    else if (GeometryDataPtr->VertexBuffer_InstanceData)
        GeometryDataPtr->VertexBuffer_InstanceData->Bind(InRenderFrameContext);

    if (GeometryDataPtr->IndexBuffer)
        GeometryDataPtr->IndexBuffer->Bind(InRenderFrameContext);
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
    if (GeometryDataPtr->VertexStream && !GeometryDataPtr->VertexStream->Params.empty())
		return static_cast<jStreamParam<float>*>(GeometryDataPtr->VertexStream->Params[0].get())->Data;

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

        if (RenderObjectUniformParametersPtr)
            RenderObjectUniformParametersPtr->Free();

        RenderObjectUniformParametersPtr = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("RenderObjectUniformParameters"), jLifeTimeType::MultiFrame, sizeof(jRenderObjectUniformBuffer)));
        RenderObjectUniformParametersPtr->UpdateBufferData(&ubo, sizeof(ubo));

        LastMetallic = gOptions.Metallic;
        LastRoughness = gOptions.Roughness;

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllactor.Alloc<jUniformBufferResource>(RenderObjectUniformParametersPtr.get()));

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

