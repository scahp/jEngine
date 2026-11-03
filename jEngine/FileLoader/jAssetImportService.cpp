#include "pch.h"
#include "jAssetImportService.h"

struct jAssetImportService::Job
{
    RequestId Id = 0;
    std::uint64_t Generation = 0;
    std::string Filename;
    std::string MaterialRoot;
    jAssetCancellation Cancellation = std::make_shared<std::atomic<bool>>(false);
};

jAssetImportService::jAssetImportService() : jAssetImportService(Limits{}) {}

jAssetImportService::jAssetImportService(Limits limits, PrepareFunction prepare)
    : OwnerThread(std::this_thread::get_id())
    , Capacity{std::clamp(limits.MaxQueued, size_t(1), size_t(16)),
        std::clamp(limits.MaxReady, size_t(1), size_t(2))}
    , Prepare(std::move(prepare))
    , Executor(1, 1)
{
    if (!Prepare)
    {
        // Establish GPU ownership on the constructing thread, never lazily in Run.
        auto& loader = jModelLoader::GetInstance();
        Prepare = [&loader](const std::string& filename, const std::string& root,
            const jAssetCancellation& cancellation, std::string& error) {
            return loader.PrepareModel(filename.c_str(), root.c_str(), cancellation, &error);
        };
    }
    Completed.reserve(Capacity.MaxReady); // Publication never grows a result backlog.
    jTask run;
    run.pFunc = [this] { Run(); };
    Executor.Enqueue(run);
}

jAssetImportService::~jAssetImportService()
{
    Shutdown();
}

bool jAssetImportService::IsOwnerThread() const
{
    return OwnerThread == std::this_thread::get_id();
}

jAssetImportService::RequestId jAssetImportService::Request(std::string filename, std::string materialRootDir)
{
    if (!IsOwnerThread() || filename.empty())
        return 0;
    std::lock_guard<std::mutex> lock(Lock);
    if (Stopping || Queue.size() >= Capacity.MaxQueued || NextId == 0)
        return 0;
    auto job = std::make_shared<Job>();
    job->Id = NextId++;
    job->Generation = Generation;
    job->Filename = std::move(filename);
    job->MaterialRoot = std::move(materialRootDir);
    Queue.push_back(job);
    Wake.notify_all();
    return job->Id;
}

bool jAssetImportService::Cancel(RequestId id)
{
    if (!IsOwnerThread() || !id)
        return false;
    std::lock_guard<std::mutex> lock(Lock);
    bool found = false;
    if (Active && Active->Id == id)
    {
        found = !Active->Cancellation->exchange(true);
    }
    for (auto it = Queue.begin(); it != Queue.end();)
    {
        if ((*it)->Id == id)
        {
            (*it)->Cancellation->store(true);
            it = Queue.erase(it);
            found = true;
        }
        else
            ++it;
    }
    for (auto it = Completed.begin(); it != Completed.end();)
    {
        if (it->Id == id)
        {
            it = Completed.erase(it);
            found = true;
        }
        else
            ++it;
    }
    CancelledCount += found;
    Wake.notify_all();
    return found;
}

void jAssetImportService::CancelAll()
{
    if (!IsOwnerThread())
        return;
    std::lock_guard<std::mutex> lock(Lock);
    ++Generation;
    CancelledCount += Queue.size() + Completed.size();
    if (Active && !Active->Cancellation->exchange(true))
        ++CancelledCount;
    for (const auto& job : Queue)
        job->Cancellation->store(true);
    Queue.clear();
    Completed.clear();
    Wake.notify_all();
}

std::optional<jAssetImportService::Result> jAssetImportService::Poll()
{
    if (!IsOwnerThread())
        return std::nullopt;
    std::lock_guard<std::mutex> lock(Lock);
    if (Completed.empty())
        return std::nullopt;
    Result result = std::move(Completed.front());
    Completed.erase(Completed.begin());
    Wake.notify_all();
    return result;
}

jAssetImportService::Stats jAssetImportService::GetStats() const
{
    std::lock_guard<std::mutex> lock(Lock);
    return {Queue.size(), Active ? size_t(1) : size_t(0), Completed.size(),
        Generation, CancelledCount, Stopping};
}

void jAssetImportService::Shutdown()
{
    // Destruction also joins if called by a different thread after ownership has
    // been relinquished. Never destroy the service from its own prepare callback.
    {
        std::lock_guard<std::mutex> lock(Lock);
        Stopping = true;
        ++Generation;
        if (Active)
            Active->Cancellation->store(true);
        for (const auto& job : Queue)
            job->Cancellation->store(true);
        Queue.clear();
        Completed.clear();
    }
    Wake.notify_all();
    Executor.Shutdown();
}

void jAssetImportService::Run()
{
    for (;;)
    {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(Lock);
            // Reserve a completion slot before preparing. Ready + active CPU
            // payloads can therefore never exceed MaxReady, even when unpolled.
            Wake.wait(lock, [this] {
                return Stopping || (!Queue.empty() && Completed.size() < Capacity.MaxReady);
            });
            if (Stopping)
                return;
            job = Queue.front();
            Queue.pop_front();
            Active = job;
        }
        Result result;
        result.Id = job->Id;
        result.Generation = job->Generation;
        try
        {
            result.Model = Prepare(job->Filename, job->MaterialRoot, job->Cancellation, result.Error);
            result.State = result.Model ? Status::Ready : Status::Failed;
            if (!result.Model && result.Error.empty())
                result.Error = "Model preparation failed";
        }
        catch (const std::exception& error)
        {
            result.Error = error.what();
        }
        catch (...)
        {
            result.Error = "Unknown model preparation failure";
        }
        {
            std::lock_guard<std::mutex> lock(Lock);
            Active.reset();
            if (!Stopping && job->Generation == Generation && !job->Cancellation->load())
                Completed.push_back(std::move(result));
        }
        Wake.notify_all();
    }
}
