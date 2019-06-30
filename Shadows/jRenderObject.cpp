#include "pch.h"
#include "jRenderObject.h"
#include "jGame.h"
#include "jRHI.h"
#include "jCamera.h"
#include "jLight.h"
#include "jRHI_OpenGL.h"
#include "jRenderTargetPool.h"
#include "jShadowAppProperties.h"


jRenderObject::jRenderObject()
{
}


jRenderObject::~jRenderObject()
{
}

void jRenderObject::CreateRenderObject(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream)
{
	VertexStream = vertexStream;
	IndexStream = indexStream;
	VertexBuffer = g_rhi->CreateVertexBuffer(VertexStream);
	IndexBuffer = g_rhi->CreateIndexBuffer(IndexStream);
}

void jRenderObject::Draw(jCamera* camera, jShader* shader, int32 startIndex, int32 count)
{
	if (VertexBuffer->VertexStreamData.expired())
		return;

	g_rhi->SetShader(shader);
	g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);

	jMaterialData materialData;

	SetRenderProperty(shader);
	SetCameraProperty(shader, camera);
	SetLightProperty(shader, camera, &materialData);
	SetTextureProperty(shader, &materialData);
	SetMaterialProperty(shader, &materialData);

	startIndex = startIndex != -1 ? startIndex : 0;

	auto vertexStreamData = VertexBuffer->VertexStreamData.lock();
	auto primitiveType = vertexStreamData->PrimitiveType;
	if (IndexBuffer)
	{
		auto indexStreamData = IndexBuffer->IndexStreamData.lock();		
		count = count != -1 ? count : indexStreamData->ElementCount;
		g_rhi->DrawElement(primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count);
	}
	else
	{
		count = count != -1 ? count : vertexStreamData->ElementCount;
		g_rhi->DrawArray(primitiveType, 0, count);
	}

	for (auto& iter : materialData.Params)
		delete iter;
	materialData.Params.clear();
}

void jRenderObject::Draw(jCamera* camera, jShader* shader, jLight* light, int32 startIndex, int32 count)
{
	if (VertexBuffer->VertexStreamData.expired())
		return;

	g_rhi->SetShader(shader);
	g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);

	jMaterialData materialData;

	SetRenderProperty(shader);
	SetCameraProperty(shader, camera);
	SetLightProperty(shader, light, &materialData);
	SetTextureProperty(shader, &materialData);
	SetMaterialProperty(shader, &materialData);
	
	startIndex = startIndex != -1 ? startIndex : 0;

	auto vertexStreamData = VertexBuffer->VertexStreamData.lock();
	auto primitiveType = vertexStreamData->PrimitiveType;
	if (IndexBuffer)
	{
		auto indexStreamData = IndexBuffer->IndexStreamData.lock();
		count = count != -1 ? count : indexStreamData->ElementCount;
		g_rhi->DrawElement(primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count);
	}
	else
	{
		count = count != -1 ? count : vertexStreamData->ElementCount;
		g_rhi->DrawArray(primitiveType, 0, count);
	}

	for (auto& iter : materialData.Params)
		delete iter;
	materialData.Params.clear();
}

void jRenderObject::Draw(jCamera* camera, jShader* shader, jLight* light, int32 startIndex, int32 count, int32 baseVertexIndex)
{
	if (VertexBuffer->VertexStreamData.expired())
		return;

	g_rhi->SetShader(shader);
	g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);

	jMaterialData materialData;

	SetRenderProperty(shader);
	SetCameraProperty(shader, camera);
	SetLightProperty(shader, light, &materialData);
	SetTextureProperty(shader, &materialData);
	SetMaterialProperty(shader, &materialData);

	auto vertexStreamData = VertexBuffer->VertexStreamData.lock();
	auto primitiveType = vertexStreamData->PrimitiveType;
	if (IndexBuffer)
	{
		auto indexStreamData = IndexBuffer->IndexStreamData.lock();
		g_rhi->DrawElementBaseVertex(primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, baseVertexIndex);
	}
	else
	{
		g_rhi->DrawArray(primitiveType, baseVertexIndex, count);
	}

	for (auto& iter : materialData.Params)
		delete iter;
	materialData.Params.clear();
}

void jRenderObject::Draw(jCamera* camera, jShader* shader, int32 startIndex, int32 count, int32 baseVertexIndex)
{
	if (VertexBuffer->VertexStreamData.expired())
		return;

	g_rhi->SetShader(shader);
	g_rhi->EnableCullFace(camera->IsEnableCullMode && !IsTwoSided);

	jMaterialData materialData;

	SetRenderProperty(shader);
	SetCameraProperty(shader, camera);
	SetLightProperty(shader, camera, &materialData);
	SetTextureProperty(shader, &materialData);
	SetMaterialProperty(shader, &materialData);

	startIndex = startIndex != -1 ? startIndex : 0;

	auto vertexStreamData = VertexBuffer->VertexStreamData.lock();
	auto primitiveType = vertexStreamData->PrimitiveType;
	if (IndexBuffer)
	{
		auto indexStreamData = IndexBuffer->IndexStreamData.lock();
		count = count != -1 ? count : indexStreamData->ElementCount;
		g_rhi->DrawElementBaseVertex(primitiveType, static_cast<int32>(indexStreamData->Param->GetElementSize()), startIndex, count, baseVertexIndex);
	}
	else
	{
		count = count != -1 ? count : vertexStreamData->ElementCount;
		g_rhi->DrawArray(primitiveType, baseVertexIndex, count);
	}

	for (auto& iter : materialData.Params)
		delete iter;
	materialData.Params.clear();
}

void jRenderObject::SetRenderProperty(jShader* shader)
{
	if (VertexBuffer)
		VertexBuffer->Bind(shader);
	if (IndexBuffer)
		IndexBuffer->Bind(shader);
}

void jRenderObject::SetCameraProperty(jShader* shader, jCamera* camera)
{
	auto posMatrix = Matrix::MakeTranslate(Pos);
	auto rotMatrix = Matrix::MakeRotate(Rot);
	auto scaleMatrix = Matrix::MakeScale(Scale);

	World = posMatrix * rotMatrix * scaleMatrix;
	auto MV = camera->View * World;
	auto MVP = camera->Projection * MV;
	auto VP = camera->Projection * camera->View;
	
	g_rhi->SetUniformbuffer(&jUniformBuffer<Matrix>("MVP", MVP), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Matrix>("MV", MV), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Matrix>("VP", VP), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Matrix>("M", World), shader);
	
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>("Eye", camera->Pos), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("Collided", Collided), shader);

	// todo 어디로 옮겨야 함.
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector2>("ShadowMapSize", Vector2(camera->ShadowMapTexelSize)), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("PCF_Size_Directional", camera->PCF_SIZE_DIRECTIONAL), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("PCF_Size_OmniDirectional", camera->PCF_SIZE_OMNIDIRECTIONAL), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("ESM_C", 40.0f), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("PointLightESM_C", 40.0f), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("SpotLightESM_C", 40.0f), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("UseUniformColor", UseUniformColor), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector4>("Color", Color), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("UseMaterial", UseMaterial), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowOn", jShadowAppSettingProperties::GetInstance().ShadowOn ? 1 : 0), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("DeepShadowAlpha", jShadowAppSettingProperties::GetInstance().DeepShadowAlpha), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("ShadingModel", static_cast<int>(ShadingModel)), shader);
}

void jRenderObject::SetLightProperty(jShader* shader, jCamera* camera, jMaterialData* materialData)
{
	int ambient = 0;
	int directional = 0;
	int point = 0;
	int spot = 0;
	for (int i = 0; i < camera->LightList.size(); ++i)
	{
		auto light = camera->LightList[i];
		switch (light->Type)
		{
		case ELightType::AMBIENT:
			light->BindLight(shader, materialData);
			ambient = 1;
			break;
		case ELightType::DIRECTIONAL:
			light->BindLight(shader, materialData, directional);
			++directional;
			break;
		case ELightType::POINT:
			light->BindLight(shader, materialData, point);
			++point;
			break;
		case ELightType::SPOT:
			light->BindLight(shader, materialData, spot);
			++spot;
			break;
		default:
			break;
		}
	}

	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("UseAmbientLight", ambient), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("NumOfDirectionalLight", directional), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("NumOfPointLight", point), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("NumOfSpotLight", spot), shader);

	if (!directional)
	{
		auto materialParam = new jMaterialParam_OpenGL();
		materialParam->Name = "shadow_object";
		materialParam->Texture = jRenderTargetPool::GetNullTexture(ETextureType::TEXTURE_2D);
		materialParam->Minification = ETextureFilter::LINEAR;
		materialParam->Magnification = ETextureFilter::LINEAR;
		materialData->Params.push_back(materialParam);
	}
	if (!point)
	{
		auto materialParam = new jMaterialParam_OpenGL();
		materialParam->Name = "shadow_object_point_array";
		materialParam->Texture = jRenderTargetPool::GetNullTexture(ETextureType::TEXTURE_2D_ARRAY);
		materialParam->Minification = ETextureFilter::LINEAR;
		materialParam->Magnification = ETextureFilter::LINEAR;
		materialData->Params.push_back(materialParam);
	}
	if (!spot)
	{
		auto materialParam = new jMaterialParam_OpenGL();
		materialParam->Name = "shadow_object_spot_array";
		materialParam->Texture = jRenderTargetPool::GetNullTexture(ETextureType::TEXTURE_2D_ARRAY);
		materialParam->Minification = ETextureFilter::LINEAR;
		materialParam->Magnification = ETextureFilter::LINEAR;
		materialData->Params.push_back(materialParam);
	}
}

void jRenderObject::SetLightProperty(jShader* shader, jLight* light, jMaterialData* materialData)
{
	int ambient = 0;
	int directional = 0;
	int point = 0;
	int spot = 0;
	if (light)
	{
		switch (light->Type)
		{
		case ELightType::AMBIENT:
			light->BindLight(shader, materialData);
			ambient = 1;
			break;
		case ELightType::DIRECTIONAL:
			light->BindLight(shader, materialData, directional);
			++directional;
			break;
		case ELightType::POINT:
			light->BindLight(shader, materialData, point);
			++point;
			break;
		case ELightType::SPOT:
			light->BindLight(shader, materialData, spot);
			++spot;
			break;
		default:
			break;
		}
	}

	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("UseAmbientLight", ambient), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("NumOfDirectionalLight", directional), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("NumOfPointLight", point), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<int>("NumOfSpotLight", spot), shader);
}

void jRenderObject::SetTextureProperty(jShader* shader, jMaterialData* materialData)
{
	if (materialData)
	{
		if (tex_object)
		{
			auto tex_object_param = new jMaterialParam_OpenGL();
			tex_object_param->Name = "tex_object";
			tex_object_param->Texture = tex_object;
			materialData->Params.push_back(tex_object_param);
		}

		bool useTexture = false;
		if (tex_object2)
		{
			auto tex_object2_param = new jMaterialParam_OpenGL();
			tex_object2_param->Name = "tex_object2";
			tex_object2_param->Texture = tex_object2;
			materialData->Params.push_back(tex_object2_param);
			useTexture = true;
		}
		g_rhi->SetUniformbuffer(&jUniformBuffer<int>("UseTexture", useTexture), shader);

		if (tex_object_array)
		{
			auto tex_objectArray_param = new jMaterialParam_OpenGL();
			tex_objectArray_param->Name = "tex_object_array";
			tex_objectArray_param->Texture = tex_object_array;
			materialData->Params.push_back(tex_objectArray_param);
		}

		// todo 어디로 옮겨야함.
		if (GBuffer)
		{
			{
				auto tex_gl = static_cast<jTexture_OpenGL*>(GBuffer->Textures[0]);
				auto param = new jMaterialParam_OpenGL();
				param->Name = "ColorSampler";
				param->Texture = tex_gl;
				materialData->Params.push_back(param);
			}

			{
				auto tex_gl = static_cast<jTexture_OpenGL*>(GBuffer->Textures[1]);
				auto param = new jMaterialParam_OpenGL();
				param->Name = "NormalSampler";
				param->Texture = tex_gl;
				materialData->Params.push_back(param);
			}

			{
				auto tex_gl = static_cast<jTexture_OpenGL*>(GBuffer->Textures[2]);
				auto param = new jMaterialParam_OpenGL();
				param->Name = "PosInWorldSampler";
				param->Texture = tex_gl;
				materialData->Params.push_back(param);
			}

			{
				auto tex_gl = static_cast<jTexture_OpenGL*>(GBuffer->Textures[3]);
				auto param = new jMaterialParam_OpenGL();
				param->Name = "PosInLightSampler";
				param->Texture = tex_gl;
				materialData->Params.push_back(param);
			}
		}
	}
}

void jRenderObject::SetMaterialProperty(jShader* shader, jMaterialData* materialData)
{
	g_rhi->SetMatetrial(materialData, shader);
}
