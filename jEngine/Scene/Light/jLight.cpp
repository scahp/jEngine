﻿#include "pch.h"
#include "jLight.h"
#include "Math/Vector.h"
#include "RHI/jFrameBufferPool.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"

namespace jLightUtil
{
	void MakeDirectionalLightViewInfo(Vector& outPos, Vector& outTarget, Vector& outUp, const Vector& direction)
	{
		outPos = Vector(-200.0f) * direction;
		outTarget = Vector::ZeroVector;
		outUp = outPos + Vector(0.0f, 1.0f, 0.0f);
	}

	void MakeDirectionalLightViewInfoWithPos(Vector& outTarget, Vector& outUp, const Vector& pos, const Vector& direction)
	{
		outTarget = pos + direction;
		outUp = pos + Vector(0.0f, 1.0f, 0.0f);
	}

	jShadowMapData* CreateShadowMap(const Vector& direction, const Vector&)
	{
		Vector pos, target, up;
		pos = Vector(350.0f, 360.0f, 100.0f);
		//pos = Vector(500.0f, 500.0f, 500.0f);
		//MakeDirectionalLightViewInfoWithPos(target, up, pos, direction);
		MakeDirectionalLightViewInfo(pos, target, up, direction);

		// todo remove constant variable
		auto shadowMapData = new jShadowMapData("DirectionalLight");
		//shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, 3.14f / 4.0f, 300.0f, 900.0f, SM_WIDTH, SM_HEIGHT, true);		// todo for deep shadow map. it should be replaced
		//shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, DegreeToRadian(90.0f), 10.0f, 900.0f, SM_WIDTH, SM_HEIGHT, false);
		const float width = 512;
		const float height = 512;
		const float nearDist = 10.0f;
		const float farDist = 1000.0f;
		shadowMapData->ShadowMapCamera = jOrthographicCamera::CreateCamera(pos, target, up, -width / 2.0f, -height / 2.0f, width / 2.0f, height / 2.0f, nearDist, farDist);

		// FrameBuffer 생성 필요
		// shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, EDepthBufferType::DEPTH32, SM_WIDTH, SM_HEIGHT, 1, ETextureFilter::LINEAR, ETextureFilter::LINEAR });
		shadowMapData->ShadowMapFrameBuffer = jFrameBufferPool::GetFrameBuffer({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, SM_WIDTH, SM_HEIGHT, 1/*, ETextureFilter::LINEAR, ETextureFilter::LINEAR*/ });
		shadowMapData->ShadowMapSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();

		return shadowMapData;
	}

	jShadowMapData* CreateCascadeShadowMap(const Vector& direction, const Vector&)
	{
		Vector pos, target, up;
		pos = Vector(350.0f, 360.0f, 100.0f);
		//pos = Vector(500.0f, 500.0f, 500.0f);
		//MakeDirectionalLightViewInfoWithPos(target, up, pos, direction);
		MakeDirectionalLightViewInfo(pos, target, up, direction);

		// todo remove constant variable
		auto shadowMapData = new jShadowMapData("DirectionalLight");
		//shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, 3.14f / 4.0f, 300.0f, 900.0f, SM_WIDTH, SM_HEIGHT, true);		// todo for deep shadow map. it should be replaced
		//shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, DegreeToRadian(90.0f), 10.0f, 900.0f, SM_WIDTH, SM_HEIGHT, false);
		float width = SM_WIDTH;
		float height = SM_HEIGHT;
		float nearDist = 10.0f;
		float farDist = 1000.0f;
		shadowMapData->ShadowMapCamera = jOrthographicCamera::CreateCamera(pos, target, up, -width / 2.0f, -height / 2.0f, width / 2.0f, height / 2.0f, nearDist, farDist);

		// FrameBuffer 생성 필요
		// shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, EDepthBufferType::DEPTH32, SM_WIDTH, SM_HEIGHT * NUM_CASCADES, 1, ETextureFilter::LINEAR, ETextureFilter::LINEAR });
		shadowMapData->ShadowMapFrameBuffer = jFrameBufferPool::GetFrameBuffer({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, SM_WIDTH, SM_HEIGHT * NUM_CASCADES, 1/*, ETextureFilter::LINEAR, ETextureFilter::LINEAR*/ });
		shadowMapData->ShadowMapSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();

		return shadowMapData;
	}

	jShadowMapArrayData* CreateShadowMapArray(const Vector& pos, const char* prefix = nullptr)
	{
		// todo remove constant variable
		auto shadowMapData = new jShadowMapArrayData(prefix);

		const float nearDist = 10.0f;
		const float farDist = 500.0f;
		shadowMapData->ShadowMapCamera[0] = jCamera::CreateCamera(pos, pos + Vector(1.0f, 0.0f, 0.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT, true);
		shadowMapData->ShadowMapCamera[1] = jCamera::CreateCamera(pos, pos + Vector(-1.0f, 0.0f, 0.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT, true);
		shadowMapData->ShadowMapCamera[2] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 1.0f, 0.0f), pos + Vector(0.0f, 0.0f, -1.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT, true);
		shadowMapData->ShadowMapCamera[3] = jCamera::CreateCamera(pos, pos + Vector(0.0f, -1.0f, 0.0f), pos + Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT, true);
		shadowMapData->ShadowMapCamera[4] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 0.0f, 1.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT, true);
		shadowMapData->ShadowMapCamera[5] = jCamera::CreateCamera(pos, pos + Vector(0.0f, 0.0f, -1.0f), pos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), nearDist, farDist, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT, true);
		// FrameBuffer 생성 필요
		// shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D_ARRAY_OMNISHADOW, ETextureFormat::RG32F, EDepthBufferType::DEPTH32, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT * 6, 1, ETextureFilter::NEAREST, ETextureFilter::NEAREST });
		shadowMapData->ShadowMapFrameBuffer = jFrameBufferPool::GetFrameBuffer({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, SM_ARRAY_WIDTH, SM_ARRAY_HEIGHT * 6, 1/*, ETextureFilter::NEAREST, ETextureFilter::NEAREST*/ });
		shadowMapData->ShadowMapSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();

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
	if (directionalLight->ShadowMapData)
	{
		const auto& renderTargetInfo = directionalLight->ShadowMapData->ShadowMapFrameBuffer->Info;
		directionalLight->Viewports.clear();
		directionalLight->Viewports.emplace_back(jViewport{ 0.0f, 0.0f, static_cast<float>(renderTargetInfo.Width), static_cast<float>(renderTargetInfo.Height) });
	}
	return directionalLight;
}

jCascadeDirectionalLight* jLight::CreateCascadeDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
{
	auto directionalLight = new jCascadeDirectionalLight();
	JASSERT(directionalLight);

	directionalLight->Data.Direction = direction;
	directionalLight->Data.Color = color;
	directionalLight->Data.DiffuseIntensity = diffuseIntensity;
	directionalLight->Data.SpecularIntensity = specularIntensity;
	directionalLight->Data.SpecularPow = specularPower;
	directionalLight->ShadowMapData = jLightUtil::CreateCascadeShadowMap(direction, Vector::ZeroVector);
	if (directionalLight->ShadowMapData)
	{
		const int32 width = directionalLight->ShadowMapData->ShadowMapFrameBuffer->Info.Width;
		const int32 height = directionalLight->ShadowMapData->ShadowMapFrameBuffer->Info.Height / NUM_CASCADES;

		directionalLight->Viewports.reserve(NUM_CASCADES);
		for (int i = 0; i < NUM_CASCADES; ++i)
			directionalLight->Viewports.push_back({ 0.0f, static_cast<float>(height * i), static_cast<float>(width), static_cast<float>(height) });
	}

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
	if (pointLight->ShadowMapData)
	{
		const int32 width = pointLight->ShadowMapData->ShadowMapFrameBuffer->Info.Width;
		const int32 height = pointLight->ShadowMapData->ShadowMapFrameBuffer->Info.Height / 6;

		pointLight->Viewports.reserve(6);
		for (int i = 0; i < 6; ++i)
			pointLight->Viewports.push_back({ 0.0f, static_cast<float>(height * i), static_cast<float>(width), static_cast<float>(height) });
	}
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
	if (spotLight->ShadowMapData)
	{
		const int32 width = spotLight->ShadowMapData->ShadowMapFrameBuffer->Info.Width;
		const int32 height = spotLight->ShadowMapData->ShadowMapFrameBuffer->Info.Height / 6;
		spotLight->Viewports.reserve(6);
		for (int i = 0; i < 6; ++i)
			spotLight->Viewports.push_back({ 0.0f, static_cast<float>(height * i), static_cast<float>(width), static_cast<float>(height) });
	}
	return spotLight;
}

void jLight::BindLights(const std::list<const jLight*>& lights, const jShader* shader)
{
	int ambient = 0;
	int directional = 0;
	int point = 0;
	int spot = 0;
	for (auto light : lights)
	{
		if (!light)
			continue;

		switch (light->Type)
		{
		case ELightType::AMBIENT:
			light->BindLight(shader);
			ambient = 1;
			break;
		case ELightType::DIRECTIONAL:
			light->BindLight(shader);
			++directional;
			break;
		case ELightType::POINT:
			light->BindLight(shader);
			++point;
			break;
		case ELightType::SPOT:
			light->BindLight(shader);
			++spot;
			break;
		default:
			break;
		}
	}

	SET_UNIFORM_BUFFER_STATIC("UseAmbientLight", ambient, shader);
	SET_UNIFORM_BUFFER_STATIC("NumOfDirectionalLight", directional, shader);
	SET_UNIFORM_BUFFER_STATIC("NumOfPointLight", point, shader);
	SET_UNIFORM_BUFFER_STATIC("NumOfSpotLight", spot, shader);
}

//////////////////////////////////////////////////////////////////////////

void jDirectionalLight::BindLight(const jShader* shader) const
{
	JASSERT(shader);

	LightDataUniformBlock->Bind(shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		shadowDataUniformBlock->Bind(shader);
	}
}

void jDirectionalLight::GetMaterialData(jMaterialData* OutMaterialData) const
{
	JASSERT(OutMaterialData);

	if (OutMaterialData)
	{
		OutMaterialData->Params.insert(OutMaterialData->Params.end(), MaterialData.Params.begin(), MaterialData.Params.end());
	}
}

jFrameBuffer* jDirectionalLight::GetShadowMapFrameBuffer() const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapFrameBuffer.get() : nullptr);
}

std::shared_ptr<jFrameBuffer> jDirectionalLight::GetShadowMapFrameBufferPtr() const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapFrameBuffer : nullptr);
}

jCamera* jDirectionalLight::GetLightCamra(int index /*= 0*/) const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapCamera : nullptr);
}

jTexture* jDirectionalLight::GetShadowMap() const
{
	return (ShadowMapData && ShadowMapData->ShadowMapFrameBuffer) ? ShadowMapData->ShadowMapFrameBuffer->GetTexture() : nullptr;
}

void jDirectionalLight::Update(float deltaTime)
{
	if (ShadowMapData && ShadowMapData->ShadowMapCamera)
	{
		auto* camera = ShadowMapData->ShadowMapCamera;
		camera->UpdateCamera();

		jLightUtil::MakeDirectionalLightViewInfo(camera->Pos, camera->Target, camera->Up, Data.Direction);
		Data.ShadowVP = (camera->Projection * camera->View);
		Data.ShadowV = camera->View;

		UpdateMaterialData();
	}
}

void jDirectionalLight::SetupUniformBuffer()
{
	LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		struct ShadowData
		{
			Matrix ShadowVP_Transposed;
			Matrix ShadowV_Transposed;
			Vector LightPos;
			float Near;
			float Far;
			float padding0;
			Vector2 ShadowMapSize;

			bool operator == (const ShadowData& rhs) const
			{
				return (ShadowVP_Transposed == rhs.ShadowVP_Transposed) && (ShadowV_Transposed == rhs.ShadowV_Transposed)
					&& (LightPos == rhs.LightPos) && (Near == rhs.Near) && (Far == rhs.Far) && (ShadowMapSize == rhs.ShadowMapSize);
			}

			bool operator != (const ShadowData& rhs) const
			{
				return !(*this == rhs);
			}

			void SetData(jLightUtil::jShadowMapData* shadowMapData)
			{
				auto camera = shadowMapData->ShadowMapCamera;
				JASSERT(camera);

				const auto& renderTargetInfo = shadowMapData->ShadowMapFrameBuffer->Info;

				ShadowVP_Transposed = (camera->Projection * camera->View);
				ShadowV_Transposed = (camera->View);
				LightPos = camera->Pos;
				Near = camera->Near;
				Far = camera->Far;
				ShadowMapSize.x = static_cast<float>(renderTargetInfo.Width);
				ShadowMapSize.y = static_cast<float>(renderTargetInfo.Height);
			}
		};

		ShadowData shadowData;
		shadowData.SetData(ShadowMapData);

		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		if (shadowDataUniformBlock)
			shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
	}
}

void jDirectionalLight::UpdateMaterialData()
{
	if (!DirtyMaterialData)
		return;
	DirtyMaterialData = false;

	MaterialData.Params.clear();
	{
		MaterialData.AddMaterialParam(jNameStatic("shadow_object"), ShadowMapData->ShadowMapFrameBuffer->GetTextureDepth()
			, ShadowMapData->ShadowMapSamplerState);
	}

	{
		jTexture* texture = nullptr;

		texture = ShadowMapData->ShadowMapFrameBuffer->GetTextureDepth();

		MaterialData.AddMaterialParam(jNameStatic("shadow_object_test"), texture);
	}
}

//////////////////////////////////////////////////////////////////////////
// jCascadeDirectionalLight
void jCascadeDirectionalLight::Update(float deltaTime)
{
	__super::Update(deltaTime);

	if (ShadowMapData)
	{
		for (int i = 0; i < NUM_CASCADES; ++i)
		{
			CascadeLightVP[i].Data = ShadowMapData->CascadeLightVP[i];
			CascadeEndsW[i].Data = ShadowMapData->CascadeEndsW[i];
		}
	}
}

void jCascadeDirectionalLight::BindLight(const jShader* shader) const
{
	__super::BindLight(shader);

	for (int32 i = 0; i < NUM_CASCADES; ++i)
	{
		CascadeEndsW[i].SetUniformbuffer(shader);
		CascadeLightVP[i].SetUniformbuffer(shader);
	}
}

void jPointLight::BindLight(const jShader* shader) const
{
	LightDataUniformBlock->Bind(shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		const IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		shadowDataUniformBlock->Bind(shader);
	}

	for (int i = 0; i < 6; ++i)
	{
		OmniShadowMapVP[i].SetUniformbuffer(shader);
	}
}

void jPointLight::GetMaterialData(jMaterialData* OutMaterialData) const
{
	JASSERT(OutMaterialData);

	if (OutMaterialData)
	{
		OutMaterialData->Params.insert(OutMaterialData->Params.end(), MaterialData.Params.begin(), MaterialData.Params.end());
	}
}

jFrameBuffer* jPointLight::GetShadowMapFrameBuffer() const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapFrameBuffer.get() : nullptr);
}

std::shared_ptr<jFrameBuffer> jPointLight::GetShadowMapFrameBufferPtr() const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapFrameBuffer : nullptr);
}

jCamera* jPointLight::GetLightCamra(int index /*= 0*/) const
{
	JASSERT(ShadowMapData && _countof(ShadowMapData->ShadowMapCamera) <= index);
	return (ShadowMapData ? ShadowMapData->ShadowMapCamera[index] : nullptr);
}

void jPointLight::Update(float deltaTime)
{
	if (ShadowMapData)
	{
		auto camers = ShadowMapData->ShadowMapCamera;
		for (int32 i = 0; i < 6; ++i)
		{
			auto currentCamera = camers[i];
			const auto offset = Data.Position - currentCamera->Pos;
			currentCamera->Pos = Data.Position;
			currentCamera->Target += offset;
			currentCamera->Up += offset;
			currentCamera->UpdateCamera();
		}
		UpdateMaterialData();

		SetupUniformBuffer();
		return;

		for (int i = 0; i < 6; ++i)
		{
			auto camera = ShadowMapData->ShadowMapCamera[i];
			const auto vp = (camera->Projection * camera->View);
			OmniShadowMapVP[i].Data = vp;
		}
	}
}

void jPointLight::SetupUniformBuffer()
{
	LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		struct ShadowData
		{
			float Near;
			float Far;
			Vector2 ShadowMapSize;

			bool operator == (const ShadowData& rhs) const
			{
				return (Near == rhs.Near) && (Far == rhs.Far) && (ShadowMapSize == rhs.ShadowMapSize);
			}

			bool operator != (const ShadowData& rhs) const
			{
				return !(*this == rhs);
			}

			void SetData(jLightUtil::jShadowMapArrayData* shadowMapData)
			{
				auto camera = shadowMapData->ShadowMapCamera[0];
				JASSERT(camera);

				const auto& renderTargetInfo = shadowMapData->ShadowMapFrameBuffer->Info;

				Near = camera->Near;
				Far = camera->Far;
				ShadowMapSize.x = static_cast<float>(renderTargetInfo.Width);
				ShadowMapSize.y = static_cast<float>(renderTargetInfo.Height);
			}
		};

		ShadowData shadowData;
		shadowData.SetData(ShadowMapData);

		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
	}
}

void jPointLight::UpdateMaterialData()
{
	if (!DirtyMaterialData)
		return;
	DirtyMaterialData = false;

	MaterialData.Params.clear();
	{
		MaterialData.AddMaterialParam(jNameStatic("shadow_object_point_shadow"), ShadowMapData->ShadowMapFrameBuffer->GetTextureDepth()
			, ShadowMapData->ShadowMapSamplerState);
	}

	{
		jTexture* texture = nullptr;
		texture = ShadowMapData->ShadowMapFrameBuffer->GetTextureDepth();

		MaterialData.AddMaterialParam(jNameStatic("shadow_object_point"), texture);
	}
}

void jSpotLight::BindLight(const jShader* shader) const
{
	LightDataUniformBlock->Bind(shader);

	if (ShadowMapData && ShadowMapData->IsValid())
	{
		const IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		shadowDataUniformBlock->Bind(shader);
	}

	for (int i = 0; i < 6; ++i)
	{
		OmniShadowMapVP[i].SetUniformbuffer(shader);
	}
}

void jSpotLight::GetMaterialData(jMaterialData* OutMaterialData) const
{
	JASSERT(OutMaterialData);

	if (OutMaterialData)
	{
		OutMaterialData->Params.insert(OutMaterialData->Params.end(), MaterialData.Params.begin(), MaterialData.Params.end());
	}
}

jFrameBuffer* jSpotLight::GetShadowMapFrameBuffer() const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapFrameBuffer.get() : nullptr);
}

std::shared_ptr<jFrameBuffer> jSpotLight::GetShadowMapFrameBufferPtr() const
{
	return (ShadowMapData ? ShadowMapData->ShadowMapFrameBuffer : nullptr);
}

jCamera* jSpotLight::GetLightCamra(int index /*= 0*/) const
{
	JASSERT(ShadowMapData && _countof(ShadowMapData->ShadowMapCamera) <= index);
	return (ShadowMapData ? ShadowMapData->ShadowMapCamera[index] : nullptr);
}

void jSpotLight::Update(float deltaTime)
{
	auto camers = ShadowMapData->ShadowMapCamera;
	for (int32 i = 0; i < 6; ++i)
	{
		auto currentCamera = camers[i];
		const auto offset = Data.Position - currentCamera->Pos;
		currentCamera->Pos = Data.Position;
		currentCamera->Target += offset;
		currentCamera->Up += offset;
		currentCamera->UpdateCamera();
	}
	UpdateMaterialData();

	SetupUniformBuffer();
	return;
}

void jSpotLight::SetupUniformBuffer()
{
	LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
	if (ShadowMapData && ShadowMapData->IsValid())
	{
		// Both near and far are all the same within camera array.
		struct ShadowData
		{
			float Near;
			float Far;
			Vector2 ShadowMapSize;
			// 16 byte aligned don't need a padding

			bool operator == (const ShadowData& rhs) const
			{
				return (Near == rhs.Near) && (Far == rhs.Far) && (ShadowMapSize == rhs.ShadowMapSize);
			}

			bool operator != (const ShadowData& rhs) const
			{
				return !(*this == rhs);
			}

			void SetData(jLightUtil::jShadowMapArrayData* shadowMapData)
			{
				auto camera = shadowMapData->ShadowMapCamera[0];
				JASSERT(camera);

				const auto& renderTargetInfo = shadowMapData->ShadowMapFrameBuffer->Info;

				Near = camera->Near;
				Far = camera->Far;
				ShadowMapSize.x = static_cast<float>(renderTargetInfo.Width);
				ShadowMapSize.y = static_cast<float>(renderTargetInfo.Height);
			}
		};

		ShadowData shadowData;
		shadowData.SetData(ShadowMapData);

		IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
		shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));

		for (int i = 0; i < 6; ++i)
		{
			auto camera = ShadowMapData->ShadowMapCamera[i];
			const auto vp = (camera->Projection * camera->View);
			OmniShadowMapVP[i].Data = vp;
		}
	}
}

void jSpotLight::UpdateMaterialData()
{
	if (!DirtyMaterialData)
		return;
	DirtyMaterialData = false;

	MaterialData.Params.clear();
	{
		MaterialData.AddMaterialParam(jNameStatic("shadow_object_spot_shadow"), ShadowMapData->ShadowMapFrameBuffer->GetTextureDepth()
			, ShadowMapData->ShadowMapSamplerState);
	}

	{
		jTexture* texture = nullptr;
		texture = ShadowMapData->ShadowMapFrameBuffer->GetTextureDepth();

		MaterialData.AddMaterialParam(jNameStatic("shadow_object_spot"), texture);
	}
}
//////////////////////////////////////////////////////////////////////////
void jAmbientLight::BindLight(const jShader* shader) const
{
	//const std::string structName = "AmbientLight";

	SET_UNIFORM_BUFFER_STATIC("AmbientLight.Color", Data.Color, shader);
	SET_UNIFORM_BUFFER_STATIC("AmbientLight.Intensity", Data.Intensity, shader);
}