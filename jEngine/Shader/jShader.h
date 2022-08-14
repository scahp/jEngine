#pragma once
#include "Core/jName.h"

struct jShaderInfo
{
	static std::unordered_map<jName, jShaderInfo, jNameHashFunc> s_ShaderInfoMap;
	static void AddShaderInfo(const jShaderInfo& shaderInfo);
	static void CreateShaders();
	
	void Initialize() {}

	FORCEINLINE size_t GetHash() const
	{
		if (!Hash)
		{
			Hash = 0;
			if (name.IsValid())
				Hash = CityHash64WithSeed(name, Hash);
			if (vs.IsValid())
				Hash = CityHash64WithSeed(vs, Hash);
			if (gs.IsValid())
				Hash = CityHash64WithSeed(gs, Hash);
			if (fs.IsValid())
				Hash = CityHash64WithSeed(fs, Hash);
			if (cs.IsValid())
				Hash = CityHash64WithSeed(cs, Hash);
			if (vsPreProcessor.IsValid())
				Hash = CityHash64WithSeed(vsPreProcessor, Hash);
			if (gsPreProcessor.IsValid())
				Hash = CityHash64WithSeed(gsPreProcessor, Hash);
			if (fsPreProcessor.IsValid())
				Hash = CityHash64WithSeed(fsPreProcessor, Hash);
			if (csPreProcessor.IsValid())
				Hash = CityHash64WithSeed(csPreProcessor, Hash);
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
	virtual ~jShader() {}

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
	virtual void Initialize() {}
};