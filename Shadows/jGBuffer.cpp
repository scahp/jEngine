#include "pch.h"
#include "jGBuffer.h"
#include "jRHI.h"

jGBuffer::~jGBuffer()
{
	GeometryBuffer;
}

bool jGBuffer::GBufferBegin(int index /*= 0*/, bool mrt /*= false*/) const
{
	return GeometryBuffer && GeometryBuffer->FBOBegin(index, mrt);
}

void jGBuffer::End() const
{
	GeometryBuffer->End();
}

void jGBuffer::BindGeometryBuffer(const jShader* shader) const
{
	jMaterialData materialData;
	{
		static auto name = jName("ColorSampler");
		materialData.AddMaterialParam(name, GeometryBuffer->Textures[0].get());
	}

	{
        static auto name = jName("NormalSampler");
        materialData.AddMaterialParam(name, GeometryBuffer->Textures[1].get());
	}

	{
        static auto name = jName("PosInWorldSampler");
        materialData.AddMaterialParam(name, GeometryBuffer->Textures[2].get());
	}

	{
        static auto name = jName("PosInLightSampler");
        materialData.AddMaterialParam(name, GeometryBuffer->Textures[3].get());
	}

	g_rhi->SetMatetrial(&materialData, shader, 20);
}