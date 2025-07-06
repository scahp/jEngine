#pragma once
#include <ppl.h>

struct jParallelFor
{
    FORCEINLINE static void ParallelForWithTaskPerThread(uint32 InTaskPerThread, const auto& InContainer, auto Lambda)
    {
        if (0 == InTaskPerThread)
            return;

        const size_t size = (InContainer.size() / InTaskPerThread) + ((InContainer.size() % InTaskPerThread) > 0 ? 1 : 0);
        concurrency::parallel_for(size_t(0), size, [&](size_t InIndex)
        {
            const size_t StartIndex = InTaskPerThread * InIndex;
            const size_t EndIndex = Min(StartIndex + InTaskPerThread, InContainer.size());
            for (size_t k = StartIndex; k < EndIndex; ++k)
            {
                Lambda(k, InContainer[k]);
            }
        });
    }

    FORCEINLINE static void ParallelForWithThreadCount(size_t InMaxThreadCount, const auto& InContainer, auto Lambda)
    {
        if (0 == InMaxThreadCount)
            return;

        const size_t TaskPerThread = (InContainer.size() / InMaxThreadCount);
        const size_t RemainingTasks = InContainer.size() % InMaxThreadCount;
        if (TaskPerThread > 0)
        {
            concurrency::parallel_for(size_t(0), InMaxThreadCount, [&](size_t InIndex)
            {
                // 남은 작업이 있는 경우, 각 스레드에 하나씩 추가로 할당
                const size_t StartIndex = InIndex * TaskPerThread + min(InIndex, RemainingTasks);
                const size_t EndIndex = StartIndex + TaskPerThread + (InIndex < RemainingTasks ? 1 : 0);

                for (size_t k = StartIndex; k < EndIndex; ++k)
                {
                    Lambda(k, InContainer[k]);
                }
            });
        }
        else
        {
            for (size_t k = 0; k < InContainer.size(); ++k)
            {
                Lambda(k, InContainer[k]);
            }
        }
    }
};