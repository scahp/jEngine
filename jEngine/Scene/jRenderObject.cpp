#include "pch.h"
#include "jRenderObject.h"
#include "jGame.h"
#include "Scene/jCamera.h"
#include "Scene/jLight.h"
#include "Scene/jObject.h"
#include "RHI/jFrameBufferPool.h"


jRenderObject::jRenderObject()
{
}


jRenderObject::~jRenderObject()
{
    delete VertexBuffer;
    delete VertexBuffer_PositionOnly;
    
	VertexStream.reset();
    VertexStream_PositionOnly.reset();

	delete IndexBuffer;
	MaterialData.Clear();
	DynamicMaterialData.clear();

	// 다른 곳에서 레퍼런싱 한 데이터는 nullptr 로 비워줌
    tex_object_array = nullptr;
    samplerStateTexArray = nullptr;

	// UniofrmBuffer를 해제 해줌
	delete RenderObjectUniformBuffer;
	RenderObjectUniformBuffer = nullptr;
	
	ShaderBindingInstance = nullptr;		// Pool 에서 제거되기 때문에 nullptr 로 초기화만 해줌
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

void jRenderObject::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader
	, const std::list<const jLight*>& lights, int32 startIndex /*= 0*/, int32 count /*= -1*/, int32 instanceCount /*= 1*/, bool bPoisitionOnly /*= false*/)
{
	if (!VertexBuffer->VertexStreamData)
		return;

	g_rhi->SetShader(shader);
	if (camera)
		g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);
	else
		g_rhi->EnableCullFace(!IsTwoSided);

	DynamicMaterialData.clear();

	SetRenderProperty(InRenderFrameContext,shader);
	//SetCameraProperty(shader, camera);
	for (auto iter : lights)
	{
		auto matData = iter->GetMaterialData();
		if (matData)
			DynamicMaterialData.push_back(matData);
	}
	SetTextureProperty(shader, &MaterialData);
	SetMaterialProperty(shader, &MaterialData, DynamicMaterialData);
	
	startIndex = startIndex != -1 ? startIndex : 0;

	auto& vertexStreamData = VertexBuffer->VertexStreamData;
	auto primitiveType = vertexStreamData->PrimitiveType;
	if (IndexBuffer)
	{
		auto& indexStreamData = IndexBuffer->IndexStreamData;
		count = count != -1 ? count : indexStreamData->ElementCount;
		if (instanceCount <= 0)
			g_rhi->DrawElements(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count);
		else
			g_rhi->DrawElementsInstanced(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, instanceCount);
	}
	else
	{
		count = count != -1 ? count : vertexStreamData->ElementCount;
		if (instanceCount <= 0)
			g_rhi->DrawArrays(InRenderFrameContext, primitiveType, 0, count);
		else
			g_rhi->DrawArraysInstanced(InRenderFrameContext, primitiveType, 0, count, instanceCount);
	}
}

void jRenderObject::DrawBaseVertexIndex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader
	, const std::list<const jLight*>& lights, const jMaterialData& materialData, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount /*= 1*/)
{
	if (VertexBuffer->VertexStreamData)
		return;

	g_rhi->SetShader(shader);
	g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);

	{
		//SCOPE_PROFILE(jRenderObject_UpdateMaterialData);
		std::vector<const jMaterialData*> DynamicMaterialData;
		DynamicMaterialData.reserve(lights.size());

		SetRenderProperty(InRenderFrameContext,shader);
		SetCameraProperty(shader, camera);
		//SetLightProperty(shader, camera, lights, &DynamicMaterialData);
		for (auto iter : lights)
		{
			auto matData = iter->GetMaterialData();
			if (matData)
				DynamicMaterialData.push_back(matData);
		}
		SetTextureProperty(shader, &materialData);
		SetMaterialProperty(shader, &materialData, DynamicMaterialData);
	}

	{
		//SCOPE_PROFILE(jRenderObject_DrawBaseVertexIndex);
		auto& vertexStreamData = VertexBuffer->VertexStreamData;
		auto primitiveType = vertexStreamData->PrimitiveType;
		if (IndexBuffer)
		{
			auto& indexStreamData = IndexBuffer->IndexStreamData;
			if (instanceCount <= 0)
				g_rhi->DrawElementsBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, baseVertexIndex);
			else
				g_rhi->DrawElementsInstancedBaseVertex(InRenderFrameContext, primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, baseVertexIndex, instanceCount);
		}
		else
		{
			if (instanceCount <= 0)
				g_rhi->DrawArrays(InRenderFrameContext, primitiveType, baseVertexIndex, count);
			else
				g_rhi->DrawArraysInstanced(InRenderFrameContext, primitiveType, baseVertexIndex, count, instanceCount);
		}
	}
}

void jRenderObject::SetRenderProperty(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jShader* shader, bool bPositionOnly /*= false*/)
{
#if USE_OPENGL
	if (bPositionOnly)
	{
		if (VertexBuffer_PositionOnly)
			VertexBuffer_PositionOnly->Bind(shader);
	}
	else
	{
		if (VertexBuffer)
			VertexBuffer->Bind(shader);
	}
	if (IndexBuffer)
		IndexBuffer->Bind(shader);
#elif USE_VULKAN
	if (bPositionOnly)
	{
		if (VertexBuffer_PositionOnly)
			VertexBuffer_PositionOnly->Bind(InRenderFrameContext);
	}
	else
	{
		if (VertexBuffer)
			VertexBuffer->Bind(InRenderFrameContext);
	}
	if (IndexBuffer)
		IndexBuffer->Bind(InRenderFrameContext);
#endif
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

Matrix jRenderObject::GetWorld() const
{
	return World;
}

void jRenderObject::SetCameraProperty(const jShader* shader, const jCamera* camera)
{
	if (!camera)
		return;

	if (static_cast<int32>(DirtyFlags) & static_cast<int32>(EDirty::POS_ROT_SCALE))
	{
		auto posMatrix = Matrix::MakeTranslate(Pos);
		auto rotMatrix = Matrix::MakeRotate(Rot);
		auto scaleMatrix = Matrix::MakeScale(Scale);
		World = posMatrix * rotMatrix * scaleMatrix;

		ClearDirtyFlags(EDirty::POS_ROT_SCALE);
	}

	auto MV = camera->View * World;
	auto MVP = camera->Projection * MV;

	SET_UNIFORM_BUFFER_STATIC("MVP", MVP, shader);
	SET_UNIFORM_BUFFER_STATIC("MV", MV, shader);
	SET_UNIFORM_BUFFER_STATIC("M", World, shader);
	SET_UNIFORM_BUFFER_STATIC("InvM", World.GetInverse(), shader);
	SET_UNIFORM_BUFFER_STATIC("Collided", Collided, shader);
	SET_UNIFORM_BUFFER_STATIC("UseUniformColor", UseUniformColor, shader);
	SET_UNIFORM_BUFFER_STATIC("Color", Color, shader);
	SET_UNIFORM_BUFFER_STATIC("UseMaterial", UseMaterial, shader);
	SET_UNIFORM_BUFFER_STATIC("ShadingModel", static_cast<int>(ShadingModel), shader);
	SET_UNIFORM_BUFFER_STATIC("IsTwoSided", IsTwoSided, shader);
}

void jRenderObject::SetLightProperty(const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights, jMaterialData* materialData)
{
	for (auto iter : lights)
		iter->GetMaterialData(materialData);
}

void jRenderObject::SetTextureProperty(const jShader* shader, const jMaterialData* materialData)
{
	const bool useTexture = (materialData->Params.size() > 0);
	SET_UNIFORM_BUFFER_STATIC("UseTexture", useTexture, shader);
}

void jRenderObject::SetMaterialProperty(const jShader* shader, const jMaterialData* materialData, const std::vector<const jMaterialData*>& dynamicMaterialData)
{
	int32 lastIndex = 0;
	if (materialData)
		lastIndex = g_rhi->SetMatetrial(materialData, shader, lastIndex);
	
	for (auto MatData : dynamicMaterialData)
	{
		lastIndex = g_rhi->SetMatetrial(MatData, shader, lastIndex);
	}
}

const std::vector<float>& jRenderObject::GetVertices() const
{
	if (VertexStream && !VertexStream->Params.empty())
		return static_cast<jStreamParam<float>*>(VertexStream->Params[0].get())->Data;

	static const std::vector<float> s_emtpy;
	return s_emtpy;
}

void jRenderObject::SetTexture(int32 index, const jName& name, const jTexture* texture, const jSamplerStateInfo* samplerState)
{
	MaterialData.SetMaterialParam(index, name, texture, samplerState);
}

void jRenderObject::SetTextureWithCommonName(int32 index, const jTexture* texture, const jSamplerStateInfo* samplerState)
{
	MaterialData.SetMaterialParam(index, GetCommonTextureName(index), texture, samplerState);
}

void jRenderObject::ClearTexture()
{
	MaterialData.Clear();
}

void jRenderObject::UpdateRenderObjectUniformBuffer(const jView* view)
{
	check(view);
	check(view->Camera);

	jRenderObjectUniformBuffer ubo;
	ubo.M = World;
	ubo.MV = (view->Camera->View * World);
	ubo.MVP = (view->Camera->Projection * ubo.MV);
	ubo.MV = ubo.MV;
	ubo.InvM = ubo.M;

	if (!RenderObjectUniformBuffer)
		RenderObjectUniformBuffer = CreateRenderObjectUniformBuffer();
	RenderObjectUniformBuffer->UpdateBufferData(&ubo, sizeof(ubo));
}
