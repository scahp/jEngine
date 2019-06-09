#include <pch.h>
#include "jAppSettings.h"

jAppSettings* jAppSettings::_instance = nullptr;

//////////////////////////////////////////////////////////////////////////
void jAppSettings::Init(int32 width, int32 height)
{
	// todo it's depend on rhi type
	TwInit(TW_OPENGL_CORE, nullptr);
	TwWindowSize(width, height);

	jAppSettings::GetInstance().AddTwBar("MainPannel");		// Default Pannel
}

jAppSettingBase* jAppSettings::AddTwBar(const char* barName)
{
	auto it_find = BarMap.find(barName);
	if (it_find != BarMap.end())
		delete it_find->second;

	auto appSettings = new jAppSettingBase(TwNewBar("TestNumberOneTeakBar!"));
	BarMap[barName] = appSettings;
	return appSettings;
}

void jAppSettings::DeleteTwBar(const char* barName)
{
	auto it_find = BarMap.find(barName);
	if (it_find != BarMap.end())
	{
		delete it_find->second;
		BarMap.erase(it_find);
	}
}

//////////////////////////////////////////////////////////////////////////
