#pragma once

#include "jModelLoader.h"
#include <cstdint>
#include <deque>
#include <optional>

// Owner-thread request/poll facade. Workers only create move-owned CPU payloads.
class jAssetImportService
{
public:
    using RequestId = std::uint64_t;
    enum class Status { Ready, Failed, Cancelled };
    struct Result
    {
        RequestId Id = 0;
        std::uint64_t Generation = 0;
        Status State = Status::Failed;
        std::unique_ptr<jPreparedModel> Model;
        std::string Error;
    };
    struct Stats
    {
        size_t Queued = 0;
        size_t Active = 0;
        size_t Ready = 0;
        std::uint64_t Generation = 0;
        std::uint64_t Cancelled = 0;
        bool Stopping = false;
    };
    struct Limits
    {
        size_t MaxQueued = 4;
        size_t MaxReady = 2;
    };
    using PrepareFunction = std::function<std::unique_ptr<jPreparedModel>(
        const std::string&, const std::string&, const jAssetCancellation&, std::string&)>;

    jAssetImportService();
    explicit jAssetImportService(Limits limits, PrepareFunction prepare = {});
    ~jAssetImportService();
    jAssetImportService(const jAssetImportService&) = delete;
    jAssetImportService& operator=(const jAssetImportService&) = delete;

    // Copies paths. Returns zero if stopped, wrong owner, empty path, or queue full.
    RequestId Request(std::string filename, std::string materialRootDir);
    // Cancellation discards queued/ready results and suppresses active publication.
    // It is cooperative: an active Assimp/decoder call finishes before joining.
    bool Cancel(RequestId id);
    void CancelAll(); // Advances generation; accepted old work can never publish.
    std::optional<Result> Poll(); // One terminal result; no per-frame blocking.
    Stats GetStats() const;
    bool IsOwnerThread() const;
    void Shutdown(); // Idempotent owner call; cancels and joins before RHI teardown.

private:
    struct Job;
    void Run();
    const std::thread::id OwnerThread;
    const Limits Capacity;
    PrepareFunction Prepare;
    mutable std::mutex Lock;
    std::condition_variable Wake;
    std::deque<std::shared_ptr<Job>> Queue;
    std::vector<Result> Completed;
    std::shared_ptr<Job> Active;
    RequestId NextId = 1;
    std::uint64_t Generation = 1;
    std::uint64_t CancelledCount = 0;
    bool Stopping = false;
    jThreadPool Executor;
};
