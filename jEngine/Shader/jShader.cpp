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
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderForwardPixelShader
	, "ForwardPS"
	, "Resource/Shaders/hlsl/shader_fs.hlsl"
	, ""
	, EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderGBufferVertexShader
    , "GBufferVS"
    , "Resource/Shaders/hlsl/gbuffer_vs.hlsl"
    , ""
    , EShaderAccessStageFlag::VERTEX)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderGBufferPixelShader
	, "GBufferPS"
	, "Resource/Shaders/hlsl/gbuffer_fs.hlsl"
	, ""
	, EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderDirectionalLightPixelShader
    , "DirectionalLightShaderPS"
    , "Resource/Shaders/hlsl/directionallight_fs.hlsl"
    , ""
    , EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPointLightPixelShader
    , "PointLightShaderPS"
    , "Resource/Shaders/hlsl/pointlight_fs.hlsl"
    , ""
    , EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderSpotLightPixelShader
    , "SpotLightShaderPS"
    , "Resource/Shaders/hlsl/spotlight_fs.hlsl"
    , ""
    , EShaderAccessStageFlag::FRAGMENT)
