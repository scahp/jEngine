// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include "jEngine.h"
#include "Profiler/jPerformanceProfile.h"
#ifdef _WIN32
#include "RHI/DX12/jRHI_DX12.h"
#endif
#include "jCommandlineArgument.h"
#include "jOptions.h"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#ifdef __APPLE__
void DumpSurfelGIDiagnostics();
void UpdateSurfelGIAuditOptions(uint32 frame);
#endif
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void char_callback(GLFWwindow* window, uint32 codepoint);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void window_focus_callback(GLFWwindow* window, int focused);
void window_size_callback(GLFWwindow* window, int width, int height);

#include "Math/Quaternion.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

namespace
{
    std::string ReadEngineEnvironment(const char* name)
    {
#ifdef _WIN32
        char* value = nullptr; size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || !value) return {};
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name);
        return value ? value : "";
#endif
    }
}

int RunCpuSelfTests();
int ExportSurfelGIShaderFixtures(const char* directory, bool vulkan);

int main(int argc, char** argv)
{
    std::string commandLine;
    for (int i = 1; i < argc; ++i) { commandLine += argv[i]; commandLine += " "; }
    gCommandLineArgument.Init(commandLine.c_str());
    if (argc >= 2 && std::strcmp(argv[1], "--ci-cpu") == 0) return RunCpuSelfTests();
    if (argc >= 3 && std::strcmp(argv[1], "--ci-export-shaders") == 0)
        return ExportSurfelGIShaderFixtures(argv[2], argc < 4 || std::strcmp(argv[3], "dx12") != 0);
	GMainThreadID = std::this_thread::get_id();

	g_Engine = new jEngine();
	g_Engine->PreInit();

	if (!g_rhi->InitRHI())
		return EXIT_FAILURE;

	g_rhi->OnInitRHI();

	GLFWwindow* window = nullptr;
	if (IsUseVulkan())
	{
		window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
		glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
		glfwSetKeyCallback(window, key_callback);
		glfwSetCharCallback(window, char_callback);
		glfwSetCursorPosCallback(window, cursor_position_callback);
		glfwSetMouseButtonCallback(window, mouse_button_callback);
		glfwSetScrollCallback(window, scroll_callback);
		glfwSetWindowFocusCallback(window, window_focus_callback);
		glfwSetWindowSizeCallback(window, window_size_callback);
	}

	//int major, minor, rev;
	//glfwGetVersion(&major, &minor, &rev);
	//auto versionCheck = glfwGetVersionString();

	// glfwSwapInterval(0);		// 0 is no limit fps

    if (const auto value = ReadEngineEnvironment("JENGINE_SURFEL_GI"); !value.empty()) gOptions.UseSurfelGI = std::atoi(value.c_str()) != 0;
    if (const auto value = ReadEngineEnvironment("JENGINE_INLINE_RAY"); !value.empty()) gOptions.SurfelGIInlineRayEnable = std::atoi(value.c_str()) != 0;
#ifdef __APPLE__
    if (std::getenv("JENGINE_TEST_SURFEL_RECONFIGURE")) UpdateSurfelGIAuditOptions(0);
#endif
	g_Engine->Init();
    std::cout << "SurfelGI: " << gOptions.UseSurfelGI << "; inline ray option: " << gOptions.SurfelGIInlineRayEnable << std::endl;
    uint32 renderedFrames = 0;
    const auto frameLimitText = ReadEngineEnvironment("JENGINE_MAX_FRAMES");
    const uint32 frameLimit = !frameLimitText.empty() ? static_cast<uint32>(std::stoul(frameLimitText)) : 0;
    const auto fixedStepText = ReadEngineEnvironment("JENGINE_FIXED_TIMESTEP");
    const float fixedStep = !fixedStepText.empty() ? std::stof(fixedStepText) : 0.0f;
    std::cout << "jEngine initialized: " << g_rhi->GetRHIName().ToStr() << std::endl;

	// render loop
	// -----------
	#ifdef _WIN32
	if (IsUseDX12())
	{
		MSG msg = {};
		while (::IsWindow((HWND)g_rhi->GetWindow()))
		{
			static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
			std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
			std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
			g_timeDeltaSecond = (float)elapsed_seconds.count();
			lastTime = currentTime;

            g_Engine->ProcessInput(g_timeDeltaSecond);
                
			g_rhi->IncrementFrameNumber();
			g_Engine->Update(g_timeDeltaSecond);
			g_Engine->Draw();
			g_Engine->EndOfFrame();

			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
	}
	else
	#endif
	{
		while (!glfwWindowShouldClose(window))
		{
			static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
			std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
			std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
			g_timeDeltaSecond = (float)elapsed_seconds.count();
			lastTime = currentTime;

            if (fixedStep > 0) g_timeDeltaSecond = fixedStep;

            // input
            // -----
            g_Engine->ProcessInput(g_timeDeltaSecond);

            // render
            // ------
			g_rhi->IncrementFrameNumber();
			g_Engine->Update(g_timeDeltaSecond);
			g_Engine->Draw();
			g_Engine->EndOfFrame();

            ++renderedFrames;
#ifdef __APPLE__
            if (std::getenv("JENGINE_TEST_SURFEL_RECONFIGURE") && (renderedFrames == 30 || renderedFrames == 60 || renderedFrames == 90))
            {
                DumpSurfelGIDiagnostics();
                UpdateSurfelGIAuditOptions(renderedFrames);
            }
            if (std::getenv("JENGINE_TEST_INLINE_TOGGLE") && std::atoi(std::getenv("JENGINE_TEST_INLINE_TOGGLE")) != 0 && (renderedFrames == 30 || renderedFrames == 60)) {
                DumpSurfelGIDiagnostics();
                gOptions.SurfelGIInlineRayEnable = renderedFrames == 60;
                std::cout << "Inline ray option toggled: " << gOptions.SurfelGIInlineRayEnable
                          << " after frame " << renderedFrames << std::endl;
            }
            if (renderedFrames == 90 && std::getenv("JENGINE_SURFEL_DIAGNOSTICS")) DumpSurfelGIDiagnostics();
#endif
            if (renderedFrames == 1 || renderedFrames % 120 == 0)
                std::cout << "Rendered frames: " << renderedFrames << std::endl;
            if (frameLimit && renderedFrames >= frameLimit) glfwSetWindowShouldClose(window, GLFW_TRUE);
			glfwPollEvents();
		}
	}

	g_Engine->Release();

	g_rhi->ReleaseRHI();

	// glfw: terminate, clearing all previously allocated GLFW resources.
	// ------------------------------------------------------------------
	glfwTerminate();
    std::cout << "jEngine clean shutdown; rendered frames: " << renderedFrames << std::endl;
	return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and 
	// height will be significantly larger than specified on retina displays.
	g_Engine->Resize(width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

	const EInputKey inputKey = ToInputKeyFromGLFW(key);
	if (inputKey == EInputKey::UNKNOWN)
		return;

	if (GLFW_PRESS == action)
	{
		if (!ImGui::IsAnyItemActive())
			SetKeyDownState(inputKey, true);
	}
	else if (GLFW_RELEASE == action)
	{
		SetKeyDownState(inputKey, false);
	}
}

void char_callback(GLFWwindow* window, uint32 codepoint)
{
}

void cursor_position_callback(GLFWwindow* window, double x, double y)
{
	ImGui_ImplGlfw_CursorPosCallback(window, x, y);
	UpdateMousePosition((int32)x, (int32)y);

	static double xOld = -1.0;
	static double yOld = -1.0;

	if (-1.0 == xOld)
		xOld = x;

	if (-1.0 == yOld)
		yOld = y;

	g_Engine->OnMouseMove((int32)(x - xOld), (int32)(y - yOld));

	xOld = x;
	yOld = y;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

	EMouseButtonType buttonType;
	if (GLFW_MOUSE_BUTTON_RIGHT == button)
		buttonType = EMouseButtonType::RIGHT;
	else if (GLFW_MOUSE_BUTTON_LEFT == button)
		buttonType = EMouseButtonType::LEFT;
	else if (GLFW_MOUSE_BUTTON_MIDDLE == button)
		buttonType = EMouseButtonType::MIDDLE;
	else
		return;

	jMouseButtonState& buttonState = g_MouseState[MouseButtonIndex(buttonType)];
	const uint64 eventTimeMS = GetInputTimeMS();
	double xpos = 0.0;
	double ypos = 0.0;
	glfwGetCursorPos(window, &xpos, &ypos);
	UpdateMousePosition((int32)xpos, (int32)ypos);
	if (GLFW_PRESS == action)
	{
		const bool IsCapturedButtonInputOnUI = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
		buttonState.SetDownState(!IsCapturedButtonInputOnUI, g_MousePosX, g_MousePosY, eventTimeMS);
		if (!IsCapturedButtonInputOnUI)
		{
			#ifdef _WIN32
			HWND hwnd = glfwGetWin32Window(window);
			if (hwnd)
				SetCapture(hwnd);
			#endif
		}
	}
	else if (GLFW_RELEASE == action)
	{
		buttonState.SetDownState(false, g_MousePosX, g_MousePosY, eventTimeMS);
		const bool anyButtonDown =
			g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down ||
			g_MouseState[MouseButtonIndex(EMouseButtonType::MIDDLE)].Down ||
			g_MouseState[MouseButtonIndex(EMouseButtonType::RIGHT)].Down;
		#ifdef _WIN32
		if (!anyButtonDown)
			#ifdef _WIN32
		ReleaseCapture();
		#endif
		#endif
	}
	else
	{
		return;
	}

	g_Engine->OnMouseButton();
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

    //ImGuiIO& io = ImGui::GetIO();
    //io.MouseWheelH += (float)xoffset;
    //io.MouseWheel += (float)yoffset;
}

void window_focus_callback(GLFWwindow* window, int focused)
{
	ImGui_ImplGlfw_WindowFocusCallback(window, focused);
	if (!focused)
	{
		ResetAllKeyState();
		ResetMouseAllState();
		#ifdef _WIN32
		ReleaseCapture();
		#endif
	}
}

void window_size_callback(GLFWwindow* window, int width, int height)
{
	if (g_Engine)
		g_Engine->Resize(width, height);
}
