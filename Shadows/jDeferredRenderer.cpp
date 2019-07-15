#include "pch.h"
#include "jDeferredRenderer.h"
#include "jObject.h"
#include "jShadowAppProperties.h"
#include "jRenderTargetPool.h"

#include "jRHI_OpenGL.h"		// todo it should be removed.
#include "jCamera.h"
#include "jLight.h"

//////////////////////////////////////////////////////////////////////////
// jDeferredRenderer
jDeferredRenderer::jDeferredRenderer(const jRenderTargetInfo& geometryBufferInfo)
{
	GeometryBufferInfo = geometryBufferInfo;
}

jDeferredRenderer::~jDeferredRenderer()
{
}

void jDeferredRenderer::Setup()
{
	GBuffer.GeometryBuffer = jRenderTargetPool::GetRenderTarget(GeometryBufferInfo);

	DeferredDeepShadowMapPipelineSet = new jDeferredDeepShadowMapPipelineSet(&GBuffer);
	DeferredDeepShadowMapPipelineSet->Setup();

	SetChangePipelineSet(DeferredDeepShadowMapPipelineSet);

	//////////////////////////////////////////////////////////////////////////
	// Setup a postprocess chain
	auto tempRenderTarget = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RGBA, EFormat::RGBA, EFormatType::FLOAT, SCR_WIDTH, SCR_HEIGHT, 1 }));
	{
		// this postprocess have to have DeepShadowMap PipelineSet.
		JASSERT(PipelineSet->GetType() == EPipelineSetType::DeepShadowMap);
		auto deferredDeepShadowMapPipelineSet = static_cast<jDeferredDeepShadowMapPipelineSet*>(PipelineSet);
		auto& DeepShadowMapBuffers = deferredDeepShadowMapPipelineSet->DeepShadowMapBuffers;

		// LightPass of DeepShadowMap
		auto postprocess = new jPostProcess_DeepShadowMap("DeepShadow", tempRenderTarget, { DeepShadowMapBuffers.StartElementBuf, DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext, DeepShadowMapBuffers.LinkedListEntryNeighbors }, &GBuffer);
		PostProcessChain.AddNewPostprocess(postprocess);
	}

	{
		auto postprocess = new jPostProcess_AA_DeepShadowAddition("AA_DeepShadowAddition", nullptr);
		PostProcessChain.AddNewPostprocess(postprocess);
	}
}

void jDeferredRenderer::Teardown()
{
	
}

void jDeferredRenderer::ShadowPrePass(const jCamera* camera)
{
	const auto directionalLight = camera->GetLight(ELightType::DIRECTIONAL);
	std::list<const jLight*> lights;
	if (directionalLight)
		lights.push_back(directionalLight);

	// todo Directional Light 만 쓸건가?
	const jPipelineData data(jObject::GetShadowCasterObject(), camera, lights);

	for (auto& iter : PipelineSet->ShadowPrePass)
		iter->Do(data);
}

void jDeferredRenderer::RenderPass(const jCamera* camera)
{
	const auto directionalLight = camera->GetLight(ELightType::DIRECTIONAL);

	std::list<const jLight*> lights;
	if (directionalLight)
		lights.push_back(directionalLight);

	// Geometry Pass
	// todo Directional Light 만 쓸건가?
	const jPipelineData data(jObject::GetStaticObject(), camera, lights);

	for (auto& iter : PipelineSet->RenderPass)
		iter->Do(data);
}

void jDeferredRenderer::DebugRenderPass(const jCamera* camera)
{
	if (GBuffer.Begin())
	{
		const jPipelineData data(jObject::GetDebugObject(), camera, {});
		for (auto& iter : PipelineSet->DebugRenderPass)
			iter->Do(data);
		GBuffer.End();
	}
}

void jDeferredRenderer::BoundVolumeRenderPass(const jCamera* camera)
{
	if (GBuffer.Begin())
	{
		if (jShadowAppSettingProperties::GetInstance().ShowBoundBox)
		{
			const jPipelineData data(jObject::GetBoundBoxObject(), camera, {});
			for (auto& iter : PipelineSet->BoundVolumeRenderPass)
				iter->Do(data);
		}

		if (jShadowAppSettingProperties::GetInstance().ShowBoundSphere)
		{
			const jPipelineData data(jObject::GetBoundSphereObject(), camera, {});
			for (auto& iter : PipelineSet->BoundVolumeRenderPass)
				iter->Do(data);
		}
		GBuffer.End();
	}
}

void jDeferredRenderer::PostProcessPass(const jCamera* camera)
{
	PostProcessChain.Process(camera);
}

void jDeferredRenderer::PostRenderPass(const jCamera* camera)
{
	for (auto& iter : PipelineSet->PostRenderPass)
		iter->Do({});
}
