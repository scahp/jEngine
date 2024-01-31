#include "pch.h"
#include "jRenderer.h"
#include "ImGui/jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"

void jRenderer::UIPass()
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
				{
					ImGui::Text("[CPU]");
					const auto& CPUAvgProfileMap = jPerformanceProfile::GetInstance().GetCPUAvgProfileMap();
					double TotalPassesMS = 0.0;
					int32 MostLeastIndent = INT_MAX;
					for (auto& pair : CPUAvgProfileMap)
					{
						const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
						const float Indent = IndentSpace * (float)AvgProfile.Indent;
						if (Indent > 0)
							ImGui::Indent(Indent);

						if (CurrentThreadId == AvgProfile.ThreadId)
							ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
						else
							ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);

						if (Indent > 0)
							ImGui::Unindent(Indent);

						// 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
						// 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
						if (CurrentThreadId == AvgProfile.ThreadId)
						{
							if (MostLeastIndent > AvgProfile.Indent)
							{
								MostLeastIndent = AvgProfile.Indent;
								TotalPassesMS = AvgProfile.AvgElapsedMS;
							}
							else if (MostLeastIndent == AvgProfile.Indent)
							{
								TotalPassesMS += AvgProfile.AvgElapsedMS;
							}
						}
					}
					ImGui::Text("[CPU]Total Passes : %lf ms", TotalPassesMS);
				}
				ImGui::Separator();
				{
					ImGui::Text("[GPU]");
					const auto& GPUAvgProfileMap = jPerformanceProfile::GetInstance().GetGPUAvgProfileMap();
					double TotalPassesMS = 0.0;
					int32 MostLeastIndent = INT_MAX;
					for (auto& pair : GPUAvgProfileMap)
					{
						const jPerformanceProfile::jAvgProfile& AvgProfile = pair.second;
						const float Indent = IndentSpace * (float)AvgProfile.Indent;
						if (Indent > 0)
							ImGui::Indent(Indent);

						if (CurrentThreadId == AvgProfile.ThreadId)
							ImGui::Text("%s : %lf ms", pair.first.ToStr(), AvgProfile.AvgElapsedMS);
						else
							ImGui::TextColored(OtherThreadColor, "%s : %lf ms [0x%p]", pair.first.ToStr(), AvgProfile.AvgElapsedMS, AvgProfile.ThreadId);

						if (Indent > 0)
							ImGui::Unindent(Indent);

						// 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
						// 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
						if (CurrentThreadId == AvgProfile.ThreadId)
						{
							if (MostLeastIndent > AvgProfile.Indent)
							{
								MostLeastIndent = AvgProfile.Indent;
								TotalPassesMS = AvgProfile.AvgElapsedMS;
							}
							else if (MostLeastIndent == AvgProfile.Indent)
							{
								TotalPassesMS += AvgProfile.AvgElapsedMS;
							}
						}
					}
					ImGui::Text("[GPU]Total Passes : %lf ms", TotalPassesMS);
				}
				ImGui::Separator();
				//for (auto& pair : CounterMap)
				//{
				//    ImGui::Text("%s : %lld", pair.first.ToStr(), pair.second);
				//}
				ImGui::Separator();
				ImGui::Text("CameraPos : %.2f, %.2f, %.2f", gOptions.CameraPos.x, gOptions.CameraPos.y, gOptions.CameraPos.z);
				//ImGui::End();

				ImGui::EndTabItem();
			}

#if SUPPORT_RAYTRACING
			if (ImGui::BeginTabItem("RTAO Options"))
			{
				ImGui::Checkbox("UseRTAO", &gOptions.UseRTAO);
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
			
				ImGui::Separator();
				ImGui::TextColored(ImVec4(1, 1, 0, 1), "RTAO ray options");
				ImGui::SliderFloat("Radius", &gOptions.AORadius, 0.0f, 150.0f);
				ImGui::SliderFloat("Intensity", &gOptions.AOIntensity, 0.0f, 1.0f);
				ImGui::SliderInt("SamplePerPixel", &gOptions.SamplePerPixel, 1, 100);
			
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
			//ImGui::SetWindowFocus(szTitle);
#endif // SUPPORT_RAYTRACING
			ImGui::EndTabBar();
		}
		ImGui::End();
	});
	g_ImGUI->Draw(RenderFrameContextPtr);
}