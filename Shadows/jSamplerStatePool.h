#pragma once

struct jSamplerStateInfo;

class jSamplerStatePool
{
public:
	static std::shared_ptr<jSamplerStateInfo> CreateSamplerState(const jName& name, const jSamplerStateInfo& info);
	static std::shared_ptr<jSamplerStateInfo> GetSamplerState(const jName& name);

	static void CreateDefaultSamplerState();

private:
	static std::map<jName, std::shared_ptr<jSamplerStateInfo> > SamplerStateMap;
	static std::map<jSamplerStateInfo*, jName > SamplerStateNameVariableMap;
};

