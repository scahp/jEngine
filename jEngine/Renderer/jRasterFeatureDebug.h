#pragma once
#include <cstdint>
#include <memory>
#include <vector>
struct jRenderTarget;
namespace jRasterFeatureDebug
{
struct MultisampleOutput
{
    std::uint64_t FrameNumber = 0;
    // Original total colour, five GBuffer targets, optional SSS mask/diffuse.
    std::vector<std::shared_ptr<jRenderTarget>> Sources, Resolved;
    std::shared_ptr<jRenderTarget> SourceDepth, ResolvedDepth;
};
// Opt-in inspection of actual submitted images, no extra normal render work.
bool GetLastMultisampleOutput(MultisampleOutput& output);
void ReleaseResources();
}
