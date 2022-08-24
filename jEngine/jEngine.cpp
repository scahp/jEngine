#include "pch.h"
#include "jEngine.h"
#include "Shader/jShader.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jFrameBufferPool.h"

jEngine::jEngine()
{
}


jEngine::~jEngine()
{
}

void jEngine::Init()
{
#if USE_OPENGL
	jSamplerStatePool::CreateDefaultSamplerState();
	jShaderInfo::CreateShaders();
	IPipeline::SetupPipelines();

	g_rhi->EnableSRGB(false);

	Game.Setup();

	jShadowAppSettingProperties::GetInstance().Setup(jAppSettings::GetInstance().Get("MainPannel"));
#elif USE_VULKAN
	Game.Setup();
#endif
}

void jEngine::Release()
{
	g_rhi_vk->Flush();
	Game.Release();
}

void jEngine::ProcessInput()
{
	Game.ProcessInput();
}

void jEngine::Update(float deltaTime)
{
	SCOPE_PROFILE(Engine_Update);
	//SCOPE_GPU_PROFILE(Engine_Update);

	g_timeDeltaSecond = deltaTime;
	Game.Update(deltaTime);

	jShader::UpdateShaders();
}

void jEngine::Draw()
{
	Game.Draw();
}

void jEngine::Resize(int width, int height)
{
	g_rhi->SetViewport(0, 0, width, height);
}

void jEngine::OnMouseButton()
{
	Game.OnMouseButton();
}

void jEngine::OnMouseMove(int32 xOffset, int32 yOffset)
{
	Game.OnMouseMove(xOffset, yOffset);
}
