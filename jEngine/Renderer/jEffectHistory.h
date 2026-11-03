#pragma once

#include <cstdint>
#include <memory>

// Swapchain image indices need not follow rendering order. Keep the actual
// preceding output, and reject history after a skipped frame or resource reset.
template <typename TRenderTarget>
class jEffectHistory
{
public:
    std::shared_ptr<TRenderTarget> GetPrevious(std::uint32_t FrameNumber,
        const std::shared_ptr<TRenderTarget>& Destination) const
    {
        auto Previous = LastOutput.lock();
        if (!Previous || Previous == Destination || FrameNumber - LastFrameNumber != 1
            || Previous->Info.Width != Destination->Info.Width
            || Previous->Info.Height != Destination->Info.Height)
        {
            return {};
        }
        return Previous;
    }

    void Commit(const std::shared_ptr<TRenderTarget>& Output, std::uint32_t FrameNumber)
    {
        LastOutput = Output;
        LastFrameNumber = FrameNumber;
    }

    void Invalidate() { LastOutput.reset(); }

private:
    std::weak_ptr<TRenderTarget> LastOutput;
    std::uint32_t LastFrameNumber = 0;
};
