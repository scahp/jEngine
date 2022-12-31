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
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderForward
	, "ForwardPS"
	, "Resource/Shaders/hlsl/shader_fs.hlsl"
	, ""
	, EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderGBuffer
	, "GBufferPS"
	, "Resource/Shaders/hlsl/gbuffer_fs.hlsl"
	, ""
	, EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderDirectionalLight
    , "DirectionalLightShaderPS"
    , "Resource/Shaders/hlsl/directionallight_fs.hlsl"
    , ""
    , EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPointLight
    , "PointLightShaderPS"
    , "Resource/Shaders/hlsl/pointlight_fs.hlsl"
    , ""
    , EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderSpotLight
    , "SpotLightShaderPS"
    , "Resource/Shaders/hlsl/spotlight_fs.hlsl"
    , ""
    , EShaderAccessStageFlag::FRAGMENT)
