#include "pch.h"
#include "jMeshObject.h"
#include "jRenderObject.h"
#include "jRHI.h"

jMeshMaterial jMeshObject::NullMeshMateral;

jMeshObject::jMeshObject()
{	
}

void jMeshObject::Draw(const jCamera* camera, const jShader* shader)
{
	if (Visible && RenderObject)
		DrawNode(RootNode, camera, shader, nullptr);
}

void jMeshObject::Draw(const jCamera* camera, const jShader* shader, const jLight* light)
{
	if (Visible && RenderObject)
		DrawNode(RootNode, camera, shader, light);
}

void jMeshObject::SetMaterialUniform(const jShader* shader, const jMeshMaterial* material)
{
	g_rhi->SetShader(shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector4>("Material.Diffuse", material->Data.Diffuse), shader);

	// todo vec4인데 일단 vec3으로 넘김.. 고민임.
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>("Material.Specular", material->Data.Specular), shader);


	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>("Material.Emissive", material->Data.Emissive), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>("Material.Shininess", material->Data.SpecularPow), shader);
}

void jMeshObject::DrawNode(const jMeshNode* node, const jCamera* camera, const jShader* shader, const jLight* light)
{
	for (auto& iter : node->MeshIndex)
		DrawSubMesh(iter, camera, shader, light);

	for (auto& iter : node->childNode)
		DrawNode(iter, camera, shader, light);
}

void jMeshObject::DrawSubMesh(int32 meshIndex, const jCamera* camera, const jShader* shader, const jLight* light)
{
	auto& subMesh = SubMeshes[meshIndex];
	auto it_find = MeshData->Materials.find(subMesh.MaterialIndex);
	if (MeshData->Materials.end() != it_find)
	{
		RenderObject->tex_object2 = it_find->second->Texture;
		SetMaterialUniform(shader, it_find->second);
	}
	else
	{
		RenderObject->tex_object2 = nullptr;
		SetMaterialUniform(shader, &NullMeshMateral);
	}

	if (light)
	{
		if (subMesh.EndFace > 0)
			RenderObject->Draw(camera, shader, light, subMesh.StartFace, subMesh.EndFace - subMesh.StartFace, subMesh.StartVertex);
		else
			RenderObject->Draw(camera, shader, light, subMesh.StartVertex, subMesh.EndVertex - subMesh.StartVertex, subMesh.StartVertex);
	}
	else
	{
		if (subMesh.EndFace > 0)
			RenderObject->Draw(camera, shader, subMesh.StartFace, subMesh.EndFace - subMesh.StartFace, subMesh.StartVertex);
		else
			RenderObject->Draw(camera, shader, subMesh.StartVertex, subMesh.EndVertex - subMesh.StartVertex, subMesh.StartVertex);
	}
}

