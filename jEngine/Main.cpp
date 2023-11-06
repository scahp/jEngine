// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include "jEngine.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/DX12/jRHI_DX12.h"
#include "jCommandlineArgument.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void char_callback(GLFWwindow* window, uint32 codepoint);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void window_focus_callback(GLFWwindow* window, int focused);

std::vector<jRenderPass*> ImGuiRenderPasses;
std::vector<VkPipeline> Pipelines;

int main()
{
    gCommandLineArgument.Init(GetCommandLineA());

	g_Engine = new jEngine();
	g_Engine->PreInit();

	g_rhi->InitRHI();
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
	}

	//int major, minor, rev;
	//glfwGetVersion(&major, &minor, &rev);
	//auto versionCheck = glfwGetVersionString();

	// glfwSwapInterval(0);		// 0 is no limit fps

	g_Engine->Init();

	// render loop
	// -----------
	if (IsUseDX12())
	{
		MSG msg = {};
		while (::IsWindow((HWND)g_rhi->GetWindow()))
		{
			if (g_rhi_dx12)
			{
				g_Engine->ProcessInput();

				static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
				std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
				std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
				g_timeDeltaSecond = (float)elapsed_seconds.count();
				lastTime = currentTime;

                g_rhi->IncrementFrameNumber();
				g_Engine->Update(g_timeDeltaSecond);
				g_Engine->Draw();
				jPerformanceProfile::GetInstance().Update(g_timeDeltaSecond);
			}

			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
	}
	else
	{
		while (!glfwWindowShouldClose(window))
		{
			// input
			// -----
			//processInput(window);
			g_Engine->ProcessInput();

			// render
			// ------
			static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
			std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
			std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
			g_timeDeltaSecond = (float)elapsed_seconds.count();
			lastTime = currentTime;

			g_rhi->IncrementFrameNumber();
			g_Engine->Update(g_timeDeltaSecond);
			g_Engine->Draw();
			jPerformanceProfile::GetInstance().Update(g_timeDeltaSecond);

			glfwPollEvents();
		}
	}

	g_Engine->Release();

	g_rhi->ReleaseRHI();

	// glfw: terminate, clearing all previously allocated GLFW resources.
	// ------------------------------------------------------------------
	glfwTerminate();
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

	const char* key_name = glfwGetKeyName(key, 0);
	if (!key_name)
		return;

	if (GLFW_PRESS == action)
	{
		if (!ImGui::IsAnyItemActive())
			g_KeyState[*key_name] = true;
	}
	else if (GLFW_RELEASE == action)
	{
		g_KeyState[*key_name] = false;
	}
}

void char_callback(GLFWwindow* window, uint32 codepoint)
{
}

void cursor_position_callback(GLFWwindow* window, double x, double y)
{
	ImGui_ImplGlfw_CursorPosCallback(window, x, y);

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

	bool buttonDown = false;
	if (GLFW_PRESS == action)
	{
		const bool IsCapturedButtonInputOnUI = ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
		if (!IsCapturedButtonInputOnUI)
		{
			buttonDown = true;
		}
	}
	else if (GLFW_RELEASE == action)
	{
		buttonDown = false;
	}
	else
	{
		return;
	}

	g_MouseState[buttonType] = buttonDown;
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
	//if (focused)
 //   {
	//	// 윈도우가 Focus 를 얻는 경우 Mouse click callback 이 호출되는데, 
	//	// 이때 아직 UI는 Mouse Hover 상태 정보를 모르기 때문에 여기서 갱신
	//	ImGui_ImplGlfw_NewFrame();
	//	ImGui::NewFrame();
	//	ImGui::EndFrame();
	//}
}
