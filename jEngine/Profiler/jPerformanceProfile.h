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

//////////////////////////////////////////////////////////////////////////
// jGPUPriorityName, for jProfile_GPU
//////////////////////////////////////////////////////////////////////////
struct jGPUPriorityName : public jPriorityName
{
	jGPUPriorityName() = default;

	FORCEINLINE explicit jGPUPriorityName(uint32 InNameHash, uint32 InPriority, ECommandBufferType InCommandBufferType)
		: jPriorityName(InNameHash, InPriority), CommandBufferType(InCommandBufferType)
	{}

	FORCEINLINE explicit jGPUPriorityName(const char* pName, uint32 InPriority, ECommandBufferType InCommandBufferType)
		: jPriorityName(pName, InPriority), CommandBufferType(InCommandBufferType)
	{}

	FORCEINLINE explicit jGPUPriorityName(const char* pName, size_t size, uint32 InPriority, ECommandBufferType InCommandBufferType)
		: jPriorityName(pName, size, InPriority), CommandBufferType(InCommandBufferType)
	{}

	FORCEINLINE explicit jGPUPriorityName(const std::string& name, uint32 InPriority, ECommandBufferType InCommandBufferType)
		: jPriorityName(name, InPriority), CommandBufferType(InCommandBufferType)
	{}

	FORCEINLINE explicit jGPUPriorityName(const jName& name, uint32 InPriority, ECommandBufferType InCommandBufferType)
		: jPriorityName(name, InPriority), CommandBufferType(InCommandBufferType)
	{}

	ECommandBufferType CommandBufferType = ECommandBufferType::GRAPHICS;
};

extern jMutexRWLock ScopedCPULock;
extern jMutexRWLock ScopedGPULock;
extern robin_hood::unordered_map<jPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileCPUMap[MaxProfileFrame];
extern robin_hood::unordered_map<jGPUPriorityName, jScopedProfileData, jPriorityNameHashFunc> ScopedProfileGPUMap[MaxProfileFrame];
struct jQuery;

void ClearScopedProfileCPU();
void AddScopedProfileCPU(const jPriorityName& name, uint64 elapsedTick, int32 Indent = 0);

void ClearScopedProfileGPU();
void AddScopedProfileGPU(const jGPUPriorityName& name, uint64 elapsedTick, int32 Indent = 0);

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
#define SCOPE_CPU_PROFILE_NAME(Name) jScopedProfile_CPU Name##ScopedProfileCPU(Name);
#else
#define SCOPE_CPU_PROFILE(Name)
#define SCOPE_CPU_PROFILE_NAME(Name) 
#endif

//////////////////////////////////////////////////////////////////////////
// jQueryTimePool
struct jQueryTimePool
{
public:
	static jQuery* GetQueryTime(ECommandBufferType InCmdBufferType)
	{
		jQuery* queryTime = nullptr;

		auto& CurPending = s_pending[(int32)InCmdBufferType];
		auto& CurRunning = s_running[(int32)InCmdBufferType];

		if (CurPending.empty())
		{
			queryTime = g_rhi->CreateQueryTime(InCmdBufferType);
		}
		else
		{
			auto iter = CurPending.begin();
			queryTime = *iter;
			CurPending.erase(iter);
		}
        JASSERT(queryTime);
		queryTime->Init();
		CurRunning.insert(queryTime);
		return queryTime;
	}
	static void ReturnQueryTime(jQuery* queryTime)
	{
		auto CommandBufferType = queryTime->GetCommandBufferType();
		s_running[(int32)CommandBufferType].erase(queryTime);
		s_pending[(int32)CommandBufferType].insert(queryTime);
	}
	static void DeleteAllQueryTime()
	{
		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			JASSERT(s_running[i].empty());
			for (auto& iter : s_pending[i])
			{
				g_rhi->ReleaseQueryTime(iter);
				delete iter;
			}
			s_pending[i].clear();
		}
	}

private:
	static robin_hood::unordered_set<jQuery*> s_running[(uint32)ECommandBufferType::MAX];
	static robin_hood::unordered_set<jQuery*> s_pending[(uint32)ECommandBufferType::MAX];
};

//////////////////////////////////////////////////////////////////////////
// jProfile_GPU
struct jProfile_GPU
{
	jGPUPriorityName Name;
	jQuery* Query = nullptr;
	int32 Indent = 0;

	static int32 CurrentWatingResultListIndex;
	static std::vector<jProfile_GPU> WatingResultList[jRHI::MaxWaitingQuerySet];
	static void ProcessWaitings()
	{
		int32 prevIndex = CurrentWatingResultListIndex + 1;
		prevIndex %= jRHI::MaxWaitingQuerySet;

        // Query 결과를 한번에 다 받아올 수 있는 API 는 그렇게 처리하고, 아니면 개별로 결과를 얻어오도록 함
		bool CanWholeQueryTimeStampResult[(int32)ECommandBufferType::MAX] = { false, };
        std::vector<uint64> WholeQueryTimeStampArray[(int32)ECommandBufferType::MAX];

		for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
		{
			check(g_rhi->GetQueryTimePool((ECommandBufferType)i));

			CanWholeQueryTimeStampResult[i] = g_rhi->GetQueryTimePool((ECommandBufferType)i)->CanWholeQueryResult();

			if (CanWholeQueryTimeStampResult[i])
			{
				WholeQueryTimeStampArray[i] = g_rhi->GetQueryTimePool((ECommandBufferType)i)->GetWholeQueryResult(prevIndex);
			}
		}

		auto& prevList = WatingResultList[prevIndex];
		for (auto it = prevList.begin(); prevList.end() != it;++it)
		{
			const auto& iter = *it;
			ECommandBufferType CurrentCommandBufferType = iter.Query->GetCommandBufferType();

			if (CanWholeQueryTimeStampResult[(int32)CurrentCommandBufferType])
			{
				iter.Query->GetQueryResultFromQueryArray(prevIndex, WholeQueryTimeStampArray[(int32)CurrentCommandBufferType]);
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
		Profile.Name = jGPUPriorityName(name, s_priority.fetch_add(1), InRenderFrameContext->GetActiveCommandBuffer()->Type);
        Profile.Indent = ScopedProfilerGPUIndent.fetch_add(1);
        
		Profile.Query = jQueryTimePool::GetQueryTime(InRenderFrameContext->GetActiveCommandBuffer()->Type);
		Profile.Query->BeginQuery(InRenderFrameContext->GetActiveCommandBuffer());
	}

	~jScopedProfile_GPU()
	{
		Profile.Query->EndQuery(RenderFrameContextPtr.lock()->GetActiveCommandBuffer());
		jProfile_GPU::WatingResultList[jProfile_GPU::CurrentWatingResultListIndex].emplace_back(Profile);
		ScopedProfilerGPUIndent.fetch_add(-1);
	}

	std::weak_ptr<jRenderFrameContext> RenderFrameContextPtr;
	jProfile_GPU Profile;
};

#if ENABLE_PROFILE_GPU
#define SCOPE_GPU_PROFILE(RenderFrameContextPtr, Name) jScopedProfile_GPU Name##ScopedProfileGPU(RenderFrameContextPtr, jNameStatic(#Name));
#define SCOPE_GPU_PROFILE_NAME(RenderFrameContextPtr, Name) jScopedProfile_GPU Name##ScopedProfileGPU(RenderFrameContextPtr, Name);
#else
#define SCOPE_GPU_PROFILE(RenderFrameContextPtr, Name)
#define SCOPE_GPU_PROFILE_NAME(RenderFrameContextPtr, Name) 
#endif

//////////////////////////////////////////////////////////////////////////
// jPerformanceProfile
class jPerformanceProfile
{
public:
	struct jAvgProfile
	{
		jPriorityName Name;
		uint64 TotalElapsedTick = 0;
		double AvgElapsedMS = 0;
		int32 TotalSampleCount = 0;
		int32 Indent = 0;
		std::thread::id ThreadId;
		ECommandBufferType GPUCommandBufferType = ECommandBufferType::MAX;
	};

	// jPriorityNameComapreFunc
	using AvgProfileMapType = std::vector<jAvgProfile>;

	void Update(float deltaTime);
	void CalcAvg();
	void PrintOutputDebugString();

    const AvgProfileMapType& GetAvgCPUProfiles() const { return AvgCPUProfiles; }
    const AvgProfileMapType& GetAvgGPUProfiles() const { return AvgGPUProfiles; }

	double GetTotalAvgCPUPassesMS() const { return TotalAvgCPUPassesMS; }
	double GetTotalAvgGPUPassesMS() const { return TotalAvgGPUPassesMS; }

private:
	double TotalAvgCPUPassesMS = 0.0;
	double TotalAvgGPUPassesMS = 0.0;

    AvgProfileMapType AvgCPUProfiles;		// ms
    AvgProfileMapType AvgGPUProfiles;		// ms

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