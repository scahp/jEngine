#include "pch.h"
#include "jRenderer.h"
#include "ImGui/jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jObject.h"
#include "jEngine.h"
#include "RHI/jRaytracingScene.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include <filesystem>

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

// Helper functions for Copy/Paste context menus
namespace
{
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

		char szTitle[128] = { 0, };
		sprintf_s(szTitle, sizeof(szTitle), "RHI : %s", g_rhi->GetRHIName().ToStr());

		ImGui::SetNextWindowPos(ImVec2(27.0f, 27.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(350.0f, 682.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin(szTitle);
        static bool bSelectHWRTTabByDefault = false;
        static bool bSelectSurfelGITabByDefault = true;
			
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

            const ImGuiTabItemFlags HWRTTabFlags = bSelectHWRTTabByDefault ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("HWRT", nullptr, HWRTTabFlags))
            {
                bSelectHWRTTabByDefault = false;

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

            if (ImGui::BeginTabItem("SSGI Options"))
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

            const ImGuiTabItemFlags SurfelGITabFlags = bSelectSurfelGITabByDefault ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
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
				if (ImGui::BeginTabItem("AO Options"))
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

#if USE_PATH_TRACING
			{
				ImGui::SetNextWindowPos(ImVec2(400.0f, 27.0f), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSize(ImVec2(200.0f, 80.0f), ImGuiCond_FirstUseEver);
				if (ImGui::Begin("PathTracing Options", 0, ImGuiWindowFlags_AlwaysAutoResize))
				{
					if (ImGui::BeginCombo("PathTracingScene", gSelectedScene, ImGuiComboFlags_None))
					{
						for (int32 i = 0; i < (int32)gPathTracingScenesNameOnly.size(); ++i)
						{
							const bool is_selected = (gSelectedScene == gPathTracingScenesNameOnly[i].c_str());
							if (ImGui::Selectable(gPathTracingScenesNameOnly[i].c_str(), is_selected))
							{
								gSelectedScene = gPathTracingScenesNameOnly[i].c_str();
								gSelectedSceneIndex = i;
							}
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					ImGui::SliderInt("MaxRecursionDepth", &gOptions.MaxRecursionDepthForPathTracing, 1, 100);
					ImGui::SliderInt("RayPerPixel", &gOptions.RayPerPixelForPathTracing, 1, 100);

					ImGui::End();
				}
			}
#endif // USE_PATH_TRACING
			ImGui::EndTabBar();
		}
		ImGui::End();

		// Console overlay (rendered on top of everything)
		jConsole::Get().Render();
	});
	g_ImGUI->Draw(RenderFrameContextPtr);
}
