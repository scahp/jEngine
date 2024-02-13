#include "pch.h"
#include "jPerformanceProfile.h"

jPerformanceProfile* jPerformanceProfile::_instance = nullptr;

jMutexRWLock ScopedCPULock;
jMutexRWLock ScopedGPULock;
robin_hood::unordered_map<jPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileCPUMap[MaxProfileFrame];
robin_hood::unordered_map<jGPUPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileGPUMap[MaxProfileFrame];
static int32 PerformanceFrame = 0;

robin_hood::unordered_set<jQuery*> jQueryTimePool::s_running[(uint32)ECommandBufferType::MAX];
robin_hood::unordered_set<jQuery*> jQueryTimePool::s_pending[(uint32)ECommandBufferType::MAX];

std::atomic<int32> jScopedProfile_CPU::s_priority = 0;
std::atomic<int32> jScopedProfile_GPU::s_priority = 0;

thread_local std::atomic<int32> ScopedProfilerCPUIndent;
thread_local std::atomic<int32> ScopedProfilerGPUIndent;

std::vector<jProfile_GPU> jProfile_GPU::WatingResultList[jRHI::MaxWaitingQuerySet];
int32 jProfile_GPU::CurrentWatingResultListIndex = 0;

int32 NextFrame()
{
	jScopedProfile_CPU::ResetPriority();
	jScopedProfile_GPU::ResetPriority();

	PerformanceFrame = (PerformanceFrame + 1) % MaxProfileFrame;

	// 이번에 수집할 프로파일링 데이터 초기화
	{
		jScopeWriteLock s(&ScopedCPULock);
		ScopedProfileCPUMap[PerformanceFrame].clear();
	}
	{
		jScopeWriteLock s(&ScopedGPULock);
		ScopedProfileGPUMap[PerformanceFrame].clear();
	}
	return PerformanceFrame;
}

void ClearScopedProfileCPU()
{
	jScopeWriteLock s(&ScopedCPULock);

	for (int32 i = 0; i < MaxProfileFrame; ++i)
		ScopedProfileCPUMap[i].clear();
}

void AddScopedProfileCPU(const jPriorityName& name, uint64 elapsedTick, int32 Indent)
{
	jScopeWriteLock s(&ScopedCPULock);
	ScopedProfileCPUMap[PerformanceFrame][name] = jScopedProfileData(elapsedTick, Indent, std::this_thread::get_id());
}

void ClearScopedProfileGPU()
{
	jScopeWriteLock s(&ScopedGPULock);
	for (int32 i = 0; i < MaxProfileFrame; ++i)
		ScopedProfileGPUMap[i].clear();
}

void AddScopedProfileGPU(const jGPUPriorityName& name, uint64 elapsedTick, int32 Indent)
{
	jScopeWriteLock s(&ScopedGPULock);
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
	auto AccumulateTotalMS = [](double& OutTotalCPUPassesMS, int32& InOutMostLeastIndent, const jAvgProfile& InProfile)
	{
		// 최 상위에 있는 Pass 의 평균 MS 만 더하면 하위에 있는 모든 MS 는 다 포함됨
		// 다른 스레드에 한 작업도 렌더링 프레임이 종료 되기 전에 마치기 때문에 추가로 더해줄 필요 없음
		if (IsMainThread(InProfile.ThreadId))
		{
			if (InOutMostLeastIndent > InProfile.Indent)
			{
				InOutMostLeastIndent = InProfile.Indent;
				OutTotalCPUPassesMS = InProfile.AvgElapsedMS;
			}
			else if (InOutMostLeastIndent == InProfile.Indent)
			{
				OutTotalCPUPassesMS += InProfile.AvgElapsedMS;
			}
		}
	};
	
	{
        std::map<jPriorityName, jAvgProfile, jPriorityNameComapreFunc> SumOfScopedProfileCPUMap;
		{
			jScopeReadLock s(&ScopedCPULock);
			for (int32 i = 0; i < MaxProfileFrame; ++i)
			{
				const auto& scopedProfileMap = ScopedProfileCPUMap[i];
				for (auto& iter : scopedProfileMap)
				{
					auto& avgProfile = SumOfScopedProfileCPUMap[iter.first];
					avgProfile.TotalElapsedTick += iter.second.ElapsedTick;
					avgProfile.Indent = iter.second.Indent;
					avgProfile.ThreadId = iter.second.ThreadId;
					avgProfile.Name = iter.first;
					++avgProfile.TotalSampleCount;
				}
			}
		}

		AvgCPUProfiles.clear();
		AvgCPUProfiles.reserve(SumOfScopedProfileCPUMap.size());
		int32 MostLeastIndent = INT_MAX;
		TotalAvgCPUPassesMS = 0.0;
		for (auto& iter : SumOfScopedProfileCPUMap)
		{
			JASSERT(iter.second.TotalSampleCount > 0);
			iter.second.AvgElapsedMS = (iter.second.TotalElapsedTick / static_cast<double>(iter.second.TotalSampleCount)) * 0.000001;	// ns -> ms
			AvgCPUProfiles.push_back(iter.second);

			// Accumulate Total CPU time
			AccumulateTotalMS(TotalAvgCPUPassesMS, MostLeastIndent, iter.second);
		}
	}

	{
		std::map<jPriorityName, jAvgProfile, jPriorityNameComapreFunc> SumOfScopedProfileGPUMap;
		{
			jScopeReadLock s(&ScopedGPULock);
			for (int32 i = 0; i < MaxProfileFrame; ++i)
			{
				const auto& scopedProfileMap = ScopedProfileGPUMap[i];
				for (auto& iter : scopedProfileMap)
				{
					auto& avgProfile = SumOfScopedProfileGPUMap[iter.first];
					avgProfile.TotalElapsedTick += iter.second.ElapsedTick;
					avgProfile.Indent = iter.second.Indent;
					avgProfile.ThreadId = iter.second.ThreadId;
					avgProfile.Name = iter.first;
					avgProfile.GPUCommandBufferType = iter.first.CommandBufferType;
					++avgProfile.TotalSampleCount;
				}
			}
		}

        AvgGPUProfiles.clear();
		AvgGPUProfiles.reserve(SumOfScopedProfileGPUMap.size());
		
		int32 MostLeastIndent = INT_MAX;
		TotalAvgGPUPassesMS = 0.0;

		for (auto& iter : SumOfScopedProfileGPUMap)
		{
			JASSERT(iter.second.TotalSampleCount > 0);
			iter.second.AvgElapsedMS = (iter.second.TotalElapsedTick / static_cast<double>(iter.second.TotalSampleCount)) * 0.000001;	// ns -> ms
			AvgGPUProfiles.push_back(iter.second);

			// Accumulate Total GPU time
			AccumulateTotalMS(TotalAvgGPUPassesMS, MostLeastIndent, iter.second);
		}
	}
}

void jPerformanceProfile::PrintOutputDebugString()
{
	std::string result;
	char szTemp[128] = { 0, };
	if (!AvgCPUProfiles.empty())
	{
		result += "-----CPU---PerformanceProfile----------\n";
		for (auto& iter : AvgCPUProfiles)
		{
			sprintf_s(szTemp, sizeof(szTemp), "%s : \t\t\t\t%lf ms", iter.Name.ToStr(), iter.AvgElapsedMS);
			result += szTemp;
			result += "\n";
		}
	}

	if (!AvgGPUProfiles.empty())
	{
		result += "-----GPU---PerformanceProfile----------\n";
		for (auto& iter : AvgGPUProfiles)
		{
			sprintf_s(szTemp, sizeof(szTemp), "%s : \t\t\t\t%lf ms", iter.Name.ToStr(), iter.AvgElapsedMS);
			result += szTemp;
			result += "\n";
		}
	}
	OutputDebugStringA(result.c_str());
}
