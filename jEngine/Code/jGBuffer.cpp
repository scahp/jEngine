#include "pch.h"
#include "jGBuffer.h"
#include "jRHI.h"

jGBuffer::~jGBuffer()
{
	GeometryBuffer;
}

bool jGBuffer::Begin(int index /*= 0*/, bool mrt /*= false*/) const
{
	return GeometryBuffer && GeometryBuffer->Begin(index, mrt);
}

void jGBuffer::End() const
{
	GeometryBuffer->End();
}

void jGBuffer::BindGeometryBuffer(const jShader* shader) const
{
	jMaterialData materialData;
	{
		auto param = new jMaterialParam();
		param->Name = "ColorSampler";
		param->Texture = GeometryBuffer->Textures[0].get();
		materialData.Params.push_back(param);
	}

	{
		auto param = new jMaterialParam();
		param->Name = "NormalSampler";
		param->Texture = GeometryBuffer->Textures[1].get();
		materialData.Params.push_back(param);
	}

	{
		auto param = new jMaterialParam();
		param->Name = "PosSampler";
		param->Texture = GeometryBuffer->Textures[2].get();
		materialData.Params.push_back(param);
	}

	//{
	//	auto param = new jMaterialParam();
	//	param->Name = "PosInLightSampler";
	//	param->Texture = GeometryBuffer->Textures[3].get();
	//	materialData.Params.push_back(param);
	//}

	g_rhi->SetMatetrial(&materialData, shader, 20);
}