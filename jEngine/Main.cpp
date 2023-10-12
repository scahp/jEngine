// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include "jEngine.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/DX12/jRHI_DX12.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void char_callback(GLFWwindow* window, uint32 codepoint);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void window_focus_callback(GLFWwindow* window, int focused);
void showFPS(GLFWwindow* pWindow);

#if USE_OPENGL
void APIENTRY glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei length, const GLchar* message, const void* userParam);
#endif

jEngine g_Engine;

std::vector<jRenderPass*> ImGuiRenderPasses;
std::vector<VkPipeline> Pipelines;

int main()
{
//#if USE_DX12
//    jRHI_DX12 dx12;
//    dx12.m_hWnd = dx12.CreateMainWindow();
//    dx12.InitRHI();
//    dx12.Run();
//    dx12.Release();
//    return 0;
//#endif

	//::ShowWindow(::GetConsoleWindow(), SW_HIDE);		// hide console window

	g_rhi->InitRHI();
	g_rhi->OnInitRHI();

#if USE_VULKAN || USE_OPENGL
	GLFWwindow* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetCharCallback(window, char_callback);
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetWindowFocusCallback(window, window_focus_callback);
#endif

	//int major, minor, rev;
	//glfwGetVersion(&major, &minor, &rev);
	//auto versionCheck = glfwGetVersionString();

	// glfwSwapInterval(0);		// 0 is no limit fps

	// Decide GL+GLSL versions
#if __APPLE__
	// GL 3.2 + GLSL 150
	const char* glsl_version = "#version 150";
#else
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
#endif

#if USE_OPENGL
#if (DEBUG_OUTPUT_ON == 1)
	GLint flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
	if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
	{
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(glDebugOutput, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	}
#endif // DEBUG_OUTPUT_ON
#endif

	g_Engine.Init();

#if USE_OPENGL
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    ImGui::StyleColorsClassic();

    ImGuiIO& io = ImGui::GetIO(); (void)io;

    jAppSettings::GetInstance().Init(SCR_WIDTH, SCR_HEIGHT);
#endif

	// render loop
	// -----------
#if USE_DX12
    MSG msg = {};
    while (::IsWindow((HWND)g_rhi->GetWindow()))
    {
        if (g_rhi_dx12)
        {
            g_rhi->IncrementFrameNumber();

            static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
            std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
            g_timeDeltaSecond = (float)elapsed_seconds.count();
            lastTime = currentTime;

            g_Engine.Update(g_timeDeltaSecond);
            g_Engine.Draw();
        }

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }
#else
	while (!glfwWindowShouldClose(window))
	{
		// input
		// -----
		//processInput(window);
		g_Engine.ProcessInput();

		// render
		// ------
		static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
		std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
        g_timeDeltaSecond = (float)elapsed_seconds.count();
		lastTime = currentTime;

		g_rhi->IncrementFrameNumber();
        g_Engine.Update(g_timeDeltaSecond);
        g_Engine.Draw();
		jPerformanceProfile::GetInstance().Update(g_timeDeltaSecond);

		#if USE_OPENGL
        {
            SCOPE_DEBUG_EVENT(g_rhi, "TwDraw");
            TwDraw();
        }

		if (0)
		{
			SCOPE_DEBUG_EVENT(g_rhi, "IMGUI");

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			ImGui::EndFrame();
			ImGui::Render();

			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}
		#endif

#if USE_OPENGL
		g_rhi->Flush();

		// glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
		// -------------------------------------------------------------------------------
		glfwSwapBuffers(window);
#endif
		glfwPollEvents();

		showFPS(window);
	}
#endif // USE_DX12

	g_Engine.Release();

#if USE_OPENGL
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
#endif
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
	g_Engine.Resize(width, height);
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

	g_Engine.OnMouseMove((int32)(x - xOld), (int32)(y - yOld));

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
	g_Engine.OnMouseButton();
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

// https://stackoverflow.com/questions/18412120/displaying-fps-in-glfw-window-title
void showFPS(GLFWwindow* pWindow)
{
	// Measure speed
	static double lastTime = glfwGetTime();
	double currentTime = glfwGetTime();
	double delta = currentTime - lastTime;
	static int32 nbFrames = 0;
	++nbFrames;
	if (delta >= 1.0) { // If last cout was more than 1 sec ago
		std::cout << 1000.0 / double(nbFrames) << std::endl;

		double fps = double(nbFrames) / delta;

		std::stringstream ss;
#if USE_VULKAN
		ss << "Vulkan"
#else USE_OPENGL
		ss << "OpenGL" 
#endif
			<< " " << 0.1 << " [" << fps << " FPS]" << " - " << (float)(delta * 1000.0f / nbFrames) << " ms";

		glfwSetWindowTitle(pWindow, ss.str().c_str());

		nbFrames = 0;
		lastTime = currentTime;
	}
}

#if USE_OPENGL
void APIENTRY glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei length, const GLchar* message, const void* userParam)
{
	// ignore non-significant error/warning codes
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

#if DEBUG_OUTPUT_LEVEL == 2
	if (severity != GL_DEBUG_SEVERITY_HIGH)
		return;
#elif DEBUG_OUTPUT_LEVEL == 1
	if (severity != GL_DEBUG_SEVERITY_HIGH && severity != GL_DEBUG_SEVERITY_MEDIUM)
		return;
#endif

	std::cout << "---------------" << std::endl;
	std::cout << "Debug message (" << id << "): " << message << std::endl;

	switch (source)
	{
	case GL_DEBUG_SOURCE_API:             std::cout << "Source: API"; break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "Source: Window System"; break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "Source: Shader Compiler"; break;
	case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "Source: Third Party"; break;
	case GL_DEBUG_SOURCE_APPLICATION:     std::cout << "Source: Application"; break;
	case GL_DEBUG_SOURCE_OTHER:           std::cout << "Source: Other"; break;
	} std::cout << std::endl;

	switch (type)
	{
	case GL_DEBUG_TYPE_ERROR:               std::cout << "Type: Error"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "Type: Deprecated Behaviour"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "Type: Undefined Behaviour"; break;
	case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "Type: Portability"; break;
	case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "Type: Performance"; break;
	case GL_DEBUG_TYPE_MARKER:              std::cout << "Type: Marker"; break;
	case GL_DEBUG_TYPE_PUSH_GROUP:          std::cout << "Type: Push Group"; break;
	case GL_DEBUG_TYPE_POP_GROUP:           std::cout << "Type: Pop Group"; break;
	case GL_DEBUG_TYPE_OTHER:               std::cout << "Type: Other"; break;
	} std::cout << std::endl;

	switch (severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:         std::cout << "Severity: high"; break;
	case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "Severity: medium"; break;
	case GL_DEBUG_SEVERITY_LOW:          std::cout << "Severity: low"; break;
	case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "Severity: notification"; break;
	} std::cout << std::endl;
	std::cout << std::endl;
}
#endif // USE_OPENGL
