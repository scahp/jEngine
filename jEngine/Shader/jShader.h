#pragma once
#include "Core/jName.h"

struct jShaderInfo
{
	static std::unordered_map<jName, jShaderInfo, jNameHashFunc> s_ShaderInfoMap;
	static void AddShaderInfo(const jShaderInfo& shaderInfo);
	static void CreateShaders();

	FORCEINLINE size_t GetHash() const
	{
		if (!Hash)
		{
			Hash = 0;
			if (name.IsValid())
				Hash ^= name;
			if (vs.IsValid())
				Hash ^= vs;
			if (gs.IsValid())
				Hash ^= gs;
			if (fs.IsValid())
				Hash ^= fs;
			if (cs.IsValid())
				Hash ^= cs;
			if (vsPreProcessor.IsValid())
				Hash ^= vsPreProcessor;
			if (gsPreProcessor.IsValid())
				Hash ^= gsPreProcessor;
			if (fsPreProcessor.IsValid())
				Hash ^= fsPreProcessor;
			if (csPreProcessor.IsValid())
				Hash ^= csPreProcessor;
		}
		return Hash;
	}
	mutable size_t Hash = 0;

	jName name;
	jName vs;
	jName gs;
	jName fs;
	jName cs;
	jName vsPreProcessor;
	jName gsPreProcessor;
	jName fsPreProcessor;
	jName csPreProcessor;
};

struct jShader : public std::enable_shared_from_this<jShader>
{
	static std::vector<std::shared_ptr<jShader> > ShaderVector;
	static std::unordered_map<jName, std::shared_ptr<jShader>, jNameHashFunc> ShaderNameMap;
	static jShader* GetShader(const jName& name);
	static jShader* CreateShader(const jShaderInfo& shaderInfo);
	static std::shared_ptr<jShader> GetShaderPtr(const jName& name);
	static std::shared_ptr<jShader> CreateShaderPtr(const jShaderInfo& shaderInfo);
	static void UpdateShaders();

	uint64 TimeStamp = 0;
	jShaderInfo ShaderInfo;

	void UpdateShader();
};