#include "pch.h"
#include "jRHI.h"

std::unordered_map<size_t, std::shared_ptr<jShader> > jShader::ShaderMap;
std::unordered_map < std::string , std::shared_ptr<jShader> > jShader::ShaderNameMap;

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
		return it_find->second.get();
	return nullptr;
}

std::shared_ptr<jShader> jShader::GetShaderPtr(size_t hashCode)
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
		return it_find->second.get();
	return nullptr;
}

std::shared_ptr<jShader> jShader::GetShaderPtr(const std::string& name)
{
	auto it_find = ShaderNameMap.find(name);
	if (it_find != ShaderNameMap.end())
		return it_find->second;
	return nullptr;
}

jShader* jShader::CreateShader(const jShaderInfo& shaderInfo)
{
	auto shaderPtr = CreateShaderPtr(shaderInfo);
	return (shaderPtr ? shaderPtr.get() : nullptr);
}

std::shared_ptr<jShader> jShader::CreateShaderPtr(const jShaderInfo& shaderInfo)
{
	auto hash = shaderInfo.CreateShaderHash();
	auto shaderPtr = GetShaderPtr(hash);
	if (shaderPtr)
	{
		if (ShaderNameMap.end() == ShaderNameMap.find(shaderInfo.name))
			ShaderNameMap.insert(std::make_pair(shaderInfo.name, shaderPtr));
		else
			JASSERT(!"The shader was already created before.");
		return shaderPtr;
	}

	shaderPtr = std::shared_ptr<jShader>(g_rhi->CreateShader(shaderInfo));

	if (!shaderPtr)
		return nullptr;

	ShaderMap[hash] = shaderPtr;
	if (!shaderInfo.name.empty())
	{
		JASSERT(ShaderNameMap.end() == ShaderNameMap.find(shaderInfo.name));
		ShaderNameMap[shaderInfo.name] = shaderPtr;
	}
	return shaderPtr;
}
