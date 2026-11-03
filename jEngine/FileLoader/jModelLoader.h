#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

class jMeshObject;

struct jTask
{
    using HashFunc = std::function<size_t()>;
    using Func = std::function<void()>;
    void Do() const { pFunc(); }
    HashFunc pHashFunc = nullptr;
    Func pFunc = nullptr;
    size_t GetHash() const { return pHashFunc ? pHashFunc() : 0; }
    bool IsValid() const { return !!pFunc; }
};

// Small bounded executor shared by image preparation and model requests.
// Legacy batch users retain Enqueue/WaitAll; independent requests use Submit.
class jThreadPool
{
public:
    explicit jThreadPool(size_t numThreads, size_t queueCapacity = 0);
    ~jThreadPool();
    jThreadPool(const jThreadPool&) = delete;
    jThreadPool& operator=(const jThreadPool&) = delete;
    void Enqueue(const jTask& task);
    std::future<void> Submit(std::function<void()> task);
    void WaitAll();
    void Shutdown(); // Drains accepted work and joins. No concurrent Shutdown calls.
    static size_t DefaultWorkerCount(); // Reserves two logical CPUs; caps at four.

private:
    std::vector<std::thread> WorkerThreads;
    std::condition_variable ConditionVariable;
    std::condition_variable CompletionConditionVariable;
    std::condition_variable QueueSpaceConditionVariable;
    bool IsStop = false;
    size_t QueueCapacity = 1;
    std::queue<jTask> TaskQueue;
    std::mutex QueueLock;
    size_t RemainTask = 0;
    std::unordered_set<size_t> ProcessingTaskHashes;
    std::exception_ptr TaskException;
};

using jAssetCancellation = std::shared_ptr<std::atomic<bool>>;

struct jModelPreparationStats
{
    size_t VertexCount = 0;
    size_t IndexCount = 0;
    size_t SubMeshCount = 0;
    size_t TextureCount = 0;
    double CpuMilliseconds = 0.0;
};

// CPU data only: no jObject IDs, render objects, RHI resources or scene entries.
// Its private data is mutable only during preparation, then transferred once.
class jPreparedModel
{
public:
    jPreparedModel();
    ~jPreparedModel();
    jPreparedModel(jPreparedModel&&) noexcept;
    jPreparedModel& operator=(jPreparedModel&&) noexcept;
    jPreparedModel(const jPreparedModel&) = delete;
    jPreparedModel& operator=(const jPreparedModel&) = delete;
    const jModelPreparationStats& GetStats() const;

private:
    struct Data;
    std::unique_ptr<Data> Payload;
    friend class jModelLoader;
};

class jModelLoader
{
public:
    ~jModelLoader();
    static jModelLoader& GetInstance();

    // Construct the singleton on the render owner before starting import workers.
    // Preparation can run on workers, owns all output, and never accesses RHI/scene.
    std::unique_ptr<jPreparedModel> PrepareModel(const char* filename,
        const char* materialRootDir = "Image/", const jAssetCancellation& cancellation = {},
        std::string* error = nullptr);
    // Consumes the CPU payload. Rejected before RHI access on any other thread.
    // The caller adds the returned object to the scene only after success.
    jMeshObject* FinalizeModel(std::unique_ptr<jPreparedModel> prepared, std::string* error = nullptr);
    bool IsOwnerThread() const;
    void Shutdown(); // Call after import service Shutdown and before RHI teardown.

    jMeshObject* LoadFromFile(const char* filename);
    jMeshObject* LoadFromFile(const char* filename, const char* materialRootDir);

private:
    jModelLoader();
    jThreadPool ThreadPool;
    const std::thread::id OwnerThread;
};
