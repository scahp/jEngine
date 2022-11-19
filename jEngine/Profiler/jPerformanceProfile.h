#pragma once
#include <sysinfoapi.h>

#define ENABLE_PROFILE 1
#define ENABLE_PROFILE_CPU (ENABLE_PROFILE && 1)
#define ENABLE_PROFILE_GPU (ENABLE_PROFILE && 1)

static constexpr int32 MaxProfileFrame = 10;

struct jScopedProfileData
{
	uint64 ElapsedTick = 0;
	int32 Indent = 0;
	std::thread::id ThreadId;
};

extern jMutexRWLock ScopedCPULock;
extern jMutexRWLock ScopedGPULock;
extern robin_hood::unordered_map<jPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileCPUMap[MaxProfileFrame];
extern robin_hood::unordered_map<jPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileGPUMap[MaxProfileFrame];
struct jQuery;

void ClearScopedProfileCPU();
void AddScopedProfileCPU(const jPriorityName& name, uint64 elapsedTick, int32 Indent = 0);

void ClearScopedProfileGPU();
void AddScopedProfileGPU(const jPriorityName& name, uint64 elapsedTick, int32 Indent = 0);

extern thread_local std::atomic<int32> ScopedProfilerCPUIndent;
extern thread_local std::atomic<int32> ScopedProfilerGPUIndent;

//////////////////////////////////////////////////////////////////////////
// jScopedProfile_CPU
class jScopedProfile_CPU
{
public:
	static std::atomic<int32> s_priority;

	static void ResetPriority() { s_priority = 0; }

	jScopedProfile_CPU(const jName& name)
		: Name(name)
	{
		Start = std::chrono::system_clock::now();
		Priority = s_priority.fetch_add(1);
		Indent = ScopedProfilerCPUIndent.fetch_add(1);
	}

	~jScopedProfile_CPU()
	{
        const std::chrono::nanoseconds nano_seconds 
			= std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now() - Start);
		AddScopedProfileCPU(jPriorityName(Name, Priority), nano_seconds.count(), Indent);
		ScopedProfilerCPUIndent.fetch_add(-1);
	}

	jName Name;
	uint64 StartTick = 0;
    std::chrono::system_clock::time_point Start;
	int32 Priority = 0;
	int32 Indent = 0;
};

#if ENABLE_PROFILE_CPU
#define SCOPE_CPU_PROFILE(Name) jScopedProfile_CPU Name##ScopedProfileCPU(jNameStatic(#Name));
#else
#define SCOPE_CPU_PROFILE(Name)
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
	static robin_hood::unordered_set<jQuery*> s_running;
	static robin_hood::unordered_set<jQuery*> s_resting;
};

//////////////////////////////////////////////////////////////////////////
// jProfile_GPU
struct jProfile_GPU
{
	jPriorityName Name;
	jQuery* Query = nullptr;
	int32 Indent = 0;

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
			AddScopedProfileGPU(iter.Name, iter.Query->GetElpasedTime(), iter.Indent);
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
	static std::atomic<int32> s_priority;
	static void ResetPriority() { s_priority = 0; }

	jScopedProfile_GPU(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jName& name)
		: RenderFrameContextPtr(InRenderFrameContext)
	{
		Profile.Name = jPriorityName(name, s_priority.fetch_add(1));
        Profile.Indent = ScopedProfilerGPUIndent.fetch_add(1);
        
		Profile.Query = jQueryTimePool::GetQueryTime();
		Profile.Query->BeginQuery(InRenderFrameContext->GetActiveCommandBuffer());
	}

	~jScopedProfile_GPU()
	{
		//Profile.End = jQueryTimePool::GetQueryTime();
		//g_rhi->QueryTimeStamp(Profile.End);

		Profile.Query->EndQuery(RenderFrameContextPtr.lock()->GetActiveCommandBuffer());
		jProfile_GPU::WatingResultList[jProfile_GPU::CurrentWatingResultListIndex].emplace_back(Profile);
		ScopedProfilerGPUIndent.fetch_add(-1);
	}

	std::weak_ptr<jRenderFrameContext> RenderFrameContextPtr;
	jProfile_GPU Profile;
};

#if ENABLE_PROFILE_GPU
#define SCOPE_GPU_PROFILE(RenderFrameContextPtr, Name) jScopedProfile_GPU Name##ScopedProfileGPU(RenderFrameContextPtr, jNameStatic(#Name));
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
		int32 Indent = 0;
		std::thread::id ThreadId;
	};

	using AvgProfileMapType = std::map<jPriorityName, jAvgProfile, jPriorityNameComapreFunc>;

	void Update(float deltaTime);
	void CalcAvg();
	void PrintOutputDebugString();

    const AvgProfileMapType& GetCPUAvgProfileMap() const { return CPUAvgProfileMap; }
    const AvgProfileMapType& GetGPUAvgProfileMap() const { return GPUAvgProfileMap; }

private:
    AvgProfileMapType CPUAvgProfileMap;		// ms
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