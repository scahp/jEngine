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
	RenderTarget = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RGBA16F, EFormat::RGBA, EFormatType::FLOAT, EDepthBufferType::DEPTH_STENCIL, SCR_WIDTH, SCR_HEIGHT, 1 }));

	PostProcessInput = std::shared_ptr<jPostProcessInOutput>(new jPostProcessInOutput());
	PostProcessInput->RenderTarget = RenderTarget.get();

	//////////////////////////////////////////////////////////////////////////
	// Setup a postprocess chain
	LuminanceRenderTarget = std::shared_ptr<jRenderTarget>(jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::R32F, EFormat::R, EFormatType::FLOAT, EDepthBufferType::DEPTH, LUMINANCE_WIDTH, LUMINANCE_HEIGHT, 1, ETextureFilter::LINEAR, ETextureFilter::LINEAR_MIPMAP_LINEAR }));
	auto luminancePostProcessOutput = std::shared_ptr<jPostProcessInOutput>(new jPostProcessInOutput());
	luminancePostProcessOutput->RenderTarget = LuminanceRenderTarget.get();
	{
		auto postprocess = new jPostProcess_LuminanceMapGeneration("LuminanceMapGeneration");
		postprocess->AddInput(PostProcessInput);
		postprocess->SetOutput(luminancePostProcessOutput);
		PostProcessChain.AddNewPostprocess(postprocess);
	}

	auto avgLuminancePostProcessOutput = std::shared_ptr<jPostProcessInOutput>(new jPostProcessInOutput());
	{
		auto postprocess = new jPostProcess_AdaptiveLuminance("AdaptiveLuminance");
		postprocess->AddInput(luminancePostProcessOutput);
		postprocess->SetOutput(avgLuminancePostProcessOutput);
		PostProcessChain.AddNewPostprocess(postprocess);
	}

	{
		auto postprocess = new jPostProcess_Tonemap("Tonemap");
		postprocess->AddInput(PostProcessInput);
		postprocess->AddInput(avgLuminancePostProcessOutput);
		postprocess->SetOutput(nullptr);
		PostProcessChain.AddNewPostprocess(postprocess);
	}
}

void jForwardRenderer::Teardown()
{
	
}

void jForwardRenderer::ShadowPrePass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "ShadowPrePass");

	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const jPipelineData data(RenderTarget.get(), jObject::GetShadowCasterObject(), camera, lights);

	for (auto& iter : PipelineSet->ShadowPrePass)
		iter->Do(data);
}

void jForwardRenderer::RenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "RenderPass");

	std::list<const jLight*> lights;
	lights.insert(lights.end(), camera->LightList.begin(), camera->LightList.end());
	const jPipelineData data(RenderTarget.get(), jObject::GetStaticObject(), camera, lights);

	for (auto& iter : PipelineSet->RenderPass)
		iter->Do(data);
}

void jForwardRenderer::DebugRenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "DebugRenderPass");

	const jPipelineData data(RenderTarget.get(), jObject::GetDebugObject(), camera, {});
	for (auto& iter : PipelineSet->DebugRenderPass)
		iter->Do(data);
}

void jForwardRenderer::BoundVolumeRenderPass(const jCamera* camera)
{
	SCOPE_DEBUG_EVENT(g_rhi, "BoundVolumeRenderPass");

	if (jShadowAppSettingProperties::GetInstance().ShowBoundBox)
	{
		const jPipelineData data(RenderTarget.get(), jObject::GetBoundBoxObject(), camera, {});
		for (auto& iter : PipelineSet->BoundVolumeRenderPass)
			iter->Do(data);
	}

	if (jShadowAppSettingProperties::GetInstance().ShowBoundSphere)
	{
		const jPipelineData data(RenderTarget.get(), jObject::GetBoundSphereObject(), camera, {});
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
	const jPipelineData data(RenderTarget.get(), jObject::GetUIDebugObject(), camera, {});
	for (auto& iter : PipelineSet->DebugUIPass)
		iter->Do(data);
}
