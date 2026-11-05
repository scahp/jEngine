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
#include "Core/jLog.h"
#include "Code/Engine/Editor/jEditorStartup.h"
#include "Code/Engine/Editor/jEditorWorkspace.h"
#include "Code/Engine/jEditor.h"
#include "Tests/jEditorIntegrationTests.h"
#include "FileLoader/jEditorAssetPath.h"
#ifdef _WIN32
#include <shellapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#ifdef __APPLE__
void DumpSurfelGIDiagnostics();
void UpdateSurfelGIAuditOptions(uint32 frame);
void DumpEffectAuditDiagnostics();
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

    bool ReadEngineBoolean(const char* name, bool& result)
    {
        const auto value = ReadEngineEnvironment(name);
        if (value.empty())
            return true;
        uint32 parsed = 0;
        if (!jCommandLineArgument::TryParseUInt32(value, parsed) || parsed > 1)
        {
            std::cerr << name << " must be 0 or 1" << std::endl;
            return false;
        }
        result = parsed != 0;
        return true;
    }

#ifdef _WIN32
    bool RestartForSceneBrowser(const std::vector<std::string>& scene, const std::filesystem::path& project)
    {
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) return false;
        executable.resize(length);

        // Scene IDs and project paths come from Windows filenames, which cannot
        // contain quotes or end in a path separator.
        std::wstring command = L"\"" + executable + L"\" -dx12 --scene-browser \"" +
            jEditorAssetPath::FromUtf8(scene[0]).native() + L"\" " + jEditorAssetPath::FromUtf8(scene[1]).native();
        if (!project.empty()) command += L" --project \"" + project.native() + L"\"";

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
            return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }
#endif
}

int RunCpuSelfTests();
int RunEditorGraphicsTests();
int RunEditorProjectSettingsTests();
int RunSceneFeatureCpuTests();
int RunSceneFeatureTests();
int RunMaterialMappingTests();
int RunEditorWaterTests();
int RunSceneFeatureProjectTests();
int RunEditorToolsTests();
int RunEditorAuthoringRuntimeTests();
int RunSceneFeatureIntegrationGpuTests(const char* directory);
int RunModelUploadGpuTests();
int RunEditorParticleMaterialGpuTests();
int RunLogBenchmark();
int RunAssetDecodeBenchmarks(const char* fixtureDirectory, const char* outputPath);
int RunEditorWorkflowBenchmarks(const char* outputPath);
int ExportSurfelGIShaderFixtures(const char* directory, bool vulkan);
int ExportPostProcessShaderFixtures(const char* directory, bool vulkan);
int ExportAOEffectShaderFixtures(const char* directory, bool vulkan);
int ExportEditorGraphicsShaderFixtures(const char* directory, bool vulkan);
int ExportSceneFeatureShaderFixtures(const char* directory, bool vulkan);
int ExportMaterialMappingShaderFixtures(const char* directory, bool vulkan);

int main(int argc, char** argv)
{
#ifdef _WIN32
    // Windows supplies native Unicode arguments independently of the CRT's
    // current ANSI codepage. Keep every editor/path string boundary UTF-8.
    int nativeCount = 0;
    auto** nativeArguments = CommandLineToArgvW(GetCommandLineW(), &nativeCount);
    if (!nativeArguments) { std::cerr << "Cannot read Unicode command line" << std::endl; return EXIT_FAILURE; }
    std::vector<std::string> utf8Arguments; utf8Arguments.reserve(nativeCount);
    bool validArguments = true;
    for (int i = 0; i < nativeCount; ++i)
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, nativeArguments[i], -1, nullptr, 0, nullptr, nullptr);
        if (!bytes) { validArguments = false; break; }
        std::string text(static_cast<size_t>(bytes), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, nativeArguments[i], -1, text.data(), bytes, nullptr, nullptr))
        { validArguments = false; break; }
        text.pop_back(); utf8Arguments.push_back(std::move(text));
    }
    LocalFree(nativeArguments);
    if (!validArguments) { std::cerr << "Command line contains invalid Unicode" << std::endl; return EXIT_FAILURE; }
    std::vector<char*> utf8Pointers; utf8Pointers.reserve(utf8Arguments.size() + 1);
    for (auto& argument : utf8Arguments) utf8Pointers.push_back(argument.data());
    utf8Pointers.push_back(nullptr); argc = nativeCount; argv = utf8Pointers.data();
#endif
    gCommandLineArgument.Init(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "--ci-cpu") == 0) return RunCpuSelfTests();
    if (argc >= 2 && std::strcmp(argv[1], "--ci-rendering-features") == 0)
    {
        if (argc != 2)
        {
            std::cerr << "Usage: --ci-rendering-features" << std::endl;
            return EXIT_FAILURE;
        }
        const int graphics = RunEditorGraphicsTests();
        const int settings = RunEditorProjectSettingsTests();
        const int sceneCpu = RunSceneFeatureCpuTests();
        const int sceneSettings = RunSceneFeatureTests();
        const int mapping = RunMaterialMappingTests();
        const int water = RunEditorWaterTests();
        const int project = RunSceneFeatureProjectTests();
        const int tools = RunEditorToolsTests();
        const int runtime = RunEditorAuthoringRuntimeTests();
        return graphics || settings || sceneCpu || sceneSettings || mapping || water || project || tools || runtime ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (argc == 2 && std::strcmp(argv[1], "--ci-log-benchmark") == 0) return RunLogBenchmark();
    if (argc >= 2 && (std::strcmp(argv[1], "--benchmark-assets") == 0 || std::strcmp(argv[1], "--benchmark-editor") == 0))
    {
        const bool assets = std::strcmp(argv[1], "--benchmark-assets") == 0;
        if (argc != (assets ? 4 : 3))
        {
            std::cerr << (assets ? "Usage: --benchmark-assets <PNG-directory> <output.json>" : "Usage: --benchmark-editor <output.json>") << std::endl;
            return EXIT_FAILURE;
        }
        try { return assets ? RunAssetDecodeBenchmarks(argv[2], argv[3]) : RunEditorWorkflowBenchmarks(argv[2]); }
        catch (const std::exception& error) { std::cerr << "Benchmark failed: " << error.what() << std::endl; return EXIT_FAILURE; }
    }
    if (argc >= 2 && std::strcmp(argv[1], "--ci-export-shaders") == 0)
    {
        if (argc < 3 || argc > 4 || (argc == 4 && std::strcmp(argv[3], "dx12") != 0 && std::strcmp(argv[3], "vulkan") != 0))
        {
            std::cerr << "Usage: --ci-export-shaders <directory> [vulkan|dx12]" << std::endl;
            return EXIT_FAILURE;
        }
        const bool vulkan = argc < 4 || std::strcmp(argv[3], "dx12") != 0;
        if (ExportSurfelGIShaderFixtures(argv[2], vulkan) != EXIT_SUCCESS) return EXIT_FAILURE;
        if (ExportPostProcessShaderFixtures(argv[2], vulkan) != EXIT_SUCCESS) return EXIT_FAILURE;
        if (ExportAOEffectShaderFixtures(argv[2], vulkan) != EXIT_SUCCESS) return EXIT_FAILURE;
        if (ExportEditorGraphicsShaderFixtures(argv[2], vulkan) != EXIT_SUCCESS) return EXIT_FAILURE;
        if (ExportSceneFeatureShaderFixtures(argv[2], vulkan) != EXIT_SUCCESS) return EXIT_FAILURE;
        return ExportMaterialMappingShaderFixtures(argv[2], vulkan);
    }

    // Validate external input before creating a window, GPU resources or worker threads.
    std::string editorFlowDirectory, rendererSwitchDirectory, sceneFeaturesDirectory;
    for (int i = 1; i < argc; ++i)
    {
        const bool rendererSwitch = std::strcmp(argv[i], "--ci-renderer-switch") == 0;
        const bool sceneFeatures = std::strcmp(argv[i], "--ci-scene-features") == 0;
        if (!rendererSwitch && !sceneFeatures && std::strcmp(argv[i], "--ci-editor-flow") != 0) continue;
        auto& directory = sceneFeatures ? sceneFeaturesDirectory : rendererSwitch ? rendererSwitchDirectory : editorFlowDirectory;
        if (!directory.empty() || ++i == argc || !argv[i][0] || argv[i][0] == '-')
        { std::cerr << "Usage: --ci-editor-flow|--ci-renderer-switch|--ci-scene-features <owned-output-directory> [-vulkan|-dx12]" << std::endl; return EXIT_FAILURE; }
        try { directory = jEditorAssetPath::ToUtf8(std::filesystem::absolute(jEditorAssetPath::FromUtf8(argv[i]))); }
        catch (const std::exception& error) { std::cerr << error.what() << std::endl; return EXIT_FAILURE; }
    }
    if ((int(!editorFlowDirectory.empty()) + int(!rendererSwitchDirectory.empty()) + int(!sceneFeaturesDirectory.empty()) > 1) ||
        ((!editorFlowDirectory.empty() || !rendererSwitchDirectory.empty() || !sceneFeaturesDirectory.empty()) && gCommandLineArgument.HasArgument("--ci-editor-gpu")))
    { std::cerr << "Select only one editor GPU test" << std::endl; return EXIT_FAILURE; }
    if (!editorFlowDirectory.empty()) SetAuthoringTestStateDirectory(jEditorAssetPath::FromUtf8(editorFlowDirectory) / "workspace-state");
    if (!rendererSwitchDirectory.empty()) SetAuthoringTestStateDirectory(jEditorAssetPath::FromUtf8(rendererSwitchDirectory) / "workspace-state");
    if (!sceneFeaturesDirectory.empty()) SetAuthoringTestStateDirectory(jEditorAssetPath::FromUtf8(sceneFeaturesDirectory) / "workspace-state");
    std::string authoringError;
    if (!ConfigureEditorStartup(argc, argv, authoringError)) { std::cerr << authoringError << std::endl; return EXIT_FAILURE; }
    std::string browserScene, browserLoader;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--scene-browser") != 0) continue;
        if (!browserScene.empty() || i + 2 >= argc || !argv[i + 1][0] || IsAuthoringRuntimeMode())
        { std::cerr << "Usage: --scene-browser <scene-id> <loader> [-dx12|-vulkan]" << std::endl; return EXIT_FAILURE; }
        browserScene = argv[++i];
        browserLoader = argv[++i];
    }
    uint32 frameLimit = 0;
    const auto frameLimitText = ReadEngineEnvironment("JENGINE_MAX_FRAMES");
    if (!frameLimitText.empty() && !jCommandLineArgument::TryParseUInt32(frameLimitText, frameLimit))
    {
        std::cerr << "JENGINE_MAX_FRAMES must be an unsigned 32-bit integer" << std::endl;
        return EXIT_FAILURE;
    }
    float fixedStep = 0.0f;
    const auto fixedStepText = ReadEngineEnvironment("JENGINE_FIXED_TIMESTEP");
    if (!fixedStepText.empty() && !jCommandLineArgument::TryParseNonNegativeFloat(fixedStepText, fixedStep))
    {
        std::cerr << "JENGINE_FIXED_TIMESTEP must be a finite nonnegative number" << std::endl;
        return EXIT_FAILURE;
    }
    bool waitForTestScene = false;
    if (!ReadEngineBoolean("JENGINE_TEST_WAIT_FOR_SCENE", waitForTestScene)) return EXIT_FAILURE;
    if (gCommandLineArgument.HasArgument("--ci-editor-gpu") || !editorFlowDirectory.empty() || !rendererSwitchDirectory.empty() || !sceneFeaturesDirectory.empty()) waitForTestScene = true;
    if (!ReadEngineBoolean("JENGINE_SURFEL_GI", gOptions.UseSurfelGI)
        || !ReadEngineBoolean("JENGINE_INLINE_RAY", gOptions.SurfelGIInlineRayEnable))
        return EXIT_FAILURE;
#ifdef __APPLE__
    bool inlineToggle = false;
    if (!ReadEngineBoolean("JENGINE_TEST_INLINE_TOGGLE", inlineToggle))
        return EXIT_FAILURE;
#endif
    if (gCommandLineArgument.HasArgument("-dx12") && gCommandLineArgument.HasArgument("-vulkan"))
    {
        std::cerr << "Select only one rendering API: -dx12 or -vulkan" << std::endl;
        return EXIT_FAILURE;
    }
#ifndef _WIN32
    if (gCommandLineArgument.HasArgument("-dx12"))
    {
        std::cerr << "DX12 is available only on Windows; use -vulkan" << std::endl;
        return EXIT_FAILURE;
    }
#endif
#ifdef __APPLE__
    const auto effectsAudit = ReadEngineEnvironment("JENGINE_TEST_EFFECTS");
    if (!effectsAudit.empty())
    {
        if (effectsAudit != "odd" && effectsAudit != "tiny")
        {
            std::cerr << "JENGINE_TEST_EFFECTS must be odd or tiny" << std::endl;
            return EXIT_FAILURE;
        }
        SCR_WIDTH = effectsAudit == "odd" ? 1283 : 17;
        SCR_HEIGHT = effectsAudit == "odd" ? 723 : 17;
        gOptions.BloomEyeAdaptation = true;
        gOptions.UseSurfelGI = false;
        gOptions.UseSSGI = true;
        gOptions.UseSSGITemporalAccumulation = true;
        gOptions.UseSSGIReprojection = true;
        gOptions.UseDiscontinuityWeightForSSGI = true;
        gOptions.SSGIResolutionScale = 0.5f;
        gOptions.SSGIDenoiser = EDenoiser::GAUSSIAN_SEPARABLE;
        gOptions.Denoiser = EDenoiser::GAUSSIAN_SEPARABLE;
        gOptions.AOType = 1; // SSAO, independent of RT-pipeline support.
        gOptions.UseResolution = "50";
        gOptions.UseAtmosphericShadowing = true;
    }
#endif
    jLogConfig logConfig;
    const auto logFile = ReadEngineEnvironment("JENGINE_LOG_FILE");
    if (!logFile.empty()) logConfig.FilePath = std::filesystem::path(logFile);
    struct LogSession { ~LogSession() { jLog::Get().Shutdown(); } } logSession;
    if (!jLog::Get().Initialize(logConfig))
    {
        std::cerr << "Log initialization failed: " << jLog::Get().GetLastError() << std::endl;
        return EXIT_FAILURE;
    }
    jLog::Get().Write(EjLogLevel::Info, "Engine", "Starting jEngine");
	GMainThreadID = std::this_thread::get_id();

	g_Engine = new jEngine();
    if (!browserScene.empty() && !g_Engine->Game.ConfigureSceneBrowserStartup(browserScene, browserLoader, authoringError))
    { std::cerr << authoringError << std::endl; return EXIT_FAILURE; }
	g_Engine->PreInit();

	if (!g_rhi->InitRHI())
    {
        jLog::Get().Write(EjLogLevel::Error, "RHI", "Graphics initialization failed");
        return EXIT_FAILURE;
    }

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
        glfwSetWindowCloseCallback(window, [](GLFWwindow* requested) {
            if (!RequestAuthoringWindowClose()) glfwSetWindowShouldClose(requested, GLFW_FALSE);
        });
	}

	//int major, minor, rev;
	//glfwGetVersion(&major, &minor, &rev);
	//auto versionCheck = glfwGetVersionString();

	// glfwSwapInterval(0);		// 0 is no limit fps

#ifdef __APPLE__
    if (std::getenv("JENGINE_TEST_SURFEL_RECONFIGURE")) UpdateSurfelGIAuditOptions(0);
#endif
	g_Engine->Init();
#ifdef ENABLE_EDITOR_FEATURES
    if (g_Editor && !InitializeEditorStartup(*g_Editor, authoringError))
    {
        std::cerr << "Editor startup: " << authoringError << std::endl;
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate(); return EXIT_FAILURE;
    }
#endif
    if (!browserScene.empty() && HasEditorStartupScene() &&
        !g_Engine->Game.ConfigureSceneBrowserStartup(browserScene, browserLoader, authoringError))
    {
        std::cerr << authoringError << std::endl;
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate(); return EXIT_FAILURE;
    }
    if (waitForTestScene && !g_Engine->Game.WaitForSceneImportForTesting())
    {
        jLog::Get().Write(EjLogLevel::Error, "Assets", "GPU test scene import did not complete successfully");
        g_Engine->Release();
        g_rhi->ReleaseRHI();
        glfwTerminate();
        return EXIT_FAILURE;
    }
#ifdef ENABLE_EDITOR_FEATURES
    if (waitForTestScene && g_Editor && !WaitForEditorImports(*g_Editor, 60000, authoringError))
    {
        std::cerr << authoringError << std::endl;
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate(); return EXIT_FAILURE;
    }
#endif
    if (!sceneFeaturesDirectory.empty())
    {
        const int failures = RunSceneFeatureIntegrationGpuTests(sceneFeaturesDirectory.c_str());
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate();
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (!rendererSwitchDirectory.empty())
    {
        const int failures = RunRendererSwitchGpuTests(rendererSwitchDirectory.c_str());
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate();
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (!editorFlowDirectory.empty())
    {
        const int failures = RunEditorAuthoringGpuTests(editorFlowDirectory.c_str());
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate();
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (gCommandLineArgument.HasArgument("--ci-editor-gpu"))
    {
        const int failures = RunModelUploadGpuTests() + RunEditorParticleMaterialGpuTests();
        g_Engine->Release(); g_rhi->ReleaseRHI(); glfwTerminate();
        return failures ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    std::cout << "SurfelGI: " << gOptions.UseSurfelGI << "; inline ray option: " << gOptions.SurfelGIInlineRayEnable << std::endl;
    uint32 renderedFrames = 0;
    std::cout << "jEngine initialized: " << g_rhi->GetRHIName().ToStr() << std::endl;
    jLog::Get().Write(EjLogLevel::Info, "Engine", std::string("Initialized: ") + g_rhi->GetRHIName().ToStr());

	// render loop
	// -----------
	#ifdef _WIN32
	if (IsUseDX12())
	{
		MSG msg = {};
		while (::IsWindow((HWND)g_rhi->GetWindow()) && msg.message != WM_QUIT)
		{
			static std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
			std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
			std::chrono::duration<double> elapsed_seconds = currentTime - lastTime;
			g_timeDeltaSecond = (float)elapsed_seconds.count();
			lastTime = currentTime;

            if (fixedStep > 0) g_timeDeltaSecond = fixedStep;

            g_Engine->ProcessInput(g_timeDeltaSecond);
                
			g_rhi->IncrementFrameNumber();
			g_Engine->Update(g_timeDeltaSecond);
			g_Engine->Draw();
			g_Engine->EndOfFrame();

            ++renderedFrames;
            if (renderedFrames == 1 || renderedFrames % 120 == 0)
                std::cout << "Rendered frames: " << renderedFrames << std::endl;
            if (g_Engine->Game.IsSceneBrowserRestartRequested() || (frameLimit && renderedFrames >= frameLimit))
                break;

			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
					break;
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
			static std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
			std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
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
            if (!effectsAudit.empty() && (renderedFrames == 1 || renderedFrames == 2 || renderedFrames == 90)) DumpEffectAuditDiagnostics();
            if (std::getenv("JENGINE_TEST_SURFEL_RECONFIGURE") && renderedFrames == 100) UpdateSurfelGIAuditOptions(renderedFrames);
            if (std::getenv("JENGINE_TEST_SURFEL_RECONFIGURE") && (renderedFrames == 30 || renderedFrames == 60 || renderedFrames == 90 || renderedFrames == 110))
            {
                DumpSurfelGIDiagnostics();
                UpdateSurfelGIAuditOptions(renderedFrames);
            }
            if (inlineToggle && (renderedFrames == 30 || renderedFrames == 60)) {
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

#ifdef _WIN32
    const auto restartScene = g_Engine->Game.GetSceneBrowserRestartArguments();
    const auto restartProject = restartScene.empty() ? std::filesystem::path{} : GetAuthoringProjectFile();
#endif
	g_Engine->Release();

	g_rhi->ReleaseRHI();

	// glfw: terminate, clearing all previously allocated GLFW resources.
	// ------------------------------------------------------------------
	glfwTerminate();
    std::cout << "jEngine clean shutdown; rendered frames: " << renderedFrames << std::endl;
    jLog::Get().Write(EjLogLevel::Info, "Engine", "Clean shutdown; rendered frames: " + std::to_string(renderedFrames));
    if (!jLog::Get().Shutdown())
    {
        std::cerr << "Log shutdown failed: " << jLog::Get().GetLastError() << std::endl;
        return EXIT_FAILURE;
    }
#ifdef _WIN32
    if (!restartScene.empty() && !RestartForSceneBrowser(restartScene, restartProject))
    {
        std::cerr << "Could not restart jEngine for Scene Browser (Windows error " << GetLastError() << ")" << std::endl;
        return EXIT_FAILURE;
    }
#endif
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
	ImGui_ImplGlfw_CharCallback(window, codepoint);
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
