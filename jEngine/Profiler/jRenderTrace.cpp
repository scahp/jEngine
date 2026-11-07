#include "jRenderTrace.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace jRenderTrace
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr size_t MaxFrames = 32, MaxPasses = 1024, MaxResources = 64;
struct State
{
    std::mutex Mutex;
    std::atomic_bool Enabled{false};
    std::atomic_uint32_t CaptureCount{0};
    uint64_t NextLease = 0;
    std::unordered_set<uint64_t> Captures;
    uint64_t Current = 0;
    std::deque<Frame> Frames;
    std::unordered_map<uint32_t, Clock::time_point> Starts;
};
State& Get() { static State state; return state; }
thread_local std::vector<Token> Stack;
Frame* Find(State& state, uint64_t id)
{
    const auto found = std::find_if(state.Frames.begin(), state.Frames.end(),
        [id](const Frame& frame) { return frame.Id == id; });
    return found == state.Frames.end() ? nullptr : &*found;
}
Pass* FindPass(Frame* frame, uint32_t id)
{
    return frame && id && id <= frame->Passes.size() ? &frame->Passes[id - 1] : nullptr;
}
}
void SetEnabled(bool enabled)
{
    Get().Enabled.store(enabled, std::memory_order_relaxed);
}
bool IsEnabled() { auto& s = Get(); return s.Enabled.load(std::memory_order_relaxed) || s.CaptureCount.load(std::memory_order_relaxed) != 0; }
uint64_t BeginCapture()
{
    auto& s = Get(); std::lock_guard lock(s.Mutex); const auto lease = ++s.NextLease;
    s.Captures.insert(lease); s.CaptureCount.store(uint32_t(s.Captures.size()),std::memory_order_relaxed); return lease;
}
void EndCapture(uint64_t lease)
{
    auto& s = Get(); std::lock_guard lock(s.Mutex); s.Captures.erase(lease);
    s.CaptureCount.store(uint32_t(s.Captures.size()),std::memory_order_relaxed);
}
void BeginFrame(uint64_t id, const char* renderer, const char* api, int width, int height)
{
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    s.Current = 0; s.Starts.clear(); Stack.clear();
    if (!IsEnabled() || !id) return;
    // Frame identifiers are monotonic within one engine lifetime. Refuse reuse.
    if (!s.Frames.empty() && id <= s.Frames.back().Id) return;
    if (s.Frames.size() >= MaxFrames) s.Frames.pop_front();
    s.Frames.push_back({}); auto& frame = s.Frames.back();
    frame.Id = s.Current = id; frame.Renderer = renderer ? renderer : "Unknown";
    frame.Api = api ? api : "Unknown"; frame.Width = width; frame.Height = height;
}
void EndFrame()
{
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    if (auto* frame = Find(s, s.Current)) frame->Ended = true;
    s.Current = 0; s.Starts.clear(); Stack.clear();
}
Token BeginPass(const char* name, uint32_t queue)
{
    if (!IsEnabled()) return {};
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    auto* frame = Find(s, s.Current); if (!frame || !IsEnabled()) return {};
    if (frame->Passes.size() >= MaxPasses) { ++frame->DroppedPasses; return {}; }
    const uint32_t id = static_cast<uint32_t>(frame->Passes.size() + 1);
    const uint32_t parent = !Stack.empty() && Stack.back().Frame == frame->Id ? Stack.back().Pass : 0;
    const std::string label = name ? name : "Unnamed";
    uint32_t occurrence = 0;
    for (const auto& pass : frame->Passes) if (pass.Parent == parent && pass.Name == label) ++occurrence;
    const std::string key = (parent ? frame->Passes[parent - 1].Key : std::string{}) + "/" + label + "#" + std::to_string(occurrence);
    frame->Passes.push_back({id, parent, queue, label, key});
    ++frame->PendingGpu; s.Starts[id] = Clock::now();
    const Token token{frame->Id, id}; Stack.push_back(token); return token;
}
void EndPass(Token token)
{
    if (!token.Frame || !token.Pass) return;
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    if (auto* pass = FindPass(Find(s, token.Frame), token.Pass))
    {
        const auto found = s.Starts.find(token.Pass);
        if (!pass->Ended && token.Frame == s.Current && found != s.Starts.end())
            pass->CpuMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - found->second).count();
        pass->Ended = true;
    }
    const auto found = std::find_if(Stack.begin(), Stack.end(), [token](Token item) {
        return item.Frame == token.Frame && item.Pass == token.Pass;
    });
    if (found != Stack.end()) Stack.erase(found);
}
void CompleteGpu(Token token, uint64_t elapsedNanoseconds)
{
    if (!token.Frame || !token.Pass) return;
    auto& s = Get(); std::lock_guard lock(s.Mutex); auto* frame = Find(s, token.Frame);
    auto* pass = FindPass(frame, token.Pass); if (!pass || pass->GpuReady) return;
    pass->GpuMilliseconds = double(elapsedNanoseconds) * 1.0e-6;
    pass->GpuReady = true;
    if (frame->PendingGpu) --frame->PendingGpu;
}
void RecordCommand(bool compute)
{
    if (!IsEnabled() || Stack.empty()) return;
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    const auto token = Stack.back();
    if (token.Frame == s.Current)
        if (auto* pass = FindPass(Find(s, token.Frame), token.Pass))
            if (compute) ++pass->ComputeCalls; else ++pass->DrawCalls;
}
void RecordResource(Resource resource)
{
    auto& s = Get(); std::lock_guard lock(s.Mutex); auto* frame = Find(s, s.Current);
    if (!frame) return;
    for (auto it = frame->Passes.rbegin(); it != frame->Passes.rend(); ++it)
        if (it->Name == resource.Producer) { resource.ProducerId = it->Id; break; }
    for (auto& existing : frame->Resources)
        if (existing.Key == resource.Key) { existing = std::move(resource); return; }
    if (frame->Resources.size() < MaxResources) frame->Resources.push_back(std::move(resource));
}
std::shared_ptr<const Frame> LatestCompletedFrame()
{
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    for (auto it = s.Frames.rbegin(); it != s.Frames.rend(); ++it)
        if (it->Ended && !it->PendingGpu) return std::make_shared<Frame>(*it);
    return {};
}
std::shared_ptr<const Frame> CopyFrame(uint64_t id)
{
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    if (auto* frame = Find(s, id)) return std::make_shared<Frame>(*frame);
    return {};
}
uint64_t CurrentFrameId() { auto& s = Get(); std::lock_guard lock(s.Mutex); return s.Current; }
void Reset()
{
    auto& s = Get(); std::lock_guard lock(s.Mutex);
    s.Current = 0; s.Enabled = false; s.Frames.clear(); s.Starts.clear(); Stack.clear();
    s.Captures.clear(); s.CaptureCount.store(0,std::memory_order_relaxed);
}
}
