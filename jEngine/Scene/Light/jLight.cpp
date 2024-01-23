#include "pch.h"
#include "jLight.h"
#include "Math/Vector.h"
#include "RHI/jFrameBufferPool.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "jDirectionalLight.h"
#include "jPointLight.h"
#include "jSpotLight.h"

std::vector<jLight*> jLight::s_Lights;

namespace jLightUtil
{
	void MakeDirectionalLightViewInfo(Vector& outPos, Vector& outTarget, Vector& outUp, const Vector& direction)
	{
		outPos = Vector(-jDirectionalLight::SM_PosDist) * direction;
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

jLight::~jLight()
{
	delete LightDebugObject;
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
	pointLight->Initialize(pos, color, maxDistance, diffuseIntensity, specularIntensity, specularPower);
	return pointLight;
}

jSpotLight* jLight::CreateSpotLight(const Vector& pos, const Vector& direction, const Vector& color, float maxDistance
	, float penumbraRadian, float umbraRadian, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower)
{
	auto spotLight = new jSpotLight();
	spotLight->Initialize(pos, direction, color, maxDistance, penumbraRadian, umbraRadian, diffuseIntensity, specularIntensity, specularPower);
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

//////////////////////////////////////////////////////////////////////////
//void jAmbientLight::BindLight() const
//{
//	check(0); // todo
//	//SET_UNIFORM_BUFFER_STATIC("AmbientLight.Color", Data.Color, shader);
//	//SET_UNIFORM_BUFFER_STATIC("AmbientLight.Intensity", Data.Intensity, shader);
//}
