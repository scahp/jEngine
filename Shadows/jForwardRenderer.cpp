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
	PipelineSet = new jForwardPipelineSet();
	PipelineSet->Setup();
}

void jForwardRenderer::Teardown()
{
	
}

void jForwardRenderer::ShadowPrePass(const jCamera* camera)
{
	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const jPipelineData data(jObject::GetShadowCasterObject(), camera, lights);

	for (auto& iter : PipelineSet->ShadowPrePass)
		iter->Do(data);
}

void jForwardRenderer::RenderPass(const jCamera* camera)
{
	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const jPipelineData data(jObject::GetStaticObject(), camera, lights);

	for (auto& iter : PipelineSet->RenderPass)
		iter->Do(data);
}

void jForwardRenderer::PostProcessPass(const jCamera* camera)
{
	PostProcessChain.Process(camera);
}

void jForwardRenderer::PostRenderPass(const jCamera* camera)
{
	for (auto& iter : PipelineSet->PostRenderPass)
		iter->Do({});
}
