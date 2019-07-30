#include "pch.h"
#include "jForwardRenderer.h"
#include "jObject.h"
#include "jShadowAppProperties.h"
#include "jRenderTargetPool.h"

#include "jRHI_OpenGL.h"		// todo it should be removed.
#include "jCamera.h"
#include "jLight.h"

//////////////////////////////////////////////////////////////////////////
// jForwardRenderer
jForwardRenderer::~jForwardRenderer()
{
}

void jForwardRenderer::Setup()
{	
}

void jForwardRenderer::Teardown()
{
	
}

void jForwardRenderer::ShadowPrePass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "ShadowPrePass");

	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const jPipelineData data(jObject::GetShadowCasterObject(), camera, lights);

	for (auto& iter : PipelineSet->ShadowPrePass)
		iter->Do(data);
}

void jForwardRenderer::RenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "RenderPass");

	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const jPipelineData data(jObject::GetStaticObject(), camera, lights);

	for (auto& iter : PipelineSet->RenderPass)
		iter->Do(data);
}

void jForwardRenderer::DebugRenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "DebugRenderPass");

	const jPipelineData data(jObject::GetDebugObject(), camera, {});
	for (auto& iter : PipelineSet->DebugRenderPass)
		iter->Do(data);
}

void jForwardRenderer::BoundVolumeRenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "BoundVolumeRenderPass");

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
}

void jForwardRenderer::PostProcessPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "PostProcessPass");
	PostProcessChain.Process(camera);
}

void jForwardRenderer::PostRenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "PostRenderPass");
	for (auto& iter : PipelineSet->PostRenderPass)
		iter->Do({});
}

void jForwardRenderer::DebugUIPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "DebugUIPass");
	const jPipelineData data(jObject::GetUIDebugObject(), camera, {});
	for (auto& iter : PipelineSet->DebugUIPass)
		iter->Do(data);
}
