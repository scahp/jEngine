#include "pch.h"
#include "jShader.h"
#include "FileLoader/jFile.h"

//////////////////////////////////////////////////////////////////////////
// jShaderInfo
static robin_hood::unordered_map<jName, jShaderInfo, jNameHashFunc> s_ShaderInfoMap;
void jShaderInfo::AddShaderInfo(const jShaderInfo& shaderInfo)
{
	s_ShaderInfoMap[shaderInfo.name] = shaderInfo;
}

void jShaderInfo::CreateShaders()
{
	for (auto& it : s_ShaderInfoMap)
		jShader::CreateShader(it.second);
}

struct jShaderInfoCreation
{
	jShaderInfoCreation()
	{ }
} s_shaderInfoCreation;

//////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<jShader> > jShader::ShaderVector;
robin_hood::unordered_map<jName, std::shared_ptr<jShader>, jNameHashFunc> jShader::ShaderNameMap;

jShader* jShader::GetShader(const jName& name)
{
	auto it_find = ShaderNameMap.find(name);
	if (it_find != ShaderNameMap.end())
		return it_find->second.get();
	return nullptr;
}

std::shared_ptr<jShader> jShader::GetShaderPtr(const jName& name)
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
	auto shaderPtr = GetShaderPtr(shaderInfo.name);
	if (shaderPtr)
	{
		if (ShaderNameMap.end() == ShaderNameMap.find(shaderInfo.name))
			ShaderNameMap[shaderInfo.name] = shaderPtr;
		else
			JASSERT(!"The shader was already created before.");
		return shaderPtr;
	}

	shaderPtr = std::shared_ptr<jShader>(g_rhi->CreateShader(shaderInfo));

	if (!shaderPtr)
		return nullptr;

	if (shaderInfo.name.IsValid())
	{
		JASSERT(ShaderNameMap.end() == ShaderNameMap.find(shaderInfo.name));
		ShaderNameMap[shaderInfo.name] = shaderPtr;
	}
	ShaderVector.push_back(shaderPtr);
	return shaderPtr;
}

void jShader::UpdateShaders()
{
	if (ShaderVector.empty())
		return;

	static uint64 currentFrame = 0;
	currentFrame = (currentFrame + 1) % 5;
	if (currentFrame)
		return;

	static uint64 currentFile = 0;
	currentFile = (currentFile + 1) % ShaderVector.size();
	const std::shared_ptr<jShader>& currentShader = ShaderVector[currentFile];
	JASSERT(currentShader);
	currentShader->UpdateShader();
}

void jShader::UpdateShader()
{
	auto checkTimeStampFunc = [this](const std::string& filename) -> uint64
	{
		if (filename.length() > 0)
			return jFile::GetFileTimeStamp(filename.c_str());
		return 0;
	};

	uint64 currentTimeStamp = checkTimeStampFunc(ShaderInfo.vs.ToStr());
	currentTimeStamp = Max(currentTimeStamp, checkTimeStampFunc(ShaderInfo.fs.ToStr()));
	currentTimeStamp = Max(currentTimeStamp, checkTimeStampFunc(ShaderInfo.cs.ToStr()));
	currentTimeStamp = Max(currentTimeStamp, checkTimeStampFunc(ShaderInfo.gs.ToStr()));
	
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
