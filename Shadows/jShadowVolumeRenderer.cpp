#include "pch.h"
#include "jShadowVolumeRenderer.h"
#include "jRHI.h"
#include "jObject.h"
#include "jCamera.h"
#include "jLight.h"
#include "jShadowVolume.h"
#include "jShadowAppProperties.h"
#include "jRenderObject.h"
#include "Math/Vector.h"


jShadowVolumeRenderer::jShadowVolumeRenderer()
{
}


jShadowVolumeRenderer::~jShadowVolumeRenderer()
{
}

bool jShadowVolumeRenderer::CanSkipShadowObject(const jCamera* camera, const jObject* object, const Vector* lightPos, const Vector* lightDir, const jLight* light) const
{
	if (!object->ShadowVolume || object->SkipUpdateShadowVolume)
		return true;

	const float radius = object->RenderObject->Scale.x;
	//var radius = 0.0;
	//if (obj.hasOwnProperty('radius'))
	//	radius = obj.radius;
	//else
	//	radius = obj.scale.x;

	if (lightDir)       // Directional light
	{
		// 1. check direction against frustum
		if (!camera->IsInFrustumWithDirection(object->RenderObject->Pos, *lightDir, radius))
			return true;
	}
	else if (lightPos)  // Sphere or Spot Light
	{
		float maxDistance = 0.0f;
		if (light->Type == ELightType::POINT)
			maxDistance = static_cast<const jPointLight*>(light)->Data.MaxDistance;
		else if (light->Type == ELightType::SPOT)
			maxDistance = static_cast<const jSpotLight*>(light)->Data.MaxDistance;

		// 1. check out of light radius with obj
		const auto isCasterOutOfLightRadius = ((*lightPos - object->RenderObject->Pos).Length() > maxDistance);
		if (isCasterOutOfLightRadius)
			return true;

		// 2. check direction against frustum
		if (!camera->IsInFrustumWithDirection(object->RenderObject->Pos, object->RenderObject->Pos - *lightPos, radius))
			return true;

		// 3. check Spot light range with obj
		if (light->Type == ELightType::SPOT)
		{
			const auto lightToObjVector = object->RenderObject->Pos - *lightPos;
			const auto radianOfRadiusOffset = atanf(radius / lightToObjVector.Length());

			auto spotLight = static_cast<const jSpotLight*>(light);
			const auto radian = lightToObjVector.GetNormalize().DotProduct(spotLight->Data.Direction);
			const auto limitRadian = cosf(Max(spotLight->Data.UmbraRadian, spotLight->Data.PenumbraRadian)) - radianOfRadiusOffset;
			if (limitRadian > radian)
				return true;
		}
	}
	return false;
}

void jShadowVolumeRenderer::RenderPass(const jCamera* camera)
{
	//const ambientPipeLineHashCode = LoadPipeline(CreateBaseShadowVolumeAmbientOnlyShaderFile()).hashCode;
	//const defaultPipeLineHashCode = LoadPipeline(CreateBaseShadowVolumeShaderFile()).hashCode;

	jShaderInfo info;

	jShader* ambientShader = nullptr;
	jShader* shadowVolumeBaseShader = nullptr;

	info.vs = "shaders/shadowvolume/vs.glsl";
	info.fs = "shaders/shadowvolume/fs_ambientonly.glsl";
	ambientShader = jShader::CreateShader(info);

	info.vs = "shaders/shadowvolume/vs.glsl";
	info.fs = "shaders/shadowvolume/fs.glsl";
	shadowVolumeBaseShader = jShader::CreateShader(info);

	info.vs = "Shaders/shadowvolume/vs_infinityFar.glsl";
	info.fs = "Shaders/shadowvolume/fs_infinityFar.glsl";
	auto ShadowVolumeInfinityFarShader = jShader::CreateShader(info);

	//////////////////////////////////////////////////////////////////
	// 1. Render objects to depth buffer and Ambient & Emissive to color buffer.
	g_rhi->EnableBlend(true);
	g_rhi->SetBlendFunc(EBlendSrc::ONE, EBlendDest::ZERO);

	g_rhi->EnableDepthTest(true);

	const_cast<jCamera*>(camera)->IsEnableCullMode = true;		// todo remove
	g_rhi->SetClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH, ERenderBufferType::STENCIL }));

	g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);
	g_rhi->EnableStencil(false);

	g_rhi->SetDepthMask(true);
	g_rhi->SetColorMask(true, true, true, true);

	const_cast<jCamera*>(camera)->UseAmbient = true;			// todo remove
	for (auto& iter : g_StaticObjectArray)
		iter->Draw(camera, ambientShader, camera->Ambient);

	//////////////////////////////////////////////////////////////////
	// 2. Stencil volume update & rendering (z-fail)
	const auto numOfLights = camera->LightList.size();
	const_cast<jCamera*>(camera)->UseAmbient = false;			// todo remove
	g_rhi->EnableStencil(true);
	for (auto i = 0; i < numOfLights; ++i)
	{
		Vector* lightDirection = nullptr;
		Vector* lightPos = nullptr;

		bool skip = false;
		auto light = camera->LightList[i];
		switch (light->Type)
		{
		case ELightType::DIRECTIONAL:
		{
			auto directionalLight = static_cast<jDirectionalLight*>(light);
			if (directionalLight)
				lightDirection = &directionalLight->Data.Direction;
			break;
		}
		case ELightType::POINT:
		{
			auto pointLight = static_cast<jPointLight*>(light);
			if (pointLight)
				lightPos = &pointLight->Data.Position;
			break;
		}
		case ELightType::SPOT:
		{
			auto spotLight = static_cast<jSpotLight*>(light);
			if (spotLight)
				lightPos = &spotLight->Data.Position;
			break;
		}
		default:
			skip = true;
			break;
		}
		if (skip)
			continue;

		g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::STENCIL }));
		g_rhi->SetStencilOpSeparate(EFace::FRONT, EStencilOp::KEEP, EStencilOp::DECR_WRAP, EStencilOp::KEEP);
		g_rhi->SetStencilOpSeparate(EFace::BACK, EStencilOp::KEEP, EStencilOp::INCR_WRAP, EStencilOp::KEEP);

		g_rhi->SetStencilFunc(EDepthStencilFunc::ALWAYS, 0, 0xff);
		g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);
		g_rhi->SetDepthMask(false);
		g_rhi->SetColorMask(false, false, false, false);

		const_cast<jCamera*>(camera)->IsEnableCullMode = false;			// todo remove

		{
			// todo
			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(0.0f, 100.0f);

			for (auto& iter : g_StaticObjectArray)
			{
				if (CanSkipShadowObject(camera, iter, lightPos, lightDirection, light))
					continue;
				
				iter->ShadowVolume->Update(lightDirection, lightPos, iter);
				iter->ShadowVolume->QuadObject->Draw(camera, ShadowVolumeInfinityFarShader, light);
			}

			// todo
			// disable polygon offset fill
			glDisable(GL_POLYGON_OFFSET_FILL);
		}

		//////////////////////////////////////////////////////////////////
		// 3. Final light(Directional, Point, Spot) rendering.
		g_rhi->SetStencilFunc(EDepthStencilFunc::EQUAL, 0, 0xff);
		g_rhi->SetStencilOpSeparate(EFace::FRONT, EStencilOp::KEEP, EStencilOp::KEEP, EStencilOp::KEEP);
		g_rhi->SetStencilOpSeparate(EFace::BACK, EStencilOp::KEEP, EStencilOp::KEEP, EStencilOp::KEEP);

		g_rhi->SetDepthMask(false);
		g_rhi->SetColorMask(true, true, true, true);
		const_cast<jCamera*>(camera)->IsEnableCullMode = true;			// todo remove

		g_rhi->SetDepthFunc(EDepthStencilFunc::EQUAL);
		g_rhi->SetBlendFunc(EBlendSrc::ONE, EBlendDest::ONE);

		for (auto& iter : g_StaticObjectArray)
			iter->Draw(camera, shadowVolumeBaseShader, light);
	}
	const_cast<jCamera*>(camera)->UseAmbient = true;			// todo remove

	g_rhi->EnableBlend(true);
	g_rhi->SetBlendFunc(EBlendSrc::SRC_ALPHA, EBlendDest::ONE_MINUS_SRC_ALPHA);
	g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);
	g_rhi->SetDepthMask(true);
	g_rhi->SetColorMask(true, true, true, true);
	g_rhi->EnableStencil(false);
}

void jShadowVolumeRenderer::DebugRenderPass(const jCamera* camera)
{
	const_cast<jCamera*>(camera)->IsEnableCullMode = false;		// todo remove
	g_rhi->EnableBlend(true);
	g_rhi->SetBlendFunc(EBlendSrc::SRC_ALPHA, EBlendDest::ONE_MINUS_SRC_ALPHA);

	for (auto& light : camera->LightList)
	{
		Vector* lightDirection = nullptr;
		Vector* lightPos = nullptr;

		bool skip = false;
		switch (light->Type)
		{
		case ELightType::DIRECTIONAL:
			if (!ShowSilhouette_DirectionalLight)
				skip = true;

			lightDirection = &static_cast<jDirectionalLight*>(light)->Data.Direction;
			break;
		case ELightType::POINT:
			if (!ShowSilhouette_PointLight)
				skip = true;

			lightPos = &static_cast<jPointLight*>(light)->Data.Position;
			break;
		case ELightType::SPOT:
			if (!ShowSilhouette_SpotLight)
				skip = true;

			lightPos = &static_cast<jSpotLight*>(light)->Data.Position;
			break;
		default:
			skip = true;
			break;
		}
		if (skip)
			continue;

		for (auto& iter : g_StaticObjectArray)
		{
			if (CanSkipShadowObject(camera, iter, lightPos, lightDirection, light))
				continue;

			//iter->ShadowVolume->Update(&DirectionalLight->Data.Direction, nullptr, iter);

			iter->ShadowVolume->Update(lightDirection, lightPos, iter);
			if (iter->ShadowVolume->EdgeObject)
				iter->ShadowVolume->EdgeObject->Draw(camera, ShadowVolumeInfinityFarShader, light);
			if (iter->ShadowVolume->QuadObject)
				iter->ShadowVolume->QuadObject->Draw(camera, ShadowVolumeInfinityFarShader, light);
		}
	}

	static jShader* texShader = nullptr;
	if (!texShader)
	{
		jShaderInfo info;
		info.vs = "Shaders/tex_vs.glsl";
		info.fs = "Shaders/tex_fs.glsl";
		texShader = jShader::CreateShader(info);
	}

	const auto ShowDirectionalLightInfo = jShadowAppSettingProperties::GetInstance().ShowDirectionalLightInfo;
	const auto ShowPointLightInfo = jShadowAppSettingProperties::GetInstance().ShowPointLightInfo;
	const auto ShowSpotLightInfo = jShadowAppSettingProperties::GetInstance().ShowSpotLightInfo;

	for (auto& iter : camera->LightList)
	{
		if (iter->LightDebugObject)
		{
			if (iter->Type == ELightType::DIRECTIONAL && !ShowDirectionalLightInfo)
				continue;
			if (iter->Type == ELightType::POINT && !ShowPointLightInfo)
				continue;
			if (iter->Type == ELightType::SPOT && !ShowSpotLightInfo)
				continue;

			iter->LightDebugObject->Draw(camera, texShader, nullptr);
		}
	}

	g_rhi->EnableBlend(false);
	const_cast<jCamera*>(camera)->IsEnableCullMode = false;			// todo remove

	__super::DebugRenderPass(camera);
}

void jShadowVolumeRenderer::UpdateSettings()
{
	ShowSilhouette_DirectionalLight = jShadowAppSettingProperties::GetInstance().ShowSilhouette_DirectionalLight;
	ShowSilhouette_PointLight = jShadowAppSettingProperties::GetInstance().ShowSilhouette_PointLight;
	ShowSilhouette_SpotLight = jShadowAppSettingProperties::GetInstance().ShowSilhouette_SpotLight;
}

void jShadowVolumeRenderer::Setup()
{
	ShadowVolumeInfinityFarShaderInfo.vs = "Shaders/shadowvolume/vs_infinityFar.glsl";
	ShadowVolumeInfinityFarShaderInfo.fs = "Shaders/shadowvolume/fs_infinityFar.glsl";
	ShadowVolumeInfinityFarShader = jShader::CreateShader(ShadowVolumeInfinityFarShaderInfo);
}
