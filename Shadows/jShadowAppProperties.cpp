#include <pch.h>
#include "jShadowAppProperties.h"

jShadowAppSettingProperties* jShadowAppSettingProperties::_instance = nullptr;

void jShadowAppSettingProperties::Setup(jAppSettingBase* appSetting)
{
	//appSetting->AddVariable("ShadowOn", ShadowOn);

	//appSetting->AddEnumVariable("ShadowType", ShadowType, "EShadowType", EShadowTypeString);
	appSetting->AddEnumVariable("ShadowMapType", ShadowMapType, "EShadowMapType", EShadowMapTypeString);
	//appSetting->AddVariable("UsePoissonSample", UsePoissonSample);
	//appSetting->AddVariable("DirectionalLightMap", ShowDirectionalLightMap);

	////////////////////////////////////////////////////////////////////////////
	//// Silhouette Group
	//appSetting->AddVariable("DirectionalLight_Silhouette", ShowSilhouette_DirectionalLight);
	//appSetting->SetGroup("DirectionalLight_Silhouette", "Silhouette");
	//appSetting->SetLabel("DirectionalLight_Silhouette", "DirectionalLight");

	//appSetting->AddVariable("PointLight_Silhouette", ShowSilhouette_PointLight);
	//appSetting->SetGroup("PointLight_Silhouette", "Silhouette");
	//appSetting->SetLabel("PointLight_Silhouette", "PointLight");

	//appSetting->AddVariable("SpotLight_Silhouette", ShowSilhouette_SpotLight);
	//appSetting->SetGroup("SpotLight_Silhouette", "Silhouette");
	//appSetting->SetLabel("SpotLight_Silhouette", "SpotLight");

	////////////////////////////////////////////////////////////////////////////
	//// LightInfo Group
	//appSetting->AddVariable("DirectionalLight_Info", ShowDirectionalLightInfo);
	//appSetting->SetGroup("DirectionalLight_Info", "LightInfo");
	//appSetting->SetLabel("DirectionalLight_Info", "DirectionalLight");

	//appSetting->AddVariable("PointLight_Info", ShowPointLightInfo);
	//appSetting->SetGroup("PointLight_Info", "LightInfo");
	//appSetting->SetLabel("PointLight_Info", "PointLight");

	//appSetting->AddVariable("SpotLight_Info", ShowSpotLightInfo);
	//appSetting->SetGroup("SpotLight_Info", "LightInfo");
	//appSetting->SetLabel("SpotLight_Info", "SpotLight");

	////////////////////////////////////////////////////////////////////////////
	//// Light Setting
	//appSetting->AddDirectionVariable("DirecionalLight_Direction", DirecionalLightDirection);
	//appSetting->SetGroup("DirecionalLight_Direction", "DirectionalLight");
	//appSetting->SetLabel("DirecionalLight_Direction", "Direction");

	//appSetting->AddVariable("PointLight_PositionX", PointLightPosition.x);
	//appSetting->SetGroup("PointLight_PositionX", "PointLight");
	//appSetting->SetLabel("PointLight_PositionX", "X");
	//appSetting->AddVariable("PointLight_PositionY", PointLightPosition.y);
	//appSetting->SetGroup("PointLight_PositionY", "PointLight");
	//appSetting->SetLabel("PointLight_PositionY", "Y");
	//appSetting->AddVariable("PointLight_PositionZ", PointLightPosition.z);
	//appSetting->SetGroup("PointLight_PositionZ", "PointLight");
	//appSetting->SetLabel("PointLight_PositionZ", "Z");

	//appSetting->AddVariable("SpotLight_PositionX", SpotLightPosition.x);
	//appSetting->SetGroup("SpotLight_PositionX", "SpotLightPos");
	//appSetting->SetLabel("SpotLight_PositionX", "X");
	//appSetting->AddVariable("SpotLight_PositionY", SpotLightPosition.y);
	//appSetting->SetGroup("SpotLight_PositionY", "SpotLightPos");
	//appSetting->SetLabel("SpotLight_PositionY", "Y");
	//appSetting->AddVariable("SpotLight_PositionZ", SpotLightPosition.z);
	//appSetting->SetGroup("SpotLight_PositionZ", "SpotLightPos");
	//appSetting->SetLabel("SpotLight_PositionZ", "Z");

	//appSetting->AddDirectionVariable("SpotLight_Direction", SpotLightDirection);
	//appSetting->SetGroup("SpotLight_Direction", "SpotLight");
	//appSetting->SetLabel("SpotLight_Direction", "Direction");

	////////////////////////////////////////////////////////////////////////////
	//// Box Group
	//appSetting->AddVariable("Box", ShowBoundBox);
	//appSetting->SetGroup("Box", "Bound");
	//appSetting->AddVariable("Sphere", ShowBoundSphere);
	//appSetting->SetGroup("Sphere", "Bound");

	////////////////////////////////////////////////////////////////////////////
	//// Deep Shadow Option
	//appSetting->AddVariable("DeepShadowAlpha", DeepShadowAlpha);
	//appSetting->SetGroup("DeepShadowAlpha", "DeepShadow");
	//appSetting->SetStep("DeepShadowAlpha", 0.01f);
	//appSetting->SetMinMax("DeepShadowAlpha", 0.05f, 1.0f);
	
	//appSetting->AddVariable("ExponentDeepShadowOn", ExponentDeepShadowOn);
	//appSetting->SetGroup("ExponentDeepShadowOn", "DeepShadow");
}

void jShadowAppSettingProperties::Teardown(jAppSettingBase* appSetting)
{
	//appSetting->RemoveVariable("EShadowType");
	//appSetting->RemoveVariable("DirectionalLightSilhouette");
	//appSetting->RemoveVariable("PointLightSilhouette");
	//appSetting->RemoveVariable("SpotLightSilhouette");
	appSetting->RemoveVariable("ShadowMapType");
	//appSetting->RemoveVariable("UsePoissonSample");
	//appSetting->RemoveVariable("DirectionalLightMap");
	//appSetting->RemoveVariable("DirectionalInfo");
	//appSetting->RemoveVariable("PointInfo");
	//appSetting->RemoveVariable("SpotLightInfo");
	//appSetting->RemoveVariable("DirecionalLight_Direction");
	//appSetting->RemoveVariable("PointLight_PositionX");
	//appSetting->RemoveVariable("PointLight_PositionY");
	//appSetting->RemoveVariable("PointLight_PositionZ");
	//appSetting->RemoveVariable("SpotLight_PositionX");
	//appSetting->RemoveVariable("SpotLight_PositionY");
	//appSetting->RemoveVariable("SpotLight_PositionZ");
	//appSetting->RemoveVariable("SpotLight_Direction");
	//appSetting->RemoveVariable("Box");
	//appSetting->RemoveVariable("Sphere");
	//appSetting->RemoveVariable("DeepShadowAlpha");
	//appSetting->RemoveVariable("ExponentDeepShadowOn");
}

void jShadowAppSettingProperties::SwitchShadowType(jAppSettingBase* appSetting)
{
	//switch (ShadowType)
	//{
	//case EShadowType::ShadowVolume:
	//	appSetting->SetVisible("Silhouette", 1);
	//	appSetting->SetVisible("UsePoissonSample", 0);
	//	appSetting->SetVisible("DirectionalLightMap", 0);
	//	appSetting->SetVisible("ShadowMapType", 0);
	//	break;
	//case EShadowType::ShadowMap:
	//	appSetting->SetVisible("Silhouette", 0);
	//	appSetting->SetVisible("UsePoissonSample", 1);
	//	appSetting->SetVisible("DirectionalLightMap", 1);
	//	appSetting->SetVisible("ShadowMapType", 1);
	//	break;
	//}
}

void jShadowAppSettingProperties::SwitchShadowMapType(jAppSettingBase* appSetting)
{
	//switch (ShadowMapType)
	//{
	//case EShadowMapType::PCF:
	//case EShadowMapType::PCSS:
	//	appSetting->SetVisible("UsePoissonSample", 1);
	//	break;
	//default:
	//	appSetting->SetVisible("UsePoissonSample", 0);
	//	break;
	//}
}
