#pragma once

#include "Features/jHiZOcclusionMath.h"
#include <memory>
#include <span>

class jDrawCommand;
class jRenderObject;
class jView;
struct jBuffer;
struct jRenderTarget;
struct jRenderFrameContext;
struct jTexture;

namespace jHiZOcclusion
{
struct FrameStats
{
    bool Requested = false, Active = false;
    std::uint32_t CandidateCount = 0, OccluderDrawCount = 0, DirectDrawCount = 0;
    std::uint32_t DepthPasses = 0, ComputePasses = 0;
    std::size_t ScannedVertices = 0, AdditionalResourceBytes = 0;
    std::string Diagnostic;
};
struct IndirectDraw
{
    std::shared_ptr<jBuffer> Buffer;
    std::uint32_t ArgumentIndex = 0; // 20-byte indexed argument records.
    explicit operator bool() const noexcept { return bool(Buffer); }
};
struct DebugOutput
{
    std::shared_ptr<jRenderTarget> Depth;
    std::shared_ptr<jBuffer> Pyramid, Arguments;
    PyramidLayout Layout;
    std::uint32_t CandidateCount = 0;
    std::vector<std::uint32_t> RenderObjectIds; // Record order; no borrowed object lifetime.
};
const FrameStats& GetLastFrameStats();
bool GetLastDebugOutput(DebugOutput& output);
// Latest actual scene HDR target, captured even when Hi-Z is disabled. Debug
// consumers may read it after normal frame completion; this performs no copy.
std::shared_ptr<jTexture> AcquireLastSceneColor();
// Call after the normal GPU flush and before RHI teardown.
void ReleaseResources();

// Owner thread, after base draw setup has joined, before BeginRenderPass.
// Full same-frame real-mesh depth -> max pyramid -> GPU indexed instance counts.
// No temporal depth/history/readback and no shared-geometry changes. Unsupported
// or failed views keep all direct draws. MSAA>1 is explicitly unsupported here.
class Frame
{
public:
    Frame();
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    bool BuildAndRecord(const std::shared_ptr<jRenderFrameContext>& frame, const jView& view,
        std::span<const jDrawCommand> baseDraws, const Settings& settings, std::string& error);
    IndirectDraw ArgumentsFor(const jRenderObject* object) const;
    const FrameStats& Stats() const;
private:
    struct Impl;
    std::shared_ptr<Impl> Data;
};
}
