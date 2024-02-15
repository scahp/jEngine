#include "pch.h"
#include "jImGui.h"

jImGUI* g_ImGUI = nullptr;

void jImGUI::CreateTreeForProfiling(const char* InTreeTitle, const jPerformanceProfile::AvgProfileMapType& InProfileData, double InTotalProfileMS, float InTabSpacing /*= 10.0f*/)
{
	const ImVec4 GraphicsQueueColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	const ImVec4 ComputeQueueColor = { 0.4f, 0.4f, 0.8f, 1.0f };
	const ImVec4 CopyQueueColor = { 0.4f, 0.8f, 0.4f, 1.0f };
	const ImVec4 OtherThreadColor = { 0.2f, 0.6f, 0.2f, 1.0f };

	auto GetTextColorByQueueType = [&](ECommandBufferType InType)
	{
		ImVec4 SelectedColor = GraphicsQueueColor;
		switch (InType)
		{
		case ECommandBufferType::GRAPHICS:	SelectedColor = GraphicsQueueColor; break;
		case ECommandBufferType::COMPUTE:	SelectedColor = ComputeQueueColor; break;
		case ECommandBufferType::COPY:		SelectedColor = CopyQueueColor; break;
		default: break;
		}
		return SelectedColor;
	};

	auto GetTooltipByQueueType = [](ECommandBufferType InType)
	{
		switch (InType)
		{
		case ECommandBufferType::GRAPHICS: return "Graphics Queue";
		case ECommandBufferType::COMPUTE: return "Compute Queue";
		case ECommandBufferType::COPY: return "Copy Queue";
		default: break;
		}
		return "";
	};

	ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, InTabSpacing);
	auto DefaultTreeNodeOptions = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;
	if (ImGui::TreeNodeEx(InTreeTitle, DefaultTreeNodeOptions, "%s : %.3lf ms", InTreeTitle, InTotalProfileMS))
	{
		int32 CurIndentLevel = 0;
		for (int32 i = 0; i < (int32)InProfileData.size(); ++i)
		{
			const auto& CurProfile = InProfileData[i];
			while (CurIndentLevel > CurProfile.Indent)
			{
				ImGui::TreePop();
				--CurIndentLevel;
			}

			const bool IsTopLevel = (CurIndentLevel == 0 && CurProfile.Indent == 0);
			if (IsTopLevel || CurIndentLevel >= CurProfile.Indent)
			{
				// Check whether this node has leaf node or not
				bool IsLeaf = InProfileData.size() > (i + 1) ? (CurProfile.Indent >= InProfileData[i + 1].Indent || CurProfile.ThreadId != InProfileData[i + 1].ThreadId) : true;
				if (IsMainThread(CurProfile.ThreadId))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, GetTextColorByQueueType(CurProfile.GPUCommandBufferType));
					if (ImGui::TreeNodeEx(CurProfile.Name.ToStr()
						, DefaultTreeNodeOptions | (IsLeaf ? ImGuiTreeNodeFlags_Leaf : 0)
						, "%s : %.3lf ms", CurProfile.Name.ToStr(), CurProfile.AvgElapsedMS))
					{
						++CurIndentLevel;

						if (CurProfile.GPUCommandBufferType != ECommandBufferType::MAX)
						{
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text(GetTooltipByQueueType(CurProfile.GPUCommandBufferType));
								ImGui::EndTooltip();
							}
						}
					}
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Text, OtherThreadColor);
					if (ImGui::TreeNodeEx(CurProfile.Name.ToStr()
						, DefaultTreeNodeOptions | (IsLeaf ? ImGuiTreeNodeFlags_Leaf : 0)
						, "%s : %.3lf ms [0x%p]", CurProfile.Name.ToStr(), CurProfile.AvgElapsedMS, CurProfile.ThreadId))
					{
						++CurIndentLevel;

						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("Executed on sub ThreadId 0x%p", CurProfile.ThreadId);
							ImGui::EndTooltip();
						}
					}
					ImGui::PopStyleColor();
				}
			}


		}
		while (CurIndentLevel--)
		{
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}
	ImGui::PopStyleVar();
}
