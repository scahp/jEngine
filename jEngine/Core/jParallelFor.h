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
        if (TaskPerThread > 0)
        {
            concurrency::parallel_for(size_t(0), InMaxThreadCount, [&](size_t InIndex)
            {
                const size_t StartIndex = TaskPerThread * InIndex;

                // 마지막 스레드가 남은 작업들을 모두 처리하도록 함.
                const bool IsLastThread = ((InMaxThreadCount - 1) == InIndex);
                const size_t EndIndex = IsLastThread ? InContainer.size() : Min(StartIndex + TaskPerThread, InContainer.size());
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