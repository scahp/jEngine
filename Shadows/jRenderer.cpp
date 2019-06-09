#include "pch.h"
#include "jRenderer.h"
#include "jObject.h"
#include "jShadowAppProperties.h"

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

void jRenderer::ShadowPrePass(jCamera* camera)
{
}

void jRenderer::DebugShadowPrePass(jCamera* camera)
{
}

void jRenderer::RenderPass(jCamera* camera)
{
}

void jRenderer::DebugRenderPass(jCamera* camera)
{
	jShaderInfo BoundVolumeShaderInfo;
	jShader* BoundVolumeShader = nullptr;

	BoundVolumeShaderInfo.vs = "Shaders/vs_boundvolume.glsl";
	BoundVolumeShaderInfo.fs = "Shaders/fs_boundvolume.glsl";
	BoundVolumeShader = jShader::CreateShader(BoundVolumeShaderInfo);

	if (jShadowAppSettingProperties::GetInstance().ShowBoundBox)
	{
		for (auto& iter : g_BoundBoxObjectArray)
			iter->Draw(camera, BoundVolumeShader, nullptr);
	}

	if (jShadowAppSettingProperties::GetInstance().ShowBoundSphere)
	{
		for (auto& iter : g_BoundSphereObjectArray)
			iter->Draw(camera, BoundVolumeShader, nullptr);
	}
}

void jRenderer::UIPass(jCamera* camera)
{
}

void jRenderer::DebugUIPass(jCamera* camera)
{
}


void jRenderer::Render(jCamera* camera)
{
	ShadowPrePass(camera);
	DebugShadowPrePass(camera);

	RenderPass(camera);
	DebugRenderPass(camera);

	UIPass(camera);
	DebugUIPass(camera);
}
