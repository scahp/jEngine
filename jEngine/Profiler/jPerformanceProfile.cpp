#include "pch.h"
#include "jPerformanceProfile.h"

jPerformanceProfile* jPerformanceProfile::_instance = nullptr;

robin_hood::unordered_map<jPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileCPUMap[MaxProfileFrame];
robin_hood::unordered_map<jPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileGPUMap[MaxProfileFrame];
static int32 PerformanceFrame = 0;

robin_hood::unordered_set<jQuery*> jQueryTimePool::s_running;
robin_hood::unordered_set<jQuery*> jQueryTimePool::s_resting;

std::atomic<int32> jScopedProfile_CPU::s_priority = 0;

thread_local std::atomic<int32> ScopedProfilerCPUIndent;
thread_local std::atomic<int32> ScopedProfilerGPUIndent;

std::vector<jProfile_GPU> jProfile_GPU::WatingResultList[jRHI::MaxWaitingQuerySet];
int32 jProfile_GPU::CurrentWatingResultListIndex = 0;

int32 NextFrame()
{
	jScopedProfile_CPU::ResetPriority();

	PerformanceFrame = (PerformanceFrame + 1) % MaxProfileFrame;
	return PerformanceFrame;
}

void ClearScopedProfileCPU()
{
	for (int32 i = 0; i < MaxProfileFrame; ++i)
		ScopedProfileCPUMap[i].clear();
}

void AddScopedProfileCPU(const jPriorityName& name, uint64 elapsedTick, int32 Indent)
{
	ScopedProfileCPUMap[PerformanceFrame][name] = jScopedProfileData(elapsedTick, Indent, std::this_thread::get_id());
}

void ClearScopedProfileGPU()
{
	for (int32 i = 0; i < MaxProfileFrame; ++i)
		ScopedProfileGPUMap[i].clear();
}

void AddScopedProfileGPU(const jPriorityName& name, uint64 elapsedTick, int32 Indent)
{
	ScopedProfileGPUMap[PerformanceFrame][name] = jScopedProfileData(elapsedTick, Indent, std::this_thread::get_id());
}

void jPerformanceProfile::Update(float deltaTime)
{
	jProfile_GPU::ProcessWaitings();

	CalcAvg();
	NextFrame();

    //if (TRUE_PER_MS(1000))
    //    PrintOutputDebugString();
}

void jPerformanceProfile::CalcAvg()
{
	{
		CPUAvgProfileMap.clear();

		for (int32 i = 0; i < MaxProfileFrame; ++i)
		{
			const auto& scopedProfileMap = ScopedProfileCPUMap[i];
			for (auto& iter : scopedProfileMap)
			{
				auto& avgProfile = CPUAvgProfileMap[iter.first];
				avgProfile.TotalElapsedTick += iter.second.ElapsedTick;
				avgProfile.Indent = iter.second.Indent;
				avgProfile.ThreadId = iter.second.ThreadId;
				++avgProfile.TotalSampleCount;
			}
		}

		for (auto& iter : CPUAvgProfileMap)
		{
			JASSERT(iter.second.TotalSampleCount > 0);
			iter.second.AvgElapsedMS = (iter.second.TotalElapsedTick / static_cast<double>(iter.second.TotalSampleCount)) * 0.000001;	// ns -> ms
		}
	}

	{
		GPUAvgProfileMap.clear();

		for (int32 i = 0; i < MaxProfileFrame; ++i)
		{
			const auto& scopedProfileMap = ScopedProfileGPUMap[i];
			for (auto& iter : scopedProfileMap)
			{
				auto& avgProfile = GPUAvgProfileMap[iter.first];
				avgProfile.TotalElapsedTick += iter.second.ElapsedTick;
				avgProfile.Indent = iter.second.Indent;
				avgProfile.ThreadId = iter.second.ThreadId;
				++avgProfile.TotalSampleCount;
			}
		}

		for (auto& iter : GPUAvgProfileMap)
		{
			JASSERT(iter.second.TotalSampleCount > 0);
			iter.second.AvgElapsedMS = (iter.second.TotalElapsedTick / static_cast<double>(iter.second.TotalSampleCount)) * 0.000001;	// ns -> ms
		}
	}
}

void jPerformanceProfile::PrintOutputDebugString()
{
	std::string result;
	char szTemp[128] = { 0, };
	if (!CPUAvgProfileMap.empty())
	{
		result += "-----CPU---PerformanceProfile----------\n";
		for (auto& iter : CPUAvgProfileMap)
		{
			sprintf_s(szTemp, sizeof(szTemp), "%s : \t\t\t\t%lf ms", iter.first.ToStr(), iter.second.AvgElapsedMS);
			result += szTemp;
			result += "\n";
		}
	}

	if (!GPUAvgProfileMap.empty())
	{
		result += "-----GPU---PerformanceProfile----------\n";
		for (auto& iter : GPUAvgProfileMap)
		{
			sprintf_s(szTemp, sizeof(szTemp), "%s : \t\t\t\t%lf ms", iter.first.ToStr(), iter.second.AvgElapsedMS);
			result += szTemp;
			result += "\n";
		}
	}
	OutputDebugStringA(result.c_str());
}
