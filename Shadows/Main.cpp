// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include "jEngine.h"
#include "jRHI.h"

#include <AntTweakBar.h>
#include "jAppSettings.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void char_callback(GLFWwindow* window, uint32 codepoint);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void showFPS(GLFWwindow* pWindow);

jEngine g_Engine;

int main()
{
	// glfw: initialize and configure
	// ------------------------------
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	//glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);

#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

	// glfw window creation
	// --------------------
	GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "OpenGL", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetCharCallback(window, char_callback);
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetScrollCallback(window, scroll_callback);

	// glad: load all OpenGL function pointers
	// ---------------------------------------
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	jAppSettings::GetInstance().Init(SCR_WIDTH, SCR_HEIGHT);

	g_Engine.Init();

	// render loop
	// -----------
	while (!glfwWindowShouldClose(window))
	{
		// input
		// -----
		//processInput(window);
		g_Engine.ProcessInput();

		// render
		// ------
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		g_Engine.Update(0.0f);

		TwDraw();

		// glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
		// -------------------------------------------------------------------------------
		glfwSwapBuffers(window);
		glfwPollEvents();

		showFPS(window);
	}

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
	if (TwEventKeyGLFW(GLFW_KEY_9, action))
		return;

	const char* key_name = glfwGetKeyName(key, 0);
	if (!key_name)
		return;

	if (GLFW_PRESS == action)
		g_KeyState[*key_name] = true;
	else if (GLFW_RELEASE == action)
		g_KeyState[*key_name] = false;

}

void char_callback(GLFWwindow* window, uint32 codepoint)
{
	TwEventCharGLFW(codepoint, GLFW_PRESS);
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (TwEventMousePosGLFW(static_cast<int>(xpos), static_cast<int>(ypos)))
		return;

	static double xOld = -1.0;
	static double yOld = -1.0;

	if (-1.0 == xOld)
		xOld = xpos;

	if (-1.0 == yOld)
		yOld = ypos;

	g_Engine.OnMouseMove((int32)(xpos - xOld), (int32)(ypos - yOld));

	xOld = xpos;
	yOld = ypos;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (TwEventMouseButtonGLFW(button, action))
		return;

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
		buttonDown = true;
	else if (GLFW_RELEASE == action)
		buttonDown = false;
	else
		return;

	g_MouseState[buttonType] = buttonDown;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	if (TwEventMouseWheelGLFW(static_cast<int>(yoffset)))
		return;
}

// https://stackoverflow.com/questions/18412120/displaying-fps-in-glfw-window-title
void showFPS(GLFWwindow* pWindow)
{
	// Measure speed
	static double lastTime = glfwGetTime();;
	double currentTime = glfwGetTime();
	double delta = currentTime - lastTime;
	static int32 nbFrames = 0;
	++nbFrames;
	if (delta >= 1.0) { // If last cout was more than 1 sec ago
		std::cout << 1000.0 / double(nbFrames) << std::endl;

		double fps = double(nbFrames) / delta;

		std::stringstream ss;
		ss << "OpenGL" << " " << 0.1 << " [" << fps << " FPS]";

		glfwSetWindowTitle(pWindow, ss.str().c_str());

		nbFrames = 0;
		lastTime = currentTime;
	}
}
