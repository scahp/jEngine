#include "pch.h"
#include "jEngine.h"
#include "Shader/jShader.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jFrameBufferPool.h"
#include "jCommandlineArgument.h"

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Code/Engine/Editor/jEditorProcedural.h"
#include "Code/Engine/Editor/jEditorUiRenderer.h"
#include "Code/Engine/Editor/jEditorProjectSettings.h"
#endif

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
#ifdef __APPLE__
        gAPIType = EAPIType::Vulkan;
#else
        gAPIType = EAPIType::DX12;
#endif
    }

    if (IsUseVulkan())
    {
        g_rhi = new jRHI_Vulkan();
    }
    else if (IsUseDX12())
    {
#ifdef _WIN32
        g_rhi = new jRHI_DX12(Game.StartupSceneUsesPathTracing());
#else
        throw std::runtime_error("DX12 is available only on Windows; use -vulkan");
#endif
    }
    else
    {
        check(0);
    }
}

void jEngine::Init()
{
#ifdef ENABLE_EDITOR_FEATURES
	// Initialize editor features
	g_Editor = new jEditor();
    g_Editor->SetAuthoringEnabled(true);
    jEditorProcedural::Install(*g_Editor);
#endif

	Game.Setup();
#ifdef ENABLE_EDITOR_FEATURES
    if (g_Editor && !Game.IsSceneImportPending()) g_Editor->CaptureWorldToDocument();
#endif
}

void jEngine::Release()
{
	if (g_rhi_vk)
		g_rhi_vk->Flush();

#ifdef ENABLE_EDITOR_FEATURES
    ShutdownAuthoringWorkspace();
    if (g_Editor) g_Editor->ShutdownWorld();
#endif
	Game.Release();
    jRenderTargetPool::Release();

#ifdef ENABLE_EDITOR_FEATURES
	// Cleanup editor features
	if (g_Editor)
	{
		delete g_Editor;
		g_Editor = nullptr;
	}
#endif
}

void jEngine::ProcessInput(float deltaTime)
{
    if (jEditorProjectSettings::ConsumePresentModeChange() && g_rhi) g_rhi->RecreateSwapChain();
	Game.ProcessInput(deltaTime);
}

void jEngine::Update(float deltaTime)
{
	//SCOPE_CPU_PROFILE(Engine_Update);
	//SCOPE_GPU_PROFILE(Engine_Update);

	g_timeDeltaSecond = deltaTime;
	Game.Update(deltaTime);
#ifdef ENABLE_EDITOR_FEATURES
    if (g_Editor)
    {
        TickAuthoringWorkspace(*g_Editor, deltaTime);
        g_Editor->Tick(deltaTime, Game.MainCamera);
    }
#endif
    Game.SyncRaytracingScene();

	jShader::StartAndRunCheckUpdateShaderThread();
}

void jEngine::Draw()
{
	Game.Draw();
}

void jEngine::Resize(int32 width, int32 height)
{
    if (SCR_WIDTH != width || SCR_HEIGHT != height)
    {
        SCR_WIDTH = width;
        SCR_HEIGHT = height;

        Game.Resize(width, height);
        g_rhi->OnHandleResized(width, height, false);
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

void jEngine::EndOfFrame()
{
#ifdef ENABLE_EDITOR_FEATURES
    jEditorUiRenderer::RenderPlatformWindows();
#endif
	jProfileFrameMark();
	jPerformanceProfile::GetInstance().Update(g_timeDeltaSecond);
	jRenderTargetPool::ReturnAllOneFrameRTs();
	ResetMouseClickedState();
}
