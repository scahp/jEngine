#pragma once

class jMeshObject;

#define MINIMUM_WORKER_THREAD_COUNT 10

struct jTask
{
    using HashFunc = std::function<size_t()>;
    using Func = std::function<void()>;

    void Do()
    {
        pFunc();
    }

    HashFunc pHashFunc = nullptr;
    Func pFunc = nullptr;
    size_t GetHash() const { return pHashFunc ? pHashFunc() : 0; }
    bool IsValid() const { return !!pFunc; }
};

class jThreadPool 
{
public:
    jThreadPool(size_t num_threads);
    ~jThreadPool();
    
    void Enqueue(const jTask& file_path);
    
    void WaitAll() 
    {
        while (RemainTask.load() != 0) {}
        ProcessingTaskHashes.clear();
    }
    
private:
    std::vector<std::thread> WorkerThreads;
    std::condition_variable ConditionVariable;
    std::atomic<bool> IsStop;

    std::queue<jTask> TaskQueue;
    std::mutex QueueLock;
    std::atomic<int32> RemainTask;
    std::unordered_set<size_t> ProcessingTaskHashes;
};


class jModelLoader
{
public:
	~jModelLoader();

	static jModelLoader& GetInstance()
	{
		if (!_instance)
			_instance = new jModelLoader();
		return *_instance;
	}

	//bool ConvertToFBX(const char* destFilename, const char* filename) const;

	jMeshObject* LoadFromFile(const char* filename);
	jMeshObject* LoadFromFile(const char* filename, const char* materialRootDir);

private:
    jThreadPool ThreadPool;

private:
	jModelLoader();

	static jModelLoader* _instance;
};

