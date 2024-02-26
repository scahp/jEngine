#include "pch.h"
#include "jRenderer.h"
#include "ImGui/jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"

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

				ImGui::Separator();
				ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);

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
						ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
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
						ImGui::SliderFloat("Bias(avoid banding)", &gOptions.SSAOBias, 0.0f, 150.0f);
						ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
					}

					ImGui::Separator();
					ImGui::TextColored(ImVec4(1, 1, 0, 1), "Spatial denosing");
					if (ImGui::BeginCombo("Denoiser", gOptions.Denoiser, ImGuiComboFlags_None))
					{
						for (int32 i = 0; i < _countof(GDenoisers); ++i)
						{
							const bool is_selected = (gOptions.Denoiser == GDenoisers[i]);
							if (ImGui::Selectable(GDenoisers[i], is_selected))
								gOptions.Denoiser = GDenoisers[i];
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
					ImGui::SliderFloat("BilateralSigma", &gOptions.BilateralKernelSigma, 0.001f, 0.1f);

					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
	});
	g_ImGUI->Draw(RenderFrameContextPtr);
}