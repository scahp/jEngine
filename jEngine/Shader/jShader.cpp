#include "pch.h"
#include "jShader.h"
#include "FileLoader/jFile.h"

jShader::~jShader()
{
	delete CompiledShader;
}

void jShader::UpdateShaders()
{
	// currentShader->UpdateShader();
}

void jShader::UpdateShader()
{
	auto checkTimeStampFunc = [this](const char* filename) -> uint64
	{
		if (filename)
			return jFile::GetFileTimeStamp(filename);
		return 0;
	};

	uint64 currentTimeStamp = checkTimeStampFunc(ShaderInfo.GetShaderFilepath().ToStr());
	
	if (currentTimeStamp <= 0)
		return;
	
	if (TimeStamp == 0)
	{
		TimeStamp = currentTimeStamp;
		return;
	}

	if (TimeStamp < currentTimeStamp)
	{
		g_rhi->CreateShaderInternal(this, ShaderInfo);
		TimeStamp = currentTimeStamp;
	}
}

void jShader::Initialize()
{
	verify(g_rhi->CreateShaderInternal(this, ShaderInfo));
}

//////////////////////////////////////////////////////////////////////////
jShaderInfo jShaderGBuffer::GShaderInfo(
    jNameStatic("default_testPS"),
    jNameStatic("Resource/Shaders/hlsl/gbuffer_fs.hlsl"),
    jNameStatic(""),
    EShaderAccessStageFlag::FRAGMENT
);

jShaderGBuffer* jShaderGBuffer::CreateShader(const jShaderGBuffer::ShaderPermutation& InPermutation)
{
	jShaderInfo TempShaderInfo = GShaderInfo;
	TempShaderInfo.SetPermutationId(InPermutation.GetPermutationId());

	jShaderGBuffer* Shader = g_rhi->CreateShader<jShaderGBuffer>(TempShaderInfo);
	Shader->Permutation = InPermutation;
    return Shader;
}
