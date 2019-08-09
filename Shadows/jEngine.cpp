#include "pch.h"
#include "jEngine.h"
#include "jAppSettings.h"
#include "jShadowAppProperties.h"
#include "jPipeline.h"
#include "jShader.h"

jEngine::jEngine()
{
}


jEngine::~jEngine()
{
	Game.Teardown();

	jShadowAppSettingProperties::GetInstance().Teardown(jAppSettings::GetInstance().Get("MainPannel"));
}

void jEngine::Init()
{
	jShaderInfo::CreateShaders();
	IPipeline::SetupPipelines();

	Game.Setup();

	jShadowAppSettingProperties::GetInstance().Setup(jAppSettings::GetInstance().Get("MainPannel"));
}

void jEngine::ProcessInput()
{
	Game.ProcessInput();
}

void jEngine::Update(float deltaTime)
{
	g_timeDeltaSecond = deltaTime;
	Game.Update(deltaTime);
}

void jEngine::Resize(int width, int height)
{
	glViewport(0, 0, width, height);
}

void jEngine::OnMouseMove(int32 xOffset, int32 yOffset)
{
	Game.OnMouseMove(xOffset, yOffset);
}
