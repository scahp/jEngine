#include "pch.h"
#include "jEngine.h"
#include "Shader/jShader.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jFrameBufferPool.h"
#include "jCommandlineArgument.h"

jEngine::jEngine()
{

}

jEngine::~jEngine()
{
}

void jEngine::PreInit()
{
    if (gCommandLineArgument.HasArgument("-dx12"))
    {
        gAPIType = EAPIType::DX12;
    }
    else if (gCommandLineArgument.HasArgument("-vulkan"))
    {
        gAPIType = EAPIType::Vulkan;
    }
    else
    {
        gAPIType = EAPIType::DX12;		// Default API is DX12
    }

    if (IsUseVulkan())
    {
        g_rhi = new jRHI_Vulkan();
    }
    else if (IsUseDX12())
    {
        g_rhi = new jRHI_DX12();
    }
    else
    {
        check(0);
    }
}

void jEngine::Init()
{
	Game.Setup();
}

void jEngine::Release()
{
	if (g_rhi_vk)
		g_rhi_vk->Flush();

	Game.Release();
}

void jEngine::ProcessInput(float deltaTime)
{
	Game.ProcessInput(deltaTime);
}

void jEngine::Update(float deltaTime)
{
	//SCOPE_CPU_PROFILE(Engine_Update);
	//SCOPE_GPU_PROFILE(Engine_Update);

	g_timeDeltaSecond = deltaTime;
	Game.Update(deltaTime);

	jShader::StartAndRunCheckUpdateShaderThread();
}

void jEngine::Draw()
{
	Game.Draw();
}

void jEngine::Resize(int32 width, int32 height)
{
    Game.Resize(width, height);

	if (IsUseVulkan())
	{
		g_rhi_vk->FramebufferResized = true;
	}
}

void jEngine::OnMouseButton()
{
	Game.OnMouseButton();
}

void jEngine::OnMouseMove(int32 xOffset, int32 yOffset)
{
	Game.OnMouseMove(xOffset, yOffset);
}
