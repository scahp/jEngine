#pragma once

#include "jEditorCapture.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace jEditorCapture::Detail
{
struct Pixels
{
    uint64_t Slot = 0;
    std::vector<uint8_t> Rgba;
};
struct Job
{
    explicit Job(Options options) : Output(std::move(options)) {}
    const Options Output;
    std::mutex Mutex;
    std::condition_variable Wake;
    std::deque<Pixels> Frames;
    bool Closed = false;
    uint64_t EndSlot = 0;
    std::string Error;
};
struct Result
{
    uint64_t Frames = 0;
    std::string Error;
};
Result Encode(Job& job);
}
