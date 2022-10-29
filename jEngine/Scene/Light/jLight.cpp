#include "pch.h"
#include "jLight.h"
#include "Math/Vector.h"
#include "RHI/jFrameBufferPool.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/Light/jDirectionalLight.h"

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

	//jShadowMapData* CreateCascadeShadowMap(const Vector& direction, const Vector&)
	//{
	//	Vector pos, target, up;
	//	pos = Vector(350.0f, 360.0f, 100.0f);
	//	//pos = Vector(500.0f, 500.0f, 500.0f);
	//	//MakeDirectionalLightViewInfoWithPos(target, up, pos, direction);
	//	MakeDirectionalLightViewInfo(pos, target, up, direction);

	//	// todo remove constant variable
	//	auto shadowMapData = new jShadowMapData();
	//	//shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, 3.14f / 4.0f, 300.0f, 900.0f, SM_WIDTH, SM_HEIGHT, true);		// todo for deep shadow map. it should be replaced
	//	//shadowMapData->ShadowMapCamera = jCamera::CreateCamera(tempPos, target, up, DegreeToRadian(90.0f), 10.0f, 900.0f, SM_WIDTH, SM_HEIGHT, false);
	//	float width = SM_WIDTH;
	//	float height = SM_HEIGHT;
	//	float nearDist = 10.0f;
	//	float farDist = 1000.0f;
	//	shadowMapData->ShadowMapCamera = jOrthographicCamera::CreateCamera(pos, target, up, -width / 2.0f, -height / 2.0f, width / 2.0f, height / 2.0f, nearDist, farDist);

	//	// FrameBuffer 생성 필요
	//	// shadowMapData->ShadowMapRenderTarget = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, EDepthBufferType::DEPTH32, SM_WIDTH, SM_HEIGHT * NUM_CASCADES, 1, ETextureFilter::LINEAR, ETextureFilter::LINEAR });
	//	//shadowMapData->ShadowMapFrameBuffer = jFrameBufferPool::GetFrameBuffer({ ETextureType::TEXTURE_2D, ETextureFormat::RG32F, SM_WIDTH, SM_HEIGHT * NUM_CASCADES, 1/*, ETextureFilter::LINEAR, ETextureFilter::LINEAR*/ });
	//	//shadowMapData->ShadowMapSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR>::Create();

	//	return shadowMapData;
	//}

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
	directionalLight->Initialize(direction, color, diffuseIntensity, specularIntensity, specularPower);

	return directionalLight;
}

//jCascadeDirectionalLight* jLight::CreateCascadeDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
//{
//	auto directionalLight = new jCascadeDirectionalLight();
//	JASSERT(directionalLight);
//
//	directionalLight->LightData.Direction = direction;
//	directionalLight->LightData.Color = color;
//	directionalLight->LightData.DiffuseIntensity = diffuseIntensity;
//	directionalLight->LightData.SpecularIntensity = specularIntensity;
//	directionalLight->LightData.SpecularPow = specularPower;
//	check(0); // todo
//	//directionalLight->ShadowMapData = jLightUtil::CreateCascadeShadowMap(direction, Vector::ZeroVector);
//
//	return directionalLight;
//}

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
		// todo
		check(0);
		//const int32 width = pointLight->ShadowMapData->ShadowMapFrameBuffer->Info.Width;
		//const int32 height = pointLight->ShadowMapData->ShadowMapFrameBuffer->Info.Height / 6;

		//pointLight->Viewports.reserve(6);
		//for (int i = 0; i < 6; ++i)
		//	pointLight->Viewports.push_back({ 0.0f, static_cast<float>(height * i), static_cast<float>(width), static_cast<float>(height) });
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
		// todo
		check(0);
		//const int32 width = spotLight->ShadowMapData->ShadowMapFrameBuffer->Info.Width;
		//const int32 height = spotLight->ShadowMapData->ShadowMapFrameBuffer->Info.Height / 6;
		//spotLight->Viewports.reserve(6);
		//for (int i = 0; i < 6; ++i)
		//	spotLight->Viewports.push_back({ 0.0f, static_cast<float>(height * i), static_cast<float>(width), static_cast<float>(height) });
	}
	return spotLight;
}

//////////////////////////////////////////////////////////////////////////
// jCascadeDirectionalLight
//void jCascadeDirectionalLight::Update(float deltaTime)
//{
//	__super::Update(deltaTime);
//
//	check(0); // todo
//	//if (ShadowMapData)
//	//{
//	//	for (int i = 0; i < NUM_CASCADES; ++i)
//	//	{
//	//		CascadeLightVP[i].Data = ShadowMapData->CascadeLightVP[i];
//	//		CascadeEndsW[i].Data = ShadowMapData->CascadeEndsW[i];
//	//	}
//	//}
//}

//void jCascadeDirectionalLight::BindLight(const jShader* shader) const
//{
//	__super::BindLight(shader);
//
//	for (int32 i = 0; i < NUM_CASCADES; ++i)
//	{
//		CascadeEndsW[i].SetUniformbuffer(shader);
//		CascadeLightVP[i].SetUniformbuffer(shader);
//	}
//}

//void jPointLight::BindLight(const jShader* shader) const
//{
//	LightDataUniformBlock->Bind(shader);
//
//	if (ShadowMapData && ShadowMapData->IsValid())
//	{
//		const IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//		shadowDataUniformBlock->Bind(shader);
//	}
//
//	for (int i = 0; i < 6; ++i)
//	{
//		OmniShadowMapVP[i].SetUniformbuffer(shader);
//	}
//}

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

		check(0); // todo
		//SetupUniformBuffer();
		return;

		for (int i = 0; i < 6; ++i)
		{
			auto camera = ShadowMapData->ShadowMapCamera[i];
			const auto vp = (camera->Projection * camera->View);
			OmniShadowMapVP[i].Data = vp;
		}
	}
}

//void jPointLight::SetupUniformBuffer()
//{
//    LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
//
//    if (ShadowMapData && ShadowMapData->IsValid())
//    {
//        struct ShadowData
//        {
//            float Near;
//            float Far;
//            Vector2 ShadowMapSize;
//
//            bool operator == (const ShadowData& rhs) const
//            {
//                return (Near == rhs.Near) && (Far == rhs.Far) && (ShadowMapSize == rhs.ShadowMapSize);
//            }
//
//            bool operator != (const ShadowData& rhs) const
//            {
//                return !(*this == rhs);
//            }
//
//            void SetData(jLightUtil::jShadowMapArrayData* shadowMapData)
//            {
//                auto camera = shadowMapData->ShadowMapCamera[0];
//                JASSERT(camera);
//
//				// todo
//				check(0);
//                //const auto& renderTargetInfo = shadowMapData->ShadowMapFrameBuffer->Info;
//
//                //Near = camera->Near;
//                //Far = camera->Far;
//                //ShadowMapSize.x = static_cast<float>(renderTargetInfo.Width);
//                //ShadowMapSize.y = static_cast<float>(renderTargetInfo.Height);
//            }
//        };
//
//        ShadowData shadowData;
//        shadowData.SetData(ShadowMapData);
//
//        IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//        shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
//    }
//}

//void jSpotLight::BindLight(const jShader* shader) const
//{
//	LightDataUniformBlock->Bind(shader);
//
//	if (ShadowMapData && ShadowMapData->IsValid())
//	{
//		const IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//		shadowDataUniformBlock->Bind(shader);
//	}
//
//	for (int i = 0; i < 6; ++i)
//	{
//		OmniShadowMapVP[i].SetUniformbuffer(shader);
//	}
//}

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

	check(0); // todo
	// SetupUniformBuffer();
	return;
}

//void jSpotLight::SetupUniformBuffer()
//{
//    LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
//    if (ShadowMapData && ShadowMapData->IsValid())
//    {
//        // Both near and far are all the same within camera array.
//        struct ShadowData
//        {
//            float Near;
//            float Far;
//            Vector2 ShadowMapSize;
//            // 16 byte aligned don't need a padding
//
//            bool operator == (const ShadowData& rhs) const
//            {
//                return (Near == rhs.Near) && (Far == rhs.Far) && (ShadowMapSize == rhs.ShadowMapSize);
//            }
//
//            bool operator != (const ShadowData& rhs) const
//            {
//                return !(*this == rhs);
//            }
//
//            void SetData(jLightUtil::jShadowMapArrayData* shadowMapData)
//            {
//                auto camera = shadowMapData->ShadowMapCamera[0];
//                JASSERT(camera);
//
//				// todo
//				check(0);
//                //const auto& renderTargetInfo = shadowMapData->ShadowMapFrameBuffer->Info;
//
//                //Near = camera->Near;
//                //Far = camera->Far;
//                //ShadowMapSize.x = static_cast<float>(renderTargetInfo.Width);
//                //ShadowMapSize.y = static_cast<float>(renderTargetInfo.Height);
//            }
//        };
//
//        ShadowData shadowData;
//        shadowData.SetData(ShadowMapData);
//
//        IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//        shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
//
//        for (int i = 0; i < 6; ++i)
//        {
//            auto camera = ShadowMapData->ShadowMapCamera[i];
//            const auto vp = (camera->Projection * camera->View);
//            OmniShadowMapVP[i].Data = vp;
//        }
//    }
//}

//////////////////////////////////////////////////////////////////////////
//void jAmbientLight::BindLight() const
//{
//	check(0); // todo
//	//SET_UNIFORM_BUFFER_STATIC("AmbientLight.Color", Data.Color, shader);
//	//SET_UNIFORM_BUFFER_STATIC("AmbientLight.Intensity", Data.Intensity, shader);
//}
