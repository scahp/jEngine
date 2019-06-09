#include "pch.h"
#include "jLight.h"
#include "jRHI.h"
#include "Math\Vector.h"
#include "jRenderTargetPool.h"
#include "jCamera.h"
#include "jRHI_OpenGL.h"

namespace jLightUtil
{
	jShadowMapData* CreateShadowMap(const Vector& direction, const Vector& pos)
	{
		auto tempPos = Vector(100.0f);
		const auto target = tempPos + direction;
		const auto up = tempPos + Vector(0.0f, 1.0f, 0.0f);

		// todo remove constant variable
		auto shadowMapData = new jShadowMapData();
		shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, DegreeToRadian(45.0f), 10.0f, 900.0f, 200.0f, 200.0f, false);
		shadowMapData->ShadowMapRenderTarget 
= jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, 1024, 1024 });

		return shadowMapData;
	}

	jShadowMapArrayData* CreateShadowMapArray(const Vector& pos)
	{
		// todo remove constant variable
		auto shadowMapData = new jShadowMapArrayData();

		const float nearDist = 10.0f;
		const float farDist = 500.0f;
		shadowMapData->ShadowMapCamera[0] = jCamera::CreateCamera(pos, pos + Vector(1.0f, 0.0f, 0.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(45.0f), nearDist, farDist, 1024, 1024, true);
		shadowMapData->ShadowMapCamera[1] = jCamera::CreateCamera(pos, pos + Vector(-1.0f, 0.0f, 0.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(45.0f), nearDist, farDist, 1024, 1024, true);
		shadowMapData->ShadowMapCamera[2] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 1.0f, 0.0f), pos + Vector(0.0f, 0.0f, -1.0f), DegreeToRadian(45.0f), nearDist, farDist, 1024, 1024, true);
		shadowMapData->ShadowMapCamera[3] = jCamera::CreateCamera(pos, pos + Vector(0.0f, -1.0f, 0.0f), pos + Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(45.0f), nearDist, farDist, 1024, 1024, true);
		shadowMapData->ShadowMapCamera[4] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 0.0f, 1.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(45.0f), nearDist, farDist, 1024, 1024, true);
		shadowMapData->ShadowMapCamera[5] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 0.0f, -1.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(45.0f), nearDist, farDist, 1024, 1024, true);
		shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D_ARRAY_OMNISHADOW, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, 1024, 1024 });

		return shadowMapData;
	}

}

jLight* jLight::CreateAmbientLight(const Vector& color, const Vector& intensity)
{
	auto ambientLight = new jAmbientLight();
	JASSERT(ambientLight);
	
	ambientLight->Data.Color = color;
	ambientLight->Data.Intensity = intensity;
	
	return ambientLight;
}

//////////////////////////////////////////////////////////////////////////
jDirectionalLight* jLight::CreateDirectionalLight(const Vector& direction, const Vector4& color, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
{
	auto directionalLight = new jDirectionalLight();
	JASSERT(directionalLight);

	directionalLight->Data.Direction = direction;
	directionalLight->Data.Color = color;
	directionalLight->Data.DiffuseIntensity = diffuseIntensity;
	directionalLight->Data.SpecularIntensity = specularIntensity;
	directionalLight->Data.SpecularPow = specularPower;
	directionalLight->ShadowMapData = jLightUtil::CreateShadowMap(direction, Vector::ZeroVector);

	return directionalLight;
}

jPointLight* jLight::CreatePointLight(const Vector& pos, const Vector4& color, float maxDistance, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
{
	auto pointLight = new jPointLight();
	pointLight->Data.Position = pos;
	pointLight->Data.Color = color;
	pointLight->Data.DiffuseIntensity = diffuseIntensity;
	pointLight->Data.SpecularIntensity = specularIntensity;
	pointLight->Data.SpecularPow = specularPower;
	pointLight->Data.MaxDistance = maxDistance;
	pointLight->ShadowMapData = jLightUtil::CreateShadowMapArray(pos);

	return pointLight;
}

jSpotLight* jLight::CreateSpotLight(const Vector& pos, const Vector& direction, const Vector4& color, float maxDistance
	, float penumbraRadian, float umbraRadian, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
{
	auto spotLight = new jSpotLight();

	spotLight->Data.Position = pos;
	spotLight->Data.Direction = direction;
	spotLight->Data.Color = color;
	spotLight->Data.DiffuseIntensity = diffuseIntensity;
	spotLight->Data.SpecularIntensity = specularIntensity;
	spotLight->Data.SpecularPow = specularPower;
	spotLight->Data.MaxDistance = maxDistance;
	spotLight->Data.PenumbraRadian = penumbraRadian;
	spotLight->Data.UmbraRadian = umbraRadian;
	spotLight->ShadowMapData = jLightUtil::CreateShadowMapArray(pos);

	return spotLight;
}

//////////////////////////////////////////////////////////////////////////

void jDirectionalLight::BindLight(jShader* shader, jMaterialData* materialData, int32 index)
{
	JASSERT(shader);
	JASSERT(materialData);

	char szTemp[128] = {0,};
	sprintf_s(szTemp, sizeof(szTemp), "DirectionalLight[%d].", index);
	const std::string structName = szTemp;

	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "LightDirection", Data.Direction), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "Color", Data.Color), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "DiffuseLightIntensity", Data.DiffuseIntensity), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "SpecularLightIntensity", Data.SpecularIntensity), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "SpecularPow", Data.SpecularPow), shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		g_rhi->SetUniformbuffer(&jUniformBuffer<float>("LightZNear", ShadowMapData->ShadowMapCamera->Near), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<float>("LightZFar", ShadowMapData->ShadowMapCamera->Far), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<Matrix>("ShadowVP", ShadowMapData->ShadowMapCamera->Projection * ShadowMapData->ShadowMapCamera->View), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<Matrix>("ShadowV", ShadowMapData->ShadowMapCamera->View), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>("LightPos", ShadowMapData->ShadowMapCamera->Pos), shader);

		if (materialData)
		{
			auto materialParam = new jMaterialParam_OpenGL();
			materialParam->Name = "shadow_object";
			materialParam->Texture = static_cast<jTexture_OpenGL*>(ShadowMapData->ShadowMapRenderTarget->GetTexture());
			materialParam->Minification = ETextureFilter::LINEAR;
			materialParam->Magnification = ETextureFilter::LINEAR;
			materialData->Params.push_back(materialParam);
		}
	}
}

jTexture* jDirectionalLight::GetShadowMap() const
{
	return (ShadowMapData && ShadowMapData->ShadowMapRenderTarget) ? ShadowMapData->ShadowMapRenderTarget->GetTexture() : nullptr;
}

void jPointLight::BindLight(jShader* shader, jMaterialData* materialData, int32 index)
{
	char szTemp[128] = { 0, };
	sprintf_s(szTemp, sizeof(szTemp), "PointLight[%d].", index);
	const std::string structName = szTemp;

	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "LightPos", Data.Position), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "Color", Data.Color), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "DiffuseLightIntensity", Data.DiffuseIntensity), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "SpecularLightIntensity", Data.SpecularIntensity), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "SpecularPow", Data.SpecularPow), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "MaxDistance", Data.MaxDistance), shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		// Both near and far are all same within camera array.
		g_rhi->SetUniformbuffer(&jUniformBuffer<float>("PointLightZNear", ShadowMapData->ShadowMapCamera[0]->Near), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<float>("PointLightZFar", ShadowMapData->ShadowMapCamera[0]->Far), shader);

		if (materialData)
		{
			auto materialParam = new jMaterialParam_OpenGL();
			materialParam->Name = "shadow_object_point_array";
			materialParam->Texture = static_cast<jTexture_OpenGL*>(ShadowMapData->ShadowMapRenderTarget->GetTexture());
			materialParam->Minification = ETextureFilter::LINEAR;
			materialParam->Magnification = ETextureFilter::LINEAR;
			materialData->Params.push_back(materialParam);
		}
	}
}

void jSpotLight::BindLight(jShader* shader, jMaterialData* materialData, int32 index)
{
	char szTemp[128] = { 0, };
	sprintf_s(szTemp, sizeof(szTemp), "SpotLight[%d].", index);
	const std::string structName = szTemp;

	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "LightPos", Data.Position), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "Direction", Data.Direction), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "Color", Data.Color), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "DiffuseLightIntensity", Data.DiffuseIntensity), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + "SpecularLightIntensity", Data.SpecularIntensity), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "SpecularPow", Data.SpecularPow), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "MaxDistance", Data.MaxDistance), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "PenumbraRadian", Data.PenumbraRadian), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<float>(structName + "UmbraRadian", Data.UmbraRadian), shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		// Both near and far are all same within camera array.
		g_rhi->SetUniformbuffer(&jUniformBuffer<float>("SpotLightZNear", ShadowMapData->ShadowMapCamera[0]->Near), shader);
		g_rhi->SetUniformbuffer(&jUniformBuffer<float>("SpotLightZFar", ShadowMapData->ShadowMapCamera[0]->Far), shader);

		if (materialData)
		{
			auto materialParam = new jMaterialParam_OpenGL();
			materialParam->Name = "shadow_object_spot_array";
			materialParam->Texture = static_cast<jTexture_OpenGL*>(ShadowMapData->ShadowMapRenderTarget->GetTexture());
			materialParam->Minification = ETextureFilter::LINEAR;
			materialParam->Magnification = ETextureFilter::LINEAR;
			materialData->Params.push_back(materialParam);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void jAmbientLight::BindLight(jShader* shader, jMaterialData* materialData, int32 index)
{
	const std::string structName = "AmbientLight";

	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + ".Color", Data.Color), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + ".Intensity", Data.Intensity), shader);
}
