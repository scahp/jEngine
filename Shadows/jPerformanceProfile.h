#pragma once
#include <sysinfoapi.h>
#include "jRHI.h"
#include "jRHI_Vulkan.h"

#define ENABLE_PROFILE 1
#define ENABLE_PROFILE_CPU (ENABLE_PROFILE && 0)
#define ENABLE_PROFILE_GPU (ENABLE_PROFILE && 1)

static constexpr int32 MaxProfileFrame = 10;

extern std::unordered_map<jName, uint64, jNameHashFunc> ScopedProfileCPUMap[MaxProfileFrame];
extern std::unordered_map<jName, uint64, jNameHashFunc> ScopedProfileGPUMap[MaxProfileFrame];
struct jQueryTime;

void ClearScopedProfileCPU();
void AddScopedProfileCPU(const jName& name, uint64 elapsedTick);

void ClearScopedProfileGPU();
void AddScopedProfileGPU(const jName& name, uint64 elapsedTick);

//////////////////////////////////////////////////////////////////////////
// jScopedProfile_CPU
class jScopedProfile_CPU
{
public:
	jScopedProfile_CPU(const jName& name)
		: Name(name)
	{
		StartTick = GetTickCount64();
	}

	~jScopedProfile_CPU()
	{
		AddScopedProfileCPU(Name, GetTickCount64() - StartTick);
	}

	jName Name;
	uint64 StartTick = 0;
};

#if ENABLE_PROFILE_CPU
#define SCOPE_PROFILE(Name) static jName Name##ScopedProfileCPUName(#Name); jScopedProfile_CPU Name##ScopedProfileCPU(Name##ScopedProfileCPUName);
#else
#define SCOPE_PROFILE(Name)
#endif

//////////////////////////////////////////////////////////////////////////
// jQueryTimePool
struct jQueryTimePool
{
public:
	static jQueryTime* GetQueryTime()
	{
		jQueryTime* queryTime = nullptr;
		if (s_resting.empty())
		{
			queryTime = g_rhi->CreateQueryTime();
		}
		else
		{
			auto iter = s_resting.begin();
			queryTime = *iter;
			s_resting.erase(iter);
		}
		queryTime->Init();
		s_running.insert(queryTime);
		JASSERT(queryTime);
		return queryTime;
	}
	static void ReturnQueryTime(jQueryTime* queryTime)
	{
		s_running.erase(queryTime);
		s_resting.insert(queryTime);
	}
	static void DeleteAllQueryTime()
	{
		JASSERT(s_running.empty());
		for (auto& iter : s_resting)
		{
			g_rhi->ReleaseQueryTime(iter);
			delete iter;
		}
		s_resting.clear();
	}

private:
	static std::unordered_set<jQueryTime*> s_running;
	static std::unordered_set<jQueryTime*> s_resting;
};

//////////////////////////////////////////////////////////////////////////
// jProfile_GPU
struct jProfile_GPU
{
	jName Name;
	jQueryTime* Query = nullptr;

	static int32 CurrentWatingResultListIndex;
	static std::list<jProfile_GPU> WatingResultList[jRHI::MaxWaitingQuerySet];
	static void ProcessWaitings()
	{
		int32 prevIndex = CurrentWatingResultListIndex;
		if (jRHI::MaxWaitingQuerySet > 2)
		{
			prevIndex = (CurrentWatingResultListIndex + 2) % jRHI::MaxWaitingQuerySet;
		}
		else if (jRHI::MaxWaitingQuerySet > 1)
		{
			prevIndex = (int32)!CurrentWatingResultListIndex;
		}

		// Query 결과를 한번에 다 받아올 수 있는 API 는 그렇게 처리하고, 아니면 개별로 결과를 얻어오도록 함
        std::vector<uint64> WholeQueryTimeStampArray;
		if (g_rhi->CanWholeQueryTimeStampResult())
		{
			WholeQueryTimeStampArray = g_rhi->GetWholeQueryTimeStampResult(prevIndex);
		}

		auto& prevList = WatingResultList[prevIndex];
		for (auto rit = prevList.rbegin(); prevList.rend() != rit;++rit)
		{
			const auto& iter = *rit;
			if (g_rhi->CanWholeQueryTimeStampResult())
			{
				g_rhi->GetQueryTimeStampResultFromWholeStampArray(iter.Query, prevIndex, WholeQueryTimeStampArray);
			}
			else
			{
				g_rhi->GetQueryTimeStampResult(iter.Query);
			}
			AddScopedProfileGPU(iter.Name, iter.Query->TimeStampStartEnd[1] - iter.Query->TimeStampStartEnd[0]);
            jQueryTimePool::ReturnQueryTime(iter.Query);
		}
		prevList.clear();
		CurrentWatingResultListIndex = (++CurrentWatingResultListIndex) % jRHI::MaxWaitingQuerySet;
	}
};

//////////////////////////////////////////////////////////////////////////
// jScopedProfile_GPU
class jScopedProfile_GPU
{
public:
	jScopedProfile_GPU(const jName& name)
	{
		Profile.Name = name;
		//Profile.Start = jQueryTimePool::GetQueryTime();
		//JASSERT(Profile.Start);
		//g_rhi->QueryTimeStamp(Profile.Start);

        Profile.Query = jQueryTimePool::GetQueryTime();
		g_rhi_vk->QueryTimeStampStart(Profile.Query);
	}

	~jScopedProfile_GPU()
	{
		//Profile.End = jQueryTimePool::GetQueryTime();
		//g_rhi->QueryTimeStamp(Profile.End);

        g_rhi_vk->QueryTimeStampEnd(Profile.Query);
		jProfile_GPU::WatingResultList[jProfile_GPU::CurrentWatingResultListIndex].emplace_back(Profile);
	}

	jProfile_GPU Profile;
};

#if ENABLE_PROFILE_GPU
#define SCOPE_GPU_PROFILE(Name) static jName Name##ScopedProfileGPUName(#Name); jScopedProfile_GPU Name##ScopedProfileGPU(Name##ScopedProfileGPUName);
#else
#define SCOPE_GPU_PROFILE(Name)
#endif

//////////////////////////////////////////////////////////////////////////
// jPerformanceProfile
class jPerformanceProfile
{
public:
	struct jAvgProfile
	{
		uint64 TotalElapsedTick = 0;
		double AvgElapsedMS = 0;
		int32 TotalSampleCount = 0;
	};

	void Update(float deltaTime);
	void CalcAvg();
	void PrintOutputDebugString();

	const std::map<jName, jAvgProfile>& GetAvgProfileMap() const { return AvgProfileMap; }
	const std::map<jName, jAvgProfile>& GetGPUAvgProfileMap() const { return GPUAvgProfileMap; }

private:
	std::map<jName, jAvgProfile> AvgProfileMap;			// ms
	std::map<jName, jAvgProfile> GPUAvgProfileMap;		// ms

public:
	static jPerformanceProfile& GetInstance()
	{
		if (!_instance)
			_instance = new jPerformanceProfile();
		return *_instance;
	}

private:
	jPerformanceProfile() {}
	static jPerformanceProfile* _instance;
};