#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// CPU-owned observations of submitted work. This does not schedule passes or
// infer dependencies from recording order. GPU results retain their source frame.
namespace jRenderTrace
{
struct Token
{
    uint64_t Frame = 0;
    uint32_t Pass = 0;
};
struct Pass
{
    uint32_t Id = 0, Parent = 0, Queue = 0;
    std::string Name, Key;
    uint32_t DrawCalls = 0, ComputeCalls = 0;
    double CpuMilliseconds = 0, GpuMilliseconds = 0;
    bool Ended = false, GpuReady = false;
};
struct Resource
{
    std::string Key, Name, Producer, Format, Interpretation;
    uint32_t ProducerId = 0;
    int Width = 0, Height = 0;
    bool Available = false;
    std::string Reason;
};
struct Frame
{
    uint64_t Id = 0;
    std::string Renderer, Api;
    int Width = 0, Height = 0;
    bool Ended = false;
    uint32_t PendingGpu = 0, DroppedPasses = 0;
    std::vector<Pass> Passes;
    std::vector<Resource> Resources;
};

void SetEnabled(bool enabled);
bool IsEnabled();
void BeginFrame(uint64_t id, const char* renderer, const char* api, int width, int height);
void EndFrame();
Token BeginPass(const char* name, uint32_t queue);
void EndPass(Token token);
void CompleteGpu(Token token, uint64_t elapsedNanoseconds);
void RecordCommand(bool compute);
void RecordResource(Resource resource);
// A copy is intentional: deferred query completion must not mutate the UI's
// frozen snapshot or invalidate pointers while ImGui is enumerating a frame.
std::shared_ptr<const Frame> LatestCompletedFrame();
std::shared_ptr<const Frame> CopyFrame(uint64_t id);
uint64_t CurrentFrameId();
void Reset();
}
