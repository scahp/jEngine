#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class jMeshObject;
class jPreparedModel;
class jModelLoader;

// Owner-thread admission of bounded copy chunks, followed by nonblocking fence
// polling. Resource allocation can overrun the CPU admission deadline; measured
// overruns are reported rather than presented as a hard frame-time guarantee.
class jModelUploadSession
{
public:
    enum class State { Uploading, AwaitingGpu, Ready, Cancelled, Failed, Consumed };
    struct Budget
    {
        std::uint64_t MaxBytes = 8 * 1024 * 1024;
        double MaxAdmissionMilliseconds = 2.0;
        size_t MaxPendingChunks = 2;
    };
    struct Progress
    {
        State Current = State::Uploading;
        std::uint64_t TotalBytes = 0;
        std::uint64_t SubmittedBytes = 0;
        std::uint64_t CompletedBytes = 0;
        std::uint64_t LastStepBytes = 0;
        size_t PendingChunks = 0;
        double LastStepMilliseconds = 0;
        double WorstStepMilliseconds = 0;
        bool AdmissionOverrun = false;
        std::string Error;
    };
    ~jModelUploadSession();
    jModelUploadSession(const jModelUploadSession&) = delete;
    jModelUploadSession& operator=(const jModelUploadSession&) = delete;
    Progress Step();
    Progress Step(const Budget& budget);
    bool PollReady();
    Progress GetProgress() const;
    std::unique_ptr<jMeshObject> TakeModel();
    // Stops further admission. Already submitted staging/targets remain owned by
    // a globally bounded retirement queue until their submission fence completes.
    void Cancel();
    static void CollectRetired(); // Owner frame tick, including frames without an import.
    static void DrainRetired(); // Owner shutdown before RHI teardown; waits own fences only.

private:
    explicit jModelUploadSession(std::unique_ptr<jPreparedModel> prepared);
    void WaitSubmitted(); // Synchronous legacy adapter only; no global queue idle.
    struct Impl;
    std::unique_ptr<Impl> Data;
    friend class jModelLoader;
};
