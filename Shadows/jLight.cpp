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
		//auto tempPos = Vector(100.0f) * direction;
		auto tempPos = Vector(250.0f, 260.0f, 0.0f);
		const auto target = Vector::ZeroVector;
		const auto up = tempPos + Vector(0.0f, 1.0f, 0.0f);

		// todo remove constant variable
		auto shadowMapData = new jShadowMapData("DirectionalLight");
		shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, 3.14f / 4.0f, 1.0f, 900.0f, SM_WIDTH, SM_HEIGHT, true);		// todo for deep shadow map. it should be replaced
		// shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, DegreeToRadian(90.0f), 1.0f, 900.0f, 100.0f, 100.0f, false);
		shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, SM_WIDTH, SM_HEIGHT, 1 });

		return shadowMapData;
	}

	jShadowMapArrayData* CreateShadowMapArray(const Vector& pos, const char* prefix = nullptr)
	{
		// todo remove constant variable
		auto shadowMapData = new jShadowMapArrayData(prefix);

		const float nearDist = 10.0f;
		const float farDist = 500.0f;
		shadowMapData->ShadowMapCamera[0] = jCamera::CreateCamera(pos, pos + Vector(1.0f, 0.0f, 0.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_WIDTH, SM_HEIGHT, true);
		shadowMapData->ShadowMapCamera[1] = jCamera::CreateCamera(pos, pos + Vector(-1.0f, 0.0f, 0.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_WIDTH, SM_HEIGHT, true);
		shadowMapData->ShadowMapCamera[2] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 1.0f, 0.0f), pos + Vector(0.0f, 0.0f, -1.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_WIDTH, SM_HEIGHT, true);
		shadowMapData->ShadowMapCamera[3] = jCamera::CreateCamera(pos, pos + Vector(0.0f, -1.0f, 0.0f), pos + Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_WIDTH, SM_HEIGHT, true);
		shadowMapData->ShadowMapCamera[4] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 0.0f, 1.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_WIDTH, SM_HEIGHT, true);
		shadowMapData->ShadowMapCamera[5] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 0.0f, -1.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_WIDTH, SM_HEIGHT, true);
		shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D_ARRAY_OMNISHADOW, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, SM_WIDTH, SM_HEIGHT, 6 });

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
jDirectionalLight* jLight::CreateDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
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

jPointLight* jLight::CreatePointLight(const Vector& pos, const Vector& color, float maxDistance, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
{
	auto pointLight = new jPointLight();
	pointLight->Data.Position = pos;
	pointLight->Data.Color = color;
	pointLight->Data.DiffuseIntensity = diffuseIntensity;
	pointLight->Data.SpecularIntensity = specularIntensity;
	pointLight->Data.SpecularPow = specularPower;
	pointLight->Data.MaxDistance = maxDistance;
	pointLight->ShadowMapData = jLightUtil::CreateShadowMapArray(pos, "PointLight");

	return pointLight;
}

jSpotLight* jLight::CreateSpotLight(const Vector& pos, const Vector& direction, const Vector& color, float maxDistance
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
	spotLight->ShadowMapData = jLightUtil::CreateShadowMapArray(pos, "SpotLight");

	return spotLight;
}

//////////////////////////////////////////////////////////////////////////

void jDirectionalLight::BindLight(const jShader* shader, jMaterialData* materialData, int32 index /*= 0*/) const
{
	JASSERT(shader);
	JASSERT(materialData);

	//if (!LightDataUniformBlock->Data || *static_cast<LightData*>(LightDataUniformBlock->Data) != Data)
		LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
	LightDataUniformBlock->Bind(shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		struct ShadowData
		{
			Matrix ShadowVP_Transposed;
			Matrix ShadowV_Transposed;
			Vector LightPos;
			float Near;
			float Far;
			Vector padding0;

			bool operator == (const ShadowData& rhs) const
			{
				return (ShadowVP_Transposed == rhs.ShadowVP_Transposed) && (ShadowV_Transposed == rhs.ShadowV_Transposed) 
					&& (LightPos == rhs.LightPos) && (Near == rhs.Near) && (Far == rhs.Far);
			}

			bool operator != (const ShadowData& rhs) const
			{
				return !(*this == rhs);
			}

			void SetData(jCamera* camera)
			{
				JASSERT(camera);

				ShadowVP_Transposed = (camera->Projection * camera->View).GetTranspose();
				ShadowV_Transposed = (camera->View).GetTranspose();
				LightPos = camera->Pos;
				Near = camera->Near;
				Far = camera->Far;
			}
		};

		ShadowData shadowData;
		shadowData.SetData(ShadowMapData->ShadowMapCamera);

		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		//if (!shadowDataUniformBlock->Data || *static_cast<ShadowData*>(shadowDataUniformBlock->Data) != shadowData)
			shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
		shadowDataUniformBlock->Bind(shader);

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

void jPointLight::BindLight(const jShader* shader, jMaterialData* materialData, int32 index /*= 0*/) const
{
	//if (!LightDataUniformBlock->Data || *static_cast<LightData*>(LightDataUniformBlock->Data) != Data)
		LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
	LightDataUniformBlock->Bind(shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		// Both near and far are all the same within camera array.
		struct ShadowData
		{
			float Near;
			float Far;
			Vector2 padding0;

			bool operator == (const ShadowData& rhs) const
			{
				return (Near == rhs.Near) && (Far == rhs.Far);
			}

			bool operator != (const ShadowData& rhs) const
			{
				return !(*this == rhs);
			}

			void SetData(jCamera* camera)
			{
				JASSERT(camera);

				Near = camera->Near;
				Far = camera->Far;
			}
		};

		ShadowData shadowData;
		shadowData.SetData(ShadowMapData->ShadowMapCamera[0]);

		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		//if (!shadowDataUniformBlock->Data || *static_cast<ShadowData*>(shadowDataUniformBlock->Data) != shadowData)
			shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
		shadowDataUniformBlock->Bind(shader);

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

void jSpotLight::BindLight(const jShader* shader, jMaterialData* materialData, int32 index /*= 0*/) const
{
	//if (!LightDataUniformBlock->Data || *static_cast<LightData*>(LightDataUniformBlock->Data) != Data)
		LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
	LightDataUniformBlock->Bind(shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		// Both near and far are all the same within camera array.
		struct ShadowData
		{
			float Near;
			float Far;
			Vector2 padding0;

			bool operator == (const ShadowData& rhs) const
			{
				return (Near == rhs.Near) && (Far == rhs.Far);
			}

			bool operator != (const ShadowData& rhs) const
			{
				return !(*this == rhs);
			}

			void SetData(jCamera* camera)
			{
				JASSERT(camera);

				Near = camera->Near;
				Far = camera->Far;
			}
		};

		ShadowData shadowData;
		shadowData.SetData(ShadowMapData->ShadowMapCamera[0]);

		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		//if (!shadowDataUniformBlock->Data || *static_cast<ShadowData*>(shadowDataUniformBlock->Data) != shadowData)
			shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
		shadowDataUniformBlock->Bind(shader);

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
void jAmbientLight::BindLight(const jShader* shader, jMaterialData* materialData, int32 index /*= 0*/) const
{
	const std::string structName = "AmbientLight";

	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + ".Color", Data.Color), shader);
	g_rhi->SetUniformbuffer(&jUniformBuffer<Vector>(structName + ".Intensity", Data.Intensity), shader);
}
