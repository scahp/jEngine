#include "pch.h"
#include "jRenderer.h"
#include "jObject.h"
#include "jShadowAppProperties.h"
#include "jShader.h"

jRenderer::jRenderer()
{
}


jRenderer::~jRenderer()
{
}

void jRenderer::Setup()
{
}

void jRenderer::Teardown()
{

}

void jRenderer::Reset()
{

}

void jRenderer::ShadowPrePass(const jCamera* camera)
{
}

void jRenderer::DebugShadowPrePass(const jCamera* camera)
{
}

void jRenderer::RenderPass(const jCamera* camera)
{
}

void jRenderer::PostRenderPass(const jCamera* camera)
{

}

void jRenderer::DebugRenderPass(const jCamera* camera)
{
	jShaderInfo BoundVolumeShaderInfo;
	static jShader* BoundVolumeShader = nullptr;
	if (!BoundVolumeShader)
	{
		BoundVolumeShaderInfo.name = "BoundVolumeShader";
		BoundVolumeShaderInfo.vs = "Shaders/vs_boundvolume.glsl";
		BoundVolumeShaderInfo.fs = "Shaders/fs_boundvolume.glsl";
		BoundVolumeShader = jShader::CreateShader(BoundVolumeShaderInfo);
	}

	if (jShadowAppSettingProperties::GetInstance().ShowBoundBox)
	{
		for (auto& iter : g_BoundBoxObjectArray)
			iter->Draw(camera, BoundVolumeShader, {});
	}

	if (jShadowAppSettingProperties::GetInstance().ShowBoundSphere)
	{
		for (auto& iter : g_BoundSphereObjectArray)
			iter->Draw(camera, BoundVolumeShader, {});
	}
}

void jRenderer::PostProcessPass(const jCamera* camera)
{

}

void jRenderer::DebugPostProcessPass(const jCamera* camera)
{

}

void jRenderer::UIPass(const jCamera* camera)
{
}

void jRenderer::DebugUIPass(const jCamera* camera)
{
}

void jRenderer::Render(const jCamera* camera)
{
	ShadowPrePass(camera);
	DebugShadowPrePass(camera);

	RenderPass(camera);
	PostRenderPass(camera);
	DebugRenderPass(camera);

	PostProcessPass(camera);

	UIPass(camera);
	DebugUIPass(camera);
}
