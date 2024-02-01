#include "pch.h"
#include "jImGui.h"

jImGUI* g_ImGUI = nullptr;

void jImGUI::CreateTreeForProfiling(const char* InTreeTitle, const jPerformanceProfile::AvgProfileMapType& InProfileData, double InTotalProfileMS, float InTabSpacing /*= 10.0f*/)
{
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
				bool IsLeaf = InProfileData.size() > (i + 1) ? CurProfile.Indent >= InProfileData[i + 1].Indent : true;

				if (ImGui::TreeNodeEx(CurProfile.Name.ToStr()
					, DefaultTreeNodeOptions | (IsLeaf ? ImGuiTreeNodeFlags_Leaf : 0)
					, "%s : %.3lf ms", CurProfile.Name.ToStr(), CurProfile.AvgElapsedMS))
				{
					++CurIndentLevel;
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
