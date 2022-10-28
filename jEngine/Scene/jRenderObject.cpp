#include "pch.h"
#include "jRenderObject.h"
#include "jGame.h"
#include "Scene/jCamera.h"
#include "Scene/jLight.h"
#include "Scene/jObject.h"
#include "RHI/jFrameBufferPool.h"
#include "RHI/Vulkan/jUniformBufferBlock_Vulkan.h"


jRenderObject::jRenderObject()
{
}


jRenderObject::~jRenderObject()
{
    delete VertexBuffer;
    delete VertexBuffer_PositionOnly;
	delete VertexBuffer_InstanceData;
	delete IndirectCommandBuffer;
    
	VertexStream.reset();
    VertexStream_PositionOnly.reset();

	delete IndexBuffer;
}

void jRenderObject::CreateRenderObject(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream)
{
	VertexStream = vertexStream;
	IndexStream = indexStream;

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
}

void jRenderObject::UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream)
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

void jRenderObject::UpdateVertexStream()
{
	g_rhi->UpdateVertexBuffer(VertexBuffer, VertexStream);
	g_rhi->UpdateVertexBuffer(VertexBuffer_PositionOnly, VertexStream_PositionOnly);
}

void jRenderObject::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext
	, int32 startIndex, int32 count, int32 instanceCount)
{
	if (!VertexStream)
		return;
	
	startIndex = startIndex != -1 ? startIndex : 0;

	const EPrimitiveType primitiveType = GetPrimitiveType();
	if (IndexBuffer)
	{
        if (IndirectCommandBuffer)
        {
			const int32 instanceCount = VertexStream_InstanceData->ElementCount;
            g_rhi->DrawElementsIndirect(InRenderFrameContext, primitiveType, IndirectCommandBuffer, 0, instanceCount);
        }
		else
		{
			auto& indexStreamData = IndexBuffer->IndexStreamData;
			count = count != -1 ? count : indexStreamData->ElementCount;
			if (instanceCount <= 0)
				g_rhi->DrawElements(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count);
			else
				g_rhi->DrawElementsInstanced(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, instanceCount);
		}
	}
	else
	{
		if (IndirectCommandBuffer)
		{
			const int32 instanceCount = VertexStream_InstanceData->ElementCount;
			g_rhi->DrawIndirect(InRenderFrameContext, primitiveType, IndirectCommandBuffer, 0, instanceCount);
		}
		else
		{
			count = count != -1 ? count : VertexStream->ElementCount;
			if (instanceCount <= 0)
				g_rhi->DrawArrays(InRenderFrameContext, primitiveType, 0, count);
			else
				g_rhi->DrawArraysInstanced(InRenderFrameContext, primitiveType, 0, count, instanceCount);
		}
	}
}

//void jRenderObject::DrawBaseVertexIndex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader
//	, const std::list<const jLight*>& lights, const jMaterialData& materialData, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount /*= 1*/)
//{
//	if (VertexBuffer->VertexStreamData)
//		return;
//
//	g_rhi->SetShader(shader);
//	g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);
//
//	{
//		//SCOPE_CPU_PROFILE(jRenderObject_UpdateMaterialData);
//		std::vector<const jMaterialData*> DynamicMaterialData;
//		DynamicMaterialData.reserve(lights.size());
//
//		SetRenderProperty(InRenderFrameContext,shader);
//		SetCameraProperty(shader, camera);
//		//SetLightProperty(shader, camera, lights, &DynamicMaterialData);
//		for (auto iter : lights)
//		{
//			auto matData = iter->GetMaterialData();
//			if (matData)
//				DynamicMaterialData.push_back(matData);
//		}
//		SetTextureProperty(shader, &materialData);
//		SetMaterialProperty(shader, &materialData, DynamicMaterialData);
//	}
//
//	{
//		//SCOPE_CPU_PROFILE(jRenderObject_DrawBaseVertexIndex);
//		auto& vertexStreamData = VertexBuffer->VertexStreamData;
//		auto primitiveType = vertexStreamData->PrimitiveType;
//		if (IndexBuffer)
//		{
//			if (IndirectCommandBuffer)
//			{
//				g_rhi->DrawElementsIndirect(InRenderFrameContext, primitiveType, IndirectCommandBuffer, 0, 1);
//			}
//			else
//			{
//				auto& indexStreamData = IndexBuffer->IndexStreamData;
//				if (instanceCount <= 0)
//					g_rhi->DrawElementsBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, baseVertexIndex);
//				else
//					g_rhi->DrawElementsInstancedBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, baseVertexIndex, instanceCount);
//			}
//		}
//		else
//		{
//			if (instanceCount <= 0)
//				g_rhi->DrawArrays(InRenderFrameContext, primitiveType, baseVertexIndex, count);
//			else
//				g_rhi->DrawArraysInstanced(InRenderFrameContext, primitiveType, baseVertexIndex, count, instanceCount);
//		}
//	}
//}

void jRenderObject::BindBuffers(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, bool InPositionOnly) const
{
    if (InPositionOnly)
    {
        if (VertexBuffer_PositionOnly)
            VertexBuffer_PositionOnly->Bind(InRenderFrameContext);
    }
    else
    {
        if (VertexBuffer)
            VertexBuffer->Bind(InRenderFrameContext);
    }
    if (VertexBuffer_InstanceData)
        VertexBuffer_InstanceData->Bind(InRenderFrameContext);
    if (IndexBuffer)
        IndexBuffer->Bind(InRenderFrameContext);
}

void jRenderObject::UpdateWorldMatrix()
{
    if (static_cast<int32>(DirtyFlags) & static_cast<int32>(EDirty::POS_ROT_SCALE))
    {
        auto posMatrix = Matrix::MakeTranslate(Pos);
        auto rotMatrix = Matrix::MakeRotate(Rot);
        auto scaleMatrix = Matrix::MakeScale(Scale);
        World = posMatrix * rotMatrix * scaleMatrix;

        ClearDirtyFlags(EDirty::POS_ROT_SCALE);
    }
}

const std::vector<float>& jRenderObject::GetVertices() const
{
	if (VertexStream && !VertexStream->Params.empty())
		return static_cast<jStreamParam<float>*>(VertexStream->Params[0].get())->Data;

	static const std::vector<float> s_emtpy;
	return s_emtpy;
}

jShaderBindingInstance* jRenderObject::CreateShaderBindingInstance(const jView* view)
{
	check(view);
	check(view->Camera);

	jRenderObjectUniformBuffer ubo;
	ubo.M = World;
	ubo.MV = (view->Camera->View * World);
	ubo.MVP = (view->Camera->Projection * ubo.MV);
	ubo.MV = ubo.MV;
	ubo.InvM = ubo.M;

    int32 BindingPoint = 0;
	jShaderBindingArray ShaderBindingArray;
	jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

    jUniformBufferBlock_Vulkan OneFrameUniformBuffer(jNameStatic("RenderObjectUniformParameters"), jLifeTimeType::OneFrame);
    OneFrameUniformBuffer.Init(sizeof(ubo));
    OneFrameUniformBuffer.UpdateBufferData(&ubo, sizeof(ubo));
	
	ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_GRAPHICS
		, ResourceInlineAllactor.Alloc<jUniformBufferResource>(&OneFrameUniformBuffer));

  //  for (int32 i = 0; i < (int32)MaterialData.Params.size(); ++i)
  //  {
		//ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
		//	, ResourceInlineAllactor.Alloc<jTextureResource>(MaterialData.Params[i].Texture, MaterialData.Params[i].SamplerState));
  //  }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray);
}
