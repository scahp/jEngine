#pragma once
#include <sysinfoapi.h>
#include "jRHI.h"

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

#define SCOPE_PROFILE(Name) static jName Name##ScopedProfileCPUName(#Name); jScopedProfile_CPU Name##ScopedProfileCPU(Name##ScopedProfileCPUName);

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
	jQueryTime* Start = nullptr;
	jQueryTime* End = nullptr;

	static int32 CurrentWatingResultListIndex;
	static std::list<jProfile_GPU> WatingResultList[2];
	static void ProcessWaitings()
	{
		const auto prevIndex = !CurrentWatingResultListIndex;
		auto& prevList = WatingResultList[prevIndex];
		for (auto rit = prevList.rbegin(); prevList.rend() != rit;++rit)
		{
			const auto& iter = *rit;
			g_rhi->IsQueryTimeStampResult(iter.End, true);
			g_rhi->GetQueryTimeStampResult(iter.End);
			g_rhi->IsQueryTimeStampResult(iter.Start, true);
			g_rhi->GetQueryTimeStampResult(iter.Start);
			AddScopedProfileGPU(iter.Name, iter.End->TimeStamp - iter.Start->TimeStamp);
			jQueryTimePool::ReturnQueryTime(iter.Start);
			jQueryTimePool::ReturnQueryTime(iter.End);
		}
		prevList.clear();
		CurrentWatingResultListIndex = prevIndex;
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
		Profile.Start = jQueryTimePool::GetQueryTime();
		JASSERT(Profile.Start);
		g_rhi->QueryTimeStamp(Profile.Start);
	}

	~jScopedProfile_GPU()
	{
		Profile.End = jQueryTimePool::GetQueryTime();
		g_rhi->QueryTimeStamp(Profile.End);
		
		jProfile_GPU::WatingResultList[jProfile_GPU::CurrentWatingResultListIndex].emplace_back(Profile);
	}

	jProfile_GPU Profile;
};

#define SCOPE_GPU_PROFILE(Name) static jName Name##ScopedProfileGPUName(#Name); jScopedProfile_GPU Name##ScopedProfileGPU(Name##ScopedProfileGPUName);

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

private:
	std::map<jName, jAvgProfile> AvgProfileMap;			// ms
	std::map<jName, jAvgProfile> GPUAvgProfileMap;		// ns

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