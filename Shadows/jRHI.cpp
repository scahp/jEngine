#include "pch.h"
#include "jRHI.h"

std::unordered_map<size_t, jShader*> jShader::ShaderMap;
std::unordered_map < std::string , jShader* > jShader::ShaderNameMap;

//////////////////////////////////////////////////////////////////////////
void IUniformBuffer::Bind(const jShader* shader) const
{
	g_rhi->SetUniformbuffer(this, shader);
}

//////////////////////////////////////////////////////////////////////////

jRHI::jRHI()
{
}

void jRHI::MapBufferdata(IBuffer* buffer)
{

}

jShader* jShader::GetShader(size_t hashCode)
{
	auto it_find = ShaderMap.find(hashCode);
	if (it_find != ShaderMap.end())
		return it_find->second;
	return nullptr;
}

jShader* jShader::GetShader(const std::string& name)
{
	auto it_find = ShaderNameMap.find(name);
	if (it_find != ShaderNameMap.end())
		return it_find->second;
	return nullptr;
}

jShader* jShader::CreateShader(const jShaderInfo& shaderInfo)
{
	auto hash = shaderInfo.CreateShaderHash();
	auto shader = GetShader(hash);
	if (shader)
		return shader;

	shader = g_rhi->CreateShader(shaderInfo);

	if (!shader)
		return nullptr;

	ShaderMap[hash] = shader;
	if (!shaderInfo.name.empty())
	{
		JASSERT(ShaderNameMap.end() == ShaderNameMap.find(shaderInfo.name));
		ShaderNameMap[shaderInfo.name] = shader;
	}
	return shader;
}
