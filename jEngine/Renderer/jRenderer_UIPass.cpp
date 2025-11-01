#include "pch.h"
#include "jRenderer.h"
#include "ImGui/jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jDirectionalLight.h"

// Helper functions for Copy/Paste context menus
namespace
{
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
	g_ImGUI->NewFrame([]()
	{
		Vector4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

		char szTitle[128] = { 0, };
		sprintf_s(szTitle, sizeof(szTitle), "RHI : %s", g_rhi->GetRHIName().ToStr());

		ImGui::SetNextWindowPos(ImVec2(27.0f, 27.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(350.0f, 682.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin(szTitle);
			
		if (ImGui::BeginTabBar("RHI"))
		{
			if (ImGui::BeginTabItem("Default")) 
			{
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

				ImGui::Separator();
				ImGui::TextColored(ImVec4(1, 1, 0, 1), "Directional Light");
				if (ImGui::SliderFloat3("Light Direction", &gOptions.SunDir.x, -1.0f, 1.0f))
				{
					// Normalize the direction vector
					gOptions.SunDir = gOptions.SunDir.GetNormalize();

					// Update directional light direction when UI changes
					const auto& lights = jLight::GetLights();
					for (auto light : lights)
					{
						if (light->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
							dirLight->SetDirection(gOptions.SunDir);
							break;
						}
					}
				}
				AddCopyPasteContextMenu("LightDirectionContext", gOptions.SunDir, [](float x, float y, float z) {
					gOptions.SunDir.x = x;
					gOptions.SunDir.y = y;
					gOptions.SunDir.z = z;
					gOptions.SunDir = gOptions.SunDir.GetNormalize();

					// Update light
					const auto& lights = jLight::GetLights();
					for (auto light : lights)
					{
						if (light->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
							dirLight->SetDirection(gOptions.SunDir);
							break;
						}
					}
				});

				if (ImGui::ColorEdit3("Light Color", &gOptions.DirectionalLightColor.x, ImGuiColorEditFlags_Float |
					ImGuiColorEditFlags_HDR))
				{
					// Update directional light color when UI changes
					const auto& lights = jLight::GetLights();
					for (auto light : lights)
					{
						if (light->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
							Vector finalColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
							Vector color = Vector4(finalColor.x, finalColor.y, finalColor.z, 1.0f);
							dirLight->SetColor(color);
							break;
						}
					}
				}
				AddCopyPasteContextMenu("LightColorContext", gOptions.DirectionalLightColor, [](float x, float y, float z) {
					gOptions.DirectionalLightColor.x = x;
					gOptions.DirectionalLightColor.y = y;
					gOptions.DirectionalLightColor.z = z;

					// Update light
					const auto& lights = jLight::GetLights();
					for (auto light : lights)
					{
						if (light->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
							Vector finalColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
							Vector color = Vector4(finalColor.x, finalColor.y, finalColor.z, 1.0f);
							dirLight->SetColor(color);
							break;
						}
					}
				});

				if (ImGui::SliderFloat("Light Intensity", &gOptions.DirectionalLightIntensity, 0.0f, 100.0f))
				{
					// Update directional light intensity when UI changes
					const auto& lights = jLight::GetLights();
					for (auto light : lights)
					{
						if (light->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
							Vector finalColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
							Vector color = Vector4(finalColor.x, finalColor.y, finalColor.z, 1.0f);
							dirLight->SetColor(color);
							break;
						}
					}
				}
				AddCopyPasteContextMenu("LightIntensityContext", gOptions.DirectionalLightIntensity, [](float value) {
					gOptions.DirectionalLightIntensity = value;

					// Update light
					const auto& lights = jLight::GetLights();
					for (auto light : lights)
					{
						if (light->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(light);
							Vector finalColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
							Vector color = Vector4(finalColor.x, finalColor.y, finalColor.z, 1.0f);
							dirLight->SetColor(color);
							break;
						}
					}
				});

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
	});
	g_ImGUI->Draw(RenderFrameContextPtr);
}