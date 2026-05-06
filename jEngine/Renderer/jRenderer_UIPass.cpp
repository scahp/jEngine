#include "pch.h"
#include "jRenderer.h"
#include "ImGui/jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"
#include "jEngine.h"
#include "RHI/jRaytracingScene.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "Scene/jCamera.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <sstream>

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

// Helper functions for Copy/Paste context menus
namespace
{
    struct jUIPersistedState
    {
        bool ShowEditorUI = true;
        bool ShowMainPanel = true;
        bool FeaturePanelAutoHidden = false;
        bool ShowSceneBrowserPanel = true;
        bool ShowPathTracingPanel = false;
        bool ShowSurfelGIPanel = true;
        bool ShowAtmospherePanel = false;
        bool ShowDebugVisualizationPanel = false;
        bool ShowHWRTPanel = false;
        bool ShowSSGIPanel = false;
        bool ShowAOPanel = false;
        bool ShowLightPanel = false;
        bool ShowProfilerPanel = false;
        bool ShowPlacementPanel = false;
        std::string MainActiveTab = "Status";
        std::string FeatureActiveTab = "Scene Browser";
        float MainPanelWidth = 280.0f;
        float FeaturePanelWidth = 318.75f;
    };

    std::string TrimUIString(const std::string& InValue)
    {
        const size_t begin = InValue.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return {};
        const size_t end = InValue.find_last_not_of(" \t\r\n");
        return InValue.substr(begin, end - begin + 1);
    }

    std::string ToLowerUIString(std::string InValue)
    {
        std::transform(InValue.begin(), InValue.end(), InValue.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return InValue;
    }

    bool TryGetUIIniSectionName(const std::string& InLine, std::string& OutSectionName)
    {
        const std::string trimmed = TrimUIString(InLine);
        if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']')
            return false;
        OutSectionName = ToLowerUIString(TrimUIString(trimmed.substr(1, trimmed.size() - 2)));
        return true;
    }

    bool ParseUIBool(const std::string& InValue, bool InDefaultValue)
    {
        const std::string value = ToLowerUIString(TrimUIString(InValue));
        if (value == "1" || value == "true" || value == "yes" || value == "on")
            return true;
        if (value == "0" || value == "false" || value == "no" || value == "off")
            return false;
        return InDefaultValue;
    }

    float ParseUIFloat(const std::string& InValue, float InDefaultValue)
    {
        try
        {
            return std::stof(TrimUIString(InValue));
        }
        catch (...)
        {
            return InDefaultValue;
        }
    }

    void LoadUIPersistedState(jUIPersistedState& OutState)
    {
        std::ifstream inputFile("jengine.ini");
        if (!inputFile.is_open())
            return;

        bool isUISection = false;
        std::string line;
        while (std::getline(inputFile, line))
        {
            std::string sectionName;
            if (TryGetUIIniSectionName(line, sectionName))
            {
                isUISection = (sectionName == "ui");
                continue;
            }

            if (!isUISection)
                continue;

            const size_t separatorPos = line.find('=');
            if (separatorPos == std::string::npos)
                continue;

            const std::string key = ToLowerUIString(TrimUIString(line.substr(0, separatorPos)));
            const std::string value = TrimUIString(line.substr(separatorPos + 1));
            if (key == "showeditorui") OutState.ShowEditorUI = ParseUIBool(value, OutState.ShowEditorUI);
            else if (key == "showmainpanel") OutState.ShowMainPanel = ParseUIBool(value, OutState.ShowMainPanel);
            else if (key == "featurepanelautohidden") OutState.FeaturePanelAutoHidden = ParseUIBool(value, OutState.FeaturePanelAutoHidden);
            else if (key == "showscenebrowserpanel") OutState.ShowSceneBrowserPanel = ParseUIBool(value, OutState.ShowSceneBrowserPanel);
            else if (key == "showpathtracingpanel") OutState.ShowPathTracingPanel = ParseUIBool(value, OutState.ShowPathTracingPanel);
            else if (key == "showsurfelgipanel") OutState.ShowSurfelGIPanel = ParseUIBool(value, OutState.ShowSurfelGIPanel);
            else if (key == "showatmospherepanel") OutState.ShowAtmospherePanel = ParseUIBool(value, OutState.ShowAtmospherePanel);
            else if (key == "showdebugvisualizationpanel") OutState.ShowDebugVisualizationPanel = ParseUIBool(value, OutState.ShowDebugVisualizationPanel);
            else if (key == "showhwrtpanel") OutState.ShowHWRTPanel = ParseUIBool(value, OutState.ShowHWRTPanel);
            else if (key == "showssgipanel") OutState.ShowSSGIPanel = ParseUIBool(value, OutState.ShowSSGIPanel);
            else if (key == "showaopanel") OutState.ShowAOPanel = ParseUIBool(value, OutState.ShowAOPanel);
            else if (key == "showlightpanel") OutState.ShowLightPanel = ParseUIBool(value, OutState.ShowLightPanel);
            else if (key == "showprofilerpanel") OutState.ShowProfilerPanel = ParseUIBool(value, OutState.ShowProfilerPanel);
            else if (key == "showplacementpanel") OutState.ShowPlacementPanel = ParseUIBool(value, OutState.ShowPlacementPanel);
            else if (key == "mainactivetab") OutState.MainActiveTab = value;
            else if (key == "featureactivetab") OutState.FeatureActiveTab = value;
            else if (key == "mainpanelwidth") OutState.MainPanelWidth = ParseUIFloat(value, OutState.MainPanelWidth);
            else if (key == "featurepanelwidth") OutState.FeaturePanelWidth = ParseUIFloat(value, OutState.FeaturePanelWidth);
        }
    }

    void SaveUIPersistedState(const jUIPersistedState& InState)
    {
        std::vector<std::string> preservedLines;
        std::ifstream inputFile("jengine.ini");
        if (inputFile.is_open())
        {
            bool isFilteredSection = false;
            std::string line;
            while (std::getline(inputFile, line))
            {
                std::string sectionName;
                if (TryGetUIIniSectionName(line, sectionName))
                {
                    isFilteredSection = (sectionName == "ui");
                    if (isFilteredSection)
                        continue;
                }
                if (!isFilteredSection)
                    preservedLines.push_back(line);
            }
        }

        std::ofstream outputFile("jengine.ini", std::ios::trunc);
        if (!outputFile.is_open())
            return;

        for (const std::string& line : preservedLines)
            outputFile << line << '\n';
        if (!preservedLines.empty() && !preservedLines.back().empty())
            outputFile << '\n';

        outputFile << "[UI]\n";
        outputFile << "ShowEditorUI=" << (InState.ShowEditorUI ? 1 : 0) << '\n';
        outputFile << "ShowMainPanel=" << (InState.ShowMainPanel ? 1 : 0) << '\n';
        outputFile << "FeaturePanelAutoHidden=" << (InState.FeaturePanelAutoHidden ? 1 : 0) << '\n';
        outputFile << "ShowSceneBrowserPanel=" << (InState.ShowSceneBrowserPanel ? 1 : 0) << '\n';
        outputFile << "ShowPathTracingPanel=" << (InState.ShowPathTracingPanel ? 1 : 0) << '\n';
        outputFile << "ShowSurfelGIPanel=" << (InState.ShowSurfelGIPanel ? 1 : 0) << '\n';
        outputFile << "ShowAtmospherePanel=" << (InState.ShowAtmospherePanel ? 1 : 0) << '\n';
        outputFile << "ShowDebugVisualizationPanel=" << (InState.ShowDebugVisualizationPanel ? 1 : 0) << '\n';
        outputFile << "ShowHWRTPanel=" << (InState.ShowHWRTPanel ? 1 : 0) << '\n';
        outputFile << "ShowSSGIPanel=" << (InState.ShowSSGIPanel ? 1 : 0) << '\n';
        outputFile << "ShowAOPanel=" << (InState.ShowAOPanel ? 1 : 0) << '\n';
        outputFile << "ShowLightPanel=" << (InState.ShowLightPanel ? 1 : 0) << '\n';
        outputFile << "ShowProfilerPanel=" << (InState.ShowProfilerPanel ? 1 : 0) << '\n';
        outputFile << "ShowPlacementPanel=" << (InState.ShowPlacementPanel ? 1 : 0) << '\n';
        outputFile << "MainActiveTab=" << InState.MainActiveTab << '\n';
        outputFile << "FeatureActiveTab=" << InState.FeatureActiveTab << '\n';
        outputFile << "MainPanelWidth=" << InState.MainPanelWidth << '\n';
        outputFile << "FeaturePanelWidth=" << InState.FeaturePanelWidth << '\n';
    }

    bool operator==(const jUIPersistedState& Lhs, const jUIPersistedState& Rhs)
    {
        return Lhs.ShowEditorUI == Rhs.ShowEditorUI
            && Lhs.ShowMainPanel == Rhs.ShowMainPanel
            && Lhs.FeaturePanelAutoHidden == Rhs.FeaturePanelAutoHidden
            && Lhs.ShowSceneBrowserPanel == Rhs.ShowSceneBrowserPanel
            && Lhs.ShowPathTracingPanel == Rhs.ShowPathTracingPanel
            && Lhs.ShowSurfelGIPanel == Rhs.ShowSurfelGIPanel
            && Lhs.ShowAtmospherePanel == Rhs.ShowAtmospherePanel
            && Lhs.ShowDebugVisualizationPanel == Rhs.ShowDebugVisualizationPanel
            && Lhs.ShowHWRTPanel == Rhs.ShowHWRTPanel
            && Lhs.ShowSSGIPanel == Rhs.ShowSSGIPanel
            && Lhs.ShowAOPanel == Rhs.ShowAOPanel
            && Lhs.ShowLightPanel == Rhs.ShowLightPanel
            && Lhs.ShowProfilerPanel == Rhs.ShowProfilerPanel
            && Lhs.ShowPlacementPanel == Rhs.ShowPlacementPanel
            && Lhs.MainActiveTab == Rhs.MainActiveTab
            && Lhs.FeatureActiveTab == Rhs.FeatureActiveTab
            && std::fabs(Lhs.MainPanelWidth - Rhs.MainPanelWidth) < 0.5f
            && std::fabs(Lhs.FeaturePanelWidth - Rhs.FeaturePanelWidth) < 0.5f;
    }

    bool operator!=(const jUIPersistedState& Lhs, const jUIPersistedState& Rhs)
    {
        return !(Lhs == Rhs);
    }

	bool IsCurrentProcessElevated()
	{
#if defined(_WIN32)
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD size = 0;
		const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
		CloseHandle(token);
		return ok && (elevation.TokenIsElevated != 0);
#else
		return true;
#endif
	}

	bool LaunchTracyProfiler(std::string& OutResolvedPath, std::string& OutLaunchArgs, std::string& OutError)
	{
#if defined(_WIN32)
		namespace fs = std::filesystem;

		char modulePath[MAX_PATH] = {};
		const DWORD modulePathLen = GetModuleFileNameA(nullptr, modulePath, (DWORD)_countof(modulePath));
		if (modulePathLen == 0)
		{
			OutError = "Failed to get module path.";
			return false;
		}

		fs::path exeDir = fs::path(modulePath).parent_path();
		std::vector<fs::path> candidates;
		candidates.emplace_back(exeDir / ".." / ".." / "External" / "tracy" / "profiler" / "build_vs2022" / "Release" / "tracy-profiler.exe");
		candidates.emplace_back(exeDir / ".." / ".." / ".." / "External" / "tracy" / "profiler" / "build_vs2022" / "Release" / "tracy-profiler.exe");
		candidates.emplace_back(fs::current_path() / "External" / "tracy" / "profiler" / "build_vs2022" / "Release" / "tracy-profiler.exe");

		for (const fs::path& candidate : candidates)
		{
			std::error_code ec;
			const fs::path normalized = fs::weakly_canonical(candidate, ec);
			const fs::path finalPath = ec ? candidate : normalized;

			if (!fs::exists(finalPath))
				continue;

			OutResolvedPath = finalPath.string();
			// Tracy profiler supports immediate connect via command line: -a <address> [-p <port>]
			OutLaunchArgs = "-a 127.0.0.1 -p 8086";
			HINSTANCE result = ShellExecuteA(nullptr, "open", OutResolvedPath.c_str(), OutLaunchArgs.c_str(), finalPath.parent_path().string().c_str(), SW_SHOWNORMAL);
			if ((INT_PTR)result <= 32)
			{
				OutError = "Failed to launch tracy-profiler.exe.";
				return false;
			}
			return true;
		}

		OutError = "tracy-profiler.exe not found in relative candidate paths.";
		return false;
#else
		OutResolvedPath.clear();
		OutLaunchArgs.clear();
		OutError = "Trace connect launch is only supported on Windows.";
		return false;
#endif
	}

	// Usage: AddCopyPasteContextMenu("UniqueID", variableName);
	// Call this right after ImGui::SliderFloat() or similar UI elements

	// Float value Copy/Paste
	inline void AddCopyPasteContextMenu(const char* contextId, float& value)
	{
		if (ImGui::BeginPopupContextItem(contextId))
		{
			if (ImGui::MenuItem("Copy"))
			{
				char buffer[128];
				sprintf_s(buffer, "%.6f", value);
				ImGui::SetClipboardText(buffer);
			}
			const char* clipText = ImGui::GetClipboardText();
			if (clipText && strlen(clipText) > 0)
			{
				if (ImGui::MenuItem("Paste"))
				{
					float parsedValue;
					if (sscanf_s(clipText, "%f", &parsedValue) == 1)
					{
						value = parsedValue;
					}
				}
			}
			ImGui::EndPopup();
		}
	}

	// Vector3 Copy/Paste
	inline void AddCopyPasteContextMenu(const char* contextId, Vector& value)
	{
		if (ImGui::BeginPopupContextItem(contextId))
		{
			if (ImGui::MenuItem("Copy"))
			{
				char buffer[128];
				sprintf_s(buffer, "%.6f, %.6f, %.6f", value.x, value.y, value.z);
				ImGui::SetClipboardText(buffer);
			}
			const char* clipText = ImGui::GetClipboardText();
			if (clipText && strlen(clipText) > 0)
			{
				if (ImGui::MenuItem("Paste"))
				{
					float x, y, z;
					if (sscanf_s(clipText, "%f, %f, %f", &x, &y, &z) == 3 || sscanf_s(clipText, "%f,%f,%f", &x, &y, &z) == 3)
					{
						value.x = x;
						value.y = y;
						value.z = z;
					}
				}
			}
			ImGui::EndPopup();
		}
	}

	// Float Copy/Paste with custom callback
	// Usage: AddCopyPasteContextMenu("ID", value, [](float newValue) { /* custom logic */ });
	template<typename Func>
	inline void AddCopyPasteContextMenu(const char* contextId, float value, Func&& onPaste)
	{
		if (ImGui::BeginPopupContextItem(contextId))
		{
			if (ImGui::MenuItem("Copy"))
			{
				char buffer[128];
				sprintf_s(buffer, "%.6f", value);
				ImGui::SetClipboardText(buffer);
			}
			const char* clipText = ImGui::GetClipboardText();
			if (clipText && strlen(clipText) > 0)
			{
				if (ImGui::MenuItem("Paste"))
				{
					float parsedValue;
					if (sscanf_s(clipText, "%f", &parsedValue) == 1)
					{
						onPaste(parsedValue);
					}
				}
			}
			ImGui::EndPopup();
		}
	}

	// Vector3 Copy/Paste with custom callback
	template<typename Func>
	inline void AddCopyPasteContextMenu(const char* contextId, Vector& value, Func&& onPaste)
	{
		if (ImGui::BeginPopupContextItem(contextId))
		{
			if (ImGui::MenuItem("Copy"))
			{
				char buffer[128];
				sprintf_s(buffer, "%.6f, %.6f, %.6f", value.x, value.y, value.z);
				ImGui::SetClipboardText(buffer);
			}
			const char* clipText = ImGui::GetClipboardText();
			if (clipText && strlen(clipText) > 0)
			{
				if (ImGui::MenuItem("Paste"))
				{
					float x, y, z;
					if (sscanf_s(clipText, "%f, %f, %f", &x, &y, &z) == 3 || sscanf_s(clipText, "%f,%f,%f", &x, &y, &z) == 3)
					{
						onPaste(x, y, z);
					}
				}
			}
			ImGui::EndPopup();
		}
	}

	// ===== USAGE EXAMPLES =====
	//
	// Example 1: Simple float value Copy/Paste
	//   ImGui::SliderFloat("MyValue", &gOptions.MyValue, 0.0f, 10.0f);
	//   AddCopyPasteContextMenu("MyValueContext", gOptions.MyValue);
	//
	// Example 2: Simple Vector3 Copy/Paste
	//   ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
	//   AddCopyPasteContextMenu("PositionContext", pos);
	//
	// Example 3: Float with custom paste callback
	//   ImGui::SliderFloat("Intensity", &value, 0.0f, 100.0f);
	//   AddCopyPasteContextMenu("IntensityContext", value, [](float parsedValue) {
	//       value = std::max(0.0f, parsedValue); // Apply constraints
	//   });
	//
	// Example 4: Vector3 with custom paste callback (e.g., Camera Position)
	//   ImGui::Text("Camera Pos: %.2f, %.2f, %.2f", camPos.x, camPos.y, camPos.z);
	//   AddCopyPasteContextMenu("CameraPosContext", camPos, [](float x, float y, float z) {
	//       auto mainCamera = jCamera::GetMainCamera();
	//       if (mainCamera) {
	//           Vector newPos(x, y, z);
	//           Vector offset = newPos - mainCamera->Pos;
	//           mainCamera->Pos = newPos;
	//           mainCamera->Target += offset;
	//           mainCamera->Up += offset;
	//       }
	//   });
}

void IRenderer::UIPass()
{
	check(g_ImGUI);
    const bool HasRenderFrameContext = (RenderFrameContextPtr != nullptr);
    const bool HasRaytracingScene = HasRenderFrameContext && (RenderFrameContextPtr->RaytracingScene != nullptr);
    const bool IsRaytracingSceneValid = HasRaytracingScene && RenderFrameContextPtr->RaytracingScene->IsValid();
    const bool IsForwardRenderer = HasRenderFrameContext && RenderFrameContextPtr->UseForwardRenderer;
    const bool IsHWRTInlineSupported = GSupportInlineRaytracing;
    if (!IsHWRTInlineSupported && gOptions.HWRTDirectLightingMode != 0)
    {
        gOptions.HWRTDirectLightingMode = 0;
    }
    int32 ResolvedHWRTDirectLightingMode = Clamp(gOptions.HWRTDirectLightingMode, 0, 1);
    if (ResolvedHWRTDirectLightingMode == 1 && !IsHWRTInlineSupported)
    {
        ResolvedHWRTDirectLightingMode = 0;
    }
    const bool IsHWRTDirectLightingActive = gOptions.UseHWRTDirectLighting
        && gOptions.UseRaytracing
        && GSupportRaytracing
        && HasRenderFrameContext
        && HasRaytracingScene
        && IsRaytracingSceneValid
        && !IsForwardRenderer;

	g_ImGUI->NewFrame([=]()
	{
		Vector4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

        static bool bShowEditorUI = true;
        static bool bSelectMainStatusTab = false;
        static bool bSelectMainProfilingTab = false;
        static bool bSelectMainRenderTab = false;
        static bool bSelectSceneBrowserFeatureTab = false;
        static bool bSelectPathTracingFeatureTab = false;
        static bool bSelectSurfelGIFeatureTab = false;
        static bool bSelectAtmosphereFeatureTab = false;
        static bool bSelectDebugVisualizationFeatureTab = false;
        static bool bSelectHWRTFeatureTab = false;
        static bool bSelectSSGIFeatureTab = false;
        static bool bSelectAOFeatureTab = false;
        static bool bSelectLightFeatureTab = false;
        static bool bSelectProfilerFeatureTab = false;
        static bool bSelectPlacementFeatureTab = false;
        static bool bWasUsingPathTracingRenderer = false;
        const bool bIsUsingPathTracingRenderer = (g_Engine && g_Engine->Game.IsUsingPathTracingRenderer());
        static bool bShowMainPanel = true;
        static bool bShowRenderOptionsPanel = false;
        static bool bShowSceneBrowserPanel = true;
        static bool bShowPathTracingPanel = false;
        static bool bShowSurfelGIPanel = true;
        static bool bShowAtmospherePanel = false;
        static bool bShowDebugVisualizationPanel = false;
        static bool bShowHWRTPanel = false;
        static bool bShowSSGIPanel = false;
        static bool bShowAOPanel = false;
        static bool bShowLightPanel = false;
        static bool bShowProfilerPanel = false;
        static bool bShowPlacementPanel = false;
        static bool bFeaturePanelAutoHidden = false;
        static bool bUIStateLoaded = false;
        static jUIPersistedState LastSavedUIState;
        static std::string MainActiveTab = "Status";
        static std::string FeatureActiveTab = "Scene Browser";
        static float MainPanelWidth = 280.0f;
        static float FeaturePanelWidth = 318.75f;
        if (!bUIStateLoaded)
        {
            jUIPersistedState loadedState;
            LoadUIPersistedState(loadedState);
            bShowEditorUI = loadedState.ShowEditorUI;
            bShowMainPanel = loadedState.ShowMainPanel;
            bFeaturePanelAutoHidden = loadedState.FeaturePanelAutoHidden;
            bShowSceneBrowserPanel = loadedState.ShowSceneBrowserPanel;
            bShowPathTracingPanel = loadedState.ShowPathTracingPanel;
            bShowSurfelGIPanel = loadedState.ShowSurfelGIPanel;
            bShowAtmospherePanel = loadedState.ShowAtmospherePanel;
            bShowDebugVisualizationPanel = loadedState.ShowDebugVisualizationPanel;
            bShowHWRTPanel = loadedState.ShowHWRTPanel;
            bShowSSGIPanel = loadedState.ShowSSGIPanel;
            bShowAOPanel = loadedState.ShowAOPanel;
            bShowLightPanel = loadedState.ShowLightPanel;
            bShowProfilerPanel = loadedState.ShowProfilerPanel;
            bShowPlacementPanel = loadedState.ShowPlacementPanel;
            MainActiveTab = loadedState.MainActiveTab.empty() ? "Status" : loadedState.MainActiveTab;
            FeatureActiveTab = loadedState.FeatureActiveTab.empty() ? "Scene Browser" : loadedState.FeatureActiveTab;
            MainPanelWidth = Max(220.0f, loadedState.MainPanelWidth);
            FeaturePanelWidth = Max(318.75f, loadedState.FeaturePanelWidth);
            bSelectMainStatusTab = (MainActiveTab == "Status");
            bSelectMainProfilingTab = (MainActiveTab == "Profiling");
            bSelectMainRenderTab = (MainActiveTab == "Render");
            bSelectSceneBrowserFeatureTab = (FeatureActiveTab == "Scene Browser");
            bSelectPathTracingFeatureTab = (FeatureActiveTab == "PathTracing");
            bSelectSurfelGIFeatureTab = (FeatureActiveTab == "SurfelGI");
            bSelectAtmosphereFeatureTab = (FeatureActiveTab == "Atmosphere");
            bSelectDebugVisualizationFeatureTab = (FeatureActiveTab == "Debug Visualization");
            bSelectHWRTFeatureTab = (FeatureActiveTab == "HWRT");
            bSelectSSGIFeatureTab = (FeatureActiveTab == "SSGI");
            bSelectAOFeatureTab = (FeatureActiveTab == "AO");
            bSelectLightFeatureTab = (FeatureActiveTab == "Light");
            bSelectProfilerFeatureTab = (FeatureActiveTab == "Profiler");
            bSelectPlacementFeatureTab = (FeatureActiveTab == "Placement Tool");
            LastSavedUIState = loadedState;
            bUIStateLoaded = true;
        }
        if (!GSupportRaytracing)
            bShowAOPanel = false;
        if (bIsUsingPathTracingRenderer && !bWasUsingPathTracingRenderer)
        {
            bShowPathTracingPanel = true;
            bSelectPathTracingFeatureTab = true;
        }
        bWasUsingPathTracingRenderer = bIsUsingPathTracingRenderer;

		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const ImVec2 workPos = mainViewport ? mainViewport->WorkPos : ImVec2(0.0f, 0.0f);
		const ImVec2 workSize = mainViewport ? mainViewport->WorkSize : ImVec2(1280.0f, 720.0f);
        const float toolbarHeight = ImGui::GetFrameHeight() + 2.0f;
		const float margin = 6.0f;
        const float minMainPanelWidth = 220.0f;
        const float minFeaturePanelWidth = 318.75f;
        const float maxMainPanelWidth = Max(minMainPanelWidth, workSize.x - FeaturePanelWidth - margin * 4.0f);
        MainPanelWidth = Clamp(MainPanelWidth, minMainPanelWidth, maxMainPanelWidth);
        const float mainPanelWidth = MainPanelWidth;
        const float maxFeaturePanelWidth = Max(minFeaturePanelWidth, workSize.x - mainPanelWidth - margin * 4.0f);
        FeaturePanelWidth = Clamp(FeaturePanelWidth, minFeaturePanelWidth, maxFeaturePanelWidth);
		const float sidePanelWidth = FeaturePanelWidth;
		const float sidePanelX = workPos.x + Max(360.0f, workSize.x - sidePanelWidth - margin);
		const float renderOptionsX = workPos.x + mainPanelWidth + margin * 2.0f;
		const float renderOptionsWidth = Max(360.0f, sidePanelX - renderOptionsX - margin);
		const float topPanelY = workPos.y + toolbarHeight + 6.0f;
		const float panelBottomY = workPos.y + workSize.y - 6.0f;

		const ImGuiWindowFlags dockPanelFlags = ImGuiWindowFlags_None;
        const ImGuiWindowFlags mainPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
        const ImGuiWindowFlags featurePanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
		const ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar;
        const ImGuiWindowFlags autoHideFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        auto DrawFeatureMenuItem = [](const char* label, bool& bShowPanel, bool& bSelectTab)
        {
            const bool bWasVisible = bShowPanel;
            if (ImGui::MenuItem(label, nullptr, &bShowPanel) && bShowPanel)
            {
                bFeaturePanelAutoHidden = false;
                if (!bWasVisible)
                    bSelectTab = true;
            }
        };
        auto ConsumeFeatureTabFlags = [](bool& bSelectTab)
        {
            const ImGuiTabItemFlags flags = bSelectTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            bSelectTab = false;
            return flags;
        };
        auto PersistUIStateIfNeeded = [&]()
        {
            jUIPersistedState currentState;
            currentState.ShowEditorUI = bShowEditorUI;
            currentState.ShowMainPanel = bShowMainPanel;
            currentState.FeaturePanelAutoHidden = bFeaturePanelAutoHidden;
            currentState.ShowSceneBrowserPanel = bShowSceneBrowserPanel;
            currentState.ShowPathTracingPanel = bShowPathTracingPanel;
            currentState.ShowSurfelGIPanel = bShowSurfelGIPanel;
            currentState.ShowAtmospherePanel = bShowAtmospherePanel;
            currentState.ShowDebugVisualizationPanel = bShowDebugVisualizationPanel;
            currentState.ShowHWRTPanel = bShowHWRTPanel;
            currentState.ShowSSGIPanel = bShowSSGIPanel;
            currentState.ShowAOPanel = bShowAOPanel;
            currentState.ShowLightPanel = bShowLightPanel;
            currentState.ShowProfilerPanel = bShowProfilerPanel;
            currentState.ShowPlacementPanel = bShowPlacementPanel;
            currentState.MainActiveTab = MainActiveTab;
            currentState.FeatureActiveTab = FeatureActiveTab;
            currentState.MainPanelWidth = MainPanelWidth;
            currentState.FeaturePanelWidth = FeaturePanelWidth;
            if (currentState != LastSavedUIState)
            {
                SaveUIPersistedState(currentState);
                LastSavedUIState = currentState;
            }
        };

        if (!bShowEditorUI)
        {
            ImGui::SetNextWindowPos(ImVec2(workPos.x + margin, topPanelY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(58.0f, ImGui::GetFrameHeight() + 8.0f), ImGuiCond_Always);
            if (ImGui::Begin("UI Auto Hide", nullptr, autoHideFlags))
            {
                if (ImGui::Button("UI >", ImVec2(-1.0f, 0.0f)))
                    bShowEditorUI = true;
            }
            ImGui::End();
            PersistUIStateIfNeeded();
            jConsole::Get().Render();
            return;
        }

		ImGui::SetNextWindowPos(workPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(workSize.x, toolbarHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 0.0f));
		if (ImGui::Begin("Panel Launcher", nullptr, barFlags))
		{
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Main"))
				{
                    if (ImGui::MenuItem("Open", nullptr, bShowMainPanel))
                        bShowMainPanel = true;
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Scene Browser"))
				{
                    DrawFeatureMenuItem("Scene Browser", bShowSceneBrowserPanel, bSelectSceneBrowserFeatureTab);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Features"))
				{
                    DrawFeatureMenuItem("Scene Browser", bShowSceneBrowserPanel, bSelectSceneBrowserFeatureTab);
#if USE_PATH_TRACING
                    DrawFeatureMenuItem("PathTracing", bShowPathTracingPanel, bSelectPathTracingFeatureTab);
#endif
                    DrawFeatureMenuItem("SurfelGI", bShowSurfelGIPanel, bSelectSurfelGIFeatureTab);
                    DrawFeatureMenuItem("Atmosphere", bShowAtmospherePanel, bSelectAtmosphereFeatureTab);
                    DrawFeatureMenuItem("Debug Visualization", bShowDebugVisualizationPanel, bSelectDebugVisualizationFeatureTab);
                    DrawFeatureMenuItem("HWRT", bShowHWRTPanel, bSelectHWRTFeatureTab);
                    DrawFeatureMenuItem("SSGI", bShowSSGIPanel, bSelectSSGIFeatureTab);
                    if (GSupportRaytracing)
                        DrawFeatureMenuItem("AO", bShowAOPanel, bSelectAOFeatureTab);
                    DrawFeatureMenuItem("Light", bShowLightPanel, bSelectLightFeatureTab);
                    DrawFeatureMenuItem("Profiler", bShowProfilerPanel, bSelectProfilerFeatureTab);
#ifdef ENABLE_EDITOR_FEATURES
                    DrawFeatureMenuItem("Placement Tool", bShowPlacementPanel, bSelectPlacementFeatureTab);
#endif
					ImGui::EndMenu();
				}

                if (ImGui::Button("Hide UI"))
                    bShowEditorUI = false;
                ImGui::SameLine();
				const float fps = ImGui::GetIO().Framerate;
				const float frameMs = fps > 0.0f ? (1000.0f / fps) : 0.0f;
				const float infoWidth = ImGui::CalcTextSize("VSync  000.0 ms (000.0 FPS)").x;
                ImGui::SameLine(Max(ImGui::GetCursorPosX() + 20.0f, ImGui::GetWindowContentRegionMax().x - infoWidth - 36.0f));
				ImGui::Checkbox("VSync", &GUseVsync);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.2f, 1.0f), "%.1f ms (%.1f FPS)", frameMs, fps);
				ImGui::EndMenuBar();
			}
		}
		ImGui::End();
        ImGui::PopStyleVar();

        if (!bShowMainPanel)
        {
            ImGui::SetNextWindowPos(ImVec2(workPos.x + margin, topPanelY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(78.0f, ImGui::GetFrameHeight() + 8.0f), ImGuiCond_Always);
            if (ImGui::Begin("Main Auto Hide", nullptr, autoHideFlags))
            {
                if (ImGui::Button("Main >", ImVec2(-1.0f, 0.0f)))
                    bShowMainPanel = true;
            }
            ImGui::End();
        }

		if (bShowMainPanel)
		{
			ImGui::SetNextWindowPos(ImVec2(workPos.x + margin, topPanelY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(mainPanelWidth, panelBottomY - topPanelY), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(minMainPanelWidth, panelBottomY - topPanelY), ImVec2(maxMainPanelWidth, panelBottomY - topPanelY));
			if (ImGui::Begin("Main", &bShowMainPanel, mainPanelFlags))
			{
                MainPanelWidth = Clamp(ImGui::GetWindowSize().x, minMainPanelWidth, maxMainPanelWidth);
                if (ImGui::BeginTabBar("MainTabs"))
                {
                    if (ImGui::BeginTabItem("Status", nullptr, ConsumeFeatureTabFlags(bSelectMainStatusTab)))
                    {
                        MainActiveTab = "Status";
                        const float fps = ImGui::GetIO().Framerate;
                        ImGui::TextUnformatted("FPS");
                        ImGui::SameLine(210.0f);
                        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.2f, 1.0f), "%.1f", fps);
                        ImGui::Text("Frame Time");
                        ImGui::SameLine(190.0f);
                        ImGui::Text("%.2f ms", fps > 0.0f ? 1000.0f / fps : 0.0f);
                        ImGui::Text("RHI");
                        ImGui::SameLine(190.0f);
                        ImGui::Text("%s", g_rhi->GetRHIName().ToStr());
                        if (g_Engine)
                        {
                            ImGui::Separator();
                            ImGui::TextUnformatted("Active Scene");
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::BeginCombo("##MainActiveScene", g_Engine->Game.GetActivePathTracingSceneName()))
                            {
                                const auto& loadableScenes = g_Engine->Game.GetLoadablePathTracingScenes();
                                for (int32 i = 0; i < (int32)loadableScenes.size(); ++i)
                                {
                                    const bool isSelected = (g_Engine->Game.GetSelectedPathTracingSceneIndex() == i);
                                    if (ImGui::Selectable(loadableScenes[i].DisplayName.c_str(), isSelected))
                                        g_Engine->Game.SetSelectedPathTracingSceneIndex(i);
                                    if (isSelected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::BeginDisabled(!g_Engine->Game.CanLoadSelectedPathTracingScene());
                            if (ImGui::Button("Load Scene", ImVec2(100.0f, 0.0f)))
                                g_Engine->Game.RequestLoadSelectedPathTracingScene();
                            ImGui::EndDisabled();
                            ImGui::Spacing();
                            ImGui::Text("Renderer : %s", g_Engine->Game.GetActiveSceneRenderPipelineName());
                            ImGui::Text("Loader : %s", g_Engine->Game.GetActivePathTracingSceneLoaderName());
                        }

                        ImGui::Separator();
                        if (IsUseDX12())
                        {
                            if (ImGui::Button("Borderless Fullscreen", ImVec2(-1.0f, 0.0f)))
                                g_rhi->ToggleBorderlessFullscreen();
                            if (ImGui::Button("Exclusive Fullscreen", ImVec2(-1.0f, 0.0f)))
                                g_rhi->ToggleExclusiveFullscreen();
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Profiling", nullptr, ConsumeFeatureTabFlags(bSelectMainProfilingTab)))
                    {
                        MainActiveTab = "Profiling";
                        const float fps = ImGui::GetIO().Framerate;
                        ImGui::Text("Average %.3f ms/frame (%.1f FPS)", fps > 0.0f ? 1000.0f / fps : 0.0f, fps);
                        ImGui::Separator();
                        jImGUI::CreateTreeForProfiling("[CPU]Total Passes", jPerformanceProfile::GetInstance().GetAvgCPUProfiles(), jPerformanceProfile::GetInstance().GetTotalAvgCPUPassesMS());
                        ImGui::Separator();
                        jImGUI::CreateTreeForProfiling("[GPU]Total Passes", jPerformanceProfile::GetInstance().GetAvgGPUProfiles(), jPerformanceProfile::GetInstance().GetTotalAvgGPUPassesMS());
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Render", nullptr, ConsumeFeatureTabFlags(bSelectMainRenderTab)))
                    {
                        MainActiveTab = "Render";
#if USE_VARIABLE_SHADING_RATE_TIER2
                        ImGui::Checkbox("UseVRS", &gOptions.UseVRS);
                        ImGui::Separator();
#endif
                        if (IsUseVulkan())
                        {
                            ImGui::BeginDisabled(true);
                            ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
                            ImGui::EndDisabled();
                            ImGui::Checkbox("UseSubpass", &gOptions.UseSubpass);
                            ImGui::Checkbox("UseMemoryless", &gOptions.UseMemoryless);
                        }
                        else
                        {
                            ImGui::BeginDisabled(true);
                            ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
                            ImGui::Checkbox("[VulkanOnly]UseSubpass", &gOptions.UseSubpass);
                            ImGui::Checkbox("[VulkanOnly]UseMemoryless", &gOptions.UseMemoryless);
                            ImGui::EndDisabled();
                        }

                        ImGui::Separator();
                        ImGui::Checkbox("ShowDebugObject", &gOptions.ShowDebugObject);
                        ImGui::Checkbox("BloomEyeAdaptation", &gOptions.BloomEyeAdaptation);
                        ImGui::Checkbox("QueueSubmitAfterShadowPass", &gOptions.QueueSubmitAfterShadowPass);
                        ImGui::Checkbox("QueueSubmitAfterBasePass", &gOptions.QueueSubmitAfterBasePass);
                        ImGui::SliderFloat("AutoExposureKeyValueScale", &gOptions.AutoExposureKeyValueScale, -12.0f, 12.0f);
                        AddCopyPasteContextMenu("MainRenderAutoExposureContext", gOptions.AutoExposureKeyValueScale);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
			}
			ImGui::End();
		}

		char szTitle[128] = { 0, };
		sprintf_s(szTitle, sizeof(szTitle), "Render Options : %s", g_rhi->GetRHIName().ToStr());

		// Legacy Render Options UI is intentionally unreachable; active controls live in Main or Feature Panels.
		if (false && bShowRenderOptionsPanel)
		{
			ImGui::SetNextWindowPos(ImVec2(renderOptionsX, topPanelY), ImGuiCond_Appearing);
			ImGui::SetNextWindowSize(ImVec2(renderOptionsWidth, panelBottomY - topPanelY), ImGuiCond_Appearing);
			ImGui::Begin(szTitle, &bShowRenderOptionsPanel, dockPanelFlags);
			
		if (ImGui::BeginTabBar("RHI"))
		{
			if (ImGui::BeginTabItem("Default")) 
			{
                if (IsUseDX12() && ImGui::Button("Toggle Borderless Fullscreen"))
                    g_rhi->ToggleBorderlessFullscreen();
                if (IsUseDX12() && ImGui::Button("Toggle Exclusive Fullscreen"))
                    g_rhi->ToggleExclusiveFullscreen();
                if (IsUseDX12())
                    ImGui::Separator();

#if USE_VARIABLE_SHADING_RATE_TIER2
				ImGui::Checkbox("UseVRS", &gOptions.UseVRS);
#endif
				//ImGui::Checkbox("ShowVRSArea", &gOptions.ShowVRSArea);
				//ImGui::Checkbox("ShowGrid", &gOptions.ShowGrid);
				//ImGui::Checkbox("UseWaveIntrinsics", &gOptions.UseWaveIntrinsics);
				{
					if (IsUseVulkan())
					{
						ImGui::BeginDisabled(true);
						ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
						ImGui::EndDisabled();
						ImGui::Checkbox("UseSubpass", &gOptions.UseSubpass);
						ImGui::Checkbox("UseMemoryless", &gOptions.UseMemoryless);
					}
					else
					{
                        ImGui::BeginDisabled(true);
						ImGui::Checkbox("[ReadOnly]UseDeferredRenderer", &gOptions.UseDeferredRenderer);
						ImGui::Checkbox("[VulkanOnly]UseSubpass", &gOptions.UseSubpass);
						ImGui::Checkbox("[VulkanOnly]UseMemoryless", &gOptions.UseMemoryless);
                        ImGui::EndDisabled();
					}
				}
				{
					ImGui::Checkbox("ShowDebugObject", &gOptions.ShowDebugObject);
					ImGui::Checkbox("BloomEyeAdaptation", &gOptions.BloomEyeAdaptation);

					ImGui::Checkbox("QueueSubmitAfterShadowPass", &gOptions.QueueSubmitAfterShadowPass);
					ImGui::Checkbox("QueueSubmitAfterBasePass", &gOptions.QueueSubmitAfterBasePass);
					ImGui::SliderFloat("AutoExposureKeyValueScale", &gOptions.AutoExposureKeyValueScale, -12.0f, 12.0f);
					AddCopyPasteContextMenu("AutoExposureContext", gOptions.AutoExposureKeyValueScale); // Simplified using helper function
				}
				ImGui::Separator();
				//ImGui::Text("PBR properties");
				//ImGui::SliderFloat("Metallic", &gOptions.Metallic, 0.0f, 1.0f);
				//ImGui::SliderFloat("Roughness", &gOptions.Roughness, 0.0f, 1.0f);
				//ImGui::Separator();

				constexpr float IndentSpace = 10.0f;
				const std::thread::id CurrentThreadId = std::this_thread::get_id();
				const ImVec4 OtherThreadColor = { 0.2f, 0.6f, 0.2f, 1.0f };
				{
					ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				}

				ImGui::Separator();
				jImGUI::CreateTreeForProfiling("[CPU]Total Passes", jPerformanceProfile::GetInstance().GetAvgCPUProfiles(), jPerformanceProfile::GetInstance().GetTotalAvgCPUPassesMS());
				
				ImGui::Separator();
				jImGUI::CreateTreeForProfiling("[GPU]Total Passes", jPerformanceProfile::GetInstance().GetAvgGPUProfiles(), jPerformanceProfile::GetInstance().GetTotalAvgGPUPassesMS());

				ImGui::EndTabItem();
			}

            const ImGuiTabItemFlags HWRTTabFlags = ImGuiTabItemFlags_None;
            if (false && ImGui::BeginTabItem("HWRT", nullptr, HWRTTabFlags))
            {
                if (GSupportRaytracing)
                {
                    ImGui::Checkbox("UseHWRTDirectLighting", &gOptions.UseHWRTDirectLighting);
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox("[ReadOnly]UseHWRTDirectLighting", &gOptions.UseHWRTDirectLighting);
                    ImGui::EndDisabled();
                    ImGui::TextUnformatted("Current RHI does not support hardware ray tracing.");
                }

                ImGui::Separator();
                ImGui::Text("Direct Lighting Active : %s", IsHWRTDirectLightingActive ? "YES" : "NO");
                if (!IsHWRTInlineSupported)
                {
                    ImGui::TextUnformatted("Inline RayQuery : Unsupported (fallback to DispatchRays)");
                }

                int32 HWRTDirectLightingMode = Clamp(gOptions.HWRTDirectLightingMode, 0, 1);
                if (HWRTDirectLightingMode == 1 && !IsHWRTInlineSupported)
                {
                    HWRTDirectLightingMode = 0;
                    gOptions.HWRTDirectLightingMode = 0;
                }

                if (ImGui::BeginCombo("DirectLightingMode", GHWRTDirectLightingModes[HWRTDirectLightingMode], ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < (int32)_countof(GHWRTDirectLightingModes); ++i)
                    {
                        if (i == 1 && !IsHWRTInlineSupported)
                            continue;

                        const bool IsSelected = (HWRTDirectLightingMode == i);
                        if (ImGui::Selectable(GHWRTDirectLightingModes[i], IsSelected))
                        {
                            gOptions.HWRTDirectLightingMode = i;
                            HWRTDirectLightingMode = i;
                        }
                        if (IsSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                int32 HWRTDebugMode = gOptions.HWRTDebugViewMode;
                if (HWRTDebugMode < 0 || HWRTDebugMode >= (int32)_countof(GHWRTDebugViewModes))
                {
                    HWRTDebugMode = 0;
                    gOptions.HWRTDebugViewMode = 0;
                }

                if (ImGui::BeginCombo("DebugView", GHWRTDebugViewModes[HWRTDebugMode], ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < (int32)_countof(GHWRTDebugViewModes); ++i)
                    {
                        const bool IsSelected = (gOptions.HWRTDebugViewMode == i);
                        if (ImGui::Selectable(GHWRTDebugViewModes[i], IsSelected))
                            gOptions.HWRTDebugViewMode = i;
                        if (IsSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("DebugLineWidth", &gOptions.HWRTDebugLineWidth, 0.001f, 0.1f, "%.4f");
                ImGui::SliderFloat("DebugUVScale", &gOptions.HWRTDebugUVScale, 1.0f, 128.0f, "%.1f");
                ImGui::SliderFloat("DebugPrimitiveIDScale", &gOptions.HWRTDebugPrimitiveIDScale, 0.1f, 16.0f, "%.2f");
                ImGui::Checkbox("ForceMipLevel0", &gOptions.HWRTForceMipLevel0);
                ImGui::SliderFloat("NormalBias", &gOptions.HWRTNormalBias, 0.0f, 50.0f, "%.5f");
                ImGui::SliderFloat("ShadowRayStartOffset", &gOptions.HWRTShadowRayStartOffset, 0.0f, 50.0f, "%.5f");

                bool ConditionUseHWRTDirectLighting = gOptions.UseHWRTDirectLighting;
                bool ConditionUseRaytracing = gOptions.UseRaytracing;
                bool ConditionSupportRaytracing = GSupportRaytracing;
                bool ConditionHasRenderFrameContext = HasRenderFrameContext;
                bool ConditionHasRaytracingScene = HasRaytracingScene;
                bool ConditionRaytracingSceneValid = IsRaytracingSceneValid;
                bool ConditionNotForwardRenderer = !IsForwardRenderer;
                bool ConditionInlineRaytracingSupport = IsHWRTInlineSupported;
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Cond: gOptions.UseHWRTDirectLighting", &ConditionUseHWRTDirectLighting);
                ImGui::Checkbox("Cond: gOptions.UseRaytracing", &ConditionUseRaytracing);
                ImGui::Checkbox("Cond: GSupportRaytracing", &ConditionSupportRaytracing);
                ImGui::Checkbox("Cond: GSupportInlineRaytracing", &ConditionInlineRaytracingSupport);
                ImGui::Checkbox("Cond: RenderFrameContext", &ConditionHasRenderFrameContext);
                ImGui::Checkbox("Cond: RaytracingScene", &ConditionHasRaytracingScene);
                ImGui::Checkbox("Cond: RaytracingScene->IsValid", &ConditionRaytracingSceneValid);
                ImGui::Checkbox("Cond: !UseForwardRenderer", &ConditionNotForwardRenderer);
                ImGui::EndDisabled();

                ImGui::Separator();
                if (ResolvedHWRTDirectLightingMode == 1 && IsHWRTInlineSupported)
                {
                    ImGui::TextUnformatted("Inline RayQuery based direct lighting path.");
                }
                else
                {
                    ImGui::TextUnformatted("DispatchRays based direct lighting path.");
                }
                ImGui::TextUnformatted("Primary ray hits geometry and shades without GBuffer lighting.");

                ImGui::EndTabItem();
            }

#ifdef ENABLE_EDITOR_FEATURES
			if (g_Editor)
			{
				// Placement mode is active only while the Placement Tool tab is open.
				g_Editor->Placement.EnablePlacementMode = false;
			}

			// Placement Tool Tab (Editor-only feature)
			if (ImGui::BeginTabItem("Placement Tool"))
			{
				if (g_Editor)
				{
					g_Editor->Placement.EnablePlacementMode = true;
					auto mainCamera = jCamera::GetMainCamera();
					g_Editor->Placement.RenderUI(mainCamera);
				}
				ImGui::EndTabItem();
			}
#endif

			// Light Tab
			if (ImGui::BeginTabItem("Light"))
			{
				ImGui::TextColored(ImVec4(1, 1, 0, 1), "Camera Info");
				ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);
				AddCopyPasteContextMenu("CameraPosContext", gOptions.CameraPos, [](float x, float y, float z) {
					auto mainCamera = jCamera::GetMainCamera();
					if (mainCamera)
					{
						Vector newPos(x, y, z);
						Vector offset = newPos - mainCamera->Pos;
						mainCamera->Pos = newPos;
						mainCamera->Target += offset;
						mainCamera->Up += offset;
					}
				});

				auto mainCamera = jCamera::GetMainCamera();
				if (mainCamera)
				{
					Vector cameraDir = mainCamera->GetForwardVector();
					ImGui::Text("CameraDir : %.2f, %.2f, %.2f", cameraDir.x, cameraDir.y, cameraDir.z);
					AddCopyPasteContextMenu("CameraDirContext", cameraDir, [mainCamera](float x, float y, float z) {
						Vector newDir(x, y, z);
						newDir = newDir.GetNormalize();
						Vector eulerAngle = Vector::GetEulerAngleFrom(newDir);
						mainCamera->SetEulerAngle(eulerAngle);
					});
				}

				ImGui::EndTabItem();
			}

            if (false && ImGui::BeginTabItem("SSGI Options"))
            {
                ImGui::Checkbox("UseSSGI", &gOptions.UseSSGI);
                if (gOptions.UseSSGI)
                {
                    ImGui::Indent();
                    ImGui::Checkbox("Show SSGI Only", &gOptions.ShowSSGIOnly);
                    ImGui::Checkbox("Temporal Accumulation", &gOptions.UseSSGITemporalAccumulation);
                    if (gOptions.UseSSGITemporalAccumulation)
                    {
                        ImGui::Indent();
                        ImGui::SliderFloat("Blend Factor", &gOptions.SSGIAccumBlendFactor, 0.0f, 1.0f);
                        AddCopyPasteContextMenu("SSGIBlendFactorContext", gOptions.SSGIAccumBlendFactor);
                        ImGui::Unindent();
                    }
                    ImGui::Checkbox("UseSSGIReprojection", &gOptions.UseSSGIReprojection);
                    if (!gOptions.UseSSGIReprojection)
                        ImGui::BeginDisabled();
                    ImGui::Checkbox("UseDiscontinuityWeightForSSGI", &gOptions.UseDiscontinuityWeightForSSGI);
                    if (!gOptions.UseSSGIReprojection)
                        ImGui::EndDisabled();
                    ImGui::Checkbox("Apply attenuation", &gOptions.UseSSGIAttenuation);
                    ImGui::SliderFloat("Intensity", &gOptions.SSGIIntensity, 0.0f, 30.0f);
                    AddCopyPasteContextMenu("SSGIIntensityContext", gOptions.SSGIIntensity);
                    ImGui::SliderFloat("Resolution Scale", &gOptions.SSGIResolutionScale, 0.25f, 1.0f);
                    AddCopyPasteContextMenu("SSGIResolutionScaleContext", gOptions.SSGIResolutionScale);
                    ImGui::SliderInt("Ray Count", &gOptions.SSGIRayCount, 1, 20);
                    ImGui::SliderInt("Max Steps", &gOptions.SSGIMaxSteps, 1, 64);
                    ImGui::SliderFloat("Max Distance", &gOptions.SSGIMaxDistance, 1.0f, 1000.0f);
                    AddCopyPasteContextMenu("SSGIMaxDistanceContext", gOptions.SSGIMaxDistance);
                    ImGui::Unindent();
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "SSGI denosing");
                if (ImGui::BeginCombo("SSGIDenoiser", gOptions.GetDenoiseName(gOptions.SSGIDenoiser), ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < _countof(GDenoisers); ++i)
                    {
                        const bool is_selected = (gOptions.SSGIDenoiser == (EDenoiser)i);
                        if (ImGui::Selectable(GDenoisers[i], is_selected))
                            gOptions.SSGIDenoiser = (EDenoiser)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (gOptions.SSGIDenoiser != EDenoiser::NONE)
                {
                    ImGui::Indent();
                    ImGui::SliderInt("KernelSize", &gOptions.SSGIDenoiserKernelSize, 1, 20);
                    if ((gOptions.SSGIDenoiserKernelSize % 2) == 0)
                        gOptions.SSGIDenoiserKernelSize++;
                    ImGui::SliderFloat("KernelSigma", &gOptions.SSGIDenoiserKernelSigma, 0.1f, 30.0f);
                    AddCopyPasteContextMenu("SSGIKernelSigmaContext", gOptions.SSGIDenoiserKernelSigma);
                    ImGui::SliderFloat("BilateralSigma", &gOptions.SSGIDenoiserBilateralKernelSigma, 0.001f, 0.1f);
                    AddCopyPasteContextMenu("SSGIBilateralSigmaContext", gOptions.SSGIDenoiserBilateralKernelSigma);
                    ImGui::SliderInt("BlurQuality", &gOptions.SSGIBlurQuality, 1, 5);
                    ImGui::Unindent();
                }
                if (gOptions.SSGIDenoiser == EDenoiser::A_TROUS)
                {
                    ImGui::Indent();
                    ImGui::SliderFloat("Sigma_Color", &gOptions.SSGIATrousSigmaColor, 0.0f, 10.0f);

                    AddCopyPasteContextMenu("SSGISigmaColorContext", gOptions.SSGIATrousSigmaColor);
                    ImGui::SliderFloat("Sigma_Normal", &gOptions.SSGIATrousSigmaNormal, 0.0f, 1.0f);

                    AddCopyPasteContextMenu("SSGISigmaNormalContext", gOptions.SSGIATrousSigmaNormal);
                    ImGui::SliderFloat("Sigma_Depth", &gOptions.SSGIATrousSigmaDepth, 0.0f, 10.0f);

                    AddCopyPasteContextMenu("SSGISigmaDepthContext", gOptions.SSGIATrousSigmaDepth);
                    ImGui::Unindent();
                }
                ImGui::EndTabItem();
            }

#if 0
            // SurfelGI is rendered in the right-side Feature Panels tab group.
            // Keep this detailed legacy tab disabled to avoid duplicate UI surfaces.
            const ImGuiTabItemFlags SurfelGITabFlags = ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("SurfelGI", nullptr, SurfelGITabFlags))
            {
                bSelectSurfelGITabByDefault = false;

                auto DrawSliderInt60_40 = [](const char* label, int32* value, int32 minValue, int32 maxValue)
                {
                    const float availWidth = ImGui::GetContentRegionAvail().x;
                    const float sliderWidth = availWidth * 0.6f;
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(sliderWidth);
                    ImGui::SliderInt("##Value", value, minValue, maxValue);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(label);
                    ImGui::PopID();
                };

                auto DrawSliderFloat60_40 = [](const char* label, float* value, float minValue, float maxValue)
                {
                    const float availWidth = ImGui::GetContentRegionAvail().x;
                    const float sliderWidth = availWidth * 0.6f;
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(sliderWidth);
                    ImGui::SliderFloat("##Value", value, minValue, maxValue);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(label);
                    ImGui::PopID();
                };

                auto DrawSliderInt60_40_ReadOnly = [&](const char* label, int32 value, int32 minValue, int32 maxValue)
                {
                    int32 displayValue = value;
                    ImGui::BeginDisabled(true);
                    DrawSliderInt60_40(label, &displayValue, minValue, maxValue);
                    ImGui::EndDisabled();
                };

                auto DrawSliderFloat60_40_ReadOnly = [&](const char* label, float value, float minValue, float maxValue)
                {
                    float displayValue = value;
                    ImGui::BeginDisabled(true);
                    DrawSliderFloat60_40(label, &displayValue, minValue, maxValue);
                    ImGui::EndDisabled();
                };

                auto FormatCompactFloat = [](float value, int precision) -> std::string
                {
                    char buffer[32];
                    sprintf_s(buffer, "%.*f", precision, value);

                    std::string result(buffer);
                    while (!result.empty() && result.back() == '0')
                        result.pop_back();
                    if (!result.empty() && result.back() == '.')
                        result.pop_back();
                    if (result.empty())
                        result = "0";
                    return result;
                };

                ImGui::Checkbox("Enable SurfelGI", &gOptions.UseSurfelGI);
                if (gOptions.UseSurfelGI)
                {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Pool / Lifetime");
                    DrawSliderInt60_40("Surfel Max Pool", &gOptions.SurfelGIMaxSurfels, 4096, 2097152);
                    DrawSliderInt60_40("Spawn Budget / Frame", &gOptions.SurfelGISpawnBudgetPerFrame, 64, 16384);
                    DrawSliderInt60_40("TTL Frames", &gOptions.SurfelGITTLInFrames, 1, 600);
                    DrawSliderInt60_40("Out-Of-View Keep Frames", &gOptions.SurfelGIOutOfViewKeepFrames, 1, 120);

                    ImGui::Checkbox("Use Tile Based Sampling", &gOptions.UseSurfelGITileBasedSampling);
                    DrawSliderInt60_40("Tile Size", &gOptions.SurfelGITileSize, 4, 32);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Clipmap");
                    ImGui::SetWindowFontScale(0.92f);
                    for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
                    {
                        float cascadeCellScale = 1.0f;
                        for (int32 i = 1; i <= cascade && i < SURFEL_GI_CASCADE_COUNT; ++i)
                            cascadeCellScale *= Max(1.0f, gOptions.SurfelGICascadeCellScaleFromPrev[i]);

                        const float clipmapGridSize = gOptions.SurfelGIWorldGridCellSize * cascadeCellScale;
                        const float startDistance = (cascade == 0) ? 0.0f : gOptions.SurfelGICascadeStartDistance[cascade];
                        const float radiusScale = (cascade == 0) ? 1.0f : gOptions.SurfelGICascadeRadiusScale[cascade];
                        const std::string clipmapGridSizeText = FormatCompactFloat(clipmapGridSize, 1);
                        const std::string cascadeCellScaleText = FormatCompactFloat(cascadeCellScale, 2);
                        const std::string startDistanceText = FormatCompactFloat(startDistance, 1);
                        const std::string radiusScaleText = FormatCompactFloat(radiusScale, 2);

                        char header[256];
                        sprintf_s(header, "Cascade%d - Grid %s, Cell %s, Start %s, Radius %s, Dim %dx%dx%d, Surfels %d###SurfelGICascade%d",
                            cascade,
                            clipmapGridSizeText.c_str(),
                            cascadeCellScaleText.c_str(),
                            startDistanceText.c_str(),
                            radiusScaleText.c_str(),
                            gOptions.SurfelGIClipmapGridDimX[cascade],
                            gOptions.SurfelGIClipmapGridDimY[cascade],
                            gOptions.SurfelGIClipmapGridDimZ[cascade],
                            gOptions.SurfelGISurfelsPerCell[cascade],
                            cascade);

                        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed
                            | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (ImGui::TreeNodeEx(header, flags))
                        {
                            ImGui::Indent();
                            if (cascade == 0)
                            {
                                DrawSliderFloat60_40("Clipmap Grid Size", &gOptions.SurfelGIWorldGridCellSize, 10.0f, 1000.0f);
                                DrawSliderFloat60_40_ReadOnly("Cell Scale", 1.0f, 1.0f, 6.0f);
                                DrawSliderFloat60_40_ReadOnly("Start Distance", 0.0f, 0.0f, 4000.0f);
                                DrawSliderFloat60_40_ReadOnly("Radius Scale", 1.0f, 0.25f, 4.0f);
                            }
                            else
                            {
                                DrawSliderFloat60_40_ReadOnly("Clipmap Grid Size", clipmapGridSize, 10.0f, 1000.0f * 6.0f * 6.0f);
                                DrawSliderFloat60_40("Cell Scale", &gOptions.SurfelGICascadeCellScaleFromPrev[cascade], 1.0f, 6.0f);
                                DrawSliderFloat60_40("Start Distance", &gOptions.SurfelGICascadeStartDistance[cascade], 0.0f, 4000.0f);
                                DrawSliderFloat60_40("Radius Scale", &gOptions.SurfelGICascadeRadiusScale[cascade], 0.25f, 4.0f);
                            }

                            DrawSliderInt60_40("Clipmap Dim X", &gOptions.SurfelGIClipmapGridDimX[cascade], 8, 256);
                            DrawSliderInt60_40("Clipmap Dim Y", &gOptions.SurfelGIClipmapGridDimY[cascade], 8, 256);
                            DrawSliderInt60_40("Clipmap Dim Z", &gOptions.SurfelGIClipmapGridDimZ[cascade], 4, 128);
                            DrawSliderInt60_40("Surfels / Cell", &gOptions.SurfelGISurfelsPerCell[cascade], 1, 5);

                            ImGui::Unindent();
                            ImGui::TreePop();
                        }
                    }
                    ImGui::SetWindowFontScale(1.0f);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Surfel Placement");
                    DrawSliderFloat60_40("Surfel Radius Scale", &gOptions.SurfelGIRadiusScale, 0.25f, 2.5f);
                    DrawSliderFloat60_40("Face Margin Radius Scale", &gOptions.SurfelGIFaceMarginRadiusScale, 0.0f, 1.0f);
                    DrawSliderFloat60_40("Normal Threshold", &gOptions.SurfelGINormalThreshold, 0.0f, 1.0f);
                    ImGui::Checkbox("Prefer Cell Center For First Placement", &gOptions.UseSurfelGIPreferCellCenterForFirstPlacement);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Inline Ray Gather (HWRT DI)");
                    ImGui::Checkbox("Enable Inline Ray Gather", &gOptions.SurfelGIInlineRayEnable);
                    if (gOptions.SurfelGIInlineRayEnable)
                    {
                        ImGui::Checkbox("Enable Guiding", &gOptions.SurfelGIInlineRayGuideEnable);
                        DrawSliderInt60_40("Inline Ray Count", &gOptions.SurfelGIInlineRayCount, 1, 16);
                        DrawSliderInt60_40("New Surfel Bootstrap Rays", &gOptions.SurfelGINewSurfelBootstrapRayCount, 1, 32);
                        DrawSliderFloat60_40("Inline Ray Max Distance", &gOptions.SurfelGIInlineRayMaxDistance, 10.0f, 5000.0f);
                        DrawSliderFloat60_40("Inline Ray Normal Bias", &gOptions.SurfelGIInlineRayNormalBias, 0.001f, 10.0f);
                        DrawSliderFloat60_40("MSME History Blend", &gOptions.SurfelGIInlineRayHistoryBlend, 0.0f, 0.99f);
                    }
                    DrawSliderFloat60_40("Radiance Scale", &gOptions.SurfelGIRadianceScale, 0.0f, 8.0f);
                    DrawSliderFloat60_40("SurfelGI Intensity", &gOptions.SurfelGIIntensity, 0.0f, 8.0f);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Resolve");
                    DrawSliderInt60_40("Neighbor Cell Radius", &gOptions.SurfelGIVisualizeNeighborCellRadius, 0, 3);
                    DrawSliderFloat60_40("Resolve Softness", &gOptions.SurfelGIResolveSoftness, 0.5f, 4.0f);
                    DrawSliderFloat60_40("Resolve Irradiance Warmup Updates", &gOptions.SurfelGIResolveIrradianceWarmupUpdates, 0.0f, 16.0f);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Visualization");
                    auto DrawVisualizationGroupTitle = [](const char* title)
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", title);
                    };

                    DrawVisualizationGroupTitle("Output");
                    ImGui::Indent();
                    ImGui::Checkbox("Show Candidate Debug RT", &gOptions.ShowSurfelGIDebug);
                    ImGui::Checkbox("Blend With Scene", &gOptions.SurfelGIVisualizeBlendWithScene);
                    if (gOptions.SurfelGIVisualizeBlendWithScene)
                    {
                        DrawSliderFloat60_40("Blend Alpha (1=Overwrite Surfel)", &gOptions.SurfelGIVisualizeBlendAlpha, 0.0f, 1.0f);
                    }
                    ImGui::Unindent();

                    DrawVisualizationGroupTitle("Surfel Overlay");
                    ImGui::Indent();
                    ImGui::Checkbox("Show Placed Surfels", &gOptions.ShowSurfelGIPlacedSurfels);
                    if (!gOptions.ShowSurfelGIPlacedSurfels)
                    {
                        ImGui::TextDisabled("Surfel Color Mode / Underfilled / Cell Grid need Show Placed Surfels");
                    }
                    else if (gOptions.ShowSurfelGIIrradianceDebug)
                    {
                        ImGui::TextDisabled("Irradiance debug overrides Surfel Color Mode / Underfilled / Cell Grid");
                    }

                    const bool canEditSurfelOverlay = gOptions.ShowSurfelGIPlacedSurfels && !gOptions.ShowSurfelGIIrradianceDebug;
                    ImGui::BeginDisabled(!canEditSurfelOverlay);
                    {
                        static const char* GSurfelOverlayColorModes[] =
                        {
                            "Radius",
                            "State",
                            "Cell"
                        };
                        const int32 SurfelOverlayColorModeCount = (int32)(sizeof(GSurfelOverlayColorModes) / sizeof(GSurfelOverlayColorModes[0]));
                        int32 surfelOverlayColorMode = gOptions.ShowSurfelGICellDebug ? 2 : (gOptions.ShowSurfelGIStateDebug ? 1 : 0);
                        surfelOverlayColorMode = Clamp(surfelOverlayColorMode, 0, SurfelOverlayColorModeCount - 1);
                        if (ImGui::BeginCombo("Surfel Color Mode", GSurfelOverlayColorModes[surfelOverlayColorMode], ImGuiComboFlags_None))
                        {
                            for (int32 mode = 0; mode < SurfelOverlayColorModeCount; ++mode)
                            {
                                const bool isSelected = (surfelOverlayColorMode == mode);
                                if (ImGui::Selectable(GSurfelOverlayColorModes[mode], isSelected))
                                    surfelOverlayColorMode = mode;
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        gOptions.ShowSurfelGIStateDebug = (surfelOverlayColorMode == 1);
                        gOptions.ShowSurfelGICellDebug = (surfelOverlayColorMode == 2);

                        ImGui::Checkbox("Show Underfilled Cell Debug", &gOptions.ShowSurfelGIUnderfilledCellDebug);
                        ImGui::Checkbox("Show Surfel Cell Grid", &gOptions.ShowSurfelGICellGrid);
                    }
                    ImGui::EndDisabled();
                    ImGui::Unindent();

                    DrawVisualizationGroupTitle("Irradiance");
                    ImGui::Indent();
                    ImGui::Checkbox("Show Surfel Irradiance", &gOptions.ShowSurfelGIIrradianceDebug);
                    if (gOptions.ShowSurfelGIIrradianceDebug)
                    {
                        static const char* GIrradianceDebugModes[] =
                        {
                            "Mean",
                            "Short Mean",
                            "Variance",
                            "Inconsistency",
                            "Count / VBBR"
                        };
                        const int32 IrradianceDebugModeCount = (int32)(sizeof(GIrradianceDebugModes) / sizeof(GIrradianceDebugModes[0]));
                        gOptions.SurfelGIIrradianceDebugMode = Clamp(gOptions.SurfelGIIrradianceDebugMode, 0, IrradianceDebugModeCount - 1);
                        if (ImGui::BeginCombo("Irradiance Debug Mode", GIrradianceDebugModes[gOptions.SurfelGIIrradianceDebugMode], ImGuiComboFlags_None))
                        {
                            for (int32 mode = 0; mode < IrradianceDebugModeCount; ++mode)
                            {
                                const bool isSelected = (gOptions.SurfelGIIrradianceDebugMode == mode);
                                if (ImGui::Selectable(GIrradianceDebugModes[mode], isSelected))
                                    gOptions.SurfelGIIrradianceDebugMode = mode;
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                    ImGui::Unindent();

                    DrawVisualizationGroupTitle("Extra Overlays");
                    ImGui::Indent();
                    ImGui::Checkbox("Show Spawn Attempt Points", &gOptions.ShowSurfelGISpawnAttemptDebug);
                    ImGui::Checkbox("Show Hover Ray Debug", &gOptions.ShowSurfelGIHoverRayDebug);
                    if (gOptions.ShowSurfelGIHoverRayDebug)
                        ImGui::Checkbox("Use Hit Radiance Color", &gOptions.ShowSurfelGIHoverRayHitRadianceColor);
                    ImGui::Unindent();
                    ImGui::Unindent();
                }

                ImGui::EndTabItem();
            }
#endif

			if (ImGui::BeginTabItem("Profiler"))
			{
				static std::string sTraceConnectMessage;
				static std::string sTraceConnectPath;

				ImGui::TextColored(ImVec4(1, 1, 0, 1), "Build-time (Read-only)");

				const char* backendName = "Legacy";
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY
				backendName = "Tracy";
#endif
				ImGui::Text("Backend : %s", backendName);

				bool externalCpuAvailable = (JPROFILE_EXTERNAL_CPU_AVAILABLE != 0);
				ImGui::BeginDisabled(true);
				ImGui::Checkbox("External CPU Profiler Available", &externalCpuAvailable);
				bool localCpuWithExternal = (ENABLE_LOCAL_CPU_PROFILE_WITH_EXTERNAL != 0);
				ImGui::Checkbox("Local CPU Profile With External", &localCpuWithExternal);
				bool tracySystemTracing = (JPROFILE_TRACY_ENABLE_SYSTEM_TRACING != 0);
				ImGui::Checkbox("Tracy System Tracing", &tracySystemTracing);
				ImGui::EndDisabled();

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
				const bool elevated = IsCurrentProcessElevated();
				if (!elevated)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Context switch/CPU Data needs administrator run on Windows.");
					ImGui::TextWrapped("Run Visual Studio (or jEngine.exe) as administrator, then reconnect Tracy.");
				}

				bool tracyConnected = TracyIsConnected;
				ImGui::BeginDisabled(true);
				ImGui::Checkbox("Tracy Connected", &tracyConnected);
				bool tracyRunAsAdmin = elevated;
				ImGui::Checkbox("Run As Administrator", &tracyRunAsAdmin);
				ImGui::EndDisabled();

				if (ImGui::Button("Trace Connect"))
				{
					std::string resolvedPath;
					std::string launchArgs;
					std::string error;
					if (LaunchTracyProfiler(resolvedPath, launchArgs, error))
					{
						sTraceConnectPath = resolvedPath;
						sTraceConnectMessage = "tracy-profiler.exe launched and auto-connect requested.";
						if (!launchArgs.empty())
							sTraceConnectPath += " " + launchArgs;
					}
					else
					{
						sTraceConnectMessage = error;
					}
				}
				if (!sTraceConnectMessage.empty())
					ImGui::TextWrapped("%s", sTraceConnectMessage.c_str());
				if (!sTraceConnectPath.empty())
					ImGui::TextWrapped("Path: %s", sTraceConnectPath.c_str());
#endif

				ImGui::Separator();
				ImGui::TextColored(ImVec4(1, 1, 0, 1), "Runtime Options");

				ImGui::Checkbox("EnableAppInfo", &g_jProfileRuntimeOptions.EnableAppInfo);
				ImGui::Checkbox("EnableMessages", &g_jProfileRuntimeOptions.EnableMessages);
				ImGui::Checkbox("EnablePlots", &g_jProfileRuntimeOptions.EnablePlots);
				ImGui::BeginDisabled(true);
				ImGui::Checkbox("Memory Tracking (Restart)", &g_jProfileRuntimeOptions.EnableMemoryTracking);
				ImGui::EndDisabled();

				ImGui::Separator();
				ImGui::Text("Frame Image");
				ImGui::Checkbox("Enable", &g_jProfileRuntimeOptions.EnableFrameImage);
				ImGui::SliderInt("Capture Interval", &g_jProfileRuntimeOptions.FrameImageCaptureInterval, 1, 240);
				ImGui::SliderInt("Max Width", &g_jProfileRuntimeOptions.FrameImageCaptureMaxWidth, 64, 1920);
				ImGui::SliderInt("Source Frame Lag", &g_jProfileRuntimeOptions.FrameImageSourceFrameLag, 0, 16);
				ImGui::Checkbox("Skip If Source Not Ready", &g_jProfileRuntimeOptions.FrameImageSkipIfSourceNotReady);

				ImGui::EndTabItem();
			}

			if (GSupportRaytracing)
			{
				if (false && ImGui::BeginTabItem("AO Options"))
				{
					for(int32 i=0;i<_countof(GAOType);++i)
					{
						if (!GSupportRaytracing && i == gOptions.GetRTAOIndex())
							continue;

						ImGui::RadioButton(GAOType[i], &gOptions.AOType, i);
					}

					if (gOptions.AOType == 0 && !GSupportRaytracing)
						gOptions.AOType = 1;

					ImGui::Checkbox("ShowDebugRT", &gOptions.ShowDebugRT);
					ImGui::Checkbox("ShowAOOnly", &gOptions.ShowAOOnly);
					if (ImGui::BeginCombo("AO RT Res(%)", gOptions.UseResolution, ImGuiComboFlags_None))
					{
						for (int32 i = 0; i < _countof(GAOResolution); ++i)
						{
							const bool is_selected = (gOptions.UseResolution == GAOResolution[i]);
							if (ImGui::Selectable(GAOResolution[i], is_selected))
								gOptions.UseResolution = GAOResolution[i];
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					if (gOptions.IsRTAO())
					{
						ImGui::Separator();
						ImGui::TextColored(ImVec4(1, 1, 0, 1), "RTAO ray options");
						ImGui::SliderFloat("Radius", &gOptions.AORadius, 0.0f, 150.0f);
						AddCopyPasteContextMenu("RTAORadiusContext", gOptions.AORadius);
						ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
						AddCopyPasteContextMenu("RTAOIntensityContext", gOptions.AOIntensity);
						ImGui::SliderInt("RayPerPixel", &gOptions.RayPerPixel, 1, 100);

						ImGui::Separator();
						ImGui::TextColored(ImVec4(1, 1, 0, 1), "Temporal denosing");
						ImGui::Checkbox("UseAOReprojection", &gOptions.UseAOReprojection);
						if (!gOptions.UseAOReprojection)
							ImGui::BeginDisabled();
						ImGui::Checkbox("UseDiscontinuityWeight", &gOptions.UseDiscontinuityWeight);
						if (!gOptions.UseAOReprojection)
							ImGui::EndDisabled();
						ImGui::Checkbox("UseHaltonJitter", &gOptions.UseHaltonJitter);
						ImGui::Checkbox("UseAccumulateRay", &gOptions.UseAccumulateRay);
					}
					else if (gOptions.IsSSAO())
					{
						ImGui::Separator();
						ImGui::TextColored(ImVec4(1, 1, 0, 1), "SSAO ray options");
						ImGui::SliderFloat("Radius", &gOptions.AORadius, 0.0f, 150.0f);
						AddCopyPasteContextMenu("SSAORadiusContext", gOptions.AORadius);
						ImGui::SliderFloat("Bias(avoid banding)", &gOptions.SSAOBias, 0.0f, 150.0f);
						AddCopyPasteContextMenu("SSAOBiasContext", gOptions.SSAOBias);
						ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
						AddCopyPasteContextMenu("SSAOIntensityContext", gOptions.AOIntensity);
					}

					ImGui::Separator();
					ImGui::TextColored(ImVec4(1, 1, 0, 1), "Spatial denosing");
					if (ImGui::BeginCombo("Denoiser", gOptions.GetDenoiseName(gOptions.Denoiser), ImGuiComboFlags_None))
					{
						for (int32 i = 0; i < _countof(GDenoisers); ++i)
						{
							const bool is_selected = (gOptions.Denoiser == (EDenoiser)i);
							if (ImGui::Selectable(GDenoisers[i], is_selected))
								gOptions.Denoiser = (EDenoiser)i;
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					// The kernel size must be an odd number
					ImGui::SliderInt("KernelSize", &gOptions.GaussianKernelSize, 1, 20);
					if ((gOptions.GaussianKernelSize % 2) == 0)
						gOptions.GaussianKernelSize++;
					ImGui::SliderFloat("KernelSigma", &gOptions.GaussianKernelSigma, 0.1f, 30.0f);
					AddCopyPasteContextMenu("GaussianKernelSigmaContext", gOptions.GaussianKernelSigma);
					ImGui::SliderFloat("BilateralSigma", &gOptions.BilateralKernelSigma, 0.001f, 0.1f);
					AddCopyPasteContextMenu("BilateralKernelSigmaContext", gOptions.BilateralKernelSigma);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}
		ImGui::End();
		}

		const bool bShowFeaturePanelWindow = bShowSceneBrowserPanel || bShowPathTracingPanel || bShowSurfelGIPanel || bShowAtmospherePanel || bShowDebugVisualizationPanel || bShowHWRTPanel || bShowSSGIPanel || bShowAOPanel || bShowLightPanel || bShowProfilerPanel || bShowPlacementPanel;
#ifdef ENABLE_EDITOR_FEATURES
        if (g_Editor)
            g_Editor->Placement.EnablePlacementMode = false;
#endif
        if (bShowFeaturePanelWindow && bFeaturePanelAutoHidden)
        {
            ImGui::SetNextWindowPos(ImVec2(sidePanelX + sidePanelWidth - 100.0f, topPanelY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(100.0f, ImGui::GetFrameHeight() + 8.0f), ImGuiCond_Always);
            if (ImGui::Begin("Feature Panels Auto Hide", nullptr, autoHideFlags))
            {
                if (ImGui::Button("< Features", ImVec2(-1.0f, 0.0f)))
                    bFeaturePanelAutoHidden = false;
            }
            ImGui::End();
        }

		if (bShowFeaturePanelWindow && !bFeaturePanelAutoHidden)
		{
			ImGui::SetNextWindowPos(ImVec2(sidePanelX, topPanelY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(sidePanelWidth, panelBottomY - topPanelY), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(minFeaturePanelWidth, panelBottomY - topPanelY), ImVec2(maxFeaturePanelWidth, panelBottomY - topPanelY));
            bool bFeaturePanelOpen = true;
			if (ImGui::Begin("Feature Panels", &bFeaturePanelOpen, featurePanelFlags))
			{
                FeaturePanelWidth = Clamp(ImGui::GetWindowSize().x, minFeaturePanelWidth, maxFeaturePanelWidth);
				if (ImGui::BeginTabBar("FeaturePanelTabs", ImGuiTabBarFlags_Reorderable))
				{
					if (bShowSceneBrowserPanel && ImGui::BeginTabItem("Scene Browser", &bShowSceneBrowserPanel, ConsumeFeatureTabFlags(bSelectSceneBrowserFeatureTab)))
					{
                        FeatureActiveTab = "Scene Browser";
						auto& game = g_Engine->Game;
						const auto& loadableScenes = game.GetLoadablePathTracingScenes();
						const int32 activeSceneIndex = game.GetActivePathTracingSceneIndex();

						static char SceneFilter[128] = {};
						ImGui::SetNextItemWidth(-1.0f);
						ImGui::InputTextWithHint("##SceneFilter", "Filter (Ctrl+F)", SceneFilter, sizeof(SceneFilter));
						ImGui::Columns(2, "SceneBrowserColumns", true);
						ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.68f);
						ImGui::TextUnformatted("Name");
						ImGui::NextColumn();
						ImGui::TextUnformatted("Type");
						ImGui::NextColumn();
						ImGui::Separator();
						ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "Active Scene : %s", game.GetActivePathTracingSceneName());
						ImGui::NextColumn();
						ImGui::TextUnformatted("Scene");
						ImGui::NextColumn();
						ImGui::Text("Active Renderer : %s", game.GetActiveSceneRenderPipelineName());
						ImGui::NextColumn();
						ImGui::TextUnformatted("Renderer");
						ImGui::NextColumn();
						ImGui::Text("Active Loader : %s", game.GetActivePathTracingSceneLoaderName());
						ImGui::NextColumn();
						ImGui::TextUnformatted("Loader");
						ImGui::NextColumn();
						ImGui::Columns(1);
						ImGui::Separator();

				ImGui::Text("Loadable Scenes");
                struct SceneTreeNode
                {
                    std::map<std::string, SceneTreeNode> Children;
                    std::vector<int32> SceneIndices;
                };
                SceneTreeNode rootNode;
                const std::string filterText = ToLowerUIString(SceneFilter);
                auto AddSceneToTree = [&loadableScenes, &rootNode, &filterText](int32 sceneIndex)
                {
                    const auto& sceneDesc = loadableScenes[sceneIndex];
                    const std::string searchableText = ToLowerUIString(sceneDesc.SceneId + " " + sceneDesc.DisplayName + " " + sceneDesc.FilePath);
                    if (!filterText.empty() && searchableText.find(filterText) == std::string::npos)
                        return;

                    SceneTreeNode* currentNode = &rootNode;
                    std::stringstream pathStream(sceneDesc.SceneId);
                    std::string part;
                    std::vector<std::string> parts;
                    while (std::getline(pathStream, part, '/'))
                    {
                        if (!part.empty())
                            parts.push_back(part);
                    }
                    if (parts.empty())
                    {
                        currentNode->SceneIndices.push_back(sceneIndex);
                        return;
                    }

                    for (size_t partIndex = 0; partIndex + 1 < parts.size(); ++partIndex)
                        currentNode = &currentNode->Children[parts[partIndex]];
                    currentNode->SceneIndices.push_back(sceneIndex);
                };
                for (int32 i = 0; i < (int32)loadableScenes.size(); ++i)
                    AddSceneToTree(i);

                if (ImGui::BeginChild("##LoadableSceneTree", ImVec2(ImGui::GetContentRegionAvail().x, 190.0f), true))
                {
                    std::function<void(SceneTreeNode&)> DrawSceneTreeNode = [&](SceneTreeNode& node)
                    {
                        for (auto& child : node.Children)
                        {
                            const ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                            if (ImGui::TreeNodeEx(child.first.c_str(), treeFlags))
                            {
                                DrawSceneTreeNode(child.second);
                                ImGui::TreePop();
                            }
                        }

                        for (int32 sceneIndex : node.SceneIndices)
                        {
                            const bool isActiveScene = (activeSceneIndex == sceneIndex);
                            std::string label = std::filesystem::path(loadableScenes[sceneIndex].SceneId).filename().generic_string();
                            if (label.empty())
                                label = loadableScenes[sceneIndex].DisplayName;
                            label += " [";
                            label += game.GetSceneRenderPipelineName(sceneIndex);
                            label += "]";
                            if (isActiveScene)
                                label += " [Active]";

                            if (isActiveScene)
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));

                            const bool isSelected = (game.GetSelectedPathTracingSceneIndex() == sceneIndex);
                            if (ImGui::Selectable(label.c_str(), isSelected))
                                game.SetSelectedPathTracingSceneIndex(sceneIndex);
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();

                            if (isActiveScene)
                                ImGui::PopStyleColor();
                        }
                    };
                    DrawSceneTreeNode(rootNode);
                }
                ImGui::EndChild();

				const int32 selectedSceneIndex = game.GetSelectedPathTracingSceneIndex();
				if (selectedSceneIndex >= 0 && selectedSceneIndex < (int32)loadableScenes.size())
				{
					const char* recommendedLoaderName = game.GetSceneRecommendedLoaderName(selectedSceneIndex);
					const auto selectedLoader = game.GetSelectedPathTracingSceneLoader();
					std::string recommendedOptionLabel = std::string("Recommended (") + recommendedLoaderName + ")";
					const char* selectedLoaderPreview = nullptr;
					switch (selectedLoader)
					{
					case jGame::ESceneLoader::Recommended:
						selectedLoaderPreview = recommendedOptionLabel.c_str();
						break;
					case jGame::ESceneLoader::Model:
						selectedLoaderPreview = "Model";
						break;
					case jGame::ESceneLoader::PathTracing:
						selectedLoaderPreview = "PathTracing";
						break;
					default:
						selectedLoaderPreview = "Unknown";
						break;
					}

					ImGui::Text("Recommended Loader : %s", recommendedLoaderName);
					if (ImGui::BeginCombo("Scene Loader", selectedLoaderPreview))
					{
						const bool isRecommendedSelected = (selectedLoader == jGame::ESceneLoader::Recommended);
						if (ImGui::Selectable(recommendedOptionLabel.c_str(), isRecommendedSelected))
							game.SetSelectedPathTracingSceneLoader(jGame::ESceneLoader::Recommended);
						if (isRecommendedSelected)
							ImGui::SetItemDefaultFocus();

						const bool isModelSelected = (selectedLoader == jGame::ESceneLoader::Model);
						if (ImGui::Selectable("Model", isModelSelected))
							game.SetSelectedPathTracingSceneLoader(jGame::ESceneLoader::Model);
						if (isModelSelected)
							ImGui::SetItemDefaultFocus();

						const bool isPathTracingSelected = (selectedLoader == jGame::ESceneLoader::PathTracing);
						if (ImGui::Selectable("PathTracing", isPathTracingSelected))
							game.SetSelectedPathTracingSceneLoader(jGame::ESceneLoader::PathTracing);
						if (isPathTracingSelected)
							ImGui::SetItemDefaultFocus();

						ImGui::EndCombo();
					}
				}

				ImGui::BeginDisabled(!game.CanLoadSelectedPathTracingScene());
				if (ImGui::Button("Load Scene"))
					game.RequestLoadSelectedPathTracingScene();
				ImGui::EndDisabled();
						ImGui::EndTabItem();
			}

#if USE_PATH_TRACING
					if (bShowPathTracingPanel && ImGui::BeginTabItem("PathTracing", &bShowPathTracingPanel, ConsumeFeatureTabFlags(bSelectPathTracingFeatureTab)))
			{
                FeatureActiveTab = "PathTracing";
				ImGui::Text("Active : %s", bIsUsingPathTracingRenderer ? "YES" : "NO");
				ImGui::SliderInt("MaxRecursionDepth", &gOptions.MaxRecursionDepthForPathTracing, 1, 100);
				ImGui::SliderInt("RayPerPixel", &gOptions.RayPerPixelForPathTracing, 1, 100);
						ImGui::EndTabItem();
			}
#endif // USE_PATH_TRACING

					if (bShowAtmospherePanel && ImGui::BeginTabItem("Atmosphere", &bShowAtmospherePanel, ConsumeFeatureTabFlags(bSelectAtmosphereFeatureTab)))
			{
                FeatureActiveTab = "Atmosphere";
				ImGui::Checkbox("UseAtmosphericShadowing", &gOptions.UseAtmosphericShadowing);
				if (ImGui::BeginCombo("Atmosphere RT Res(%)", gOptions.AtmosphereResolution, ImGuiComboFlags_None))
				{
					for (int32 i = 0; i < _countof(GAtmosphereResolution); ++i)
					{
						const bool is_selected = (gOptions.AtmosphereResolution == GAtmosphereResolution[i]);
						if (ImGui::Selectable(GAtmosphereResolution[i], is_selected))
							gOptions.AtmosphereResolution = GAtmosphereResolution[i];
						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::BeginDisabled(!gOptions.UseAtmosphericShadowing);
				ImGui::SliderFloat("AnisoG", &gOptions.AnisoG, -0.95f, 0.95f, "%.3f");
				AddCopyPasteContextMenu("AtmospherePanelAnisoGContext", gOptions.AnisoG);
				ImGui::SliderFloat("SlopeOfDist", &gOptions.AtmosphericShadowSlopeOfDist, 0.0f, 1.0f, "%.3f");
				AddCopyPasteContextMenu("AtmospherePanelSlopeOfDistContext", gOptions.AtmosphericShadowSlopeOfDist);
				ImGui::SliderFloat("InScatteringLambda", &gOptions.AtmosphericShadowInScatteringLambda, 0.0f, 0.01f, "%.6f");
				AddCopyPasteContextMenu("AtmospherePanelInScatteringLambdaContext", gOptions.AtmosphericShadowInScatteringLambda);
				ImGui::SliderFloat("ApplyIntensity", &gOptions.AtmosphericShadowApplyIntensity, 0.0f, 8.0f, "%.3f");
				AddCopyPasteContextMenu("AtmospherePanelApplyIntensityContext", gOptions.AtmosphericShadowApplyIntensity);
				ImGui::SliderInt("TravelCount", &gOptions.AtmosphericShadowTravelCount, 1, 256);
				ImGui::Checkbox("UseNoise", &gOptions.AtmosphericShadowUseNoise);
				ImGui::EndDisabled();
						ImGui::EndTabItem();
			}

					if (bShowSurfelGIPanel && ImGui::BeginTabItem("SurfelGI", &bShowSurfelGIPanel, ConsumeFeatureTabFlags(bSelectSurfelGIFeatureTab)))
			{
                FeatureActiveTab = "SurfelGI";
				ImGui::Checkbox("Enable SurfelGI", &gOptions.UseSurfelGI);
				ImGui::BeginDisabled(!gOptions.UseSurfelGI);
				ImGui::SliderFloat("Radiance Scale", &gOptions.SurfelGIRadianceScale, 0.0f, 8.0f);
				ImGui::SliderFloat("SurfelGI Intensity", &gOptions.SurfelGIIntensity, 0.0f, 8.0f);
				ImGui::SliderInt("Surfel Max Pool", &gOptions.SurfelGIMaxSurfels, 4096, 2097152);
				ImGui::SliderInt("Spawn Budget / Frame", &gOptions.SurfelGISpawnBudgetPerFrame, 64, 16384);
				ImGui::SliderFloat("Surfel Radius Scale", &gOptions.SurfelGIRadiusScale, 0.25f, 2.5f);
				ImGui::Separator();
				ImGui::Checkbox("Show Candidate Debug RT", &gOptions.ShowSurfelGIDebug);
				ImGui::Checkbox("Show Placed Surfels", &gOptions.ShowSurfelGIPlacedSurfels);
				ImGui::Checkbox("Show Surfel Irradiance", &gOptions.ShowSurfelGIIrradianceDebug);
				ImGui::Checkbox("Show Spawn Attempt Points", &gOptions.ShowSurfelGISpawnAttemptDebug);
				ImGui::Checkbox("Show Hover Ray Debug", &gOptions.ShowSurfelGIHoverRayDebug);
				ImGui::EndDisabled();
						ImGui::EndTabItem();
			}

					if (bShowHWRTPanel && ImGui::BeginTabItem("HWRT", &bShowHWRTPanel, ConsumeFeatureTabFlags(bSelectHWRTFeatureTab)))
			{
                FeatureActiveTab = "HWRT";
                if (GSupportRaytracing)
                {
                    ImGui::Checkbox("UseHWRTDirectLighting", &gOptions.UseHWRTDirectLighting);
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Checkbox("[ReadOnly]UseHWRTDirectLighting", &gOptions.UseHWRTDirectLighting);
                    ImGui::EndDisabled();
                    ImGui::TextUnformatted("Current RHI does not support hardware ray tracing.");
                }

                ImGui::Separator();
                ImGui::Text("Direct Lighting Active : %s", IsHWRTDirectLightingActive ? "YES" : "NO");
                if (!IsHWRTInlineSupported)
                    ImGui::TextUnformatted("Inline RayQuery : Unsupported (fallback to DispatchRays)");

                int32 HWRTDirectLightingMode = Clamp(gOptions.HWRTDirectLightingMode, 0, 1);
                if (HWRTDirectLightingMode == 1 && !IsHWRTInlineSupported)
                {
                    HWRTDirectLightingMode = 0;
                    gOptions.HWRTDirectLightingMode = 0;
                }
                if (ImGui::BeginCombo("DirectLightingMode", GHWRTDirectLightingModes[HWRTDirectLightingMode], ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < (int32)_countof(GHWRTDirectLightingModes); ++i)
                    {
                        if (i == 1 && !IsHWRTInlineSupported)
                            continue;
                        const bool IsSelected = (HWRTDirectLightingMode == i);
                        if (ImGui::Selectable(GHWRTDirectLightingModes[i], IsSelected))
                        {
                            gOptions.HWRTDirectLightingMode = i;
                            HWRTDirectLightingMode = i;
                        }
                        if (IsSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                int32 HWRTDebugMode = gOptions.HWRTDebugViewMode;
                if (HWRTDebugMode < 0 || HWRTDebugMode >= (int32)_countof(GHWRTDebugViewModes))
                {
                    HWRTDebugMode = 0;
                    gOptions.HWRTDebugViewMode = 0;
                }
                if (ImGui::BeginCombo("DebugView", GHWRTDebugViewModes[HWRTDebugMode], ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < (int32)_countof(GHWRTDebugViewModes); ++i)
                    {
                        const bool IsSelected = (gOptions.HWRTDebugViewMode == i);
                        if (ImGui::Selectable(GHWRTDebugViewModes[i], IsSelected))
                            gOptions.HWRTDebugViewMode = i;
                        if (IsSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("DebugLineWidth", &gOptions.HWRTDebugLineWidth, 0.001f, 0.1f, "%.4f");
                ImGui::SliderFloat("DebugUVScale", &gOptions.HWRTDebugUVScale, 1.0f, 128.0f, "%.1f");
                ImGui::SliderFloat("DebugPrimitiveIDScale", &gOptions.HWRTDebugPrimitiveIDScale, 0.1f, 16.0f, "%.2f");
                ImGui::Checkbox("ForceMipLevel0", &gOptions.HWRTForceMipLevel0);
                ImGui::SliderFloat("NormalBias", &gOptions.HWRTNormalBias, 0.0f, 50.0f, "%.5f");
                ImGui::SliderFloat("ShadowRayStartOffset", &gOptions.HWRTShadowRayStartOffset, 0.0f, 50.0f, "%.5f");

                bool ConditionUseHWRTDirectLighting = gOptions.UseHWRTDirectLighting;
                bool ConditionUseRaytracing = gOptions.UseRaytracing;
                bool ConditionSupportRaytracing = GSupportRaytracing;
                bool ConditionHasRenderFrameContext = HasRenderFrameContext;
                bool ConditionHasRaytracingScene = HasRaytracingScene;
                bool ConditionRaytracingSceneValid = IsRaytracingSceneValid;
                bool ConditionNotForwardRenderer = !IsForwardRenderer;
                bool ConditionInlineRaytracingSupport = IsHWRTInlineSupported;
                ImGui::Separator();
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Cond: gOptions.UseHWRTDirectLighting", &ConditionUseHWRTDirectLighting);
                ImGui::Checkbox("Cond: gOptions.UseRaytracing", &ConditionUseRaytracing);
                ImGui::Checkbox("Cond: GSupportRaytracing", &ConditionSupportRaytracing);
                ImGui::Checkbox("Cond: GSupportInlineRaytracing", &ConditionInlineRaytracingSupport);
                ImGui::Checkbox("Cond: RenderFrameContext", &ConditionHasRenderFrameContext);
                ImGui::Checkbox("Cond: RaytracingScene", &ConditionHasRaytracingScene);
                ImGui::Checkbox("Cond: RaytracingScene->IsValid", &ConditionRaytracingSceneValid);
                ImGui::Checkbox("Cond: !UseForwardRenderer", &ConditionNotForwardRenderer);
                ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::TextUnformatted((ResolvedHWRTDirectLightingMode == 1 && IsHWRTInlineSupported)
                    ? "Inline RayQuery based direct lighting path."
                    : "DispatchRays based direct lighting path.");
						ImGui::EndTabItem();
			}

					if (bShowSSGIPanel && ImGui::BeginTabItem("SSGI", &bShowSSGIPanel, ConsumeFeatureTabFlags(bSelectSSGIFeatureTab)))
			{
                FeatureActiveTab = "SSGI";
                ImGui::Checkbox("UseSSGI", &gOptions.UseSSGI);
                if (gOptions.UseSSGI)
                {
                    ImGui::Indent();
                    ImGui::Checkbox("Show SSGI Only", &gOptions.ShowSSGIOnly);
                    ImGui::Checkbox("Temporal Accumulation", &gOptions.UseSSGITemporalAccumulation);
                    if (gOptions.UseSSGITemporalAccumulation)
                    {
                        ImGui::Indent();
                        ImGui::SliderFloat("Blend Factor", &gOptions.SSGIAccumBlendFactor, 0.0f, 1.0f);
                        AddCopyPasteContextMenu("FeatureSSGIBlendFactorContext", gOptions.SSGIAccumBlendFactor);
                        ImGui::Unindent();
                    }
                    ImGui::Checkbox("UseSSGIReprojection", &gOptions.UseSSGIReprojection);
                    if (!gOptions.UseSSGIReprojection)
                        ImGui::BeginDisabled();
                    ImGui::Checkbox("UseDiscontinuityWeightForSSGI", &gOptions.UseDiscontinuityWeightForSSGI);
                    if (!gOptions.UseSSGIReprojection)
                        ImGui::EndDisabled();
                    ImGui::Checkbox("Apply attenuation", &gOptions.UseSSGIAttenuation);
                    ImGui::SliderFloat("Intensity", &gOptions.SSGIIntensity, 0.0f, 30.0f);
                    AddCopyPasteContextMenu("FeatureSSGIIntensityContext", gOptions.SSGIIntensity);
                    ImGui::SliderFloat("Resolution Scale", &gOptions.SSGIResolutionScale, 0.25f, 1.0f);
                    AddCopyPasteContextMenu("FeatureSSGIResolutionScaleContext", gOptions.SSGIResolutionScale);
                    ImGui::SliderInt("Ray Count", &gOptions.SSGIRayCount, 1, 20);
                    ImGui::SliderInt("Max Steps", &gOptions.SSGIMaxSteps, 1, 64);
                    ImGui::SliderFloat("Max Distance", &gOptions.SSGIMaxDistance, 1.0f, 1000.0f);
                    AddCopyPasteContextMenu("FeatureSSGIMaxDistanceContext", gOptions.SSGIMaxDistance);
                    ImGui::Unindent();
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "SSGI denosing");
                if (ImGui::BeginCombo("SSGIDenoiser", gOptions.GetDenoiseName(gOptions.SSGIDenoiser), ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < _countof(GDenoisers); ++i)
                    {
                        const bool is_selected = (gOptions.SSGIDenoiser == (EDenoiser)i);
                        if (ImGui::Selectable(GDenoisers[i], is_selected))
                            gOptions.SSGIDenoiser = (EDenoiser)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (gOptions.SSGIDenoiser != EDenoiser::NONE)
                {
                    ImGui::Indent();
                    ImGui::SliderInt("KernelSize", &gOptions.SSGIDenoiserKernelSize, 1, 20);
                    if ((gOptions.SSGIDenoiserKernelSize % 2) == 0)
                        gOptions.SSGIDenoiserKernelSize++;
                    ImGui::SliderFloat("KernelSigma", &gOptions.SSGIDenoiserKernelSigma, 0.1f, 30.0f);
                    AddCopyPasteContextMenu("FeatureSSGIKernelSigmaContext", gOptions.SSGIDenoiserKernelSigma);
                    ImGui::SliderFloat("BilateralSigma", &gOptions.SSGIDenoiserBilateralKernelSigma, 0.001f, 0.1f);
                    AddCopyPasteContextMenu("FeatureSSGIBilateralSigmaContext", gOptions.SSGIDenoiserBilateralKernelSigma);
                    ImGui::SliderInt("BlurQuality", &gOptions.SSGIBlurQuality, 1, 5);
                    ImGui::Unindent();
                }
                if (gOptions.SSGIDenoiser == EDenoiser::A_TROUS)
                {
                    ImGui::Indent();
                    ImGui::SliderFloat("Sigma_Color", &gOptions.SSGIATrousSigmaColor, 0.0f, 10.0f);
                    AddCopyPasteContextMenu("FeatureSSGISigmaColorContext", gOptions.SSGIATrousSigmaColor);
                    ImGui::SliderFloat("Sigma_Normal", &gOptions.SSGIATrousSigmaNormal, 0.0f, 1.0f);
                    AddCopyPasteContextMenu("FeatureSSGISigmaNormalContext", gOptions.SSGIATrousSigmaNormal);
                    ImGui::SliderFloat("Sigma_Depth", &gOptions.SSGIATrousSigmaDepth, 0.0f, 10.0f);
                    AddCopyPasteContextMenu("FeatureSSGISigmaDepthContext", gOptions.SSGIATrousSigmaDepth);
                    ImGui::Unindent();
                }
						ImGui::EndTabItem();
			}

					if (GSupportRaytracing && bShowAOPanel && ImGui::BeginTabItem("AO", &bShowAOPanel, ConsumeFeatureTabFlags(bSelectAOFeatureTab)))
			{
                FeatureActiveTab = "AO";
                for (int32 i = 0; i < _countof(GAOType); ++i)
                {
                    if (!GSupportRaytracing && i == gOptions.GetRTAOIndex())
                        continue;
                    ImGui::RadioButton(GAOType[i], &gOptions.AOType, i);
                }

                if (gOptions.AOType == 0 && !GSupportRaytracing)
                    gOptions.AOType = 1;

                ImGui::Checkbox("ShowDebugRT", &gOptions.ShowDebugRT);
                ImGui::Checkbox("ShowAOOnly", &gOptions.ShowAOOnly);
                if (ImGui::BeginCombo("AO RT Res(%)", gOptions.UseResolution, ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < _countof(GAOResolution); ++i)
                    {
                        const bool is_selected = (gOptions.UseResolution == GAOResolution[i]);
                        if (ImGui::Selectable(GAOResolution[i], is_selected))
                            gOptions.UseResolution = GAOResolution[i];
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (gOptions.IsRTAO())
                {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "RTAO ray options");
                    ImGui::SliderFloat("Radius", &gOptions.AORadius, 0.0f, 150.0f);
                    AddCopyPasteContextMenu("FeatureRTAORadiusContext", gOptions.AORadius);
                    ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
                    AddCopyPasteContextMenu("FeatureRTAOIntensityContext", gOptions.AOIntensity);
                    ImGui::SliderInt("RayPerPixel", &gOptions.RayPerPixel, 1, 100);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Temporal denosing");
                    ImGui::Checkbox("UseAOReprojection", &gOptions.UseAOReprojection);
                    if (!gOptions.UseAOReprojection)
                        ImGui::BeginDisabled();
                    ImGui::Checkbox("UseDiscontinuityWeight", &gOptions.UseDiscontinuityWeight);
                    if (!gOptions.UseAOReprojection)
                        ImGui::EndDisabled();
                    ImGui::Checkbox("UseHaltonJitter", &gOptions.UseHaltonJitter);
                    ImGui::Checkbox("UseAccumulateRay", &gOptions.UseAccumulateRay);
                }
                else if (gOptions.IsSSAO())
                {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "SSAO ray options");
                    ImGui::SliderFloat("Radius", &gOptions.AORadius, 0.0f, 150.0f);
                    AddCopyPasteContextMenu("FeatureSSAORadiusContext", gOptions.AORadius);
                    ImGui::SliderFloat("Bias(avoid banding)", &gOptions.SSAOBias, 0.0f, 150.0f);
                    AddCopyPasteContextMenu("FeatureSSAOBiasContext", gOptions.SSAOBias);
                    ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
                    AddCopyPasteContextMenu("FeatureSSAOIntensityContext", gOptions.AOIntensity);
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Spatial denosing");
                if (ImGui::BeginCombo("Denoiser", gOptions.GetDenoiseName(gOptions.Denoiser), ImGuiComboFlags_None))
                {
                    for (int32 i = 0; i < _countof(GDenoisers); ++i)
                    {
                        const bool is_selected = (gOptions.Denoiser == (EDenoiser)i);
                        if (ImGui::Selectable(GDenoisers[i], is_selected))
                            gOptions.Denoiser = (EDenoiser)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderInt("KernelSize", &gOptions.GaussianKernelSize, 1, 20);
                if ((gOptions.GaussianKernelSize % 2) == 0)
                    gOptions.GaussianKernelSize++;
                ImGui::SliderFloat("KernelSigma", &gOptions.GaussianKernelSigma, 0.1f, 30.0f);
                AddCopyPasteContextMenu("FeatureGaussianKernelSigmaContext", gOptions.GaussianKernelSigma);
                ImGui::SliderFloat("BilateralSigma", &gOptions.BilateralKernelSigma, 0.001f, 0.1f);
                AddCopyPasteContextMenu("FeatureBilateralKernelSigmaContext", gOptions.BilateralKernelSigma);
						ImGui::EndTabItem();
			}

#ifdef ENABLE_EDITOR_FEATURES
                    if (bShowPlacementPanel && ImGui::BeginTabItem("Placement Tool", &bShowPlacementPanel, ConsumeFeatureTabFlags(bSelectPlacementFeatureTab)))
            {
                FeatureActiveTab = "Placement Tool";
                if (g_Editor)
                {
                    g_Editor->Placement.EnablePlacementMode = true;
                    auto mainCamera = jCamera::GetMainCamera();
                    g_Editor->Placement.RenderUI(mainCamera);
                }
                        ImGui::EndTabItem();
            }
#endif

                    if (bShowLightPanel && ImGui::BeginTabItem("Light", &bShowLightPanel, ConsumeFeatureTabFlags(bSelectLightFeatureTab)))
            {
                FeatureActiveTab = "Light";
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Camera Info");
                ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);
                AddCopyPasteContextMenu("FeatureCameraPosContext", gOptions.CameraPos, [](float x, float y, float z) {
                    auto mainCamera = jCamera::GetMainCamera();
                    if (mainCamera)
                    {
                        Vector newPos(x, y, z);
                        Vector offset = newPos - mainCamera->Pos;
                        mainCamera->Pos = newPos;
                        mainCamera->Target += offset;
                        mainCamera->Up += offset;
                    }
                });

                auto mainCamera = jCamera::GetMainCamera();
                if (mainCamera)
                {
                    Vector cameraDir = mainCamera->GetForwardVector();
                    ImGui::Text("CameraDir : %.2f, %.2f, %.2f", cameraDir.x, cameraDir.y, cameraDir.z);
                    AddCopyPasteContextMenu("FeatureCameraDirContext", cameraDir, [mainCamera](float x, float y, float z) {
                        Vector newDir(x, y, z);
                        newDir = newDir.GetNormalize();
                        Vector eulerAngle = Vector::GetEulerAngleFrom(newDir);
                        mainCamera->SetEulerAngle(eulerAngle);
                    });
                }
                        ImGui::EndTabItem();
            }

                    if (bShowProfilerPanel && ImGui::BeginTabItem("Profiler", &bShowProfilerPanel, ConsumeFeatureTabFlags(bSelectProfilerFeatureTab)))
            {
                FeatureActiveTab = "Profiler";
                static std::string sFeatureTraceConnectMessage;
                static std::string sFeatureTraceConnectPath;

                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Build-time (Read-only)");

                const char* backendName = "Legacy";
#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY
                backendName = "Tracy";
#endif
                ImGui::Text("Backend : %s", backendName);

                bool externalCpuAvailable = (JPROFILE_EXTERNAL_CPU_AVAILABLE != 0);
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("External CPU Profiler Available", &externalCpuAvailable);
                bool localCpuWithExternal = (ENABLE_LOCAL_CPU_PROFILE_WITH_EXTERNAL != 0);
                ImGui::Checkbox("Local CPU Profile With External", &localCpuWithExternal);
                bool tracySystemTracing = (JPROFILE_TRACY_ENABLE_SYSTEM_TRACING != 0);
                ImGui::Checkbox("Tracy System Tracing", &tracySystemTracing);
                ImGui::EndDisabled();

#if JPROFILE_BACKEND == JPROFILE_BACKEND_TRACY && JPROFILE_EXTERNAL_CPU_AVAILABLE
                const bool elevated = IsCurrentProcessElevated();
                if (!elevated)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Context switch/CPU Data needs administrator run on Windows.");
                    ImGui::TextWrapped("Run Visual Studio (or jEngine.exe) as administrator, then reconnect Tracy.");
                }

                bool tracyConnected = TracyIsConnected;
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Tracy Connected", &tracyConnected);
                bool tracyRunAsAdmin = elevated;
                ImGui::Checkbox("Run As Administrator", &tracyRunAsAdmin);
                ImGui::EndDisabled();

                if (ImGui::Button("Trace Connect"))
                {
                    std::string resolvedPath;
                    std::string launchArgs;
                    std::string error;
                    if (LaunchTracyProfiler(resolvedPath, launchArgs, error))
                    {
                        sFeatureTraceConnectPath = resolvedPath;
                        sFeatureTraceConnectMessage = "tracy-profiler.exe launched and auto-connect requested.";
                        if (!launchArgs.empty())
                            sFeatureTraceConnectPath += " " + launchArgs;
                    }
                    else
                    {
                        sFeatureTraceConnectMessage = error;
                    }
                }
                if (!sFeatureTraceConnectMessage.empty())
                    ImGui::TextWrapped("%s", sFeatureTraceConnectMessage.c_str());
                if (!sFeatureTraceConnectPath.empty())
                    ImGui::TextWrapped("Path: %s", sFeatureTraceConnectPath.c_str());
#endif

                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Runtime Options");
                ImGui::Checkbox("EnableAppInfo", &g_jProfileRuntimeOptions.EnableAppInfo);
                ImGui::Checkbox("EnableMessages", &g_jProfileRuntimeOptions.EnableMessages);
                ImGui::Checkbox("EnablePlots", &g_jProfileRuntimeOptions.EnablePlots);
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Memory Tracking (Restart)", &g_jProfileRuntimeOptions.EnableMemoryTracking);
                ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::Text("Frame Image");
                ImGui::Checkbox("Enable", &g_jProfileRuntimeOptions.EnableFrameImage);
                ImGui::SliderInt("Capture Interval", &g_jProfileRuntimeOptions.FrameImageCaptureInterval, 1, 240);
                ImGui::SliderInt("Max Width", &g_jProfileRuntimeOptions.FrameImageCaptureMaxWidth, 64, 1920);
                ImGui::SliderInt("Source Frame Lag", &g_jProfileRuntimeOptions.FrameImageSourceFrameLag, 0, 16);
                ImGui::Checkbox("Skip If Source Not Ready", &g_jProfileRuntimeOptions.FrameImageSkipIfSourceNotReady);
                        ImGui::EndTabItem();
            }

					if (bShowDebugVisualizationPanel && ImGui::BeginTabItem("Debug Visualization", &bShowDebugVisualizationPanel, ConsumeFeatureTabFlags(bSelectDebugVisualizationFeatureTab)))
			{
                FeatureActiveTab = "Debug Visualization";
				ImGui::Checkbox("ShowDebugObject", &gOptions.ShowDebugObject);
				ImGui::Checkbox("ShowDebugRT", &gOptions.ShowDebugRT);
				ImGui::Checkbox("ShowAOOnly", &gOptions.ShowAOOnly);
				ImGui::Checkbox("ShowSSGIOnly", &gOptions.ShowSSGIOnly);
				ImGui::Separator();
				int32 HWRTDebugMode = gOptions.HWRTDebugViewMode;
				if (HWRTDebugMode < 0 || HWRTDebugMode >= (int32)_countof(GHWRTDebugViewModes))
				{
					HWRTDebugMode = 0;
					gOptions.HWRTDebugViewMode = 0;
				}
				if (ImGui::BeginCombo("HWRT DebugView", GHWRTDebugViewModes[HWRTDebugMode], ImGuiComboFlags_None))
				{
					for (int32 i = 0; i < (int32)_countof(GHWRTDebugViewModes); ++i)
					{
						const bool IsSelected = (gOptions.HWRTDebugViewMode == i);
						if (ImGui::Selectable(GHWRTDebugViewModes[i], IsSelected))
							gOptions.HWRTDebugViewMode = i;
						if (IsSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::SliderFloat("HWRT DebugLineWidth", &gOptions.HWRTDebugLineWidth, 0.001f, 0.1f, "%.4f");
				ImGui::SliderFloat("HWRT DebugUVScale", &gOptions.HWRTDebugUVScale, 1.0f, 128.0f, "%.1f");
						ImGui::EndTabItem();
			}
					ImGui::EndTabBar();
				}
			}
			ImGui::End();
            if (!bFeaturePanelOpen)
                bFeaturePanelAutoHidden = true;
		}

        PersistUIStateIfNeeded();

		// Console overlay (rendered on top of everything)
		jConsole::Get().Render();
	});
	g_ImGUI->Draw(RenderFrameContextPtr);
}





