﻿#pragma once
#include <sysinfoapi.h>

#define ENABLE_PROFILE 1
#define ENABLE_PROFILE_CPU (ENABLE_PROFILE && 0)
#define ENABLE_PROFILE_GPU (ENABLE_PROFILE && 1)

static constexpr int32 MaxProfileFrame = 10;

extern std::unordered_map<jPriorityName, uint64, jPriorityNameHashFunc> ScopedProfileCPUMap[MaxProfileFrame];
extern std::unordered_map<jPriorityName, uint64, jPriorityNameHashFunc> ScopedProfileGPUMap[MaxProfileFrame];
struct jQuery;

void ClearScopedProfileCPU();
void AddScopedProfileCPU(const jPriorityName& name, uint64 elapsedTick);

void ClearScopedProfileGPU();
void AddScopedProfileGPU(const jPriorityName& name, uint64 elapsedTick);

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
		AddScopedProfileCPU(jPriorityName(Name, 0), GetTickCount64() - StartTick);
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
	static jQuery* GetQueryTime()
	{
		jQuery* queryTime = nullptr;
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
	static void ReturnQueryTime(jQuery* queryTime)
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
	static std::unordered_set<jQuery*> s_running;
	static std::unordered_set<jQuery*> s_resting;
};

//////////////////////////////////////////////////////////////////////////
// jProfile_GPU
struct jProfile_GPU
{
	jName Name;
	jQuery* Query = nullptr;

	static int32 CurrentWatingResultListIndex;
	static std::vector<jProfile_GPU> WatingResultList[jRHI::MaxWaitingQuerySet];
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

		const bool CanWholeQueryTimeStampResult = g_rhi->GetQueryTimePool()->CanWholeQueryResult();

		// Query 결과를 한번에 다 받아올 수 있는 API 는 그렇게 처리하고, 아니면 개별로 결과를 얻어오도록 함
        std::vector<uint64> WholeQueryTimeStampArray;
		if (CanWholeQueryTimeStampResult)
		{
			WholeQueryTimeStampArray = g_rhi->GetQueryTimePool()->GetWholeQueryResult(prevIndex);
		}

		auto& prevList = WatingResultList[prevIndex];
		uint32 priority = 0;
		for (auto it = prevList.begin(); prevList.end() != it;++it)
		{
			const auto& iter = *it;
			if (CanWholeQueryTimeStampResult)
			{
				iter.Query->GetQueryResultFromQueryArray(prevIndex, WholeQueryTimeStampArray);
			}
			else
			{
				iter.Query->GetQueryResult();
			}
			AddScopedProfileGPU(jPriorityName(iter.Name, priority++), iter.Query->GetElpasedTime());
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
	jScopedProfile_GPU(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jName& name)
		: RenderFrameContextPtr(InRenderFrameContext)
	{
		Profile.Name = name;
		//Profile.Start = jQueryTimePool::GetQueryTime();
		//JASSERT(Profile.Start);
		//g_rhi->QueryTimeStamp(Profile.Start);

        Profile.Query = jQueryTimePool::GetQueryTime();
		Profile.Query->BeginQuery(InRenderFrameContext->CommandBuffer);
	}

	~jScopedProfile_GPU()
	{
		//Profile.End = jQueryTimePool::GetQueryTime();
		//g_rhi->QueryTimeStamp(Profile.End);

		Profile.Query->EndQuery(RenderFrameContextPtr.lock()->CommandBuffer);
		jProfile_GPU::WatingResultList[jProfile_GPU::CurrentWatingResultListIndex].emplace_back(Profile);
	}

	std::weak_ptr<jRenderFrameContext> RenderFrameContextPtr;
	jProfile_GPU Profile;
};

#if ENABLE_PROFILE_GPU
#define SCOPE_GPU_PROFILE(RenderFrameContextPtr, Name) static jName Name##ScopedProfileGPUName(#Name); jScopedProfile_GPU Name##ScopedProfileGPU(RenderFrameContextPtr, Name##ScopedProfileGPUName);
#else
#define SCOPE_GPU_PROFILE(RenderFrameContextPtr, Name)
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

	using AvgProfileMapType = std::map<jPriorityName, jAvgProfile, jPriorityNameComapreFunc>;

	void Update(float deltaTime);
	void CalcAvg();
	void PrintOutputDebugString();

    const AvgProfileMapType& GetAvgProfileMap() const { return AvgProfileMap; }
    const AvgProfileMapType& GetGPUAvgProfileMap() const { return GPUAvgProfileMap; }

private:
    AvgProfileMapType AvgProfileMap;		// ms
    AvgProfileMapType GPUAvgProfileMap;		// ms

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