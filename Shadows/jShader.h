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

#define DECLARE_SHADER_VS_FS_WITH_OPTION_MORE(Name, VS, FS, IsUseTexture, IsUseMaterial, MoreOption) \
{ \
jShaderInfo info; \
info.name = jName(Name); \
info.vs = jName(VS); \
info.fs = jName(FS); \
info.vsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : "") + std::string("\r\n"MoreOption));\
info.fsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : "") + ("\r\n"MoreOption));\
jShaderInfo::AddShaderInfo(info); \
}

#define DECLARE_SHADER_VS_FS_WITH_OPTION(Name, VS, FS, IsUseTexture, IsUseMaterial) \
{ \
jShaderInfo info; \
info.name = jName(Name); \
info.vs = jName(VS); \
info.fs = jName(FS); \
info.vsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : ""));\
info.fsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : ""));\
jShaderInfo::AddShaderInfo(info); \
}

#define DECLARE_SHADER_VS_FS_WITH_DEFINE(Name, VS, FS, AdditionalDifine) \
{ \
jShaderInfo info; \
info.name = jName(Name); \
info.vs = jName(VS); \
info.fs = jName(FS); \
info.vsPreProcessor = jName("\r\n"AdditionalDifine);\
info.fsPreProcessor = jName("\r\n"AdditionalDifine);\
jShaderInfo::AddShaderInfo(info); \
}

#define DECLARE_SHADER_VS_FS(Name, VS, FS) DECLARE_SHADER_VS_FS_WITH_OPTION(Name, VS, FS, false, false)

#define DECLARE_SHADER_VS_GS_FS_WITH_OPTION(Name, VS, GS, FS, IsUseTexture, IsUseMaterial) \
{ \
jShaderInfo info; \
info.name = jName(Name); \
info.vs = jName(VS); \
info.gs = jName(GS); \
info.fs = jName(FS); \
info.vsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : ""));\
info.gsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : "")) ; \
info.fsPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : ""));\
jShaderInfo::AddShaderInfo(info); \
}

#define DECLARE_SHADER_VS_GS_FS(Name, VS, GS, FS) DECLARE_SHADER_VS_GS_FS_WITH_OPTION(Name, VS, GS, FS, false, false)

#define DECLARE_SHADER_CS_WITH_OPTION(Name, CS, IsUseTexture, IsUseMaterial) \
{ \
jShaderInfo info; \
info.name = jName(Name); \
info.cs = jName(CS); \
info.csPreProcessor = jName(std::string(IsUseTexture ? "#define USE_TEXTURE 1" : "") + std::string(IsUseMaterial ? "\r\n#define USE_MATERIAL 1" : ""));\
jShaderInfo::AddShaderInfo(info); \
}
#define DECLARE_SHADER_CS(Name, CS) DECLARE_SHADER_CS_WITH_OPTION(Name, CS, false, false);


struct jShader : public std::enable_shared_from_this<jShader>
{
	//static std::unordered_map<size_t, std::shared_ptr<jShader> > ShaderMap;
	static std::vector<std::shared_ptr<jShader> > ShaderVector;
	static std::unordered_map<jName, std::shared_ptr<jShader>, jNameHashFunc> ShaderNameMap;
	//static jShader* GetShader(size_t hashCode);
	static jShader* GetShader(const jName& name);
	static jShader* CreateShader(const jShaderInfo& shaderInfo);
	//static std::shared_ptr<jShader> GetShaderPtr(size_t hashCode);
	static std::shared_ptr<jShader> GetShaderPtr(const jName& name);
	static std::shared_ptr<jShader> CreateShaderPtr(const jShaderInfo& shaderInfo);
	static void UpdateShaders();

	uint64 TimeStamp = 0;
	jShaderInfo ShaderInfo;

	void UpdateShader();
};