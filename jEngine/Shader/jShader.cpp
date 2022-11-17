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
//////////////////////////////////////////////////////////////////////////
jShaderInfo jShaderDirectionalLight::GShaderInfo(
    jNameStatic("DirectionalLightShaderPS"),
    jNameStatic("Resource/Shaders/hlsl/directionallight_fs.hlsl"),
    jNameStatic(""),
    EShaderAccessStageFlag::FRAGMENT
);

jShaderDirectionalLight* jShaderDirectionalLight::CreateShader(const jShaderDirectionalLight::ShaderPermutation& InPermutation)
{
    jShaderInfo TempShaderInfo = GShaderInfo;
    TempShaderInfo.SetPermutationId(InPermutation.GetPermutationId());

    jShaderDirectionalLight* Shader = g_rhi->CreateShader<jShaderDirectionalLight>(TempShaderInfo);
    Shader->Permutation = InPermutation;
    return Shader;
}
//////////////////////////////////////////////////////////////////////////
jShaderInfo jShaderPointLight::GShaderInfo(
    jNameStatic("PointLightShaderPS"),
    jNameStatic("Resource/Shaders/hlsl/pointlight_fs.hlsl"),
    jNameStatic(""),
    EShaderAccessStageFlag::FRAGMENT
);

jShaderPointLight* jShaderPointLight::CreateShader(const jShaderPointLight::ShaderPermutation& InPermutation)
{
    jShaderInfo TempShaderInfo = GShaderInfo;
    TempShaderInfo.SetPermutationId(InPermutation.GetPermutationId());

	jShaderPointLight* Shader = g_rhi->CreateShader<jShaderPointLight>(TempShaderInfo);
    Shader->Permutation = InPermutation;
    return Shader;
}
//////////////////////////////////////////////////////////////////////////
jShaderInfo jShaderSpotLight::GShaderInfo(
    jNameStatic("SpotLightShaderPS"),
    jNameStatic("Resource/Shaders/hlsl/spotlight_fs.hlsl"),
    jNameStatic(""),
    EShaderAccessStageFlag::FRAGMENT
);

jShaderSpotLight* jShaderSpotLight::CreateShader(const jShaderSpotLight::ShaderPermutation& InPermutation)
{
    jShaderInfo TempShaderInfo = GShaderInfo;
    TempShaderInfo.SetPermutationId(InPermutation.GetPermutationId());

	jShaderSpotLight* Shader = g_rhi->CreateShader<jShaderSpotLight>(TempShaderInfo);
    Shader->Permutation = InPermutation;
    return Shader;
}
