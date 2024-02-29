#include "pch.h"
#include "jRenderer.h"
#include "ImGui/jImGui.h"
#include "Profiler/jPerformanceProfile.h"
#include "jOptions.h"

void SearchFilesRecursive(std::vector<std::string>& OutFiles, const std::string& InTargetDirectory, const std::vector<std::string>& extensions) 
{
	WIN32_FIND_DATAA findFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// 첫 번째 파일 검색
	hFind = FindFirstFileA((InTargetDirectory + "\\*").c_str(), &findFileData);

	if (hFind == INVALID_HANDLE_VALUE) 
	{
		return;
	}

	do 
	{
		if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) 
		{
			// 현재 항목이 디렉토리인 경우 (현재 디렉토리와 상위 디렉토리 제외)
			if (strcmp(findFileData.cFileName, ".") != 0 && strcmp(findFileData.cFileName, "..") != 0) 
			{
				SearchFilesRecursive(OutFiles, InTargetDirectory + "\\" + findFileData.cFileName, extensions); // 재귀 호출
			}
		}
		else {
			// 파일 확장자 확인
			std::string fileName = findFileData.cFileName;
			auto it = fileName.rfind('.');
			if (it != std::string::npos) 
			{
				std::string ext = fileName.substr(it);
				// 지정된 확장자 중 하나와 일치하는지 확인
				for (const auto& allowedExt : extensions) 
				{
					if (ext == allowedExt) 
					{
						OutFiles.push_back(InTargetDirectory + "\\" + fileName); // 조건에 맞는 파일 경로를 vector에 추가
						break;
					}
				}
			}
		}
	} while (FindNextFileA(hFind, &findFileData) != 0);

	FindClose(hFind); // 핸들 닫기
}

std::string ExtractFileName(const std::string& path) 
{
	// Windows와 POSIX 시스템의 경로 구분자 위치 찾기
	size_t lastSlashPos = path.find_last_of("/\\");

	if (lastSlashPos != std::string::npos) {
		// 경로 구분자 이후의 문자열(파일 이름) 반환
		return path.substr(lastSlashPos + 1);
	}
	else {
		// 경로 구분자가 없는 경우, 전체 문자열이 파일 이름임
		return path;
	}
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

			static bool IsUsePathTracing = true;
			if (IsUsePathTracing)
			{
				ImGui::SetNextWindowPos(ImVec2(400.0f, 27.0f), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSize(ImVec2(200.0f, 80.0f), ImGuiCond_FirstUseEver);
				if (ImGui::Begin("PathTracing Options", 0, ImGuiWindowFlags_AlwaysAutoResize))
				{
					static bool initialized = false;
					if (!initialized)
					{
						initialized = true;

						SearchFilesRecursive(gPathTracingScenes, "Resource/PathTracing", {".scene"});
						gPathTracingScenesNameOnly.resize(gPathTracingScenes.size());
						for (int32 i = 0; i < gPathTracingScenes.size(); ++i)
						{
							gPathTracingScenesNameOnly[i] = ExtractFileName(gPathTracingScenes[i]);
						}
						
						if (gPathTracingScenesNameOnly.size() > 0)
							gSelectedScene = gPathTracingScenesNameOnly[0].c_str();
					}

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

					ImGui::End();
				}
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
	});
	g_ImGUI->Draw(RenderFrameContextPtr);
}